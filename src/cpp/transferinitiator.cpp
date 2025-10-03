#include "win_compat.h"
#include "transferinitiator.h"
#include "multiwalletcontroller.h"
#include "torbackend.h"
#include "wallet.h"
#include "accountmanager.h"
#include "cryptoutils_extras.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QCryptographicHash>
#include <QMetaObject>
#include <QtGlobal>
#include <QDebug>
#include <QRunnable>
#include <QSet>
#include <algorithm>


using namespace CryptoUtils;

namespace {
static constexpr int PING_MS  = 5'000;
static constexpr int RETRY_MS = 5'000;

static QString stageName(TransferInitiator::Stage s)
{
    using S = TransferInitiator::Stage;
    switch (s) {
    case S::INIT: return "INIT";
    case S::MULTISIG_IMPORT_CHECK: return "MULTISIG_IMPORT_CHECK";
    case S::CHECKING_PEERS: return "CHECKING_PEERS";
    case S::COLLECTING_INFO: return "COLLECTING_INFO";
    case S::CREATING_TRANSFER: return "CREATING_TRANSFER";
    case S::VALIDATING: return "VALIDATING";
    case S::APPROVING: return "APPROVING";
    case S::SUBMITTING: return "SUBMITTING";
    case S::CHECKING_STATUS: return "CHECKING_STATUS";
    case S::COMPLETE: return "COMPLETE";
    case S::ERROR: return "ERROR";
    case S::DECLINED: return "DECLINED";
    }
    return {};
}

static QJsonArray destinationsToJson(const QList<TransferInitiator::Destination> &d)
{
    QJsonArray arr;
    for (const auto &x : d)
        arr.push_back(QJsonObject{ {"address", x.address}, {"amount", QString::number(x.amount)} });

    return arr;
}

static QList<TransferInitiator::Destination> jsonToDestinations(const QJsonArray &arr)
{
    QList<TransferInitiator::Destination> out;
    for (const auto &v : arr) {
        const auto o = v.toObject();
        TransferInitiator::Destination d{
            o.value("address").toString(),0
        };

        const QJsonValue av = o.value("amount");
        if (av.isString()) {
            bool ok=false; d.amount = av.toString().toULongLong(&ok, 10);
            if (!ok) d.amount = 0;
        } else if (av.isDouble()) {
            d.amount = static_cast<quint64>(av.toDouble(0.0) + 0.5);
        }
        out.push_back(d);
    }
    return out;
}

static QList<TransferInitiator::Destination> variantListToDestinations(const QVariantList &lst)
{
    QList<TransferInitiator::Destination> out;
    for (const QVariant &v : lst) {
        const QVariantMap m = v.toMap();
        const QString addr = m.value(QStringLiteral("address")).toString();
        quint64 amount = 0;
        if (m.value(QStringLiteral("amount")).typeId() == QMetaType::Double) {
            const double xmr = m.value(QStringLiteral("amount")).toDouble();
            amount = static_cast<quint64>(xmr * 1e12 + 0.5);
        } else {
            amount = static_cast<quint64>(m.value(QStringLiteral("amount")).toULongLong());
        }
        out.push_back({addr, amount});
    }
    return out;
}

static QVariantList destinationsToVariantList(const QList<TransferInitiator::Destination> &d)
{
    QVariantList lst;
    for (const auto &x : d) {
        QVariantMap m;
        m.insert(QStringLiteral("address"), x.address);
        m.insert(QStringLiteral("amount"),  static_cast<qulonglong>(x.amount));
        lst.push_back(m);
    }
    return lst;
}


static QByteArray b64urlDecodeNoPad(QByteArray s)
{
    while (s.size() % 4) s.append('=');
    return QByteArray::fromBase64(s, QByteArray::Base64UrlEncoding);
}


static constexpr qint64 kMaxJsonBytesClient   = 256 * 1024;
static constexpr int    kMaxInfoDecoded       = 256 * 1024;
static constexpr int    kMaxPostBodyBytes     = 256 * 1024;
static constexpr int    kHttpTimeoutMs        = 10'000;

static inline bool isBase64UrlString(const QByteArray &s) {
    if (s.isEmpty()) return true;
    for (unsigned char c : s) {
        if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_')) return false;
    }
    return true;
}

static inline bool decodeB64Url(const QByteArray &in, QByteArray &out) {
    out = QByteArray::fromBase64(in, QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    return true;
}

static inline bool eqSha256Hex(const QByteArray &data, const QString &hexLower) {
    const QByteArray got = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    return got == hexLower.toLatin1();
}


}





TransferInitiator::TransferInitiator(MultiWalletController *wm,
                                     TorBackend            *tor,
                                     const QString         &transferRef,
                                     const QString         &walletRef,
                                     const QList<Destination> &destinationsAtomic,
                                     const QStringList     &peersOnionLower,
                                     const QStringList     &signingOrderLower,
                                     int                    threshold,
                                     int                    feePriority,
                                     const QList<int>      &feeSplitIndices,
                                     bool                   inspectBeforeSending,
                                     const QString          &myOnion,
                                     QObject               *parent)
    : QObject(parent)
    , m_wm(wm)
    , m_tor(tor)
    , m_transferRef(transferRef)
    , m_walletRef(walletRef)
    , m_threshold(threshold)
    , m_feePriority(feePriority)
    , m_feeSplit(feeSplitIndices)
    , m_inspectBeforeSend(inspectBeforeSending)
    , m_destinations(destinationsAtomic)
    , m_destinationsInit(destinationsAtomic)
{
    Q_ASSERT(wm && tor);


    if (auto obj = m_wm->property("accountManager").value<QObject*>())
        m_acct = qobject_cast<AccountManager*>(obj);


    auto norm = [](QString s){
        s = s.trimmed().toLower();
        if (!s.endsWith(".onion")) s.append(".onion");
        return s;
    };


    QStringList peersNorm; peersNorm.reserve(peersOnionLower.size());
    for (const auto &p : peersOnionLower) peersNorm << norm(p);


    QStringList myOnionsNorm;
    if (m_acct) {
        for (const auto &o : m_acct->torOnions())
            myOnionsNorm << norm(o);
    }


    QStringList matches;
    for (const auto &p : peersNorm)
        if (myOnionsNorm.contains(p))
            matches << p;

    if (matches.size() != 1) {

        qWarning() << "[TransferInitiator] identity selection error; expected exactly one of our onions in peers,"
                   << "got" << matches.size() << "matches.";
    }

    const QString chosenOnion = matches.isEmpty() ? QString() : matches.first();
    m_myOnionSelected = myOnion;

    if (m_acct && !m_myOnionSelected.isEmpty()) {
        const QString blob = m_acct->torPrivKeyFor(m_myOnionSelected);
        if (!blob.isEmpty()) {
            QByteArray sc, pr, pb;
            if (trySplitV3BlobFlexible(blob, sc, pr, pb)) {
                m_scalar=sc; m_prefix=pr; m_pubKey=pb;
                const QString fromPub = QString::fromUtf8(onion_from_pub(m_pubKey)).toLower();
                if (fromPub != m_myOnionSelected)
                    qWarning() << "[TransferInitiator] pub->onion mismatch; fromPub=" << fromPub
                               << "chosen=" << m_myOnionSelected;
            } else {
                qWarning() << "[TransferInitiator] Tor private key blob malformed for" << m_myOnionSelected;
            }
        } else {
            qWarning() << "[TransferInitiator] No private key found for onion" << m_myOnionSelected;
        }
    }

    m_walletName     = walletNameForRef(m_walletRef);
    m_walletMainAddr = walletAddressForRef(m_walletRef);

    const QString myOn = m_myOnionSelected;
    for (const QString &o : peersOnionLower) {
        const QString low = o.trimmed().toLower();
        if (low == myOn) continue;
        m_peers.insert(low, PeerState{low});
    }
    m_signingOrder = signingOrderLower;

    if (!m_feeSplit.isEmpty()) {
        QSet<int> uniq;
        const int n = m_destinationsInit.size();
        for (int v : m_feeSplit) {
            if (v >= 0 && v < n) uniq.insert(v);
        }
        m_feeSplit = QList<int>(uniq.begin(), uniq.end());
        std::sort(m_feeSplit.begin(), m_feeSplit.end());
    }


    connect(this, &TransferInitiator::_httpResult, this, [this](QString onion, QString path, QJsonObject res, QString err){
        if (!m_peers.contains(onion)) return;
        auto &peer = m_peers[onion];

        if (!err.isEmpty() || res.contains("error")) {
            peer.online = false;
            emit peerStatusChanged();
            return;
        }

        if (path.startsWith("/api/multisig/transfer/ping")) {
            peer.online = res.value("online").toBool(false);
            peer.ready  = res.value("ready").toBool(false);
            if (peer.online) peer.lastSeen = QDateTime::currentSecsSinceEpoch();
            emit peerStatusChanged();
            if (m_stage == Stage::CHECKING_PEERS) checkPeersOnline();
            return;
        }

        if (path.startsWith("/api/multisig/transfer/request_info")) {
            const QString infoB64 = res.value("multisig_info_b64").toString();
            const qint64  ts   = res.value("time").toInteger();

            QByteArray decoded;
            if (!infoB64.isEmpty()) {decoded = b64urlDecodeNoPad(infoB64.toLatin1());}


            if (!decoded.isEmpty() && ts > 0) {
                peer.multisigInfo   = decoded;;
                peer.multisigInfoTs = ts;
            }
            emit peerStatusChanged();
            if (m_stage == Stage::COLLECTING_INFO) pollMultisigInfo();
            return;
        }

        if (path.startsWith("/api/multisig/transfer/submit")) {
            if (res.value("success").toBool(false)) {
                setStage(Stage::CHECKING_STATUS, QString("Created transfer and submitted to peer"));
                const qint64 submittedTs = QDateTime::currentSecsSinceEpoch();
                m_submittedAt = submittedTs;
                saveToAccount();
                emit submittedSuccessfully(m_transferRef);
                stop(QStringLiteral("success"));
            }
            return;
        }

        if (path.startsWith("/api/multisig/transfer/status")) {
            peer.receivedTransfer = res.value("received_transfer").toBool(false);
            peer.hasSigned        = res.value("has_signed").toBool(false);
            peer.stageName        = res.value("stage_name").toString();
            const QString tx = res.value("tx_id").toString();
            if (!tx.isEmpty() && tx != "pending") m_txId = tx;
            emit peerStatusChanged();
            return;
        }
    }, Qt::QueuedConnection);

    m_ping.setInterval(PING_MS);
    m_retry.setInterval(RETRY_MS);
    connect(&m_ping,  &QTimer::timeout, this, &TransferInitiator::pingRound);
    connect(&m_retry, &QTimer::timeout, this, &TransferInitiator::retryRound);

    m_httpPool.setMaxThreadCount(20);
}

TransferInitiator::~TransferInitiator()
{
    stop("dtor");
}

void TransferInitiator::start()
{
    if (m_stage != Stage::INIT) return;
    setStage(Stage::CHECKING_PEERS, QString("Checking peers..."));
    m_ping.start();
    m_retry.start();
    m_created_at = QDateTime::currentSecsSinceEpoch();
    pingRound();

}

void TransferInitiator::cancel()
{
    stop(QStringLiteral("abort"));
}

void TransferInitiator::pingRound()
{
    if (m_stopFlag) return;
    for (auto it=m_peers.cbegin(); it!=m_peers.cend(); ++it) {
        const QString path = QStringLiteral("/api/multisig/transfer/ping?ref=%1").arg(m_walletRef);

        httpGetAsync(it.key(), path, true);
    }
}

void TransferInitiator::retryRound()
{
    if (m_stopFlag) return;

    if (m_stage == Stage::CHECKING_PEERS) { checkPeersOnline(); return; }
    if (m_stage == Stage::COLLECTING_INFO) { pollMultisigInfo(); return; }
    if (m_stage == Stage::SUBMITTING) { submitToNextPeer(); return; }
    if (m_stage == Stage::CHECKING_STATUS) { stop(QStringLiteral("success")); return; }
    if (m_stage == Stage::COMPLETE) { m_ping.stop(); m_retry.stop(); }
}

void TransferInitiator::checkPeersOnline()
{
    const bool allReady = (!m_peers.isEmpty() &&
                           std::all_of(m_peers.cbegin(), m_peers.cend(),
                                       [](const PeerState &p){ return p.online && p.ready; }));
    if (!allReady) return;
    setStage(Stage::COLLECTING_INFO, QStringLiteral("Collecting multisig info from peers..."));
    pollMultisigInfo();
}

void TransferInitiator::pollMultisigInfo()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    for (auto it=m_peers.begin(); it!=m_peers.end(); ++it) {
        auto &p = it.value();
        if (p.multisigInfo.isEmpty() || (now - p.multisigInfoTs) > 300) {
            const QString path = QStringLiteral("/api/multisig/transfer/request_info?ref=%1").arg(m_walletRef);

            httpGetAsync(p.onion, path, true);
        }
    }

    const bool allFresh = std::all_of(m_peers.cbegin(), m_peers.cend(),
                                      [now](const PeerState &p){
                                          return !p.multisigInfo.isEmpty() && (now - p.multisigInfoTs) <= 300;
                                      });
    if (allFresh) {
        setStage(Stage::CREATING_TRANSFER, QStringLiteral("Generating own multisig info..."));
        importAllMultisigInfos();
    }
}

void TransferInitiator::createOwnMultisigInfo()
{
    Wallet *w = walletByRef(m_walletRef);
    if (!w) { setStage(Stage::ERROR, "Wallet not found"); return; }

    connect(w, &Wallet::multisigInfoPrepared,
            this, &TransferInitiator::onMultisigInfoPrepared,
            Qt::QueuedConnection);


    w->prepareMultisigInfo(m_transferRef);
}

void TransferInitiator::onMultisigInfoPrepared(QByteArray info , QString operation_caller)
{
    if (operation_caller != m_transferRef ) return;
    importAllMultisigInfos();
}

void TransferInitiator::importAllMultisigInfos()
{
    Wallet *w = walletByRef(m_walletRef);
    if (!w) { setStage(Stage::ERROR, "Wallet not found"); return; }

    QList<QByteArray> infos;
    for (const auto &p : std::as_const(m_peers))
        if (!p.multisigInfo.isEmpty())
            infos << p.multisigInfo;


    connect(w, &Wallet::multisigInfosImported,
            this, &TransferInitiator::onMultisigInfosImported,
            Qt::QueuedConnection);

    w->importMultisigInfos(infos,m_transferRef);
}

void TransferInitiator::onMultisigInfosImported(int nImported, bool needsRescan , QString operation_caller )
{
    if (operation_caller != m_transferRef ) return;
    Q_UNUSED(nImported)
    Q_UNUSED(needsRescan)
    setStage(Stage::CREATING_TRANSFER, QStringLiteral("Creating multisig transfer..."));
    createMultisigTransfer();
}

void TransferInitiator::createMultisigTransfer()
{
    Wallet *w = walletByRef(m_walletRef);
    if (!w) { setStage(Stage::ERROR, "Wallet not found"); return; }

    connect(w, &Wallet::unsignedMultisigReady,
            this, &TransferInitiator::onUnsignedMultisigReady,
            Qt::QueuedConnection);

    connect(w, &Wallet::errorOccurred,
            this, &TransferInitiator::onErrorOccurred,
            Qt::QueuedConnection);

    QVariantList dest = destinationsToVariantList(m_destinations);

    qDebug().noquote() << "[TransferInitiator] calling createUnsignedMultisigTransfer with feeSplit="
                       << QVariant::fromValue(m_feeSplit).toString();

    w->createUnsignedMultisigTransfer(dest, m_feePriority, m_feeSplit, m_transferRef);
}


void TransferInitiator::onErrorOccurred(QString error , QString operation_caller)
{
    if (operation_caller!= m_transferRef ) return;

    setStage(Stage::ERROR, error);
    saveToAccount();
    stop("error");
    return;
}

void TransferInitiator::onUnsignedMultisigReady(QByteArray txset,
                                                quint64 feeAtomic,
                                                QVariantList normalizedDestinations,
                                                QString warningOrEmpty , QString operation_caller )
{

    if (operation_caller != m_transferRef ) return;
    if (txset.isEmpty()) {
        setStage(Stage::ERROR, "Empty txset");
        stop(QStringLiteral("Error on onUnsignedMultisigReady"));

        return; }

    const QString myOn = myOnionFQDN().toLower();
    if (!m_signatures.contains(myOn))
        m_signatures << myOn;

    if (!warningOrEmpty.isEmpty())
        emit statusChanged(warningOrEmpty);

    m_transferBlob   = QString::fromLatin1(txset.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    if (!isBase64UrlString(m_transferBlob.toLatin1())) {
        setStage(Stage::ERROR, "transfer_blob: invalid base64url");
        stop("error");
        return;
    }

    if (txset.size() > 256*1024) {
        setStage(Stage::ERROR, "transfer_blob too large");
        stop("error");
        return;
    }

    m_feeAtomic      = feeAtomic;
    m_feeStr         = QString::number(m_feeAtomic);
    m_destinations   = variantListToDestinations(normalizedDestinations);
    m_txId           = "pending";

    setStage(Stage::VALIDATING, QStringLiteral("Describing transfer..."));
    describeTransfer();
}

void TransferInitiator::describeTransfer()
{
    Wallet *w = walletByRef(m_walletRef);
    if (!w) { setStage(Stage::ERROR, "Wallet not found"); return; }

    connect(w, &Wallet::describeTransferResult,
            this, &TransferInitiator::onDescribeTransferResultVariant,
            Qt::QueuedConnection);

    w->describeTransfer(m_transferBlob,m_transferRef);
}

void TransferInitiator::onDescribeTransferResultVariant(QString walletName, QVariant detailsVar, QString operation_caller)
{
    if (operation_caller != m_transferRef ) return;
    Q_UNUSED(walletName);
    QJsonObject details;

    if (detailsVar.metaType().id() == QMetaType::QJsonObject) {
        details = detailsVar.toJsonObject();
    } else if (detailsVar.canConvert<QVariantMap>()) {
        details = QJsonObject::fromVariantMap(detailsVar.toMap());
    } else if (detailsVar.canConvert<QString>()) {
        const auto s = detailsVar.toString().toUtf8();
        QJsonParseError pe{};
        const auto doc = QJsonDocument::fromJson(s, &pe);
        if (pe.error == QJsonParseError::NoError && doc.isObject())
            details = doc.object();
    }

    onDescribeTransferResultParsed(walletName, details);
}

void TransferInitiator::onDescribeTransferResultParsed(QString walletName, QJsonObject details)
{
    Q_UNUSED(walletName)

    const QJsonArray  descArr = details.value(QStringLiteral("desc")).toArray();
    if (descArr.isEmpty()) { setStage(Stage::ERROR, "Invalid describeTransfer payload"); return; }

    const QJsonObject d = descArr.first().toObject();
    m_transferDescription = QJsonObject{
        {QStringLiteral("recipients"),  d.value(QStringLiteral("recipients")).toArray()},
        {QStringLiteral("payment_id"),  d.value(QStringLiteral("payment_id")).toString()},
        {QStringLiteral("fee"),         d.value(QStringLiteral("fee"))},
        {QStringLiteral("unlock_time"), d.value(QStringLiteral("unlock_time"))}
    };

    m_unlock_time = d.value(QStringLiteral("unlock_time")).toString();
    m_paymentId = d.value(QStringLiteral("payment_id")).toString();

    {
        const QJsonValue fv = d.value(QStringLiteral("fee"));
        if (fv.isString()) {
            bool ok=false; m_feeAtomic = fv.toString().toULongLong(&ok, 10);
            if (!ok) m_feeAtomic = 0;
        } else {
            m_feeAtomic = static_cast<quint64>(fv.toDouble(0.0) + 0.5);
        }
    }

    m_feeStr    = QString::number(m_feeAtomic);
    m_destinations = jsonToDestinations(d.value(QStringLiteral("recipients")).toArray());

    quint64 sum = 0;
    for (const auto &x : m_destinations) sum += x.amount;

    const bool feeHigh = (sum > 0 && (double(m_feeAtomic)/double(sum)) > 0.005);
    if (m_inspectBeforeSend || feeHigh && m_acct->inspectGuard() ) m_inspectBeforeSend = true;

    if (!m_inspectBeforeSend) {
        setStage(Stage::SUBMITTING, QStringLiteral("Sending to next peer..."));
        submitToNextPeer();
    } else {
        setStage(Stage::APPROVING, QStringLiteral("Awaiting your approval before sending to peer"));
    }
}

void TransferInitiator::proceedAfterApproval()
{
    if (m_stage != Stage::APPROVING) return;
    setStage(Stage::SUBMITTING, QStringLiteral("Sending to next peer..."));
    submitToNextPeer();
}

void TransferInitiator::submitToNextPeer()
{
    QString nextPeer;
    for (const auto &o : m_signingOrder) {
        if (!m_signatures.contains(o)) { nextPeer = o; break; }
    }
    if (nextPeer.isEmpty()) {
        setStage(Stage::CHECKING_STATUS, QStringLiteral("All signers accounted for; awaiting broadcast"));
        return;
    }

    const QJsonObject body{
                           {"transfer_ref", m_transferRef},
                           {"transfer_blob", m_transferBlob},
                           {"signing_order", QJsonArray::fromStringList(m_signingOrder)},
                           {"who_has_signed", QJsonArray::fromStringList(m_signatures)},
                           {"transfer_description", m_transferDescription},
                           {"created_at",  static_cast<qint64>(m_created_at)},
                           };

    const QString path = QStringLiteral("/api/multisig/transfer/submit?ref=%1").arg(m_walletRef);

    m_submitAttempts[nextPeer] = m_submitAttempts.value(nextPeer,0)+1;
    const QString msg = QStringLiteral("Submitting to %1 (attempt %2)").arg(nextPeer.left(10)).arg(m_submitAttempts[nextPeer]);
    emit statusChanged(msg);
    httpPostAsync(nextPeer, path, body, true);
}

void TransferInitiator::checkPeerStatus()
{
    for (auto it=m_peers.begin(); it!=m_peers.end(); ++it) {
        const QString path = QStringLiteral("/api/multisig/transfer/status?ref=%1&transfer_ref=%2")
        .arg(m_walletRef, m_transferRef);
        httpGetAsync(it.key(), path, true);
    }
}

void TransferInitiator::saveToAccount()
{
    if (!m_acct) return;

    QJsonDocument doc = QJsonDocument::fromJson(m_acct->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonObject monero= root.value("monero").toObject();
    QJsonArray  wallets = monero.value("wallets").toArray();

    bool updated = false;

    for (int i = 0; i < wallets.size(); ++i) {
        QJsonObject w = wallets.at(i).toObject();

        const bool match =
            (w.value("reference").toString() == m_walletRef) ||
            (w.value("name").toString()      == m_walletName);

        if (!match) continue;

        QJsonObject transfers = w.value("transfers").toObject();

        QJsonObject peersObj;
        for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
            const auto &p = it.value();
            peersObj.insert(it.key(),
                            QJsonArray{ p.stageName, p.receivedTransfer, p.hasSigned , p.status });
        }

        QJsonArray dest = destinationsToJson(m_destinations);

        QJsonObject entry = transfers.value(m_transferRef).toObject();


        entry["type"]                  = QStringLiteral("MULTISIG");
        entry["wallet_name"]           = m_walletName;
        entry["wallet_ref"]            = m_walletRef;
        entry["destinations"]          = dest;
        entry["peers"]                 = peersObj;
        entry["signing_order"]         = QJsonArray::fromStringList(m_signingOrder);
        entry["stage"]                 = stageName(m_stage);
        entry["signatures"]            = QJsonArray::fromStringList(m_signatures);
        entry["status"]                = m_status;
        entry["transfer_blob"]         = m_transferBlob;
        entry["transfer_description"]  = m_transferDescription;


        if (!m_txId.isEmpty())
            entry["tx_id"] = m_txId;
        else if (!entry.contains("tx_id"))
            entry["tx_id"] = QString();


        entry["created_at"] = m_created_at;

        if (m_submittedAt > 0)
            entry["submitted_at"] = m_submittedAt;

        entry["my_onion"] = m_myOnionSelected;

        transfers.insert(m_transferRef, entry);
        w.insert("transfers", transfers);
        wallets[i] = w;

        updated = true;
        break;
    }

    if (updated) {
        monero.insert("wallets", wallets);
        root.insert("monero", monero);
        (void)m_acct->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
}

void TransferInitiator::setStage(Stage s, const QString &statusMsg)
{
    m_stage = s;
    m_status = statusMsg;
    emit stageChanged(stageName(m_stage));
    if (!statusMsg.isEmpty()) emit statusChanged(statusMsg);
}

void TransferInitiator::stop(const QString &reason)
{
    if (m_stopFlag) return;
    m_stopFlag = true;
    m_ping.stop();
    m_retry.stop();
    m_httpPool.waitForDone(10'000);
    emit finished(m_transferRef, reason);
}

void TransferInitiator::httpGetAsync(const QString &onion, const QString &path, bool signedFlag)
{
    QRunnable *task = QRunnable::create([=](){
        const QJsonObject res = httpRequestBlocking(onion, path, "GET", signedFlag);
        const QString err = res.value("error").toString();
        emit _httpResult(onion, path, res, err);
    });
    task->setAutoDelete(true);
    m_httpPool.start(task);
}

void TransferInitiator::httpPostAsync(const QString &onion, const QString &path, const QJsonObject &json, bool signedFlag)
{
    QRunnable *task = QRunnable::create([=](){
        const QJsonObject res = httpRequestBlocking(onion, path, "POST", signedFlag, json);
        const QString err = res.value("error").toString();
        emit _httpResult(onion, path, res, err);
    });
    task->setAutoDelete(true);
    m_httpPool.start(task);
}

QJsonObject TransferInitiator::httpRequestBlocking(const QString &onion,
                                                   const QString &path,
                                                   const QByteArray &method,
                                                   bool signedFlag,
                                                   const QJsonObject &json)
{
    const QUrl url(QStringLiteral("http://%1%2").arg(onion, path));
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
#if (QT_VERSION >= QT_VERSION_CHECK(5,9,0))
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(0);
#endif

    QByteArray bodyCompact;
    if (signedFlag) {
        const qint64 ts = QDateTime::currentSecsSinceEpoch();

        QUrl u(path);
        QUrlQuery q(u);
        QString canon = u.path() + QStringLiteral("?ref=") + m_walletRef;
        const QString transferRef = q.queryItemValue(QStringLiteral("transfer_ref"));
        if (!transferRef.isEmpty())
            canon += QStringLiteral("&transfer_ref=") + transferRef;

        QJsonObject msg{{"ref", m_walletRef}, {"path", canon}, {"ts", ts}};
        if (method.toUpper() == "POST") {
            bodyCompact = QJsonDocument(json).toJson(QJsonDocument::Compact);
            if (bodyCompact.size() > kMaxPostBodyBytes) {
                return QJsonObject{{"error","post-body-too-large"}, {"len", bodyCompact.size()}};
            }
            const QByteArray bh = QCryptographicHash::hash(bodyCompact, QCryptographicHash::Sha256).toHex();
            msg.insert("body", QString::fromLatin1(bh));
        }

        const QByteArray sig = signPayload(msg);
        req.setRawHeader("x-pub", _b64(m_pubKey));
        req.setRawHeader("x-ts",  QByteArray::number(ts));
        req.setRawHeader("x-sig", _b64(sig));
    } else if (method.toUpper() == "POST") {
        bodyCompact = QJsonDocument(json).toJson(QJsonDocument::Compact);
        if (bodyCompact.size() > kMaxPostBodyBytes) {
            return QJsonObject{{"error","post-body-too-large"}, {"len", bodyCompact.size()}};
        }
    }

    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy,
                               QLatin1String("127.0.0.1"),
                               m_tor->socksPort()));

    QEventLoop loop;
    QTimer to; to.setSingleShot(true); to.setInterval(kHttpTimeoutMs);
    QObject::connect(&to, &QTimer::timeout, &loop, &QEventLoop::quit);

    QNetworkReply *rep = nullptr;
    if (method.toUpper() == "GET") {
        rep = nam.get(req);
    } else {
        rep = nam.post(req, bodyCompact);
    }

    QByteArray buf;
    buf.reserve(qMin<qint64>(kMaxJsonBytesClient, 64*1024));
    bool tooLarge = false;
    bool badContentType = false;
    bool redirected = false;

    QObject::connect(rep, &QNetworkReply::readyRead, &loop, [&](){
        if (tooLarge) return;
        buf += rep->readAll();
        if (buf.size() > kMaxJsonBytesClient) {
            tooLarge = true;
            rep->abort();
        }
    });

    QObject::connect(rep, &QNetworkReply::metaDataChanged, &loop, [&](){
        QVariant redir = rep->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redir.isValid()) {
            redirected = true;
            rep->abort();
            return;
        }
        const QByteArray ctype = rep->header(QNetworkRequest::ContentTypeHeader).toByteArray().toLower();
        if (!ctype.startsWith("application/json")) {
            badContentType = true;
        }
    });

    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    to.start();
    loop.exec();

    auto makeErr = [&](const char *why){
        if (rep) { rep->abort(); rep->deleteLater(); }
        return QJsonObject{{"error", QString::fromLatin1(why)}};
    };

    if (!to.isActive()) return makeErr("timeout");
    to.stop();

    if (rep->error() != QNetworkReply::NoError) {
        return makeErr(redirected ? "redirect-disallowed" :
                           (tooLarge ? "response-too-large" : rep->errorString().toUtf8().constData()));
    }

    if (badContentType) {
        return makeErr("bad-content-type");
    }

    const int httpCode = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpCode != 200) {
        rep->deleteLater();
        return QJsonObject{{"error","http"}, {"code", httpCode}};
    }

    rep->deleteLater();

    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(buf, &jerr);
    if (jerr.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject{{"error","bad-json"}, {"detail", jerr.errorString()}};

    QJsonObject obj = doc.object();


    if (path.startsWith("/api/multisig/transfer/request_info")) {
        const QJsonValue v = obj.value("multisig_info_b64");
        if (v.isString()) {
            const QByteArray b64 = v.toString().toLatin1();
            if (!b64.isEmpty()) {
                if (!isBase64UrlString(b64)) {
                    return QJsonObject{{"error","bad-b64"}};
                }
                QByteArray decoded;
                if (!decodeB64Url(b64, decoded)) {
                    return QJsonObject{{"error","b64-decode-failed"}};
                }
                if (decoded.size() > kMaxInfoDecoded) {
                    return QJsonObject{{"error","info-too-large"}, {"len", decoded.size()}};
                }
                const int expLen = obj.value("len").toInt(-1);
                if (expLen >= 0 && expLen != decoded.size())
                    return QJsonObject{{"error","len-mismatch"}, {"got", decoded.size()}, {"exp", expLen}};
                const QString sha = obj.value("sha256").toString();
                if (!sha.isEmpty() && !eqSha256Hex(decoded, sha))
                    return QJsonObject{{"error","sha256-mismatch"}};
            }
        }
    }

    return obj;
}


QByteArray TransferInitiator::signPayload(const QJsonObject &payload) const
{
    if (m_scalar.isEmpty() || m_prefix.isEmpty() || m_pubKey.isEmpty())
        return {};
    return ed25519Sign(QJsonDocument(payload).toJson(QJsonDocument::Compact),
                       m_scalar, m_prefix);
}

QByteArray TransferInitiator::pubKey() const { return m_pubKey; }

QString TransferInitiator::myOnionFQDN() const
{
    if (m_pubKey.isEmpty()) return {};
    return QString::fromUtf8(onion_from_pub(m_pubKey)).toLower();
}

Wallet* TransferInitiator::walletByRef(const QString &ref) const
{
    const QString name = walletNameForRef(ref);
    if (name.isEmpty()) return nullptr;
    return qobject_cast<Wallet*>(m_wm->walletInstance(name));
}

QString TransferInitiator::walletNameForRef(const QString &ref) const
{

    for (const QString &n : m_wm->walletNames()) {
        const auto meta = m_wm->getWalletMeta(n);
        if (meta.value("reference").toString().compare(ref, Qt::CaseInsensitive)==0 &&
            meta.value("my_onion").toString().compare(m_myOnionSelected, Qt::CaseInsensitive)==0)
            return n;
    }
    return {};
}

QString TransferInitiator::walletAddressForRef(const QString &ref) const
{
    const QString name = walletNameForRef(ref);
    if (name.isEmpty()) return {};
    const auto meta = m_wm->getWalletMeta(name);
    return meta.value("address").toString();
}

QStringList TransferInitiator::walletPeersByRef(const QString &ref) const
{


    const QString name = walletNameForRef(ref);
    if (name.isEmpty()) return {};
    const auto meta = m_wm->getWalletMeta(name);
    const QVariant v = meta.value("peers");
    if (v.typeId() == QMetaType::QStringList) return v.toStringList();
    if (v.canConvert<QVariantList>()) {
        QStringList peers;
        for (const auto &x : v.toList()) peers << x.toString();
        return peers;
    }
    return {};



}

QString TransferInitiator::getTransferDetailsJson() const
{
    QJsonObject peers;
    for (auto it=m_peers.cbegin(); it!=m_peers.cend(); ++it) {
        const auto &p = it.value();
        peers.insert(it.key(), QJsonObject{
                                   {"onion", p.onion}, {"online", p.online}, {"ready", p.ready},
                                   {"multisig_info_timestamp", QString::number(p.multisigInfoTs)},
                                   {"received_transfer", p.receivedTransfer},
                                   {"signed", p.hasSigned}
                               });
    }

    QJsonObject out{
        {"wallet_name", m_walletName},
        {"walletMainAddress", m_walletMainAddr},
        {"wallet_ref", m_walletRef},
        {"my_onion", myOnionFQDN()},
        {"destinations", destinationsToJson(m_destinations)},
        {"peers", peers},
        {"threshold", m_threshold},
        {"stage", stageName(m_stage)},
        {"status", ""},
        {"fee", static_cast<qint64>(m_feeAtomic)},
        {"tx_id", m_txId},
        {"payment_id", m_paymentId},
        {"unlock_time", m_unlock_time}
    };
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}
