#include "win_compat.h"
#include "multisignotifier.h"
#include "multiwalletcontroller.h"
#include "torbackend.h"
#include "accountmanager.h"
#include "cryptoutils_extras.h"   // trySplitV3BlobFlexible, ed25519Sign, _b64, onionFromPub

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QEventLoop>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>



using namespace CryptoUtils;

namespace {
class HttpPostTask : public QRunnable
{
public:
    HttpPostTask(MultisigNotifier *notifier,
                 const QString &onion,
                 const QString &path,
                 bool           signedFlag,
                 const QJsonObject &body)
        : m_notifier(notifier)
        , m_onion(onion)
        , m_path(path)
        , m_signed(signedFlag)
        , m_body(body)
    { setAutoDelete(true); }

    void run() override {
        QJsonObject res = m_notifier->httpRequestBlocking(m_onion, m_path, m_signed, "POST", m_body);
        QString err;
        if (res.contains("error"))
            err = res.value("error").toString();
        emit m_notifier->_httpResult(m_onion, m_path, res, err);
    }

private:
    MultisigNotifier *m_notifier;
    QString           m_onion, m_path;
    bool              m_signed;
    QJsonObject       m_body;
};
}


QString MultisigNotifier::stageToString(Stage s)
{
    switch (s) {
    case Stage::INIT:     return QStringLiteral("INIT");
    case Stage::POSTING:  return QStringLiteral("POSTING");
    case Stage::COMPLETE: return QStringLiteral("COMPLETE");
    case Stage::ERROR:    return QStringLiteral("ERROR");
    }
    return {};
}

QString MultisigNotifier::stageName() const { return stageToString(m_stage); }


MultisigNotifier::MultisigNotifier(MultiWalletController *wm,
                                   TorBackend            *tor,
                                   const QString         &ref,
                                   int                    m,
                                   int                    n,
                                   const QStringList     &allPeers,
                                   const QStringList     &notifyPeers,
                                   const QString         &myOnion,
                                   bool                   isStandaloneNotifier,
                                   QObject               *parent)
    : QObject(parent)
    , m_wm(wm)
    , m_tor(tor)
    , m_ref(ref)
    , m_m(m)
    , m_n(n)
    , m_allPeers(allPeers)
    , m_isStandaloneNotifier(isStandaloneNotifier)
{
    Q_ASSERT(wm && tor);

    auto norm = [](QString s){
        s = s.trimmed().toLower();
        if (!s.endsWith(".onion")) s.append(".onion");
        return s;
    };


    m_myOnion = norm(myOnion);


    AccountManager *acct = nullptr;
    if (auto obj = m_wm->property("accountManager").value<QObject*>())
        acct = qobject_cast<AccountManager*>(obj);
    if (!acct)
        acct = qobject_cast<AccountManager*>(m_wm->parent());

    if (acct) {
        const QString privB64 = acct->torPrivKeyFor(m_myOnion);
        if (!privB64.isEmpty()) {
            QByteArray scalar, prefix, pub;
            if (trySplitV3BlobFlexible(privB64, scalar, prefix, pub)) {

                const QString derived = QString::fromUtf8(onion_from_pub(pub)).toLower();
                const QString derivedOnion = derived.endsWith(".onion") ? derived : (derived + ".onion");
                if (!derivedOnion.compare(m_myOnion, Qt::CaseInsensitive) == 0) {
                    qWarning() << "[MultisigNotifier] Selected onion" << m_myOnion
                               << "does not match key-derived onion" << derivedOnion
                               << "— using derived onion to avoid inconsistency.";
                    m_myOnion = derivedOnion;
                }
                m_scalar = scalar; m_prefix = prefix; m_pubKey = pub;
            } else {
                qWarning() << "[MultisigNotifier] splitV3Blob failed; Tor private key blob malformed for" << m_myOnion;
            }
        } else {
            qWarning() << "[MultisigNotifier] No Tor private key found for onion" << m_myOnion
                       << "; cannot sign notifier requests.";
        }


        QStringList normalizedAll;
        normalizedAll.reserve(m_allPeers.size());
        for (const QString &p : m_allPeers) normalizedAll << norm(p);


        QStringList ourOnions = acct->torOnions();
        for (QString &o : ourOnions) o = norm(o);

        QStringList cleaned;
        QString foundOurOnion;

        for (const QString &p : normalizedAll) {
            const bool isOur = ourOnions.contains(p, Qt::CaseInsensitive);

            if (isOur) {
                if (m_isStandaloneNotifier) {

                    if (p.compare(m_myOnion, Qt::CaseInsensitive) == 0) {

                        continue;
                    } else if (foundOurOnion.isEmpty()) {

                        foundOurOnion = p;
                        cleaned << p;
                    } else {

                        qWarning() << "[MultisigNotifier] Standalone notifier: skipping extra identity"
                                   << p << "(already have" << foundOurOnion << ")";
                        continue;
                    }
                } else {

                    if (p.compare(m_myOnion, Qt::CaseInsensitive) != 0) {

                        continue;
                    } else {

                        if (!cleaned.contains(p, Qt::CaseInsensitive))
                            cleaned << p;
                    }
                }
            } else {

                if (!cleaned.contains(p, Qt::CaseInsensitive))
                    cleaned << p;
            }
        }


        if (m_isStandaloneNotifier) {

            if (cleaned.contains(m_myOnion, Qt::CaseInsensitive)) {
                cleaned.removeAll(m_myOnion);
                qWarning() << "[MultisigNotifier] Removed notifier identity from standalone peers list";
            }


            if (foundOurOnion.isEmpty()) {
                qWarning() << "[MultisigNotifier] Standalone notifier: no target identity found in peers list";
            }
        } else {

            if (!cleaned.contains(m_myOnion, Qt::CaseInsensitive)) {
                cleaned << m_myOnion;
            }
        }

        m_allPeers = cleaned;

        qDebug() << "[MultisigNotifier]" << (m_isStandaloneNotifier ? "Standalone" : "Normal")
                 << "notifier final peers:" << m_allPeers;

    } else {
        qWarning() << "[MultisigNotifier] AccountManager not reachable";
    }


    for (const QString &o : notifyPeers) {
        Peer p; p.onion = norm(o);
        m_peers.insert(p.onion, p);
    }

    m_retry.setInterval(RETRY_MS);
    connect(&m_retry, &QTimer::timeout, this, &MultisigNotifier::retryRound);

    connect(this, &MultisigNotifier::_httpResult,
            this, &MultisigNotifier::onHttp,
            Qt::QueuedConnection);

    m_httpPool.setMaxThreadCount(20);
}

MultisigNotifier::~MultisigNotifier()
{
    m_stop = true;
    m_retry.stop();
    disconnect(this, &MultisigNotifier::_httpResult, this, &MultisigNotifier::onHttp);
    m_httpPool.waitForDone(3000);
}

//──────────────────────────────────────────────────────────────────────────────
void MultisigNotifier::start()
{
    if (m_stage != Stage::INIT) return;
    m_stage = Stage::POSTING;
    emit stageChanged(stageName(), m_myOnion, m_ref);
    m_retry.start();
    retryRound();
}

void MultisigNotifier::cancel()
{
    if (m_stop) return;
    m_stop = true;
    m_retry.stop();

    if (m_inFlight == 0) {
        emit finished(m_myOnion, m_ref, QStringLiteral("cancelled"));
    }

}

//──────────────────────────────────────────────────────────────────────────────
void MultisigNotifier::retryRound()
{
    if (m_stop) return;

    bool allDone = true;
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        Peer &p = it.value();
        if (p.ok) continue;

        allDone = false;
        if (p.trials >= MAX_TRIES_PER_PEER) {
            qWarning().noquote() << "[MultisigNotifier] Peer exhausted:" << p.onion;
            continue;
        }

        QJsonObject body{
            { "ref",   m_ref },
            { "m",     m_m   },
            { "n",     m_n   },
            { "peers", QJsonArray::fromStringList(m_allPeers) }
        };

        httpPostAsync(p.onion,
                      QStringLiteral("/api/multisig/new?ref=%1").arg(m_ref),
                      true,
                      body);
        p.trials += 1;
        p.last = QDateTime::currentSecsSinceEpoch();
    }

    if (allDone) {
        m_stage = Stage::COMPLETE;
        emit stageChanged(stageName(), m_myOnion, m_ref);
        m_retry.stop();
        m_httpPool.waitForDone(3'000);
        emit finished(m_myOnion, m_ref, QStringLiteral("success"));
    }
}

//──────────────────────────────────────────────────────────────────────────────
void MultisigNotifier::httpPostAsync(const QString &onion,
                                     const QString &path,
                                     bool           signedFlag,
                                     const QJsonObject &body)
{
    if (m_httpPool.activeThreadCount() >= m_httpPool.maxThreadCount()) return;
    ++m_inFlight;
    m_httpPool.start(new HttpPostTask(this, onion, path, signedFlag, body));
}

//──────────────────────────────────────────────────────────────────────────────
void MultisigNotifier::onHttp(QString onion, QString ,
                              QJsonObject res, QString err)
{

    if (m_inFlight > 0) --m_inFlight;

    auto it = m_peers.find(onion);
    if (it == m_peers.end()) return;

    if (!err.isEmpty() || res.contains("error")) {
        emit peerStatusChanged(m_myOnion, m_ref);
        return;
    }

    if (res.value("ok").toBool(false)) {
        it->ok = true;
        emit peerStatusChanged(m_myOnion, m_ref);
    }

    if (m_stop && m_inFlight == 0) {
        emit finished(m_myOnion, m_ref, QStringLiteral("cancelled"));
    }

}

//──────────────────────────────────────────────────────────────────────────────
QByteArray MultisigNotifier::sign(const QJsonObject &payload) const
{
    if (m_scalar.isEmpty() || m_prefix.isEmpty())
        return {};
    const QByteArray compact = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return ed25519Sign(compact, m_scalar, m_prefix);
}

QJsonObject MultisigNotifier::httpRequestBlocking(const QString &onion,
                                                  const QString &path,
                                                  bool           signedFlag,
                                                  const QByteArray &method,
                                                  const QJsonObject &jsonBody)
{
    const QUrl   url(QStringLiteral("http://%1%2").arg(onion, path));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));

    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy,
                               QLatin1String("127.0.0.1"),
                               m_tor ? m_tor->socksPort() : 9050));


    QByteArray payloadBytes;
    if (!jsonBody.isEmpty())
        payloadBytes = QJsonDocument(jsonBody).toJson(QJsonDocument::Compact);

    if (signedFlag) {
        const qint64 ts = QDateTime::currentSecsSinceEpoch();


        QUrl u(path);
        QUrlQuery q(u);
        const QString ref = q.queryItemValue(QStringLiteral("ref"));
        QString canon = u.path() + QStringLiteral("?ref=") + ref;
        const QString transferRef = q.queryItemValue(QStringLiteral("transfer_ref"));
        if (!transferRef.isEmpty())
            canon += QStringLiteral("&transfer_ref=") + transferRef;

        QJsonObject msg{
            { "ref",  ref },
            { "path", canon },
            { "ts",   ts }
        };
        if (method.toUpper() == "POST") {
            const QByteArray bodyHash =
                QCryptographicHash::hash(payloadBytes, QCryptographicHash::Sha256).toHex();
            msg.insert(QStringLiteral("body"), QString::fromLatin1(bodyHash));
        }

        const QByteArray sig = sign(msg);
        req.setRawHeader("x-pub", _b64(m_pubKey));
        req.setRawHeader("x-ts",  QByteArray::number(ts));
        req.setRawHeader("x-sig", _b64(sig));
    }


    QEventLoop loop;
    QTimer to; to.setSingleShot(true); to.setInterval(10'000);
    QObject::connect(&to, &QTimer::timeout, &loop, &QEventLoop::quit);

    QNetworkReply *rep = nullptr;
    if (method.toUpper() == "POST")
        rep = nam.post(req, payloadBytes);
    else
        rep = nam.get(req);

    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    to.start();
    loop.exec();

    if (to.isActive() && rep->error() == QNetworkReply::NoError) {
        to.stop();
        const QByteArray data = rep->readAll();
        rep->deleteLater();
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        return doc.isObject() ? doc.object() : QJsonObject{};
    }

    QJsonObject err;
    if (rep) {
        err["error"] = rep->error()==QNetworkReply::TimeoutError
                           ? QStringLiteral("timeout")
                           : QStringLiteral("request_failed");
        rep->abort();
        rep->deleteLater();
    } else {
        err["error"] = QStringLiteral("request_failed");
    }
    err["peer_offline"] = true;
    return err;
}



QVariantList MultisigNotifier::getPeerStatus() const
{
    QVariantList result;
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        const Peer &peer = it.value();
        QVariantMap peerData;
        peerData["onion"] = peer.onion;
        peerData["notified"] = peer.ok;
        peerData["trials"] = peer.trials;
        peerData["lastAttempt"] = peer.last > 0 ?
                                      QDateTime::fromSecsSinceEpoch(peer.last).toString("hh:mm:ss") : "";
        peerData["error"] = "";
        result.append(peerData);
    }
    return result;
}

int MultisigNotifier::getCompletedCount() const
{
    int completed = 0;
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        if (it.value().ok) {
            completed++;
        }
    }
    return completed;
}

int MultisigNotifier::getTotalCount() const
{
    return m_peers.size();
}
