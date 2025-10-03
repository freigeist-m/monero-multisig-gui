#include "win_compat.h"
#include "transfertracker.h"
#include "torbackend.h"
#include "accountmanager.h"
#include "cryptoutils_extras.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QEventLoop>
#include <QTimer>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QDebug>

using namespace CryptoUtils;

static inline QJsonObject loadRoot(AccountManager *acct) {
    return QJsonDocument::fromJson(acct->loadAccountData().toUtf8()).object();
}
static inline void saveRoot(AccountManager *acct, const QJsonObject &root) {
    (void)acct->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

static inline QString _normOnion(QString s) {
    s = s.trimmed().toLower();
    if (!s.isEmpty() && !s.endsWith(".onion")) s.append(".onion");
    return s;
}

namespace {
static constexpr qint64 kMaxJsonBytesTracker = 128 * 1024;
static constexpr int    kHttpTimeoutMs       = 10'000;
}

TransferTracker::TransferTracker(const QString &wref, const QString &tref, const QString &wname,
                                 TorBackend *tor, AccountManager *acct, const QString &myOnion, QObject *parent)
    : QObject(parent)
    , m_walletRef(wref.trimmed())
    , m_transferRef(tref.trimmed())
    , m_walletName(wname.trimmed())
    , m_tor(tor)
    , m_acct(acct)
{

    m_myOnion = _normOnion(myOnion);

    if (m_acct) {
        const QString blob = m_acct->torPrivKeyFor(m_myOnion);
        if (!blob.isEmpty()) {
            QByteArray sc, pr, pb;
            if (trySplitV3BlobFlexible(blob, sc, pr, pb)) {
                m_scalar = sc; m_prefix = pr; m_pubKey = pb;
            } else {
                qWarning() << "[TransferTracker]" << m_transferRef
                           << "failed to parse Tor key for" << m_myOnion;
            }
        } else {
            qWarning() << "[TransferTracker]" << m_transferRef
                       << "no Tor key for" << m_myOnion << "- signed GETs may fail";
        }
    }

}

void TransferTracker::start()
{
    if (m_running) return;
    if (!loadOnceFromAccount()) {
        qWarning() << "[TransferTracker]" << m_transferRef << "could not load from account-data";
        emit finished(m_transferRef, "error");
        return;
    }
    m_running = true;
    QTimer::singleShot(0, this, &TransferTracker::tick);
}

void TransferTracker::stop()
{
    if (m_cancelRequested) return;
    m_cancelRequested = true;
    m_running = false;
    m_pendingFinishResult = QStringLiteral("aborted");

    if (m_inFlight == 0 && !m_finishedSignaled) {
        m_finishedSignaled = true;
        emit finished(m_transferRef, m_pendingFinishResult);
    }
}

void TransferTracker::setBackoffMs(int ms)
{
    m_backoffMs = qBound(500, ms, 10'000);
}

QByteArray TransferTracker::sign(const QJsonObject &payload) const
{
    if (m_scalar.isEmpty() || m_prefix.isEmpty() || m_pubKey.isEmpty())
        return {};
    return ed25519Sign(QJsonDocument(payload).toJson(QJsonDocument::Compact),
                       m_scalar, m_prefix);
}

QJsonObject TransferTracker::httpGetBlockingSigned(const QString &onion, const QString &path)
{
    const QString url = QStringLiteral("http://%1%2").arg(onion, path);
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
#if (QT_VERSION >= QT_VERSION_CHECK(5,9,0))
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(0);
#endif


    const qint64 ts = QDateTime::currentSecsSinceEpoch();
    const QJsonObject msg{{"ref", m_walletRef}, {"path", path}, {"ts", ts}};
    const QByteArray sig = sign(msg);
    if (!sig.isEmpty()) {
        req.setRawHeader("x-pub", _b64(m_pubKey));
        req.setRawHeader("x-ts",  QByteArray::number(ts));
        req.setRawHeader("x-sig", _b64(sig));
    }

    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy,
                               QLatin1String("127.0.0.1"),
                               m_tor ? m_tor->socksPort() : 9050));

    QEventLoop loop;
    QTimer to; to.setSingleShot(true); to.setInterval(kHttpTimeoutMs);
    QObject::connect(&to, &QTimer::timeout, &loop, &QEventLoop::quit);

    QNetworkReply *rep = nam.get(req);

    QByteArray buf;
    buf.reserve(qMin<qint64>(kMaxJsonBytesTracker, 64*1024));
    bool tooLarge = false;
    bool badContentType = false;
    bool redirected = false;

    QObject::connect(rep, &QNetworkReply::readyRead, &loop, [&](){
        if (tooLarge) return;
        buf += rep->readAll();
        if (buf.size() > kMaxJsonBytesTracker) {
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

    to.start(); loop.exec();

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

    if (path.startsWith("/api/multisig/transfer/status")) {
        if (!obj.contains("received_transfer")) obj.insert("received_transfer", false);
        if (!obj.contains("has_signed"))        obj.insert("has_signed", false);
        const QString tx = obj.value("tx_id").toString();
        if (tx.size() > 256) return QJsonObject{{"error","txid-too-long"}};
    }

    return obj;
}

bool TransferTracker::loadOnceFromAccount()
{
    if (!m_acct) return false;

    m_root = loadRoot(m_acct);
    const QJsonObject monero  = m_root.value("monero").toObject();
    const QJsonArray  wallets = monero.value("wallets").toArray();

    m_walletIdx = -1;
    for (int i = 0; i < wallets.size(); ++i) {
        const QJsonObject w = wallets.at(i).toObject();
        const bool match = (w.value("reference").toString() == m_walletRef) ||
                           (w.value("name").toString()      == m_walletName);
        if (match) { m_walletIdx = i; break; }
    }
    if (m_walletIdx < 0) return false;

    const QJsonObject w   = wallets.at(m_walletIdx).toObject();
    const QJsonObject trs = w.value("transfers").toObject();
    const QJsonObject e   = trs.value(m_transferRef).toObject();


    QStringList sessionParticipants;
    if (e.contains("signing_order")) {
        for (const auto &v : e.value("signing_order").toArray())
            sessionParticipants << _normOnion(v.toString());
    } else if (e.contains("peers")) {
        const QJsonObject p = e.value("peers").toObject();
        for (auto it = p.begin(); it != p.end(); ++it)
            sessionParticipants << _normOnion(it.key());
    }
    sessionParticipants.removeAll(QString());
    sessionParticipants.removeDuplicates();


    QStringList peers = sessionParticipants;
    if (!m_myOnion.isEmpty()) {
        peers.removeAll(m_myOnion);
    }
    peers.removeAll(QString());
    peers.removeDuplicates();
    m_peersToPoll = peers;

    if (e.contains("peer_stages")) {
        const QJsonObject ps = e.value("peer_stages").toObject();
        for (auto it = ps.begin(); it != ps.end(); ++it)
            m_peerStageCache.insert(_normOnion(it.key()), it.value().toObject().toVariantMap());
    }

    m_peerInfoCache.clear();
    if (e.contains("peers")) {
        const QJsonObject peersObj = e.value("peers").toObject();
        for (auto it = peersObj.begin(); it != peersObj.end(); ++it) {
            const QString key = _normOnion(it.key());
            const QJsonArray arr = it.value().toArray();
            QVariantList lst = arr.toVariantList();
            while (lst.size() < 4) lst << QVariant();
            if (lst.size() >= 4) {
                if (!lst[0].isValid()) lst[0] = QString();
                if (!lst[1].isValid()) lst[1] = false;
                if (!lst[2].isValid()) lst[2] = false;
                if (!lst[3].isValid()) lst[3] = QString();
            }
            m_peerInfoCache.insert(key, lst);
        }
    }


    m_lastAggregateStage = e.value("stage").toString();
    m_lastTxId           = e.value("tx_id").toString();
    m_lastTime           = e.value("time").toVariant().toLongLong();

    if (m_lastAggregateStage.isEmpty() || !isTerminal(m_lastAggregateStage)) {
        persistAggregateIfChanged(QStringLiteral("CHECKING_STATUS"),
                                  m_lastTxId.isEmpty() ? QStringLiteral("pending") : m_lastTxId,
                                  m_lastTime > 0 ? m_lastTime : QDateTime::currentSecsSinceEpoch());
        writeBack();
    }

    m_loaded = true;
    return true;
}

bool TransferTracker::writeBack()
{
    if (!m_acct) return false;
    saveRoot(m_acct, m_root);
    return true;
}

bool TransferTracker::persistPeerStageIfChanged(const QString &onion,
                                                const QString &stage,
                                                const QString &txid,
                                                qint64 time)
{
    const QString key = onion.trimmed().toLower();
    if (!m_peersToPoll.contains(key))
        return false;

    QVariantMap incoming{{"stage", stage},
                         {"tx_id", txid},
                         {"time",  time > 0 ? time : QDateTime::currentSecsSinceEpoch()}};

    const QVariantMap old = m_peerStageCache.value(key);
    if (old == incoming) return false;

    m_peerStageCache.insert(key, incoming);

    QJsonObject monero  = m_root.value("monero").toObject();
    QJsonArray  wallets = monero.value("wallets").toArray();
    if (m_walletIdx < 0 || m_walletIdx >= wallets.size()) return false;

    QJsonObject w   = wallets.at(m_walletIdx).toObject();
    QJsonObject trs = w.value("transfers").toObject();
    QJsonObject e   = trs.value(m_transferRef).toObject();

    QJsonObject peerStages = e.value("peer_stages").toObject();
    peerStages.insert(key, QJsonObject::fromVariantMap(incoming));
    e.insert("peer_stages", peerStages);

    trs.insert(m_transferRef, e);
    w.insert("transfers", trs);
    wallets[m_walletIdx] = w;
    monero.insert("wallets", wallets);
    m_root.insert("monero", monero);

    writeBack();
    return true;
}

bool TransferTracker::persistAggregateIfChanged(const QString &stageCandidate,
                                                const QString &txidCandidate,
                                                qint64 timeCandidate)
{
    QString stageOut = stageCandidate;
    QString txidOut  = txidCandidate;
    qint64  timeOut  = (timeCandidate > 0 ? timeCandidate
                                        : QDateTime::currentSecsSinceEpoch());

    if (!isTerminal(stageOut))
        stageOut = QStringLiteral("CHECKING_STATUS");

    if (stageOut == m_lastAggregateStage &&
        txidOut   == m_lastTxId &&
        timeOut   == m_lastTime)
        return false;

    m_lastAggregateStage = stageOut;
    m_lastTxId           = txidOut;
    m_lastTime           = timeOut;


    QJsonObject monero  = m_root.value("monero").toObject();
    QJsonArray  wallets = monero.value("wallets").toArray();
    if (m_walletIdx < 0 || m_walletIdx >= wallets.size()) return false;

    QJsonObject w   = wallets.at(m_walletIdx).toObject();
    QJsonObject trs = w.value("transfers").toObject();
    QJsonObject e   = trs.value(m_transferRef).toObject();

    e.insert("stage",  stageOut);
    e.insert("tx_id",  txidOut.isEmpty() ? QStringLiteral("pending") : txidOut);
    e.insert("status", stageOut == "CHECKING_STATUS"
                           ? QStringLiteral("Checking status")
                           : (stageOut == "COMPLETE" ? QStringLiteral("Transfer completed")
                                                     : stageOut));
    e.insert("time",   timeOut);

    trs.insert(m_transferRef, e);
    w.insert("transfers", trs);
    wallets[m_walletIdx] = w;
    monero.insert("wallets", wallets);
    m_root.insert("monero", monero);

    writeBack();
    return true;
}

int TransferTracker::stageRank(const QString &s)
{
    if (s=="FAILED")            return 2;
    if (s=="COMPLETE")          return 6;
    if (s=="BROADCASTING")      return 5;
    if (s=="CHECKING_STATUS")   return 4;
    if (s=="DECLINED")          return 3;
    if (s=="ERROR")             return 2;
    if (s=="RECEIVED")          return 1;
    return 0;
}

bool TransferTracker::isTerminal(const QString &stage) const
{
    return stage=="COMPLETE" ||  stage=="DECLINED" || stage=="ERROR" || stage=="FAILED";;
}


void TransferTracker::tick()
{
    if (!m_running) return;
    if (!m_loaded && !loadOnceFromAccount()) {
        emit finished(m_transferRef, "error");
        return;
    }

    if (m_peersToPoll.isEmpty()) {
        if (!m_cancelRequested) {
            QTimer::singleShot(m_backoffMs, this, &TransferTracker::tick);
        } else if (m_inFlight == 0 && !m_finishedSignaled) {
            m_finishedSignaled = true;
            emit finished(m_transferRef, m_pendingFinishResult.isEmpty()
                                             ? QStringLiteral("aborted")
                                             : m_pendingFinishResult);
        }
        return;
    }

    const QString path = QStringLiteral(
                             "/api/multisig/transfer/status?ref=%1&transfer_ref=%2")
                             .arg(m_walletRef, m_transferRef);

    QString bestStage = lastBestStageFromAccount();
    QString bestTxid = m_lastTxId;
    qint64  bestTime = (m_lastTime > 0 ? m_lastTime : 0);

    bool sawTerminal = false;
    QString terminalStage;

    for (const QString &onionIn : m_peersToPoll) {
        if (m_cancelRequested) break;
        const QString onion = onionIn.trimmed().toLower();
        ++m_inFlight;
        const QJsonObject res = httpGetBlockingSigned(onion, path);
        if (m_inFlight > 0) --m_inFlight;
        if (m_cancelRequested) break;

        if (res.isEmpty() || res.contains("error")) {
            continue;
        }
        const QString ref = res.value("ref").toString();
        const QString transferRef = res.value("transferRef").toString();

        if (ref != m_walletRef || transferRef != m_transferRef ) {
            continue; }

        const QString stage = res.value("stage_name").toString();
        const QString status = res.value("status").toString();
        const QString txid  = res.value("tx_id").toString();
        const qint64  t     = res.value("time").toVariant().toLongLong();
        const bool    received = res.value("received_transfer").toBool(false);
        const bool    hasSignedRes = res.value("has_signed").toBool(false);

        const bool hasSigned = hasSignedRes || (stage=="CHECKING_STATUS") || (stage=="COMPLETE");
        persistPeerInfoIfChanged(onion, stage, received, hasSigned, status);

        if (stage=="CHECKING_STATUS" || stage=="COMPLETE")
            ensureSignatureListed(onion);

        persistPeerStageIfChanged(onion, stage, txid, t);

        if (stageRank(stage) > stageRank(bestStage)) {
            bestStage = stage;
            bestTxid  = !txid.isEmpty() ? txid : bestTxid;
            bestTime  = (t > 0 ? t : QDateTime::currentSecsSinceEpoch());
        }

        if (isTerminal(stage)) {
            sawTerminal   = true;
            terminalStage = stage;
        }
    }

    if (m_cancelRequested) {
        if (m_inFlight == 0 && !m_finishedSignaled) {
            m_finishedSignaled = true;
            emit finished(m_transferRef, m_pendingFinishResult.isEmpty()
                                             ? QStringLiteral("aborted")
                                             : m_pendingFinishResult);
        }
        return;
    }

    if (!sawTerminal) {
        persistAggregateIfChanged(bestStage, bestTxid, bestTime);
        emit progress(m_transferRef, QVariantMap{
                                         {"stage_name", QStringLiteral("CHECKING_STATUS")},
                                         {"tx_id",      m_lastTxId.isEmpty() ? "pending" : m_lastTxId},
                                         {"time",       m_lastTime}
                                     });
        if (!m_cancelRequested) QTimer::singleShot(m_backoffMs, this, &TransferTracker::tick);
        return;
    }

    QString txToWrite = bestTxid.isEmpty() ? m_lastTxId : bestTxid;
    m_lastAggregateStage.clear();
    persistAggregateIfChanged(terminalStage,
                              txToWrite,
                              bestTime > 0 ? bestTime : QDateTime::currentSecsSinceEpoch());

    QString result = "success";
    if (terminalStage=="DECLINED") result = "declined";
    else if (terminalStage=="ERROR" || terminalStage=="FAILED") result = "error";

    m_running = false;
    if (!m_finishedSignaled) {
        m_finishedSignaled = true;
        emit finished(m_transferRef, result);
    }
}


QString TransferTracker::lastBestStageFromAccount() const
{
    QString best;

    for (auto it = m_peerStageCache.cbegin(); it != m_peerStageCache.cend(); ++it) {
        const QString st = it.value().value("stage").toString();
        if (stageRank(st) > stageRank(best)) best = st;
    }

    if (best.isEmpty()) {
        for (auto it = m_peerInfoCache.cbegin(); it != m_peerInfoCache.cend(); ++it) {
            const QVariantList lst = it.value();
            const QString st = (lst.size() >= 1 ? lst[0].toString() : QString());
            if (stageRank(st) > stageRank(best)) best = st;
        }
    }

    if (best.isEmpty()) best = m_lastAggregateStage;

    return best;
}

bool TransferTracker::persistPeerInfoIfChanged(const QString &onion,
                                               const QString &stage,
                                               bool receivedTransfer,
                                               bool hasSigned,
                                               const QString &status)
{
    const QString key = onion.trimmed().toLower();
    if (!m_peersToPoll.contains(key)) return false;

    QVariantList incoming{ stage, receivedTransfer, hasSigned ,status};
    const QVariantList old = m_peerInfoCache.value(key);
    if (old.size() == 4 &&
        old[0].toString() == incoming[0].toString() &&
        old[1].toBool()   == incoming[1].toBool() &&
        old[2].toBool()   == incoming[2].toBool() &&
        old[3].toString()  ==  incoming[3].toString()   )   {
        return false;
    }

    m_peerInfoCache.insert(key, incoming);

    QJsonObject monero  = m_root.value("monero").toObject();
    QJsonArray  wallets = monero.value("wallets").toArray();
    if (m_walletIdx < 0 || m_walletIdx >= wallets.size()) return false;

    QJsonObject w   = wallets.at(m_walletIdx).toObject();
    QJsonObject trs = w.value("transfers").toObject();
    QJsonObject e   = trs.value(m_transferRef).toObject();

    QJsonObject peers = e.value("peers").toObject();
    peers.insert(key, QJsonArray{ stage, receivedTransfer, hasSigned , status });
    e.insert("peers", peers);

    trs.insert(m_transferRef, e);
    w.insert("transfers", trs);
    wallets[m_walletIdx] = w;
    monero.insert("wallets", wallets);
    m_root.insert("monero", monero);

    writeBack();
    return true;
}

bool TransferTracker::ensureSignatureListed(const QString &onion)
{
    const QString key = onion.trimmed().toLower();
    QJsonObject monero  = m_root.value("monero").toObject();
    QJsonArray  wallets = monero.value("wallets").toArray();
    if (m_walletIdx < 0 || m_walletIdx >= wallets.size()) return false;

    QJsonObject w   = wallets.at(m_walletIdx).toObject();
    QJsonObject trs = w.value("transfers").toObject();
    QJsonObject e   = trs.value(m_transferRef).toObject();

    QJsonArray sigs = e.value("signatures").toArray();
    bool present = false;
    for (const auto &v : sigs) if (v.toString().trimmed().toLower() == key) { present = true; break; }
    if (present) return false;

    sigs.append(key);
    e.insert("signatures", sigs);

    trs.insert(m_transferRef, e);
    w.insert("transfers", trs);
    wallets[m_walletIdx] = w;
    monero.insert("wallets", wallets);
    m_root.insert("monero", monero);

    writeBack();
    return true;
}
