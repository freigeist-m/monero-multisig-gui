#include "win_compat.h"
#include "incomingtransfer.h"
#include <QNetworkProxy>
#include <QRunnable>
#include <QCryptographicHash>

#include "multiwalletcontroller.h"
#include "torbackend.h"
#include "accountmanager.h"
#include "wallet.h"
#include "cryptoutils_extras.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QMetaObject>
#include <QtGlobal>
#include <QDebug>


using namespace CryptoUtils;

namespace {
constexpr int RETRY_MS = 5'000;

static constexpr qint64 kMaxHttpBytes   = 256 * 1024;
static constexpr int    kMaxBlobBytes   = 256 * 1024;

inline bool isB64UrlAlphabet(const QByteArray &s) {
    for (unsigned char c : s) {
        if (c=='=') continue;
        if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'))
            return false;
    }
    return true;
}
inline bool isB64StdAlphabet(const QByteArray &s) {
    for (unsigned char c : s) {
        if (c=='=') continue;
        if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/'))
            return false;
    }
    return true;
}


inline QByteArray decodeB64FlexibleChecked(const QString &in, int maxBytes, QString *why=nullptr) {
    const QByteArray raw = in.toLatin1();
    if (raw.isEmpty()) { if (why) *why = "empty"; return {}; }

    if (isB64UrlAlphabet(raw)) {
        QByteArray out = QByteArray::fromBase64(raw, QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        if (!out.isEmpty() && out.size() <= maxBytes) return out;
        if (why) *why = out.isEmpty() ? "b64url-decode-failed" : "b64url-too-large";
        return {};
    }
    if (isB64StdAlphabet(raw)) {
        QByteArray out = QByteArray::fromBase64(raw);
        if (!out.isEmpty() && out.size() <= maxBytes) return out;
        if (why) *why = out.isEmpty() ? "b64-decode-failed" : "b64-too-large";
        return {};
    }
    if (why) *why = "bad-b64-alphabet";
    return {};
}


static QByteArray tryB64(const QByteArray &in)
{
    QByteArray d = QByteArray::fromBase64(in, QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals);
    return d.isEmpty() ? QByteArray::fromBase64(in) : d;
}
}

// ─────────────────────────────────────────────────────────────────────────────
IncomingTransfer::IncomingTransfer(MultiWalletController *wm,
                                   TorBackend            *tor,
                                   AccountManager        *acct,
                                   const QString &walletRef,
                                   const QString &transferRef,
                                   const QString &myOnion,
                                   QObject *parent)
    : QObject(parent),
    m_wm(wm), m_tor(tor), m_acct(acct),
    m_walletRef(walletRef.trimmed()), m_transferRef(transferRef.trimmed())
{
    Q_ASSERT(wm && tor && acct);

    m_walletName = walletNameForRef(m_walletRef);


    QJsonDocument doc = QJsonDocument::fromJson(acct->loadAccountData().toUtf8());
    const QJsonArray wallets = doc.object()
                                   .value("monero").toObject()
                                   .value("wallets").toArray();
    for (const auto &wv : wallets) {
        const QJsonObject w = wv.toObject();
        const QJsonObject transfers = w.value("transfers").toObject();
        if (!transfers.contains(m_transferRef)) continue;
        const QJsonObject tr = transfers.value(m_transferRef).toObject();

        m_transferBlobB64 = tr.value("transfer_blob").toString();
        m_description     = tr.value("transfer_description").toObject();
        m_signatures      = tr.value("signatures").toVariant().toStringList();
        m_signingOrder    = tr.value("signing_order").toVariant().toStringList();
        m_txId            = tr.value("tx_id").toString();
        m_status          = tr.value("status").toString();

        const QString st  = tr.value("stage").toString();
        if (st == "RECEIVED")             m_stage = Stage::VALIDATING;
        else if (st == "VALIDATING")      m_stage = Stage::VALIDATING;
        else if (st == "SIGNING")         m_stage = Stage::SIGNING;
        else if (st == "SUBMITTING")      m_stage = Stage::SUBMITTING;
        else if (st == "BROADCASTING")    m_stage = Stage::BROADCASTING;
        else if (st == "CHECKING_STATUS") m_stage = Stage::CHECKING_STATUS;
        else if (st == "COMPLETE")        m_stage = Stage::COMPLETE;
        else                              m_stage = Stage::ERROR;
        break;
    }



    auto norm = [](QString s){
        s = s.trimmed().toLower();
        if (!s.isEmpty() && !s.endsWith(".onion")) s.append(".onion");
        return s;
    };
    m_myOnion = norm(myOnion);
    if (m_myOnion.isEmpty()) {
        qWarning() << "[IncomingTransfer]" << m_transferRef
                   << "empty myOnion passed; behavior may be undefined";
    }




    {
        QString blob = m_acct->torPrivKeyFor(m_myOnion);
        if (blob.isEmpty()) return ;

        if (!blob.isEmpty()) {
            QByteArray sc, pr, pb;
            if (trySplitV3BlobFlexible(blob, sc, pr, pb)) {
                m_scalar = sc; m_prefix = pr; m_pubKey = pb;
                // sanity: if derived onion mismatches selected, prefer derived for display
                const QString derived = QString::fromUtf8(onion_from_pub(pb)).toLower();
                const QString derivedOnion = derived.endsWith(".onion") ? derived : (derived + ".onion");
                if (!m_myOnion.isEmpty() &&
                    QString::compare(m_myOnion, derivedOnion, Qt::CaseInsensitive) != 0) {
                    qWarning() << "[IncomingTransfer]" << m_transferRef
                               << "selected onion" << m_myOnion
                               << "!= key-derived onion" << derivedOnion
                               << "— continuing with selected identity; headers signed by this key.";
                }

            } else {
                qWarning() << "[IncomingTransfer]" << m_transferRef
                           << "failed to parse Tor key for" << m_myOnion
                           << "- signed requests may fail";
            }
        } else {
            qWarning() << "[IncomingTransfer]" << m_transferRef
                       << "no Tor key available for" << m_myOnion
                       << "- signed requests may fail";
        }
    }

    connect(&m_retry, &QTimer::timeout, this, &IncomingTransfer::retryRound);
    m_retry.setInterval(RETRY_MS);

    m_httpPool.setMaxThreadCount(16);
}

IncomingTransfer::~IncomingTransfer()
{
    stop("dtor");
}



void IncomingTransfer::start()
{
    switch (m_stage) {
    case Stage::VALIDATING:      validate();         break;
    case Stage::SIGNING:         sign();             break;
    case Stage::SUBMITTING:      m_retry.start();    break;
    case Stage::BROADCASTING:    broadcastSelf();    break;
    case Stage::CHECKING_STATUS:                     break;
    default: break;
    }
}

void IncomingTransfer::cancel() { stop("abort"); }



void IncomingTransfer::validate()
{
    Wallet *w = walletByRef(m_walletRef);
    if (!w) { setStage(Stage::ERROR, "Wallet not found"); return; }

    setStage(Stage::VALIDATING, "Describing transfer…");

    saveToAccount();

    connect(w, &Wallet::describeTransferResult,
            this, &IncomingTransfer::onDescribeTransfer,
            Qt::QueuedConnection);


    w->describeTransfer(m_transferBlobB64 ,m_transferRef );
}

void IncomingTransfer::onDescribeTransfer(QString /*walletName*/, QVariant v, QString operation_caller)
{
    if (operation_caller != m_transferRef ) return;
    QJsonObject got;
    if (v.metaType().id()==QMetaType::QJsonObject) got = v.toJsonObject();
    else if (v.canConvert<QVariantMap>())          got = QJsonObject::fromVariantMap(v.toMap());
    else if (v.canConvert<QString>()) {
        QJsonParseError pe{};
        auto doc = QJsonDocument::fromJson(v.toString().toUtf8(), &pe);
        if (pe.error == QJsonParseError::NoError && doc.isObject()) got = doc.object();
    }


    const QJsonArray descArr = got.value("desc").toArray();
    if (descArr.isEmpty()) {
        setStage(Stage::ERROR, "describeTransfer returned empty payload");
        saveToAccount();
        return;
    }
    const QJsonObject first = descArr.first().toObject();
    if (first != m_description) {
        setStage(Stage::ERROR, "Mismatch between transfer data sent & local transfer description verification");
        saveToAccount();
        return;
    }


    QStringList signingOrder = m_signingOrder;
    const QJsonArray soArr = got.value("signing_order").toArray();
    if (!soArr.isEmpty()) {
        signingOrder.clear();
        signingOrder.reserve(soArr.size());
        for (const auto &v : soArr) signingOrder << v.toString();
    }

    if (signingOrder.isEmpty()) {
        setStage(Stage::ERROR, QStringLiteral("Missing signing_order for this transfer"));
        saveToAccount();
        return;
    }


    QStringList configured = walletPeersByRef(m_walletRef);
    if (configured.isEmpty())
        configured = m_wm->peersForWallet(m_walletName);


    const QString me = myOnionFQDN();
    if (!configured.contains(me, Qt::CaseInsensitive))
        configured << me;

    QSet<QString> configuredSet;
    configuredSet.reserve(configured.size());
    for (const QString &p : configured)
        configuredSet.insert(p.toLower());


    QStringList missing;
    for (const QString &p : signingOrder) {
        if (!configuredSet.contains(p.toLower()))
            missing << p;
    }

    if (!missing.isEmpty()) {
        setStage(Stage::ERROR,
                 QStringLiteral("Signing order contains unknown peer(s) for this wallet: %1")
                     .arg(missing.join(QStringLiteral(", "))));
        saveToAccount();
        return;
    }

    sign();
}

void IncomingTransfer::sign()
{
    Wallet *w = walletByRef(m_walletRef);
    if (!w) { setStage(Stage::ERROR,"Wallet not found"); return; }

    setStage(Stage::SIGNING, "Signing transfer…");

    connect(w, &Wallet::multisigSigned,
            this, &IncomingTransfer::onSigned,
            Qt::QueuedConnection);

    connect(w, &Wallet::errorOccurred,
            this, &IncomingTransfer::onErrorOccurred,
            Qt::QueuedConnection);

    const QByteArray blob = tryB64(m_transferBlobB64.toUtf8());


    w->signMultisigBlob(blob, m_transferRef);
}

void IncomingTransfer::onSigned(QByteArray newBlob,
                                bool readyToSubmit,
                                QStringList txids ,QString operation_caller )
{

    if (operation_caller != m_transferRef ) return;
    if (newBlob.isEmpty()) {
        setStage(Stage::ERROR,"signMultisig failed");
        saveToAccount();
        return; }

    m_transferBlobB64 = QString::fromLatin1(
        newBlob.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    if (!m_signatures.contains(myOnionFQDN(), Qt::CaseInsensitive))
        m_signatures << myOnionFQDN();

    if (readyToSubmit) {
        saveToAccount();
        setStage(Stage::BROADCASTING, QStringLiteral("Broadcasting to network…"));
        broadcastSelf();
        return;
    }


    saveToAccount();
    maybeSubmit();
}

void IncomingTransfer::onErrorOccurred(QString error, QString operation_caller)
{
        if (operation_caller!= m_transferRef ) return;
        setStage(Stage::ERROR, error);
        saveToAccount();
        stop("error");
        return;
}

void IncomingTransfer::maybeSubmit()
{
    const QString me = myOnionFQDN();
    const int myIdx = m_signingOrder.indexOf(me, Qt::CaseInsensitive);

    QString next;
    const int start = (myIdx >= 0) ? myIdx + 1 : 0;
    for (int i = start; i < m_signingOrder.size(); ++i) {
        const QString &candidate = m_signingOrder[i];
        if (!m_signatures.contains(candidate, Qt::CaseInsensitive)) {
            next = candidate;
            break;
        }
    }

    if (next.isEmpty()) {

        setStage(Stage::BROADCASTING, QStringLiteral("Broadcasting to network…"));
        broadcastSelf();
        return;
    }


    setStage(Stage::SUBMITTING, QStringLiteral("Sending to %1…").arg(next.left(10)));
    m_retry.start();
}

void IncomingTransfer::submitToNextPeer()
{

    const QString me = myOnionFQDN();
    const int myIdx = m_signingOrder.indexOf(me, Qt::CaseInsensitive);

    QString nextPeer;
    const int start = (myIdx >= 0) ? myIdx + 1 : 0;
    for (int i = start; i < m_signingOrder.size(); ++i) {
        const QString &candidate = m_signingOrder[i];
        if (!m_signatures.contains(candidate, Qt::CaseInsensitive)) {
            nextPeer = candidate;
            break;
        }
    }

    if (nextPeer.isEmpty()) {

        setStage(Stage::BROADCASTING, QStringLiteral("Broadcasting to network…"));
        broadcastSelf();
        return;
    }

    const QJsonObject body{
        {"transfer_ref", m_transferRef},
        {"transfer_blob", m_transferBlobB64},
        {"signing_order", QJsonArray::fromStringList(m_signingOrder)},
        {"who_has_signed",QJsonArray::fromStringList(m_signatures)},
        {"transfer_description", m_description}
    };
    const QString path = QStringLiteral("/api/multisig/transfer/submit?ref=%1").arg(m_walletRef);

    m_submitAttempts[nextPeer] = m_submitAttempts.value(nextPeer, 0) + 1;
    const QString msg = QStringLiteral("Submitting to %1 (attempt %2)")
                            .arg(nextPeer.left(10))
                            .arg(m_submitAttempts[nextPeer]);
    emit statusChanged(msg);

    QString why;
    if (decodeB64FlexibleChecked(m_transferBlobB64, kMaxBlobBytes, &why).isEmpty()) {
        setStage(Stage::ERROR, QStringLiteral("Cannot submit: invalid transfer blob (%1)").arg(why));
        saveToAccount();
        return;
    }

    httpPostAsync(nextPeer, path, body, true);
}

void IncomingTransfer::broadcastSelf()
{
    Wallet *w = walletByRef(m_walletRef);
    if (!w) { setStage(Stage::ERROR, "Wallet not found"); return; }

    if (m_stage != Stage::BROADCASTING)
        setStage(Stage::BROADCASTING, QStringLiteral("Broadcasting to network…"));

    connect(w, &Wallet::multisigSubmitResult,
            this, &IncomingTransfer::onSubmitResult,
            Qt::QueuedConnection);

    const QByteArray blob = tryB64(m_transferBlobB64.toUtf8());
    w->submitSignedMultisig(blob ,m_transferRef);
}

void IncomingTransfer::onSubmitResult(bool ok, QString result, QString operation_caller)
{
    if (operation_caller != m_transferRef ) return;
    if (!ok) {
        setStage(Stage::ERROR, QStringLiteral("Broadcast failed: %1").arg(result));
        saveToAccount();
        return;
    }


    m_txId = result;


    setStage(Stage::COMPLETE, QStringLiteral("Broadcasted"));
    saveToAccount();
    stop("success");
}

void IncomingTransfer::onHttpResult(QString onion, QString path,
                                    QJsonObject res, QString err)
{
    Q_UNUSED(onion)

    if (!err.isEmpty() || res.contains("error")) {

        return;
    }

    if (path.startsWith("/api/multisig/transfer/submit")) {
        const bool ok = res.value("success").toBool(false);
        if (!ok) return;


        setStage(Stage::CHECKING_STATUS,"Submitted – waiting for status");
        saveToAccount();
        emit submittedSuccessfully(m_transferRef);
        stop("success");
        return;
    }


}

void IncomingTransfer::retryRound()
{
    if (m_stopFlag) return;

    switch (m_stage) {
    case Stage::SUBMITTING:
        submitToNextPeer();
        break;
    case Stage::BROADCASTING:

        break;
    default:
        break;
    }
}


void IncomingTransfer::saveToAccount()
{
    if (!m_acct) return;

    QJsonDocument doc = QJsonDocument::fromJson(m_acct->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();

    QJsonObject mon   = root.value("monero").toObject();
    QJsonArray wallets= mon.value("wallets").toArray();

    for (int i=0;i<wallets.size();++i) {
        QJsonObject w = wallets[i].toObject();
        if (w.value("reference").toString()!=m_walletRef &&
            w.value("name").toString()!=m_walletName) { wallets[i]=w; continue; }

        QJsonObject transfers = w.value("transfers").toObject();
        QJsonObject e = transfers.value(m_transferRef).toObject();

        e["transfer_blob"]  = m_transferBlobB64;
        e["signatures"]     = QJsonArray::fromStringList(m_signatures);
        e["stage"]          = stageName(m_stage);
        e["status"]         = m_status;
        if (!m_myOnion.isEmpty()) e["my_onion"] = m_myOnion;
        if (!m_txId.isEmpty()) e["tx_id"] = m_txId;

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (!e.contains("received_at")) e["received_at"]=now;
        if ((m_stage==Stage::SUBMITTING || m_stage==Stage::BROADCASTING) && !e.contains("submitted_at"))
            e["submitted_at"]=now;


        QJsonObject peersObj = e.value("peers").toObject();

        const QString me = myOnionFQDN();
        const bool iSigned = m_signatures.contains(me, Qt::CaseInsensitive);


        peersObj.insert(me, QJsonArray{
                                stageName(m_stage),
                                true,
                                iSigned,
                                m_status
                            });


        e["peers"] = peersObj;

        transfers.insert(m_transferRef, e);
        w["transfers"]=transfers; wallets[i]=w;
        break;
    }

    mon["wallets"] = wallets;
    root["monero"] = mon;
    (void)m_acct->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void IncomingTransfer::setStage(Stage s,const QString &msg)
{
    m_stage = s;
    m_status = msg;
    emit stageChanged(stageName(s));
    if (!msg.isEmpty()) emit statusChanged(msg);
}

QString IncomingTransfer::stageName(Stage s) const
{
    switch(s){
    case Stage::START:            return "START";
    case Stage::VALIDATING:       return "VALIDATING";
    case Stage::SIGNING:          return "SIGNING";
    case Stage::SUBMITTING:       return "SUBMITTING";
    case Stage::BROADCASTING:     return "BROADCASTING";
    case Stage::CHECKING_STATUS:  return "CHECKING_STATUS";
    case Stage::COMPLETE:         return "COMPLETE";
    case Stage::DECLINED:         return "DECLINED";
    case Stage::ERROR:            return "ERROR";
    }
    return {};
}

void IncomingTransfer::stop(const QString &reason)
{
    if (m_stopFlag) return;
    m_stopFlag=true;
    m_retry.stop();
    m_httpPool.waitForDone(5'000);
    emit finished(m_transferRef, reason);
}

QString IncomingTransfer::getTransferDetailsJson() const
{
    QJsonObject o{
        {"wallet_name", m_walletName},
        {"ref",         m_transferRef},
        {"stage",       stageName(m_stage)},
        {"signatures",  QJsonArray::fromStringList(m_signatures)},
        {"signing_order",QJsonArray::fromStringList(m_signingOrder)},
        {"tx_id",       m_txId.isEmpty() ? QString("pending") : m_txId},
        {"my_onion",    m_myOnion}
    };
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}



void IncomingTransfer::httpGetAsync(const QString &onion,const QString &path,bool signedFlag)
{
    QRunnable *task = QRunnable::create([=](){
        const QJsonObject res = httpRequestBlocking(onion,path,"GET",signedFlag);
        const QString err = res.value("error").toString();
        emit onHttpResult(onion,path,res,err);
    });
    task->setAutoDelete(true);
    m_httpPool.start(task);
}

void IncomingTransfer::httpPostAsync(const QString &onion,const QString &path,
                                     const QJsonObject &json,bool signedFlag)
{
    QRunnable *task = QRunnable::create([=](){
        const QJsonObject res = httpRequestBlocking(onion,path,"POST",signedFlag,json);
        const QString err = res.value("error").toString();
        emit onHttpResult(onion,path,res,err);
    });
    task->setAutoDelete(true);
    m_httpPool.start(task);
}

QJsonObject IncomingTransfer::httpRequestBlocking(const QString &onion,
                                                  const QString &path,
                                                  const QByteArray &method,
                                                  bool signedFlag,
                                                  const QJsonObject &json)
{
    const QUrl url(QStringLiteral("http://%1%2").arg(onion, path));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");

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
        if (!transferRef.isEmpty()) canon += QStringLiteral("&transfer_ref=") + transferRef;

        QJsonObject msg{{"ref", m_walletRef}, {"path", canon}, {"ts", ts}};
        if (method.toUpper() == "POST") {
            bodyCompact = QJsonDocument(json).toJson(QJsonDocument::Compact);
            const QByteArray h = QCryptographicHash::hash(bodyCompact, QCryptographicHash::Sha256).toHex();
            msg.insert("body", QString::fromLatin1(h));
        }
        const QByteArray sig = signPayload(msg);
        req.setRawHeader("x-pub", _b64(m_pubKey));
        req.setRawHeader("x-ts",  QByteArray::number(ts));
        req.setRawHeader("x-sig", _b64(sig));
    }

    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, "127.0.0.1", m_tor->socksPort()));

    QEventLoop loop;
    QTimer to; to.setSingleShot(true); to.setInterval(10'000);
    QObject::connect(&to, &QTimer::timeout, &loop, &QEventLoop::quit);

    QNetworkReply *rep = nullptr;
    if (method.toUpper() == "GET") {
        rep = nam.get(req);
    } else {
        if (bodyCompact.isEmpty()) bodyCompact = QJsonDocument(json).toJson(QJsonDocument::Compact);
        rep = nam.post(req, bodyCompact);
    }

    QByteArray buf; buf.reserve(qMin<qint64>(kMaxHttpBytes, 64*1024));
    bool tooLarge=false, redirected=false, badContentType=false;

    QObject::connect(rep, &QNetworkReply::readyRead, &loop, [&](){
        if (tooLarge) return;
        buf += rep->readAll();
        if (buf.size() > kMaxHttpBytes) { tooLarge = true; rep->abort(); }
    });
    QObject::connect(rep, &QNetworkReply::metaDataChanged, &loop, [&](){
        QVariant redir = rep->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redir.isValid()) { redirected=true; rep->abort(); return; }
        const QByteArray ct = rep->header(QNetworkRequest::ContentTypeHeader).toByteArray().toLower();
        if (!ct.startsWith("application/json") && !ct.startsWith("text/plain"))
            badContentType = true;
    });
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    to.start(); loop.exec();

    auto errOut = [&](const QString &e){
        if (rep){ rep->abort(); rep->deleteLater(); }
        return QJsonObject{{"error", e}};
    };

    if (!to.isActive()) return errOut("timeout");
    to.stop();

    if (rep->error() != QNetworkReply::NoError) {
        return errOut(redirected ? "redirect-disallowed" :
                          (tooLarge ? "response-too-large" : rep->errorString()));
    }
    if (badContentType) return errOut("bad-content-type");

    const int httpCode = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpCode != 200) {
        rep->deleteLater();
        return QJsonObject{{"error","http"}, {"code", httpCode}};
    }

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(buf, &pe);
    rep->deleteLater();
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject{{"error","bad-json"}, {"detail", pe.errorString()}};

    return doc.object();
}


QByteArray IncomingTransfer::signPayload(const QJsonObject &payload) const
{
    if (m_scalar.isEmpty() || m_prefix.isEmpty() || m_pubKey.isEmpty())
        return {};
    return ed25519Sign(QJsonDocument(payload).toJson(QJsonDocument::Compact),
                       m_scalar, m_prefix);
}


Wallet* IncomingTransfer::walletByRef(const QString &ref ) const
{ return qobject_cast<Wallet*>(m_wm->walletInstance(walletNameForRef(ref) )); }

QString IncomingTransfer::walletNameForRef(const QString &ref) const
{ return m_wm->walletNameForRef(ref ,m_myOnion); }

QString IncomingTransfer::myOnionFQDN() const
{
    return m_myOnion;
}
QStringList IncomingTransfer::walletPeersByRef(const QString &ref) const
{ return m_wm->peersForRef(ref ,m_myOnion); }


void IncomingTransfer::decline()
{
    if (m_stopFlag) return;

    setStage(Stage::DECLINED, "Transfer declined by user");

    const QString me = myOnionFQDN();
    m_signatures.removeAll(me);

    saveToAccount();

    stop("declined");
}
