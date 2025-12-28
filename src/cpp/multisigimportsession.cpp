#include "win_compat.h"
#include "multisigimportsession.h"
#include "multiwalletcontroller.h"
#include "torbackend.h"
#include "accountmanager.h"
#include "wallet.h"
#include "cryptoutils_extras.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QRunnable>
#include <QPointer>
#include <QDebug>


using namespace CryptoUtils;

static constexpr int HTTP_TIMEOUT_MS = 10'000;
static constexpr Qt::ConnectionType kQueuedUnique =
    static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection);

namespace {
inline QByteArray b64urlNoPad(const QByteArray &raw) {
    return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
inline qint64 nowSecs() { return QDateTime::currentSecsSinceEpoch(); }

static QByteArray b64urlDecodeNoPad(QByteArray s)
{
    while (s.size() % 4) s.append('=');
    return QByteArray::fromBase64(s, QByteArray::Base64UrlEncoding);
}

static inline QString fqdn(QString s) {
    s = s.trimmed().toLower();
    return s.endsWith(".onion") ? s : s + ".onion";
}


class ImportHttpTask : public QRunnable
{
public:
    ImportHttpTask(MultisigImportSession *sess,
                   QString onion, QString path, bool signedFlag , QString walletName)
        : m_sess(sess), m_onion(std::move(onion)),
        m_path(std::move(path)), m_signed(signedFlag), m_wallet(std::move(walletName))  {
        setAutoDelete(true);

    }
    void run() override {


        QPointer<MultisigImportSession> s = m_sess;
        if (!s) return;
        const QJsonObject res = s->httpGetBlocking(m_onion, m_path, m_signed, m_wallet);



        const QString err = res.value("error").toString();

        s = m_sess;
        if (!s) return;
        emit s->_httpResult(m_onion, m_path, res, err, m_wallet);

    }
private:
    QPointer<MultisigImportSession> m_sess;
    QString m_onion, m_path , m_wallet;
    bool    m_signed = false;
};


static constexpr qint64 kMaxJsonBytesImport = 256 * 1024;
static constexpr int    kMaxInfoDecoded     = 256 * 1024;


inline bool isBase64UrlString(const QByteArray &s) {
    if (s.isEmpty()) return true;
    for (unsigned char c : s) {
        if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'))
            return false;
    }
    return true;
}

inline bool eqSha256Hex(const QByteArray &data, const QString &hexLower) {
    const QByteArray got = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    return got == hexLower.toLatin1();
}


}

static inline QString normOnionFqdn(QString s) {
    s = s.trimmed().toLower();
    return s.endsWith(".onion") ? s : (s + ".onion");
}

QString MultisigImportSession::_resolveOwnOnionForWallet(const QString &walletName) const
{
    if (!m_wm) return {};

    const QVariantMap meta = m_wm->getWalletMeta(walletName);
    const QString myMeta = meta.value(QStringLiteral("my_onion")).toString().trimmed().toLower();
    if (!myMeta.isEmpty()) {
        const QString fq = fqdn(myMeta);

        for (const QString &own : m_allOnions) {
            if (QString::compare(fq, own, Qt::CaseInsensitive) == 0)
                return fq;
        }

    }


    QStringList peers = m_wm->peersForWallet(walletName);
    for (QString &p : peers) p = fqdn(p);

    QStringList matches;
    for (const QString &mine : m_allOnions)
        if (peers.contains(mine, Qt::CaseInsensitive))
            matches << mine;

    return (matches.size() == 1) ? matches.first() : QString();
}

bool MultisigImportSession::_resolveKeysForWallet(const QString &walletName,
                                                  KeyMat *outKey, QString *outOnion) const
{
    const QString my = _resolveOwnOnionForWallet(walletName);
    if (my.isEmpty()) return false;

    auto it = m_keysByOnion.find(my);
    if (it == m_keysByOnion.end()) return false;

    if (outKey)   *outKey   = it.value();
    if (outOnion) *outOnion = my;
    return true;
}



MultisigImportSession::MultisigImportSession(QObject *parent)
    : QObject(parent)
{

    connect(this, &MultisigImportSession::_httpResult,
            this, &MultisigImportSession::onHttp,
            Qt::QueuedConnection);

    m_checkTimer.setInterval(60'000);
    connect(&m_checkTimer, &QTimer::timeout,
            this, &MultisigImportSession::_checkAllWallets);

    m_httpPool.setMaxThreadCount(20);

}

MultisigImportSession::~MultisigImportSession()
{


    for (auto it = m_importConnections.begin(); it != m_importConnections.end(); ++it) {
        disconnect(it.value());
    }
    m_importConnections.clear();
    stop();
    m_httpPool.waitForDone(HTTP_TIMEOUT_MS + 1000);

}

void MultisigImportSession::initialize(MultiWalletController *wm, TorBackend *tor)
{

    m_wm  = wm;
    m_tor = tor;
}

QString MultisigImportSession::_accountCacheDir() const
{
    if (!m_wm) return {};

    AccountManager *am = m_wm->accountManager();
    if (!am) return {};

    const QString acct = am->currentAccount().trimmed();
    if (acct.isEmpty())
        return QDir(am->walletsDir()).filePath("multisig_cache");

    return QDir(am->walletsDir()).filePath(acct + "/multisig_cache");
}

void MultisigImportSession::start()
{

    if (m_running) {

        return;
    }


    _loadSigningKeys();

    if (!_isReady()) {

        emit statusChanged(QStringLiteral("Cannot start: system not ready"));
        return;
    }

    m_inFlight = 0;
    m_stopRequested = false;
    m_stoppedSignaled = false;

    m_running = true;
    emit runningChanged();

    m_checkTimer.start();
    emit sessionStarted();
    emit statusChanged(QStringLiteral("Multisig import session started"));

    _checkAllWallets();
}

void MultisigImportSession::stop()
{
    if (m_stopRequested) return;
    m_stopRequested = true;
    m_running = false;

    emit runningChanged();

    m_checkTimer.stop();
    if (m_inFlight == 0 && !m_stoppedSignaled) {
        m_stoppedSignaled = true;
        emit sessionStopped();
        emit statusChanged(QStringLiteral("Multisig import session stopped"));
    } else {
        emit statusChanged(QStringLiteral("Stopping multisig import session…"));
    }

}

bool MultisigImportSession::_isReady() const
{
    if (!m_wm || !m_tor) return false;

    AccountManager *am = nullptr;
    if (auto obj = m_wm->property("accountManager").value<QObject*>())
        am = qobject_cast<AccountManager*>(obj);
    if (!am || !am->isAuthenticated()) return false;

    return !m_keysByOnion.isEmpty();
}

void MultisigImportSession::_loadSigningKeys()
{
    m_keysByOnion.clear();
    m_allOnions.clear();

    if (!m_wm) return;

    AccountManager *acct = nullptr;
    if (auto obj = m_wm->property("accountManager").value<QObject*>())
        acct = qobject_cast<AccountManager*>(obj);
    if (!acct) acct = qobject_cast<AccountManager*>(m_wm->parent());
    if (!acct) return;

    const QStringList onionsRaw = acct->torOnions();
    for (QString o : onionsRaw) {
        o = fqdn(o);
        const QString blob = acct->torPrivKeyFor(o);
        if (blob.isEmpty()) continue;

        QByteArray sc, pr, pb;
        if (!CryptoUtils::trySplitV3BlobFlexible(blob, sc, pr, pb)) continue;

        m_keysByOnion.insert(o, KeyMat{sc, pr, pb});
        m_allOnions << o;
    }
}


void MultisigImportSession::_checkAllWallets()
{

    if (!m_running || m_stopRequested || !_isReady() || !m_wm) {

        return;
    }

    const QStringList names = m_wm->connectedWalletNames();


    for (const QString &walletName : names) {
        const QVariantMap meta = m_wm->getWalletMeta(walletName);
        bool isMultisig = meta.value("multisig", true).toBool();


        if (!isMultisig) {

            continue;
        }

        QObject *w = m_wm->walletInstance(walletName);

        const bool needsImport = w->property("has_multisig_partial_key_images").toBool();



        if (needsImport) {

            _processWalletImport(walletName);
        } else {

        }
    }

}

bool MultisigImportSession::_walletNeedsImport(const QString &walletName) const
{

    QObject *w = m_wm->walletInstance(walletName);
    if (!w) {

        return true;
    }


    QMetaObject::invokeMethod(w, "refreshHasMultisigPartialKeyImages", Qt::QueuedConnection);

    const bool need = w->property("has_multisig_partial_key_images").toBool();

    return need;
}

void MultisigImportSession::_processWalletImport(const QString &walletName)
{
    if (m_stopRequested) return;


    const QVariantMap meta  = m_wm->getWalletMeta(walletName);
    const QStringList peers = m_wm->peersForWallet(walletName);


    if (peers.isEmpty()) {

        return;
    }
    const QString ref = meta.value("reference").toString();
    const QString myForThisWallet = _resolveOwnOnionForWallet(walletName);


    QStringList peersToCheck;
    for (const QString &p : peers) {
        const QString q = fqdn(p);
        if (!myForThisWallet.isEmpty() &&
            QString::compare(q, myForThisWallet, Qt::CaseInsensitive) == 0)
            continue;
        peersToCheck << q;
    }



    if (peersToCheck.isEmpty()) {

        return;
    }

    const QJsonObject cached = _loadCachedInfos(walletName);

    const QStringList toQuery = _filterStalePeers(peersToCheck, cached);




    for (const QString &peer : toQuery) {
        if (ref.isEmpty()) continue;
        const QString path = QStringLiteral("/api/multisig/transfer/request_info?ref=%1").arg(ref);
        httpGetAsync(peer, path, true, walletName);
    }

    _attemptImport(walletName, cached);
}

QJsonObject MultisigImportSession::_loadCachedInfos(const QString &walletName) const
{
    const QString file = QDir(_accountCacheDir()).filePath(QStringLiteral("%1/peer_infos.json").arg(walletName));


    QFile f(file);
    if (!f.exists()) {

        return {};
    }
    if (!f.open(QIODevice::ReadOnly)) {

        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());

    return doc.object();
}

void MultisigImportSession::_saveCachedInfo(const QString &walletName,
                                            const QString &peerOnion,
                                            const QByteArray &infoRaw)
{

    const QString dir = QDir(_accountCacheDir()).filePath(walletName);
    QDir().mkpath(dir);
    const QString file = QDir(dir).filePath("peer_infos.json");


    QJsonObject all = _loadCachedInfos(walletName);

    const QString b64 = QString::fromLatin1(
        infoRaw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    QJsonObject entry{
        { "info_b64",      b64 },
        { "timestamp", qint64(nowSecs()) },
        { "imported",  false }
    };
    all.insert(peerOnion, entry);

    QFile f(file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(all).toJson(QJsonDocument::Indented));
        qDebug() << "[_saveCachedInfo] Successfully saved cache with" << all.size() << "total entries";
    } else {
        qDebug() << "[_saveCachedInfo] Failed to open cache file for writing:" << file;
    }
}

QStringList MultisigImportSession::_filterStalePeers(const QStringList &peers,
                                                     const QJsonObject &cached) const
{

    const qint64 t = nowSecs();
    QStringList out;
    for (const QString &p : peers) {
        const QJsonObject e = cached.value(p).toObject();
        const qint64 ts     = static_cast<qint64>(e.value("timestamp").toDouble(0));
        const bool imported = e.value("imported").toBool(false);
        const bool fresh    = (ts > 0) && ((t - ts) <= m_cacheExpirySecs);

        if (!(fresh && imported)) {
            out << p;

        }
    }

    return out;
}

void MultisigImportSession::_attemptImport(const QString &walletName, const QJsonObject &cached)
{

    if (cached.isEmpty()) {
        return;
    }

    QStringList peersToMark;
    QList<QByteArray> payloads;
    QList<PendingImportEntry> pend;

    const qint64 t = nowSecs();
    for (auto it = cached.begin(); it != cached.end(); ++it) {
        const QString peer = it.key();
        const QJsonObject e = it.value().toObject();
        const qint64 ts = static_cast<qint64>(e.value("timestamp").toDouble(0));
        const bool imported = e.value("imported").toBool(false);
        const bool withinExpiry = (t - ts) <= m_cacheExpirySecs;


        if (withinExpiry && !imported) {

            const QString b64 = e.value("info_b64").toString();
            if (b64.isEmpty()) continue;

            const QByteArray b64ba = b64.toLatin1();
            if (!isBase64UrlString(b64ba)) {
                qDebug() << "[_attemptImport] invalid base64url alphabet for peer" << peer;
                continue;
            }

            const QByteArray raw = QByteArray::fromBase64(
                b64.toLatin1(),
                QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

            if (raw.isEmpty()) {
                qDebug() << "[_attemptImport] bad base64 for peer" << peer;
                continue;
            }

            if (raw.size() > kMaxInfoDecoded) {
                qDebug() << "[_attemptImport] cached info too large for peer" << peer << raw.size();
                continue;
            }

            payloads     << raw;
            peersToMark  << peer;
            pend.append(PendingImportEntry{peer, raw});

        }
    }


    if (payloads.isEmpty()) {

        return;
    }

    auto connIt = m_importConnections.find(walletName);
    if (connIt != m_importConnections.end()) {
        disconnect(connIt.value());
        m_importConnections.erase(connIt);
    }

    m_pendingImports.insert(walletName, pend);
    _recordImportAttempt(walletName, peersToMark);

    Wallet *w = qobject_cast<Wallet*>(m_wm->walletInstance(walletName));
    if (!w) {
        return;
    }

    qDebug() << "[_attemptImport] Connecting import result signal for wallet:" << walletName;


    auto connection = connect(
        w, &Wallet::multisigInfosImported,
        this, [this, walletName](int nImported, bool needsRescan, QString operation_caller) {

            if (nImported > 0) {
                _onImportMultisigResult(walletName, nImported);
            } else {

                m_pendingImports.remove(walletName);

                auto connIt = m_importConnections.find(walletName);
                if (connIt != m_importConnections.end()) {
                    disconnect(connIt.value());
                    m_importConnections.erase(connIt);
                }
            }
        },
        Qt::QueuedConnection);

    m_importConnections.insert(walletName, connection);

    w->importMultisigInfosBulk(payloads,walletName);
}

void MultisigImportSession::_onImportMultisigResult(const QString &walletName, int actualImported)
{

    auto connIt = m_importConnections.find(walletName);
    if (connIt != m_importConnections.end()) {
        disconnect(connIt.value());
        m_importConnections.erase(connIt);
    }


    const QList<PendingImportEntry> entries = m_pendingImports.take(walletName);
    QStringList peers;
    for (const auto &e : entries) peers << e.peer;


    _recordImportCompleted(walletName, entries, actualImported);
    emit walletImportCompleted(walletName);

}



void MultisigImportSession::httpGetAsync(const QString &onion,
                                         const QString &path,
                                         bool signedFlag, const QString &walletName)
{
    if (m_stopRequested) return;


    if (m_httpPool.activeThreadCount() >= m_httpPool.maxThreadCount()) {

        return;
    }

    ++m_inFlight;
    m_httpPool.start(new ImportHttpTask(this, onion, path, signedFlag, walletName));
}

QJsonObject MultisigImportSession::httpGetBlocking(const QString &onion,
                                                   const QString &path,
                                                   bool signedFlag,
                                                   const QString &walletName)
{
    const QString url = QStringLiteral("http://%1%2").arg(onion, path);
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");

#if (QT_VERSION >= QT_VERSION_CHECK(5,9,0))
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(0);
#endif

    if (signedFlag) {
        KeyMat km; QString myOnion;
        if (!_resolveKeysForWallet(walletName, &km, &myOnion)) {
            return QJsonObject{{"error","no_identity_for_wallet"}};
        }
        const qint64 ts = nowSecs();


        QUrl u(path);
        QUrlQuery q(u);
        const QString ref = q.queryItemValue(QStringLiteral("ref"));
        const QString canon = u.path() + QStringLiteral("?ref=") + ref;

        const QJsonObject msg{{"ref", ref}, {"path", canon}, {"ts", ts}};
        const QByteArray sig = _signPayload(msg, km);

        req.setRawHeader("x-pub", b64urlNoPad(km.pub));
        req.setRawHeader("x-ts",  QByteArray::number(ts));
        req.setRawHeader("x-sig", b64urlNoPad(sig));
    }

    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy, "127.0.0.1",
                               m_tor ? m_tor->socksPort() : 9050));

    QEventLoop loop;
    QTimer to; to.setSingleShot(true); to.setInterval(HTTP_TIMEOUT_MS);
    QObject::connect(&to, &QTimer::timeout, &loop, &QEventLoop::quit);

    QNetworkReply *rep = nam.get(req);


    QByteArray buf;
    buf.reserve(qMin<qint64>(kMaxJsonBytesImport, 64*1024));
    bool tooLarge = false;
    bool redirected = false;
    bool badContentType = false;

    QObject::connect(rep, &QNetworkReply::readyRead, &loop, [&](){
        if (tooLarge) return;
        buf += rep->readAll();
        if (buf.size() > kMaxJsonBytesImport) {
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

        if (!ctype.startsWith("application/json") && !ctype.startsWith("text/plain")) {
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


    if (path.startsWith("/api/multisig/transfer/request_info")) {
        const QJsonValue v = obj.value("multisig_info_b64");
        if (v.isString()) {
            const QByteArray b64 = v.toString().toLatin1();
            if (!b64.isEmpty()) {
                if (!isBase64UrlString(b64)) {
                    return QJsonObject{{"error","bad-b64"}};
                }
                const QByteArray decoded = QByteArray::fromBase64(
                    b64, QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
                if (decoded.isEmpty()) {
                    return QJsonObject{{"error","b64-decode-failed"}};
                }
                if (decoded.size() > kMaxInfoDecoded) {
                    return QJsonObject{{"error","info-too-large"}, {"len", decoded.size()}};
                }

                const int expLen = obj.value("len").toInt(-1);
                if (expLen >= 0 && expLen != decoded.size()) {
                    return QJsonObject{{"error","len-mismatch"}, {"got", decoded.size()}, {"exp", expLen}};
                }
                const QString sha = obj.value("sha256").toString();
                if (!sha.isEmpty() && !eqSha256Hex(decoded, sha)) {
                    return QJsonObject{{"error","sha256-mismatch"}};
                }
            }
        }
    }

    return obj;
}



QByteArray MultisigImportSession::_signPayload(const QJsonObject &msg, const KeyMat &km) const
{
    if (km.scalar.isEmpty() || km.prefix.isEmpty()) return {};
    return CryptoUtils::ed25519Sign(QJsonDocument(msg).toJson(QJsonDocument::Compact),
                                    km.scalar, km.prefix);
}

QByteArray MultisigImportSession::_pubKey() const
{

    return m_pub;
}



void MultisigImportSession::onHttp(QString onion, QString path, QJsonObject res, QString err,  QString walletName)
{

    if (m_inFlight > 0) --m_inFlight;

    if (m_stopRequested) {
        if (m_inFlight == 0 && !m_stoppedSignaled) {
            m_stoppedSignaled = true;
            emit sessionStopped();
            emit statusChanged(QStringLiteral("Multisig import session stopped"));
        }
        return;
    }


    if (!err.isEmpty() || res.contains("error")) {
        qDebug() << "[onHttp] HTTP response has error, ignoring" << err;
        return;
    }

    if (path.startsWith("/api/multisig/transfer/request_info")) {
        const QUrl u(path);
        const QString ref = QUrlQuery(u).queryItemValue("ref");
        if (walletName.isEmpty()) {
            qDebug() << "[onHttp] Missing walletName for response; dropping";
            return;
        }



        const QString infoB64 = res.value("multisig_info_b64").toString();
        qDebug() << "[onHttp] HTTP response infoB64" << infoB64 ;

        QByteArray decoded;
        if (!infoB64.isEmpty()) {decoded = b64urlDecodeNoPad(infoB64.toLatin1());}

        qDebug() << "[onHttp] HTTP response decoded is empty" <<decoded.isEmpty() ;

        if (decoded.isEmpty()) return;

        _saveCachedInfo(walletName, onion, decoded);
        _recordPeerInfoReceived(walletName, onion);
        emit peerInfoReceived(walletName, onion);

        _attemptImport(walletName, _loadCachedInfos(walletName));
    }
}



void MultisigImportSession::_recordPeerInfoReceived(const QString &walletName, const QString &peerOnion)
{

    QJsonObject ev{
        { "timestamp", qint64(nowSecs()) },
        { "wallet_name", walletName },
        { "peer_onion", peerOnion.size()>16 ? peerOnion.left(16) + "..." : peerOnion },
        { "action", "info_received" }
    };
    m_peerActivity.prepend(ev);
    if (m_peerActivity.size() > 100) {

        m_peerActivity = m_peerActivity.mid(0,100);
    }

}

void MultisigImportSession::_recordImportAttempt(const QString &walletName, const QStringList &peers)
{

    QJsonObject st = m_importActivity.value(walletName);
    st["wallet_name"] = walletName;
    st["status"] = "importing";
    st["last_activity_time"] = qint64(nowSecs());
    st["current_import_peers"] = QJsonArray::fromStringList(peers);
    if (!st.contains("total_imports")) st["total_imports"] = 0;
    m_importActivity.insert(walletName, st);

}

void MultisigImportSession::_recordImportCompleted(const QString &walletName,
                                                   const QList<PendingImportEntry> &entries,
                                                   int actualImported)
{
    QStringList peers;
    for (const auto &e : entries) peers << e.peer;


    QJsonObject st = m_importActivity.value(walletName);
    st["wallet_name"]        = walletName;
    st["status"]             = "completed";
    st["last_import_time"]   = qint64(nowSecs());
    st["last_import_peers"]  = QJsonArray::fromStringList(peers);
    st["last_import_count"]  = actualImported;


    if (actualImported > 0) {
        st["total_imports"] = st.value("total_imports").toInt(0) + 1;
        st["total_items_imported"] = st.value("total_items_imported").toInt(0) + actualImported;
    } else {

        st["last_failed_attempt_time"] = qint64(nowSecs());
        st["failed_attempts"] = st.value("failed_attempts").toInt(0) + 1;
    }

    m_importActivity.insert(walletName, st);

    if (actualImported > 0) {
        QJsonObject all = _loadCachedInfos(walletName);
        bool changed = false;

        for (const auto &en : entries) {
            QJsonObject e = all.value(en.peer).toObject();
            if (e.isEmpty()) continue;

            const QString cachedB64 = e.value("info_b64").toString();
            const QString justB64 = QString::fromLatin1(
                en.infoRaw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

            const bool match = (cachedB64 == justB64);
            const bool already = e.value("imported").toBool(false);

            if (match && !already) {
                e["imported"] = true;
                all.insert(en.peer, e);
                changed = true;
            } else if (!match && already) {

                e["imported"] = false;
                all.insert(en.peer, e);
                changed = true;
            }
        }

        if (changed) {
            const QString dir  = QDir(_accountCacheDir()).filePath(walletName);
            QDir().mkpath(dir);
            const QString file = QDir(dir).filePath("peer_infos.json");
            QFile f(file);
            if (f.open(QIODevice::WriteOnly))
                f.write(QJsonDocument(all).toJson(QJsonDocument::Indented));
        }
    }
}



QString MultisigImportSession::getImportActivity() const
{
    // if (m_importActivity.size()>0){}

    QJsonArray a;
    for (auto it = m_importActivity.begin(); it != m_importActivity.end(); ++it)
        a.append(it.value());
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

QString MultisigImportSession::getPeerActivity() const
{
    // if (m_peerActivity.size() >0){}

    QJsonArray a;
    for (const auto &e : m_peerActivity)
        a.append(e);
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

int MultisigImportSession::getActiveWalletCount() const
{
    int n=0;
    for (auto it=m_importActivity.begin(); it!=m_importActivity.end(); ++it) {
        const QString s = it.value().value("status").toString();
        if (s=="importing" || s=="receiving_peer_info") ++n;
    }

    // if (n >0){ }

    return n;
}

QString MultisigImportSession::getWalletStatus(const QString &walletName) const
{
    QString status = m_importActivity.value(walletName).value("status").toString("idle");
    return status;
}

void MultisigImportSession::clearActivity()
{

    m_importActivity.clear();
    m_peerActivity.clear();

}
