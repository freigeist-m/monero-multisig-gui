#include "win_compat.h"
#include "multisigsession.h"
#include "multiwalletcontroller.h"
#include "torbackend.h"
#include "httptask.h"
#include "restore_height.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QEventLoop>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include "accountmanager.h"
#include "wallet.h"
#include "cryptoutils_extras.h"
#include <QDebug>


using namespace CryptoUtils;

static constexpr int PING_MS  = 2'000;
static constexpr int RETRY_MS = 2'000;

static constexpr Qt::ConnectionType kQueuedUnique =
    static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection);


namespace {
QString opKey(const char *op, int round) {
    return QStringLiteral("%1:%2").arg(QString::fromLatin1(op)).arg(round);
}

QByteArray infosHash(const QList<QByteArray> &infos) {
    QCryptographicHash h(QCryptographicHash::Sha256);
    for (const QString &s : infos) {
        h.addData(s.toUtf8());
        h.addData("\n", 1);
    }
    return h.result();
}
}

static constexpr qint64 kMaxJsonBytes     = 256 * 1024;
static constexpr int    kMaxHeaderBytes   = 32  * 1024;
static constexpr int    kMaxBlobDecoded   = 256 * 1024;
static constexpr int    kMaxInfoDecoded   = 256 * 1024;
static constexpr int    kSkewSecs         = 120;

static inline bool isBase64UrlString(const QByteArray &s) {

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

//──────────────────────────────────────────────────────────────────────────────
QString MultisigSession::stageName(Stage s)
{
    switch (s) {
    case Stage::INIT:      return QStringLiteral("INIT");
    case Stage::WAIT_PEERS:return QStringLiteral("WAIT_PEERS");
    case Stage::KEX:       return QStringLiteral("KEX");
    case Stage::ACK:       return QStringLiteral("ACK");
    case Stage::PENDING:   return QStringLiteral("PENDING");
    case Stage::COMPLETE:  return QStringLiteral("COMPLETE");
    case Stage::ERROR:     return QStringLiteral("ERROR");
    }
    return {};
}

//──────────────────────────────────────────────────────────────────────────────
MultisigSession::MultisigSession(MultiWalletController *wm,
                                 TorBackend          *tor,
                                 const QString       &reference,
                                 int                  m,
                                 int                  n,
                                 const QStringList   &peerOnions,
                                 const QString       &walletName,
                                 const QString       &walletPassword,
                                 const QString       &myOnion,
                                 const QString       &creator,
                                 QObject             *parent)
    : QObject(parent)
    , m_wm(wm)
    , m_tor(tor)
    , m_ref(reference.trimmed())
    , m_m(m)
    , m_n(n)
    , m_walletName(walletName.trimmed())
    , m_walletPassword(walletPassword)
    , m_creator(creator)
{
    Q_ASSERT(wm && tor);


    QString chosen = myOnion.trimmed().toLower();
    if (!chosen.endsWith(".onion")) chosen.append(".onion");
    m_myOnion = chosen;


    AccountManager *acct = nullptr;
    if (auto obj = m_wm->property("accountManager").value<QObject*>())
        acct = qobject_cast<AccountManager*>(obj);
    if (!acct)
        acct = qobject_cast<AccountManager*>(m_wm->parent());

    if (acct) {
        const QString privB64 = acct->torPrivKeyFor(m_myOnion);
        if (!privB64.isEmpty()) {
            QByteArray scalar, prefix, pub;
            if (CryptoUtils::trySplitV3BlobFlexible(privB64, scalar, prefix, pub)) {

                const QString derived = QString::fromUtf8(onion_from_pub(pub)).toLower();
                const QString derivedOnion = derived.endsWith(".onion") ? derived : (derived + ".onion");

                if (!m_myOnion.isEmpty() && !derivedOnion.isEmpty() &&
                    !QString::compare(m_myOnion, derivedOnion, Qt::CaseInsensitive)==0) {
                    qWarning() << "[MultisigSession] Selected onion" << m_myOnion
                               << "does not match key-derived onion" << derivedOnion
                               << "— using derived onion to avoid inconsistency.";
                    m_myOnion = derivedOnion;
                }

                m_scalar = scalar; m_prefix = prefix; m_pubKey = pub;
            } else {
                qWarning() << "[MultisigSession] splitV3Blob failed; Tor private key blob malformed for" << m_myOnion;
            }
        } else {
            qWarning() << "[MultisigSession] No private key found in AccountManager for onion" << m_myOnion
                       << "; cannot sign requests (handshake will fail).";
        }
    } else {
        qWarning() << "[MultisigSession] AccountManager not reachable; set walletManager.accountManager property";
    }


    for (const QString &o : peerOnions)
        m_peers[o.toLower()] = PeerState{o.toLower()};


    m_ping.setInterval(PING_MS);
    m_retry.setInterval(RETRY_MS);
    QObject::connect(&m_ping,  &QTimer::timeout, this, &MultisigSession::pingRound);
    QObject::connect(&m_retry, &QTimer::timeout, this, &MultisigSession::retryRound);


    m_httpPool.setMaxThreadCount(20);
    QObject::connect(this, &MultisigSession::_httpResult,
                     this, &MultisigSession::onHttp,
                     Qt::QueuedConnection);
}

MultisigSession::~MultisigSession() { stop("dtor"); }


void MultisigSession::start()
{
    if (m_stage != Stage::INIT) return;
    m_stage = Stage::WAIT_PEERS;
    emit stageChanged(stageName(m_stage), m_myOnion, m_ref);

    m_ping.start();
    pingRound();
}

void MultisigSession::cancel() { stop("cancelled"); }

//──────────────────────────────────────────────────────────────────────────────
void MultisigSession::pingRound()
{
    if (m_stopFlag) return;
    for (const QString &o : m_peers.keys())
        httpGetAsync(o, QStringLiteral("/api/ping?ref=%1").arg(m_ref), true);
}

void MultisigSession::retryRound()
{
    if (m_stopFlag) return;

    if (m_stage == Stage::KEX && m_currentRound > 0) {
        for (const auto &p : std::as_const(m_peers)) {
            if (!p.kex.contains(m_currentRound)) {

                httpGetAsync(p.onion,
                             QStringLiteral("/api/multisig/blob?ref=%1&stage=KEX&i=%2")
                                 .arg(m_ref).arg(m_currentRound),
                             true);

                httpGetAsync(p.onion,
                             QStringLiteral("/api/multisig/blob?ref=%1&stage=KEX%2")
                                 .arg(m_ref).arg(m_currentRound),
                             true);
            }
        }
        return;
    }

    if (m_stage == Stage::ACK) {
        for (const auto &p : std::as_const(m_peers))
            if (p.ack.isEmpty())
                httpGetAsync(p.onion,
                             QStringLiteral("/api/multisig/blob?ref=%1&stage=ACK").arg(m_ref),
                             true);
        return;
    }

    if (m_stage == Stage::PENDING) {
        for (const auto &p : std::as_const(m_peers))
            if (!p.pendingComplete)
                httpGetAsync(p.onion,
                             QStringLiteral("/api/multisig/blob?ref=%1&stage=PENDING").arg(m_ref),
                             true);
        return;
    }
}

//──────────────────────────────────────────────────────────────────────────────
void MultisigSession::httpGetAsync(const QString &onion,
                                   const QString &path,
                                   bool           signedFlag)
{
    if (m_httpPool.activeThreadCount() >= m_httpPool.maxThreadCount()) return;
    ++m_inFlight;
    m_httpPool.start(new HttpTask(this, onion, path, signedFlag));
}

QJsonObject MultisigSession::httpGetBlocking(const QString &onion,
                                             const QString &path,
                                             bool           signedFlag)
{
    const QString url = QStringLiteral("http://%1%2").arg(onion, path);

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
#if (QT_VERSION >= QT_VERSION_CHECK(5,9,0))
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(0);
#endif

    req.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, false);

    if (signedFlag) {
        const qint64 ts = QDateTime::currentSecsSinceEpoch();
        QJsonObject msg{{"ref", m_ref}, {"path", path}, {"ts", ts}};
        const QByteArray sig = sign(msg);
        req.setRawHeader("x-pub", _b64(m_pubKey));
        req.setRawHeader("x-ts",  QByteArray::number(ts));
        req.setRawHeader("x-sig", _b64(sig));

    }

    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy,
                               QLatin1String("127.0.0.1"),
                               m_tor->socksPort()));

    QEventLoop loop;
    QTimer to;
    to.setSingleShot(true);
    to.setInterval(10'000);
    QObject::connect(&to, &QTimer::timeout, &loop, &QEventLoop::quit);

    QNetworkReply *rep = nam.get(req);

    QByteArray buf;
    buf.reserve(qMin<qint64>(kMaxJsonBytes, 64*1024));
    bool tooLarge = false;
    bool badContentType = false;
    bool redirected = false;

    QObject::connect(rep, &QNetworkReply::readyRead, &loop, [&](){
        if (tooLarge) return;
        buf += rep->readAll();
        if (buf.size() > kMaxJsonBytes) {
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

    if (!to.isActive()) {
        return makeErr("timeout");
    }

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
    if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
        return QJsonObject{{"error","bad-json"}, {"detail", jerr.errorString()}};
    }

    QJsonObject obj = doc.object();

    if (path.startsWith("/api/multisig/blob")) {
        const QString b64 = obj.value("blob_b64").toString();
        const QString sha = obj.value("sha256").toString();
        if (b64.isEmpty() || !isBase64UrlString(b64.toLatin1())) {
            return QJsonObject{{"error","bad-b64"}};
        }
        QByteArray decoded;
        if (!decodeB64Url(b64.toLatin1(), decoded)) {
            return QJsonObject{{"error","b64-decode-failed"}};
        }
        if (decoded.size() <= 0 || decoded.size() > kMaxBlobDecoded) {
            return QJsonObject{{"error","blob-too-large"}, {"len", decoded.size()}};
        }
        if (!sha.isEmpty() && !eqSha256Hex(decoded, sha)) {
            return QJsonObject{{"error","sha256-mismatch"}};
        }
    }
    return obj;
}


void MultisigSession::onHttp(QString onion, QString path,
                             QJsonObject res, QString err)
{

    if (m_inFlight > 0) --m_inFlight;

    if (m_stopFlag) {
        if (!m_finishedSignaled && m_inFlight == 0) {
            m_finishedSignaled = true;
            emit finished(m_myOnion, m_ref,
                          m_pendingFinishReason.isEmpty()
                              ? QStringLiteral("cancelled")
                              : m_pendingFinishReason);
        }
        return;
    }

    if (!m_peers.contains(onion)) return;

    PeerState &p = m_peers[onion];

    if (!err.isEmpty()) {
        p.online = false;
        emit peerStatusChanged(m_myOnion, m_ref);
        return;
    }

    if (path.startsWith("/api/ping")) {
        p.online   = true;
        p.lastSeen = QDateTime::currentSecsSinceEpoch();
        p.details  = res;
        p.detailsMatch = (p.details["ref"].toString() == m_ref &&
                          p.details["m"].toInt()      == m_m   &&
                          p.details["n"].toInt()      == m_n);
        emit peerStatusChanged(m_myOnion, m_ref);
        if (m_stage == Stage::WAIT_PEERS) checkStageCompletion();
        return;
    }

    if (path.startsWith("/api/multisig/blob")) {
        p.online   = true;
        p.lastSeen = QDateTime::currentSecsSinceEpoch();

        const QUrl u(path);
        const QUrlQuery q(u);
        QString tag = q.queryItemValue("stage");
        int round = q.queryItemValue("i").toInt();

        if (tag.startsWith("KEX") && round <= 0) {
            bool ok=false;
            const QString tail = tag.mid(3);
            int maybe = tail.toInt(&ok);
            if (ok && maybe>0) { tag = "KEX"; round = maybe; }
        }


        const QString enc = res.value("blob_b64").toString();
        QByteArray blob;

        blob = QByteArray::fromBase64(enc.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals );
        if (blob.isEmpty()) return;

        if (tag == "KEX" && round > 0 && !p.kex.contains(round)) p.kex.insert(round, blob);
        else if (tag == "ACK" && p.ack.isEmpty()) p.ack = blob;
        else if (tag == "PENDING" && !p.pendingComplete) p.pendingComplete = true;

        emit peerStatusChanged(m_myOnion, m_ref);
        checkStageCompletion();

        return;
    }
}

//──────────────────────────────────────────────────────────────────────────────
bool MultisigSession::allPeersOnline() const
{
    return std::all_of(m_peers.cbegin(), m_peers.cend(),
                       [](const PeerState &p){ return p.online && p.detailsMatch; });
}

bool MultisigSession::allKex(int round) const
{
    if (round <= 0) return false;
    if (!m_kex.contains(round)) return false;
    for (const auto &p : m_peers) {
        if (!p.kex.contains(round)) return false;
    }
    return true;
}

bool MultisigSession::allAck() const
{
    if (m_ack.isEmpty()) return false;

    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
        const PeerState &p = it.value();
        if (p.ack.isEmpty()){

        qWarning() << "p.ack.isEmpty" <<p.ack.isEmpty() ;
        return false;

        }

        const QByteArray raw = QByteArray::fromBase64(p.ack);
        const QJsonDocument doc = QJsonDocument::fromJson(raw);

        if (!doc.isObject()) {
            qWarning() << "Peer" << it.key() << "ACK is not JSON";
            return false;
        }

        QString why;
        if (!validateAckObject(doc.object(), &why)) {
            qWarning() << "Peer" << it.key() << "ACK invalid:" << why;
            return false;
        }
    }

    return true;
}


bool MultisigSession::allPending() const
{
    if (m_pending.isEmpty()) return false;
    for (const auto &p : m_peers) if (!p.pendingComplete) return false;
    return true;
}

void MultisigSession::checkStageCompletion()
{
    if (m_stopFlag) return;

    switch (m_stage) {
    case Stage::WAIT_PEERS:
        if (allPeersOnline()) createWallet();
        break;

    case Stage::KEX:
        if (m_currentRound > 0 && allKex(m_currentRound)) advanceHandshake();
        break;

    case Stage::ACK:
        if (allAck()) transitionToPending();
        break;

    case Stage::PENDING:
        if (allPending()) finalize();
        break;

    default:
        break;
    }
}

QByteArray MultisigSession::blobForStage(const QString &stage, int round) const
{
    if (stage == "KEX")     return m_kex.value(round);
    if (stage == "ACK")     return m_ack;
    if (stage == "PENDING") return m_pending;
    if (stage.startsWith("KEX")) {
        bool ok=false; int r = stage.mid(3).toInt(&ok);
        if (ok) return m_kex.value(r);
    }
    return {};
}

//──────────────────────────────────────────────────────────────────────────────
void MultisigSession::createWallet()
{
    m_stage = Stage::KEX;
    emit stageChanged(stageName(m_stage), m_myOnion, m_ref);

    m_ping.stop();
    m_retry.start();

    m_wm->createWallet(walletName(), walletPassword());
    auto *w = qobject_cast<Wallet*>(m_wm->walletInstance(walletName()));
    if (!w) { stop("wallet-create-failed"); return; }

    QObject::connect(w, &Wallet::firstKexMsgReady,
                     this, &MultisigSession::onFirstKexMsg,
                     kQueuedUnique);


    QObject::connect(w, &Wallet::walletCreated,
                     this, &MultisigSession::onWalletCreated,
                     kQueuedUnique);


}

void MultisigSession::onWalletCreated()
{
    auto *w = qobject_cast<Wallet*>(m_wm->walletInstance(walletName()));
    if (w) w->firstKexMsg();
}

void MultisigSession::onFirstKexMsg(QByteArray blob)
{
    if (blob.isEmpty()) { stop("first-kex-error"); return; }
    m_kex.insert(1, blob);
    m_currentRound = 1;
    emit peerStatusChanged(m_myOnion, m_ref);
    checkStageCompletion();
}

//──────────────────────────────────────────────────────────────────────────────
void MultisigSession::advanceHandshake()
{
    if (m_stopFlag) return;
    if (m_currentRound == 1)      { runMakeMultisig_Round1(); }
    else                          { runExchange_WithPassword(); }
}

void MultisigSession::runMakeMultisig_Round1()
{
    auto *w = qobject_cast<Wallet*>(m_wm->walletInstance(walletName()));
    if (!w) { stop("make-multisig-no-wallet"); return; }

    QList<QByteArray> infos;


    QStringList allOnions = m_peers.keys();
    QString myOnion =  m_myOnion;
    if (!myOnion.isEmpty()) allOnions << myOnion;


    std::sort(allOnions.begin(), allOnions.end(), [](const QString &a, const QString &b){
        return a.toLower() < b.toLower();
    });


    for (const QString &onion : allOnions) {
        if (onion == myOnion) {
            infos << m_kex.value(1);
        } else {
            infos << m_peers.value(onion).kex.value(1);
        }
    }

    if (!beginOp("MAKE", 1, infos)) return;

    QObject::connect(w, &Wallet::makeMultisigDone,
                     this, &MultisigSession::onMakeMultisigDone,
                     kQueuedUnique);

    w->makeMultisig(infos, m_m, m_walletPassword);
    qWarning() << "just run w->makeMultisig(infos, m_m, m_walletPassword)";
}

void MultisigSession::onMakeMultisigDone(QByteArray next)
{
    endOp("MAKE", 1);


    if (!next.isEmpty()){
        m_kex.insert(2, next);
        m_currentRound = 2;
        emit peerStatusChanged(m_myOnion, m_ref);
    }
    checkStageCompletion();
}

void MultisigSession::runExchange_WithPassword()
{
    auto *w = qobject_cast<Wallet*>(m_wm->walletInstance(walletName()));
    if (!w) { stop("exchange-kex-no-wallet"); return; }

    QList<QByteArray> infos;


    QStringList allOnions = m_peers.keys();
    const QString myOnion = m_myOnion;
    if (!myOnion.isEmpty()) allOnions << myOnion;


    std::sort(allOnions.begin(), allOnions.end(), [](const QString &a, const QString &b){
        return a.toLower() < b.toLower();
    });


    for (const QString &onion : allOnions) {
        if (onion == myOnion) {
            infos << m_kex.value(m_currentRound);
        } else {
            infos << m_peers.value(onion).kex.value(m_currentRound);
        }
    }

    if (!beginOp("KEX", m_currentRound, infos)) return;

    QObject::connect(w, &Wallet::exchangeMultisigKeysDone,
                     this, &MultisigSession::onExchangeMultisigKeysDone,
                     kQueuedUnique);

    w->exchangeMultisigKeys(infos, m_walletPassword);
}

void MultisigSession::onExchangeMultisigKeysDone(QByteArray next)
{

    endOp("KEX", m_currentRound);


    if (!next.isEmpty()) {
        const int nextRound = m_currentRound + 1;
        m_kex.insert(nextRound, next);
        m_currentRound = nextRound;
        emit peerStatusChanged(m_myOnion, m_ref);
    }

    probeReadiness();
}

//──────────────────────────────────────────────────────────────────────────────
void MultisigSession::probeReadiness()
{
    if (m_stopFlag || m_stage != Stage::KEX) return;
    auto *w = qobject_cast<Wallet*>(m_wm->walletInstance(walletName()));
    if (!w) return;

    QObject::connect(w, &Wallet::isMultisigReady,
                     this, &MultisigSession::onIsMultisigReady,
                     kQueuedUnique);

    w->isMultisig();
}

void MultisigSession::onIsMultisigReady(bool ready)
{
    if (!ready) {
        checkStageCompletion();
        return;
    }



    runAckPhase();
}

void MultisigSession::runAckPhase()
{
    if (m_stopFlag) return;
    m_stage = Stage::ACK;
    emit stageChanged(stageName(m_stage), m_myOnion, m_ref);

    auto *w = qobject_cast<Wallet*>(m_wm->walletInstance(walletName()));
    if (!w) { stop("ack-phase-no-wallet"); return; }

    QObject::connect(w, &Wallet::addressReady,
                     this, &MultisigSession::onAddressReady,
                     kQueuedUnique);
    w->getAddress();
    qWarning() << "ran runAckPhase()";
}

void MultisigSession::onAddressReady(QString addr)
{
    if (addr.isEmpty()) { stop("get-address-error"); return; }
    m_address = addr;

    auto *w = qobject_cast<Wallet*>(m_wm->walletInstance(walletName()));
    QObject::connect(w, &Wallet::seedMultiReady,
                     this, &MultisigSession::onSeedReady,
                     kQueuedUnique);
    w->seedMulti();
    qWarning() << "ran onAddressReady()";
}

void MultisigSession::onSeedReady(QString seed)
{
    if (seed.isEmpty()) { stop("seed-error"); return; }
    m_seed = seed;

    QJsonObject payload{{"is_multisig",true},
                        {"is_ready",   true},
                        {"address",    m_address},
                        {"ref",        m_ref},
                        {"m",          m_m},
                        {"n",          m_n},
                        {"ts",         QDateTime::currentSecsSinceEpoch()}};
    m_ack = _b64(QJsonDocument(payload).toJson(QJsonDocument::Compact));


    emit stageChanged(stageName(m_stage), m_myOnion, m_ref);
    checkStageCompletion();

    qWarning() << "ran onSeedReady()";
    qWarning() << payload;

}

//──────────────────────────────────────────────────────────────────────────────
void MultisigSession::transitionToPending()
{

    QSet<QString> addrs;
    for (const auto &p : std::as_const(m_peers)) {
        const QJsonObject obj = QJsonDocument::fromJson(QByteArray::fromBase64(p.ack)).object();

        addrs.insert(obj.value("address").toString());
    }

    if (addrs.size()!=1 || !addrs.contains(m_address)) {
        stop("address-mismatch"); return;
    }

    QJsonObject payload{{"pending_complete", true},
                        {"ref", m_ref},
                        {"ts",  QDateTime::currentSecsSinceEpoch()}};
    m_pending = _b64(QJsonDocument(payload).toJson(QJsonDocument::Compact));

    m_stage = Stage::PENDING; emit stageChanged(stageName(m_stage), m_myOnion, m_ref);
    checkStageCompletion();
}

void MultisigSession::finalize()
{
    const quint64 chainHeight = get_chain_height_robust();
    QStringList peers = m_peers.keys();
    if (!m_myOnion.isEmpty())
        peers << m_myOnion;

    m_wm->addWalletToAccount(m_walletName, m_walletPassword, m_seed,
                             m_address, chainHeight, m_myOnion, m_ref,
                             true, m_m, m_n, peers, true, m_creator);

    m_stage = Stage::COMPLETE;
    emit stageChanged(stageName(m_stage), m_myOnion, m_ref);
    emit walletAddressChanged(m_address, m_myOnion, m_ref);



    QMetaObject::invokeMethod(m_wm, [this]{
        m_wm->disconnectWallet(m_walletName);
    }, Qt::QueuedConnection);

    constexpr int kGraceMs = 120 * 1000;
    QTimer::singleShot(kGraceMs, this, [this]{
        if (!m_stopFlag)
            stop("success");
    });
}

//──────────────────────────────────────────────────────────────────────────────
void MultisigSession::stop(const QString &reason)
{
    if (m_stopFlag) return;
    m_stopFlag = true;
    m_pendingFinishReason = reason;
    m_ping.stop();
    m_retry.stop();
    if (m_inFlight == 0) {
        if (!m_finishedSignaled) {
            m_finishedSignaled = true;
            emit finished(m_myOnion, m_ref, m_pendingFinishReason);
        }
    }
}

QByteArray MultisigSession::sign(const QJsonObject &payload) const
{
    if (m_scalar.isEmpty() || m_prefix.isEmpty() || m_pubKey.isEmpty()) {
        qWarning() << "sign(): keys not loaded; returning empty sig";
        return {};
    }
    return CryptoUtils::ed25519Sign(QJsonDocument(payload).toJson(QJsonDocument::Compact),
                                    m_scalar, m_prefix);
}

QVariant MultisigSession::peerList() const
{
    QVariantList out;
    for (auto it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        const PeerState &p = it.value();
        QString stageText;
        if (p.pendingComplete) stageText = "READY";
        else if (!p.ack.isEmpty()) stageText = "ACK";
        else if (!p.kex.isEmpty()) {
            int maxRound = 0; for (auto r : p.kex.keys()) maxRound = std::max(maxRound, r);
            stageText = QStringLiteral("KEX%1").arg(maxRound);
        } else stageText = "PING";

        out << QVariantMap{
            { "onion",  it.key() },
            { "online", p.online },
            { "pstage", stageText }
        };
    }
    return out;
}

bool MultisigSession::isPeer(const QString &onion) const
{
    return m_peers.contains(onion.trimmed().toLower());
}

void MultisigSession::registerPeerPendingConfirmation(const QString &onion)
{
    const QString key = onion.trimmed().toLower();
    auto it = m_peers.find(key);
    if (it == m_peers.end()) return;

    PeerState &p = it.value();
    if (!p.pendingComplete) {
        p.pendingComplete = true;
        emit peerStatusChanged(m_myOnion, m_ref);
        checkStageCompletion();
    }
}

bool MultisigSession::validateAckObject(const QJsonObject &obj, QString *whyNot) const
{
    const bool isMulti = obj.value("is_multisig").toBool(false);
    const bool isReady = obj.value("is_ready").toBool(false);
    const QString addr = obj.value("address").toString();

    if (!isMulti) { if (whyNot) *whyNot = "is_multisig=false"; return false; }
    if (!isReady) { if (whyNot) *whyNot = "is_ready=false";   return false; }
    if (addr.isEmpty()) { if (whyNot) *whyNot = "address-empty"; return false; }
    if (!m_address.isEmpty() && addr != m_address) {
        if (whyNot) *whyNot = QStringLiteral("address-mismatch (%1 != %2)").arg(addr, m_address);
        return false;
    }

    const QString ref = obj.value("ref").toString();
    if (!ref.isEmpty() && ref != m_ref) {
        if (whyNot) *whyNot = "ref-mismatch"; return false;
    }
    if (obj.contains("m") && obj.value("m").toInt() != m_m) {
        if (whyNot) *whyNot = "m-mismatch"; return false;
    }
    if (obj.contains("n") && obj.value("n").toInt() != m_n) {
        if (whyNot) *whyNot = "n-mismatch"; return false;
    }

    return true;
}


bool MultisigSession::beginOp(const char *op, int round, const QList<QByteArray> &infos) {
    const QString key  = opKey(op, round);
    if (m_oncePerRoundOps.contains(key)) {
        qWarning() << "OncePerRound: op already executed for" << key << "; skipping";
        return false;
        }

    if (m_inflightOps.contains(key)) {
        qWarning() << "Dedup: op already in-flight:" << key;
        return false;
    }

    m_oncePerRoundOps.insert(key);
    m_inflightOps.insert(key);
    m_lastOpKey.insert(key, infosHash(infos));
    return true;
}

void MultisigSession::endOp(const char *op, int round) {
    m_inflightOps.remove(opKey(op, round));
}




quint64 MultisigSession::get_chain_height_robust()
{
    // Get AccountManager to access daemon settings
    AccountManager *acct = nullptr;
    if (auto obj = m_wm->property("accountManager").value<QObject*>())
        acct = qobject_cast<AccountManager*>(obj);
    if (!acct)
        acct = qobject_cast<AccountManager*>(m_wm->parent());

    if (!acct) {
        qWarning() << "[MultisigSession] No AccountManager available, falling back to estimator";
        const std::time_t now = QDateTime::currentSecsSinceEpoch();
        const int nettype = 0; // Assuming mainnet
        return restore_height::estimate_from_timestamp(now, nettype);
    }

    // Get user's configured daemon settings
    const QString daemonUrl = acct->daemonUrl();
    const int daemonPort = acct->daemonPort();
    const bool useTorForDaemon = acct->useTorForDaemon();

    // Build the daemon URL
    const QString fullDaemonUrl = QString("http://%1:%2/get_height").arg(daemonUrl).arg(daemonPort);

    // Determine if daemon is local
    const bool isLocalDaemon = (daemonUrl == "127.0.0.1" || daemonUrl == "localhost" || daemonUrl.startsWith("192.168.") || daemonUrl.startsWith("10.") || daemonUrl.startsWith("172."));

    // Decide whether to use tor proxy
    const bool shouldUseTor = !isLocalDaemon && useTorForDaemon;

    qDebug() << "[MultisigSession] Trying to get chain height from configured daemon:" << fullDaemonUrl
             << "useTor:" << shouldUseTor;

    QJsonObject result = httpGetDaemon(fullDaemonUrl, shouldUseTor, 5000);

    if (!result.contains("error") && result.contains("height")) {
        quint64 height = result.value("height").toVariant().toULongLong();
        if (height > 0) {
            qDebug() << "[MultisigSession] Got chain height from configured daemon:" << height;
            return height;
        }
    }

    // Fall back to estimator using current timestamp
    qDebug() << "[MultisigSession] Daemon call failed, using height estimator...";
    qDebug() << "[MultisigSession] Daemon error:" << result.value("error").toString();

    const std::time_t now = QDateTime::currentSecsSinceEpoch();
    const int nettype = 0; // Assuming mainnet - you may want to make this configurable
    const quint64 estimatedHeight = restore_height::estimate_from_timestamp(now, nettype);

    qDebug() << "[MultisigSession] Estimated chain height:" << estimatedHeight;
    return estimatedHeight;
}

QJsonObject MultisigSession::httpGetDaemon(const QString &url, bool useTorProxy, int timeoutMs)
{
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "MultisigSession/1.0");

#if (QT_VERSION >= QT_VERSION_CHECK(5,9,0))
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(0);
#endif
    req.setAttribute(QNetworkRequest::HttpPipeliningAllowedAttribute, false);

    QNetworkAccessManager nam;

    // Only use tor proxy for external calls
    if (useTorProxy && m_tor) {
        nam.setProxy(QNetworkProxy(QNetworkProxy::Socks5Proxy,
                                   QLatin1String("127.0.0.1"),
                                   m_tor->socksPort()));
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(timeoutMs);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    QNetworkReply *reply = nam.get(req);

    QByteArray responseData;
    responseData.reserve(qMin<qint64>(kMaxJsonBytes, 64*1024));
    bool tooLarge = false;
    bool badContentType = false;
    bool redirected = false;

    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&](){
        if (tooLarge) return;
        responseData += reply->readAll();
        if (responseData.size() > kMaxJsonBytes) {
            tooLarge = true;
            reply->abort();
        }
    });

    QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, [&](){
        QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirect.isValid()) {
            redirected = true;
            reply->abort();
            return;
        }

        const QByteArray contentType = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray().toLower();
        if (!contentType.startsWith("application/json")) {
            badContentType = true;
        }
    });

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    timeout.start();
    loop.exec();

    auto makeError = [&](const char *reason){
        if (reply) { reply->abort(); reply->deleteLater(); }
        return QJsonObject{{"error", QString::fromLatin1(reason)}};
    };

    if (!timeout.isActive()) {
        return makeError("timeout");
    }

    timeout.stop();

    if (reply->error() != QNetworkReply::NoError) {
        return makeError(redirected ? "redirect-disallowed" :
                             (tooLarge ? "response-too-large" : reply->errorString().toUtf8().constData()));
    }

    if (badContentType) {
        return makeError("bad-content-type");
    }

    const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpCode != 200) {
        reply->deleteLater();
        return QJsonObject{{"error","http"}, {"code", httpCode}};
    }

    reply->deleteLater();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QJsonObject{{"error","bad-json"}, {"detail", parseError.errorString()}};
    }

    return doc.object();
}
