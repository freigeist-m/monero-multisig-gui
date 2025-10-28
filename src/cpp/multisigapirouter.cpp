#include "win_compat.h"
#include "multisigapirouter.h"
#include "multisigsession.h"
#include "accountmanager.h"
#include "multiwalletcontroller.h"
#include <cryptoutils_extras.h>

#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QSet>
#include <sodium.h>

using namespace CryptoUtils;

namespace {


constexpr const char kAscii404[] = R"(#%%%#+++++#%%%%#*+======================================================++++++++++++++++++++++++++++
*#%%%%+++#%%%%%#+======++**+++===================================================+======++++=+++++++
+#%%%%%++#%%%%%*+===+##%%%%%%#*++=================================================================++
+*#%%%%%*#%%%%#*+=+#%%%%%%%%#%%%#+==================================================================
=+*#%%%%%#%%%%%*+*%%%%%%###*****##*=================================================================
+++*%%%%%%%%%%#*#%%%###%%%+======++=================================================================
%%*+*#%%%%%%%%#%%%%%%%*+++*+==============================================----==================+===
%%%%#%%%%%%%%%%%%%%#*#*+=====================================================================+***+==
%%%%%%%%%%%%%%%%%%#*+======================================================================+#%#*====
%##%%%%%%%%%%%%%#%##%###**+==================-----------------=================+***#*+====*#%%#+====
=++*%#%%%%%%%%%%%%%%%%%%%%%%#+==========-----------------------=--===============*#%%#*==+#%%#*===+#
+#%%%%##%%%%%%%%%%%%%##%%%%%%%*+====-=====----------------------========++***+====*#%%%*+#%%%*+=*%%%
%%%%%%%%%%%%%##*#*#*#%*#%%#%%%%#+=-----------------------------------=+*##%%%%%#+=+##%%%*%%%%++#%%%#
%%%%##%%%%%%##%%%#==+*+++#*###%%#+=---------------------------------=****###%%%%%#+##%%%%%%%#*%%%%##
%%#*#%%%%%%%%##%%%%#=-=-==+=*+###*=---------------------------------=====+*++*%%%%%#*%%%%%%##%%%%#+=
%*++%%%%%#%%%%+#%%%%%*=------==*#*=-------------------------------------==-=*####%%%%%%%%%%%%%%#*+==
#==##*%%**#%%%#*##%%%%#=-------=+*=-----------------------------------------===+#%%%%%%%%%%%%%#*++*#
==*#**%#=+%%%%#*=##%%%%#=--------+=---------------------------------------=======+%%%%%%%%%%%##%%%%%
==#*+#%==+*%%%%#==##%%%%#=---------------------------------------------=*#%%%%%%%%%#%%%%%%%%%%%%%%%%
-=+=*%*--=##%%%#+-=+*##%#+=------------------------------------------=+##%%%#%%%%%%%%%#*%%%%%%%#%#**
-===%#=--++%%%%#+---++###*--------------------========---------------+####**##*##**#*++#%%%%%%%*+===
---#%*---=*##%%#+---==+##+=---------------=================---------=##*++++*+===+#%%%*#%%%%%%%%%%#+
--+%#=----=+#%%*=-----=*#=-------------======================-------+*+=---=---*#%%%#+#%%%%#*#%%%%%%
-=#%*==----+*#%*=------+*-------------=========================-----=+=------+#%%%%#+#%%%#%#=+*#%%%%
-+%#+=====-=+##=-------==-----------============================----==-----=*#%%%#++*#%%#+*%+===##%%
=*%*=======-=*+--------------------=====#+================+*=====---------=*##%#*==+#%%%#==#%=--=+##
=%#+======--==---------------------=====#%*=======--=====*%#=====---------+####+=--*#%%#*=-+%#=--==*
*%#=========----------------------======#%%#*==-------=*#%%#======--------*#**=----*#%%*+=-=*%+---==
%%*===========--=====-------------======######+------+######======--------**+=-----+#%#+=---=##=---=
%%+==========--========-----------======###+####=--=####+###======--------++=------=*##=-----+%*----
%#+====================------------====-###-=+###**###+=-###=====---------=--------=*#+------=#%=-==
%#+=====================-----------===--###---=+####+=---###=====-------------------=*=-======*%#===
%*===========================-------#####**-----=+*=--===#######---------------===============+#%+==
%+===============--------------------****++==---------===++****-----------=====================*%*==
%+====================----------------=++======-------======+=--------------------=============+%#==
%+===================-------------------=======-----========-------------------==-==============#%+=
%#********************************+++++++++=============+++++++++++*****************************#%##
%#******************************++++++++++++++========++++++++++++++****************************#%%*
%****************************************++++++++++++++++++++++**********************************%%#
%#*********************++***++++++++++++++++======-=====++++++++++++++++++++++++*****************#%%
%#*##***#****+*****************************+++++*+++++*******************************************#%%
%#*##****************************************************************++++++++++++++*+++++++++++**#%%
%%%%**##**+++++++**++++++++++++++++++++++++=++===========+++++++++++++++++++++++++++++++=++++++++#%%
%%%%%%#**+++++++++++++++++++++++++++++=+==========--===+++++++++++++**************++++**********+#%%
%%%%%%##%###*****+*********+++**+*********+++++++++++++***+********++++++++++++==+++++++==++++**##%%
%%%%%%%%#*+*++++++++++*+++**++++***+++++++++++++++====================+==============+=+++++++*#%%%%
%%%%%%%%###**+==+++========================================================++++++++++++++++**#%%%%%%
%%%%%%%%%%%%#*++++==========================---==---==---=========+++++++++++=++++++++++*###%%%%%%%%
%%%%%%%%%%%%#***+++++++++++=============================+++++++++++++++++====+++++++++**####%%%%%%%%
%%%%%%%%%#%%#%%##*++++===+==+=========================================+++++++++++++++++++****#######
%%%%########*#**++++++++++++++======================================+++++++++++++++++*****+++***####
###########****+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*******+**************
#####*********************+++++++++++++++++++++++++++++++++++++++++++********+*********************#
########*********************++++++++*****+++++++++++++*******++++++++++********************########
############********************************************************************************########
)";

inline QByteArray notFoundArt()
{
    return QByteArray::fromRawData(kAscii404, sizeof(kAscii404) - 1);
}

QByteArray b64urlDecodeNoPad(QByteArray s)
{
    while (s.size() % 4) s.append('=');
    return QByteArray::fromBase64(s, QByteArray::Base64UrlEncoding);
}

QString canonicalPathForSig(const QUrl &u, const QString &ref)
{
    const QString stage = QUrlQuery(u).queryItemValue("stage");
    QString p = u.path() + QStringLiteral("?ref=") + ref;
    if (!stage.isEmpty()) p += QStringLiteral("&stage=") + stage;
    const QString i = QUrlQuery(u).queryItemValue("i");
    if (!i.isEmpty()) p += QStringLiteral("&i=") + i;
    const QString transferRef = QUrlQuery(u).queryItemValue("transfer_ref");
    if (!transferRef.isEmpty()) p += QStringLiteral("&transfer_ref=") + transferRef;
    return p;
}

bool verifyDetached(const QByteArray &msg,
                    const QByteArray &sig64,
                    const QByteArray &pub32)
{
    if (sig64.size() != 64 || pub32.size() != 32) return false;
    return crypto_sign_ed25519_verify_detached(
               reinterpret_cast<const unsigned char*>(sig64.constData()),
               reinterpret_cast<const unsigned char*>(msg.constData()),
               static_cast<unsigned long long>(msg.size()),
               reinterpret_cast<const unsigned char*>(pub32.constData())) == 0;
}

QString randomPassword(int n=20)
{
    static constexpr char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static constexpr int alphabetLen = int(sizeof(alphabet) - 1);

    QString out; out.reserve(n);
    auto *rng = QRandomGenerator::system();
    for (int i=0; i<n; ++i) {
        const int idx = rng->bounded(alphabetLen);
        out.append(QLatin1Char(alphabet[idx]));
    }
    return out;
}
constexpr qint64 kReadyWindowSecs   = 300;
constexpr qint64 kMsigInfoMaxAgeSec = 120;

constexpr int    kMaxHeaderBytes   = 32 * 1024;
constexpr int    kMaxHeaderLines   = 200;
constexpr qint64 kMaxBodyBytes     = 512 * 1024;
constexpr int    kPerRequestTimeoutMs = 5000;

}




static bool onlyAllowedQueryKeys(const QUrl &u, const QSet<QString> &allowed)
{
    const auto items = QUrlQuery(u).queryItems(QUrl::FullyDecoded);
    for (const auto &p : items) if (!allowed.contains(p.first)) return false;
    return true;
}


//──────────────────────────────────────────────────────────────────────────────

MultisigApiRouter::MultisigApiRouter(MultisigManager *mgr,
                                     AccountManager  *acct,
                                     const QString   &boundOnion,
                                     QObject         *parent)
    : RouterHandler(parent), m_mgr(mgr), m_acct(acct), m_boundOnion(boundOnion.trimmed().toLower()) {}


//──────────────────────────────────────────────────────────────────────────────
void MultisigApiRouter::incomingConnection(qintptr sd)
{

    auto *sock = new QTcpSocket(this);
    sock->setSocketDescriptor(sd);


    sock->setReadBufferSize(kMaxHeaderBytes + kMaxBodyBytes);

    struct State {
        QByteArray buf;
        bool headersParsed = false;
        qint64 headerEnd   = -1;
        qint64 contentLen  = 0;
        QByteArray method;
        QByteArray rawPath;
        QMap<QByteArray,QByteArray> headers;
        QTimer *timer = nullptr;
        int headerLines = 0;
    };
    auto *st = new State;
    st->timer = new QTimer(sock);
    st->timer->setSingleShot(true);
    connect(st->timer, &QTimer::timeout, sock, [sock]{ sock->abort(); });
    st->timer->start(kPerRequestTimeoutMs);

    auto parseAndDispatch = [this, sock, st]() {
        if (!st->headersParsed) {

            int pos = st->buf.indexOf("\r\n\r\n");
            if (pos < 0) pos = st->buf.indexOf("\n\n");
            if (pos < 0) return;
            const QByteArray head = st->buf.left(pos);
            if (head.size() > kMaxHeaderBytes) { sock->abort(); return; }
            const QList<QByteArray> lines = head.split('\n');
            if (lines.isEmpty()) { sock->abort(); return; }

            const QList<QByteArray> first = lines[0].trimmed().split(' ');
            st->method = first.value(0);
            st->rawPath= first.value(1);
            if (st->method.isEmpty() || st->rawPath.isEmpty()) { sock->abort(); return; }

            st->headerLines = 0;
            for (int i=1;i<lines.size();++i) {
                if (++st->headerLines > kMaxHeaderLines) { sock->abort(); return; }
                const QByteArray line = lines[i].trimmed();
                if (line.isEmpty()) break;
                int c = line.indexOf(':');
                if (c>0) {
                    QByteArray k = line.left(c).trimmed().toLower();
                    QByteArray v = line.mid(c+1).trimmed();
                    st->headers.insert(k,v);
                }
            }

            bool okLen=false;
            qint64 need = st->headers.value("content-length").toLongLong(&okLen);
            if (!okLen) need = 0;
            if (need < 0 || need > kMaxBodyBytes) { sock->abort(); return; }
            st->contentLen = need;
            st->headerEnd = pos + ((st->buf.mid(pos,4) == "\r\n\r\n") ? 4 : 2);
            st->headersParsed = true;
        }
        if (st->headersParsed) {
            const qint64 have = st->buf.size() - st->headerEnd;
            if (have < st->contentLen) return;
            st->timer->stop();

            const QByteArray body = (st->contentLen>0) ? st->buf.mid(st->headerEnd, st->contentLen) : QByteArray();
            const QByteArray method = st->method;
            const QByteArray rawPath= st->rawPath;
            const auto headers = st->headers;

            emit requestReceived(m_boundOnion, QString::fromUtf8(method), QString::fromUtf8(rawPath));


            if (method=="POST") {
                const QByteArray ctype = headers.value("content-type").toLower();
                if (!ctype.startsWith("application/json")) { sendPlain(sock, 404, "Not found"); return; }
            }


            if (handleTransferPing(method,rawPath,headers,sock))        return;
            if (handleTransferRequestInfo(method,rawPath,headers,sock)) return;
            if (handleTransferSubmit(method,rawPath,headers,body,sock)) return;
            if (handleTransferStatus(method,rawPath,headers,sock))      return;
            if (handlePing(method,rawPath,headers,sock)) return;
            if (handleBlob(method,rawPath,headers,sock)) return;
            if (handleNew (method,rawPath,headers,body,sock)) return;
            sendPlain(sock, 404, notFoundArt());
        }
    };

    connect(sock,&QTcpSocket::readyRead,this,[sock,st,parseAndDispatch](){
        st->buf += sock->readAll();
        if (st->buf.size() > kMaxHeaderBytes + kMaxBodyBytes) { sock->abort(); return; }
        parseAndDispatch();


    });

}


//──────────────────────────────────────────────────────────────────────────────
void MultisigApiRouter::sendPlain(QTcpSocket *sock,int status,const QByteArray &body)
{
    QByteArray statusLine = QByteArray("HTTP/1.0 ")+QByteArray::number(status)+" \r\n";
    QByteArray headers = "Connection: close\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n";

    sock->write(statusLine); sock->write(headers); sock->write(body);
    sock->disconnectFromHost();
}
void MultisigApiRouter::sendJson(QTcpSocket *sock,int status,const QJsonObject &obj)
{
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QByteArray statusLine = QByteArray("HTTP/1.0 ")+QByteArray::number(status)+" \r\n";
    QByteArray headers = "Connection: close\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n";

    sock->write(statusLine); sock->write(headers); sock->write(body);
    sock->disconnectFromHost();
}


bool MultisigApiRouter::handlePing(const QByteArray &method,const QByteArray &rawPath,const QMap<QByteArray,QByteArray> &headers,QTcpSocket *sock)
{
    if (method!="GET" || !rawPath.startsWith("/api/ping")) return false;
    QUrl  u(QString::fromUtf8(rawPath)); QUrlQuery q(u);
    const QString ref = q.queryItemValue("ref");

    if (m_boundOnion.isEmpty()) {
        sendPlain(sock, 404, "Not found");
        return true;
    }

    auto *s = m_mgr->sessionFor(m_boundOnion, ref);
    if (!s) { sendPlain(sock, 404, "Not found"); return true; }
    if (s) {
    const QString mine = s->myOnion().trimmed().toLower();
    if (!mine.isEmpty() && QString::compare(m_boundOnion, mine, Qt::CaseInsensitive) != 0) {
            sendPlain(sock, 404, "Not found");
            return true;
        }
    }


    const QByteArray xpub = headers.value("x-pub");
    const QByteArray xts  = headers.value("x-ts");
    const QByteArray xsig = headers.value("x-sig");
    if (xpub.isEmpty() || xts.isEmpty() || xsig.isEmpty()) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    const QByteArray pub = b64urlDecodeNoPad(xpub);
    const QByteArray sig = b64urlDecodeNoPad(xsig);
    bool okTs = false; const qint64 ts = QString::fromUtf8(xts).toLongLong(&okTs);
    if (!okTs) { sendJson(sock, 403, QJsonObject{{"error","bad ts"}}); return true; }
    if (qAbs(QDateTime::currentSecsSinceEpoch() - ts) > 60) {
        sendPlain(sock, 404, "Not found"); return true;
    }
    if (pub.size() != 32) { sendJson(sock, 403, QJsonObject{{"error","bad pub"}}); return true; }

    const QString callerOnion = QString::fromUtf8(onionFromPub(pub)).toLower();
    if (!s->isPeer(callerOnion)) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    const QString canonPath = canonicalPathForSig(u, ref);
    const QJsonObject msg{{"ref", ref}, {"path", canonPath}, {"ts", ts}};
    const QByteArray compact = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    if (!verifyDetached(compact, sig, pub)) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    if (!onlyAllowedQueryKeys(u, QSet<QString>{"ref"})) {
            sendPlain(sock, 404, "Not found"); return true;
        }


    QJsonObject out{
        { QStringLiteral("ref"),   s->referenceCode() },
        { QStringLiteral("m"),     s->m() },
        { QStringLiteral("n"),     s->n() },
        { QStringLiteral("nettype"), s->net_type() },
        { QStringLiteral("stage"), MultisigSession::stageName(s->currentStage()) }
    };

    sendJson(sock,200,out); return true;
}

bool MultisigApiRouter::handleBlob(const QByteArray &method,const QByteArray &rawPath,const QMap<QByteArray,QByteArray> &headers,QTcpSocket *sock)
{
    if (method!="GET" || !rawPath.startsWith("/api/multisig/blob")) return false;

    QUrl u(QString::fromUtf8(rawPath)); QUrlQuery q(u);
    const QString ref = q.queryItemValue("ref");
    QString stage = q.queryItemValue("stage");
    int round = q.queryItemValue("i").toInt();

    if (m_boundOnion.isEmpty()) {
        sendPlain(sock, 404, "Not found");
        return true;
    }

    auto *s = m_mgr->sessionFor(m_boundOnion, ref);
    if (!s) { sendPlain(sock, 404, "Not found"); return true; }

    if (s) {
        const QString mine = s->myOnion().trimmed().toLower();
        if (!mine.isEmpty() && QString::compare(m_boundOnion, mine, Qt::CaseInsensitive) != 0) {
            sendPlain(sock, 404, "Not found");
            return true;
        }
    }

    const QByteArray xpub = headers.value("x-pub");
    const QByteArray xts  = headers.value("x-ts");
    const QByteArray xsig = headers.value("x-sig");
    if (xpub.isEmpty() || xts.isEmpty() || xsig.isEmpty()) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    const QByteArray pub = b64urlDecodeNoPad(xpub);
    const QByteArray sig = b64urlDecodeNoPad(xsig);
    bool okTs = false; const qint64 ts = QString::fromUtf8(xts).toLongLong(&okTs);
    if (!okTs) { sendJson(sock, 403, QJsonObject{{"error","bad ts"}}); return true; }
    if (qAbs(QDateTime::currentSecsSinceEpoch() - ts) > 60) {
        sendPlain(sock, 404, "Not found"); return true;
    }
    if (pub.size() != 32) { sendPlain(sock, 404, "Not found"); return true; }

    const QString callerOnion = QString::fromUtf8(onionFromPub(pub)).toLower();
    if (!s->isPeer(callerOnion)) { sendPlain(sock, 404, "Not found"); return true; }

    if (stage.startsWith("KEX") && round<=0) {
        bool ok=false; int r = stage.mid(3).toInt(&ok); if (ok) { stage="KEX"; round=r; }
    }

    const QString canonPath = canonicalPathForSig(u, ref);
    const QJsonObject msg{{"ref", ref}, {"path", canonPath}, {"ts", ts}};
    const QByteArray compact = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    if (!verifyDetached(compact, sig, pub)) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    if (!onlyAllowedQueryKeys(u, QSet<QString>{"ref","stage","i"})) {
                sendPlain(sock, 404, "Not found"); return true;
    }


    if (stage == QLatin1String("PENDING")) {
        QMetaObject::invokeMethod(
            s,
            [s, callerOnion]() { s->registerPeerPendingConfirmation(callerOnion); },
            Qt::QueuedConnection
            );
    }

    QByteArray blob = s->blobForStage(stage, round);
    if (blob.isEmpty()) { sendPlain(sock, 404, "Not found"); return true; }

    const auto b64 = blob.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals
        );

    const QByteArray sha = QCryptographicHash::hash(blob, QCryptographicHash::Sha256).toHex();

    QJsonObject out{
        { "ref",   s->referenceCode() },
        { "stage", stage },
        { "i",     round },
        { "blob_b64",  QString::fromLatin1(b64) },
        { "sha256",    QString::fromLatin1(sha) }
    };
    sendJson(sock,200,out);
    return true;
}

bool MultisigApiRouter::handleNew(const QByteArray &method,const QByteArray &rawPath,const QMap<QByteArray,QByteArray> &headers,
                                  const QByteArray &body, QTcpSocket *sock)
{
    if (method!="POST" || !rawPath.startsWith("/api/multisig/new")) return false;

    if (!m_mgr || !m_acct) { sendPlain(sock, 404, "Not found"); return true; }

    if (!onlyAllowedQueryKeys(QUrl(QString::fromUtf8(rawPath)), QSet<QString>{"ref"})) {
        sendPlain(sock, 404, "Not found"); return true;
    }


    const QJsonDocument bodyDoc = QJsonDocument::fromJson(body);
    if (bodyDoc.isEmpty()) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    const QJsonObject   obj     = bodyDoc.object();
    const QString ref = obj.value("ref").toString();
    const int m = obj.value("m").toInt(0);
    const int n = obj.value("n").toInt(0);
    const QString net_type = obj.value("net_type").toString("mainnet");
    const QJsonArray peersArr = obj.value("peers").toArray();
    QStringList peers; for (const auto &v : peersArr) peers << v.toString();

    const QString my_net_type =  m_acct->networkType();
    if (net_type !=  my_net_type){
        sendPlain(sock, 404, "Not found");
        return true;
    }



    const QByteArray xpub = headers.value("x-pub");
    const QByteArray xts  = headers.value("x-ts");
    const QByteArray xsig = headers.value("x-sig");
    if (xpub.isEmpty() || xts.isEmpty() || xsig.isEmpty()) {
        sendPlain(sock, 404, "Not found"); return true;
    }


    const QByteArray pub = b64urlDecodeNoPad(xpub);
    const QByteArray sig = b64urlDecodeNoPad(xsig);
    bool okTs = false; const qint64 ts = QString::fromUtf8(xts).toLongLong(&okTs);
    if (!okTs) { sendJson(sock, 403, QJsonObject{{"error","bad ts"}}); return true; }
    if (qAbs(QDateTime::currentSecsSinceEpoch() - ts) > 60) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    if (pub.size()!=32) { sendPlain(sock, 404, "Not found"); return true; }

    QUrl u(QString::fromUtf8(rawPath));
    const QString canon = canonicalPathForSig(u, ref);

    const QByteArray bodyCompact = bodyDoc.toJson(QJsonDocument::Compact);
    const QByteArray bodyHash    = QCryptographicHash::hash(bodyCompact, QCryptographicHash::Sha256).toHex();


    QJsonObject msg{
        {"ref",  ref},
        {"path", canon},
        {"ts",   ts},
        {"body", QString::fromLatin1(bodyHash)}
    };

    const QByteArray compact = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    if (!verifyDetached(compact, sig, pub)) {
        sendPlain(sock, 404, "Not found"); return true;
    }
    if (!seenPostAndRemember(pub, canon, bodyHash)) {

        sendJson(sock,200,QJsonObject{{"ok",true},{"idempotent",true}});
        return true;
    }



    auto norm = [](QString s){
        s = s.trimmed().toLower();
        if (!s.endsWith(".onion")) s.append(".onion");
        return s;
    };



    const QString senderOnion = QString::fromUtf8(onionFromPub(pub)).toLower();

    bool senderIsOurs = false;
    if (m_acct) {
        const QStringList ownedRaw = m_acct->torOnions();
        for (const QString &o : ownedRaw) {
            if (norm(o) == senderOnion) { senderIsOurs = true; break; }
        }
    }


    QJsonObject entry;
    if (!senderIsOurs) {
        QJsonObject trusted = QJsonDocument::fromJson(m_acct->getTrustedPeers().toUtf8()).object();
        entry = trusted.value(senderOnion).toObject();
        if (entry.isEmpty()) { sendPlain(sock, 404, "Not found"); return true; }

        const bool active = entry.value("active").toBool(true);
        const int  maxN   = entry.value("max_n").toInt(1);
        const int  minT   = entry.value("min_threshold").toInt(1);
        const int maxWallets = entry.value("max_number_wallets").toInt(0);
        const int curWallets = entry.value("current_number_wallets").toInt(0);

        if (!active) { sendPlain(sock, 404, "Not found"); return true; }
        if (m < minT) { sendPlain(sock, 404, "Not found"); return true; }
        if (n > maxN) { sendPlain(sock, 404, "Not found"); return true; }
        if (maxWallets > 0 && curWallets >= maxWallets) { sendPlain(sock, 404, "Not found");  return true; }



    }


    QStringList peersLower; peersLower.reserve(peers.size());
    for (const QString &p : peers) peersLower << norm(p);


    const QStringList ourOnionsRaw = m_acct->torOnions();
    QStringList ourOnions; for (const QString &o : ourOnionsRaw) ourOnions << norm(o);


    QStringList matches;
    for (const QString &p : peersLower)
        if (ourOnions.contains(p))
            matches << p;

    if (matches.isEmpty() || matches.size() > 1) {
        sendPlain(sock, 404, "Not found"); return true;
    }
    const QString myOnion = matches.first();

    if (refExistsForOnion(ref, myOnion)) {

        sendPlain(sock, 404, "Not found");
        return true;
    }

    if (m_boundOnion.isEmpty()) {
            sendPlain(sock, 503, "Service warming up");
        return true;
    }
    if (QString::compare(myOnion, m_boundOnion, Qt::CaseInsensitive) != 0) {
        sendPlain(sock, 404, "Not found");
        return true;
    }

    if (!senderIsOurs) {
        const QJsonArray allowedArr = entry.value("allowed_identities").toArray();
        if (allowedArr.isEmpty()) { sendPlain(sock, 404, "Not found"); return true; }

        QStringList allowed; allowed.reserve(allowedArr.size());
        for (const auto &v : allowedArr) allowed << norm(v.toString());
        if (!allowed.contains(myOnion)) {
            sendPlain(sock, 404, "Not found"); return true;
        }
    }
    if (m_mgr->sessionFor(m_boundOnion, ref)) { sendJson(sock,200,QJsonObject{{"ok",true}}); return true; }

    QStringList filtered;
    for (const QString &p : peersLower)
        if (QString::compare(p, myOnion, Qt::CaseInsensitive) != 0)
            filtered << p;

    if (!senderIsOurs) {
        if (!m_acct->incrementTrustedPeerWalletCount(senderOnion)) {
            sendPlain(sock, 404, "Not found");
            return true;
        }
    }

    const QString walletName = QStringLiteral("wallet_for_ref_%1").arg(ref);
    const QString walletPass = randomPassword(20);
    m_mgr->startMultisig(ref, m, n, filtered, walletName, walletPass, myOnion, senderOnion);
    // qDebug() << "[Router] newquested new";
    sendJson(sock,201,QJsonObject{{"ok",true}});
    return true;
}


bool MultisigApiRouter::handleTransferPing(const QByteArray &method,
                                           const QByteArray &rawPath,
                                           const QMap<QByteArray,QByteArray> &headers,
                                           QTcpSocket *sock)
{
    if (method!="GET" || !rawPath.startsWith("/api/multisig/transfer/ping")) return false;

    QUrl u(QString::fromUtf8(rawPath)); QUrlQuery q(u);
    const QString ref = q.queryItemValue("ref");

    if (!onlyAllowedQueryKeys(u, QSet<QString>{"ref"})) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    if (m_boundOnion.isEmpty()) { sendPlain(sock, 503, "Service warming up"); return true; }
    {
        const QStringList peers = peersForRefOnion(ref, m_boundOnion);
        bool ok = false;
        for (const auto &p : peers)
            if (QString::compare(p, m_boundOnion, Qt::CaseInsensitive) == 0) { ok = true; break; }
        if (!ok) { sendPlain(sock, 404, "Not found"); return true; }
    }

    if (!refExistsForOnion(ref, m_boundOnion)) {
        sendPlain(sock, 404, "Not found");
        qDebug() << "[Router] pingRound() ref error";
        return true;
    }


    const QString canon = canonicalPathForSig(u, ref);
    QString callerOnion, why;
    if (!verifySignedGet(rawPath, headers, canon, &callerOnion, &why)) {
        qDebug() << "[Router] pingRound() signature error";

        sendPlain(sock, 404, "Not found"); return true;
    }

    const QStringList peers = peersForRefOnion(ref, m_boundOnion);
    if (!peers.contains(callerOnion , Qt::CaseInsensitive)) {
        qDebug() << "[Router] pingRound() peer error" <<  peers  << callerOnion ;

        sendPlain(sock, 404, "Not found"); return true;
    }


    const QString walletName = walletNameForRefOnion(ref, m_boundOnion);
    bool ready = false;
    if (!walletName.isEmpty()) {
        if (auto *m = wm()) {
            const bool running = (m->walletInstance(walletName) != nullptr);
            const qint64 last  = m->lastRefreshTs(walletName);
            const qint64 age   = (last > 0) ? (QDateTime::currentSecsSinceEpoch() - last) : LLONG_MAX;
            ready = running && (age <= kReadyWindowSecs);

            if (!running) {

                const QString key = QStringLiteral("connect:%1").arg(walletName);
                if (!cooldownAllow(key, 15)) { /* skip excessive triggers */ }
                else QMetaObject::invokeMethod(m, [m, walletName](){
                        if (!m->walletInstance(walletName))
                            m->connectWallet(walletName);
                    }, Qt::QueuedConnection);
            }
        }
    }

    QJsonObject out{
        { "ref",    ref },
        { "online", true },
        { "ready",  ready }
    };
    sendJson(sock, 200, out);
    return true;
}

bool MultisigApiRouter::handleTransferRequestInfo(const QByteArray &method,
                                                  const QByteArray &rawPath,
                                                  const QMap<QByteArray,QByteArray> &headers,
                                                  QTcpSocket *sock)
{
    if (method!="GET" || !rawPath.startsWith("/api/multisig/transfer/request_info")) return false;

    QUrl u(QString::fromUtf8(rawPath)); QUrlQuery q(u);

    if (!onlyAllowedQueryKeys(u, QSet<QString>{"ref"})) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    const QString ref = q.queryItemValue("ref");


    if (m_boundOnion.isEmpty()) { sendPlain(sock, 404, "Not found"); return true; }
    {
        const QStringList peers = peersForRefOnion(ref, m_boundOnion);
        bool ok = false;
        for (const auto &p : peers)
            if (QString::compare(p, m_boundOnion, Qt::CaseInsensitive) == 0) { ok = true; break; }
        if (!ok) { sendPlain(sock, 404, "B"); return true; }
    }



    if (!refExistsForOnion(ref, m_boundOnion)) { sendPlain(sock, 404, "Not found"); return true; }

    const QString canon = canonicalPathForSig(u, ref);
    QString callerOnion, why;
    if (!verifySignedGet(rawPath, headers, canon, &callerOnion, &why)) {
        sendPlain(sock, 404, "D"); return true;
    }

    const QStringList peers = peersForRefOnion(ref, m_boundOnion);
    if (!peers.contains(callerOnion , Qt::CaseInsensitive)) {
        sendPlain(sock, 404, "Not found"); return true;
    }


    const QString walletName = walletNameForRefOnion(ref, m_boundOnion);

    QByteArray info;
    QPair<QByteArray, qint64>  pair;
    QByteArray infoB64;
    qint64     ts = 0;
    if (auto *m = wm()) {

        pair = m->giveMultisigInfo(walletName);
        infoB64 = pair.first.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

        info = pair.first; ts = pair.second;

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const bool stale = (ts == 0) || (now - ts > kMsigInfoMaxAgeSec);


        if (stale) {
            const QString key = QStringLiteral("geninfo:%1").arg(walletName);
            if (!cooldownAllow(key, 30)) {

            } else {
                QMetaObject::invokeMethod(m, [m, walletName](){
                    if (!m->walletInstance(walletName))
                        m->connectWallet(walletName);
                    m->requestGenMultisig(walletName);
                }, Qt::QueuedConnection);
            }
        }
    }

    QJsonObject out{
                    { "ref", ref },
                    { "time", qint64(ts) },
                    { "multisig_info_b64", pair.first.isEmpty() ? QJsonValue() : QJsonValue(QString::fromLatin1(infoB64)) },
                    { "len", pair.first.size() },
                    { "sha256", QString::fromLatin1(QCryptographicHash::hash(pair.first, QCryptographicHash::Sha256).toHex()) }
    };
    sendJson(sock, 200, out);
    return true;
}

bool MultisigApiRouter::handleTransferSubmit(const QByteArray &method,
                                             const QByteArray &rawPath,
                                             const QMap<QByteArray,QByteArray> &headers,
                                             const QByteArray &body,
                                             QTcpSocket *sock)
{
    if (method!="POST" || !rawPath.startsWith("/api/multisig/transfer/submit")) return false;

    QUrl u(QString::fromUtf8(rawPath)); QUrlQuery q(u);

    if (!onlyAllowedQueryKeys(u, QSet<QString>{"ref"})) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    const QString ref = q.queryItemValue("ref");

    if (m_boundOnion.isEmpty()) { sendPlain(sock, 503, "Service warming up"); return true; }
    {
        const QStringList peers = peersForRefOnion(ref, m_boundOnion);
        bool ok = false;
        for (const auto &p : peers)
            if (QString::compare(p, m_boundOnion, Qt::CaseInsensitive) == 0) { ok = true; break; }
        if (!ok) { sendPlain(sock, 404, "Not found"); return true; }
    }


    if (!refExistsForOnion(ref, m_boundOnion)) { sendPlain(sock, 404, "Not found"); return true; }


    const QJsonDocument bodyDoc = QJsonDocument::fromJson(body);
    const QJsonObject   obj     = bodyDoc.object();

    const QString transferRef = obj.value("transfer_ref").toString();
    const QString transferBlob= obj.value("transfer_blob").toString();
    const QJsonArray signing  = obj.value("signing_order").toArray();
    const QJsonArray signedBy = obj.value("who_has_signed").toArray();
    const QJsonObject desc    = obj.value("transfer_description").toObject();

    if (transferRef.isEmpty() || transferBlob.isEmpty() ||
        signing.isEmpty() || !obj.contains("who_has_signed")) {
        sendPlain(sock, 404, "Not found"); return true;
    }


    const QString canon = canonicalPathForSig(u, ref);
    QString callerOnion, why;
    if (!verifySignedPost(rawPath, headers, bodyDoc.toJson(QJsonDocument::Compact), canon, &callerOnion, &why)) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    const QStringList peers = peersForRefOnion(ref, m_boundOnion);
    if (!peers.contains(callerOnion , Qt::CaseInsensitive)) {
        sendPlain(sock, 404, "Not found"); return true;
    }


    QStringList order;  for (const auto &v : signing)  order << v.toString();
    QStringList signedL;for (const auto &v : signedBy) signedL<< v.toString();

    if (order.isEmpty()) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    const QString walletName = walletNameForRefOnion(ref, m_boundOnion);

    const QByteArray bodyCompact = bodyDoc.toJson(QJsonDocument::Compact);
    const QByteArray bodyHash    = QCryptographicHash::hash(bodyCompact, QCryptographicHash::Sha256).toHex();
    const QByteArray xpub = headers.value("x-pub");
    const QByteArray pub  = b64urlDecodeNoPad(xpub);
    if (pub.size()==32) {
        if (!seenPostAndRemember(pub, canon, bodyHash)) {
            sendJson(sock, 200, QJsonObject{
                                    { "success", true },
                                    { "transfer_ref", transferRef },
                                    { "idempotent", true },
                                    { "message", "Duplicate submit ignored" }
                                });
            return true;
        }
    }

    if (!saveIncomingTransfer(ref, walletName, transferRef, obj, order, signedL)) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    sendJson(sock, 200, QJsonObject{
                            { "success", true },
                            { "transfer_ref", transferRef },
                            { "message", "Transfer received" }
                        });
    return true;
}

bool MultisigApiRouter::handleTransferStatus(const QByteArray &method,
                                             const QByteArray &rawPath,
                                             const QMap<QByteArray,QByteArray> &headers,
                                             QTcpSocket *sock)
{
    if (method!="GET" || !rawPath.startsWith("/api/multisig/transfer/status")) return false;

    QUrl u(QString::fromUtf8(rawPath)); QUrlQuery q(u);

    if (!onlyAllowedQueryKeys(u, QSet<QString>{"ref","transfer_ref"})) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    const QString ref = q.queryItemValue("ref");


    if (m_boundOnion.isEmpty()) { sendPlain(sock, 503, "Service warming up"); return true; }
    {
        const QStringList peers = peersForRefOnion(ref, m_boundOnion);
        bool ok = false;
        for (const auto &p : peers)
            if (QString::compare(p, m_boundOnion, Qt::CaseInsensitive) == 0) { ok = true; break; }
        if (!ok) { sendPlain(sock, 404, "Not found"); return true; }
    }



    const QString transferRef = q.queryItemValue("transfer_ref");

    if (!refExistsForOnion(ref, m_boundOnion)) { sendPlain(sock, 404, "Not found"); return true; }
    if (transferRef.isEmpty()) { sendPlain(sock, 404, "Not found"); return true; }

    const QString canon = canonicalPathForSig(u, ref);
    QString callerOnion, why;
    if (!verifySignedGet(rawPath, headers, canon, &callerOnion, &why)) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    const QStringList peers = peersForRefOnion(ref, m_boundOnion);
    if (!peers.contains(callerOnion , Qt::CaseInsensitive)) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    QJsonObject saved;
    if (!readSavedTransfer(ref, transferRef, &saved)) {
        sendPlain(sock, 404, "Not found"); return true;
    }

    const QString stage = saved.value("stage").toString();
    const QString status= saved.value("status").toString();
    const QString txid  = saved.value("tx_id").toString("pending");

    static const QStringList signedStages{
        "CHECKING_STATUS","BROADCASTING","BROADCAST_SUCCESS","COMPLETE"
    };
    const bool hasSigned = signedStages.contains(stage);

    QJsonObject out{
        { "ref", ref },
        { "transferRef", transferRef },
        { "online", true },
        { "time", qint64(QDateTime::currentSecsSinceEpoch()) },
        { "received_transfer", true },
        { "has_signed", hasSigned },
        { "stage_name", stage },
        { "status", status },
        { "tx_id", txid }
    };


    sendJson(sock, 200, out);
    return true;
}


bool MultisigApiRouter::verifySignedGet(const QByteArray &rawPath,
                                        const QMap<QByteArray,QByteArray> &headers,
                                        const QString &canonPath,
                                        QString *outCallerOnion,
                                        QString *whyNot) const
{
    const QByteArray xpub = headers.value("x-pub");
    const QByteArray xts  = headers.value("x-ts");
    const QByteArray xsig = headers.value("x-sig");
    if (xpub.isEmpty() || xts.isEmpty() || xsig.isEmpty()) {
        if (whyNot) *whyNot = "missing authentication"; return false;
    }

    const QByteArray pub = b64urlDecodeNoPad(xpub);
    const QByteArray sig = b64urlDecodeNoPad(xsig);

    bool okTs=false; const qint64 ts = QString::fromUtf8(xts).toLongLong(&okTs);
    if (!okTs) { if (whyNot) *whyNot = "bad ts"; return false; }
    if (qAbs(QDateTime::currentSecsSinceEpoch() - ts) > 60) {
        if (whyNot) *whyNot = "ts too old"; return false;
    }
    if (pub.size()!=32) { if (whyNot) *whyNot = "bad pub"; return false; }

    const QJsonObject msg{{"ref", QUrlQuery(QUrl(QString::fromUtf8(rawPath))).queryItemValue("ref")},
                          {"path", canonPath},
                          {"ts",   ts}};
    const QByteArray compact = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    if (!verifyDetached(compact, sig, pub)) { if (whyNot) *whyNot = "bad sig"; return false; }

    if (outCallerOnion) *outCallerOnion = QString::fromUtf8(onionFromPub(pub)).toLower();
    return true;
}

bool MultisigApiRouter::verifySignedPost(const QByteArray &rawPath,
                                         const QMap<QByteArray,QByteArray> &headers,
                                         const QByteArray &bodyCompact,
                                         const QString &canonPath,
                                         QString *outCallerOnion,
                                         QString *whyNot) const
{
    const QByteArray xpub = headers.value("x-pub");
    const QByteArray xts  = headers.value("x-ts");
    const QByteArray xsig = headers.value("x-sig");
    if (xpub.isEmpty() || xts.isEmpty() || xsig.isEmpty()) {
        if (whyNot) *whyNot = "missing authentication"; return false;
    }

    const QByteArray pub = b64urlDecodeNoPad(xpub);
    const QByteArray sig = b64urlDecodeNoPad(xsig);
    bool okTs=false; const qint64 ts = QString::fromUtf8(xts).toLongLong(&okTs);
    if (!okTs) { if (whyNot) *whyNot = "bad ts"; return false; }
    if (qAbs(QDateTime::currentSecsSinceEpoch() - ts) > 60) {
        if (whyNot) *whyNot = "ts too old"; return false;
    }
    if (pub.size()!=32) { if (whyNot) *whyNot = "bad pub"; return false; }

    const QByteArray bodyHash = QCryptographicHash::hash(bodyCompact, QCryptographicHash::Sha256).toHex();
    const QJsonObject msg{
        {"ref", QUrlQuery(QUrl(QString::fromUtf8(rawPath))).queryItemValue("ref")},
        {"path", canonPath},
        {"ts",   ts},
        {"body", QString::fromLatin1(bodyHash)}
    };
    const QByteArray compact = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    if (!verifyDetached(compact, sig, pub)) { if (whyNot) *whyNot = "bad sig"; return false; }

    if (outCallerOnion) *outCallerOnion = QString::fromUtf8(onionFromPub(pub)).toLower();
    return true;
}


MultiWalletController* MultisigApiRouter::wm() const
{

    return RouterHandler::walletManager();
}

QString MultisigApiRouter::walletNameForRefOnion(const QString &ref, const QString &onion) const
{
    auto *m = wm(); if (!m) return {};
    return m->walletNameForRef(ref, onion);
}

QStringList MultisigApiRouter::peersForRefOnion(const QString &ref, const QString &onion) const
{
    if (auto *m = wm()) return m->peersForRef(ref, onion);
    return {};
}

bool MultisigApiRouter::refExistsForOnion(const QString &ref, const QString &onion) const
{
    auto *m = wm(); if (!m) return false;
    const QString name = m->walletNameForRef(ref, onion);
    return !name.isEmpty();
}


bool MultisigApiRouter::saveIncomingTransfer(const QString &walletRef,
                                             const QString &walletName,
                                             const QString &transferRef,
                                             const QJsonObject &jsonBody,
                                             const QStringList &signingOrder,
                                             const QStringList &whoHasSigned)
{
    if (!m_acct) return false;


    QJsonDocument doc = QJsonDocument::fromJson(m_acct->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonObject monero= root.value("monero").toObject();
    QJsonArray wallets= monero.value("wallets").toArray();

    int idx=-1; QJsonObject wobj;
    for (int i=0;i<wallets.size();++i){
        QJsonObject o = wallets.at(i).toObject();
        if (o.value("reference").toString()==walletRef || o.value("name").toString()==walletName) {
            idx=i; wobj=o; break;
        }
    }
    if (idx<0) return false;


    QJsonObject transfer;
    transfer["type"]        = QStringLiteral("MULTISIG");
    transfer["wallet_name"] = wobj.value("name").toString();
    transfer["wallet_ref"]  = walletRef;
    transfer["transfer_blob"] = jsonBody.value("transfer_blob").toString();
    transfer["transfer_description"] = jsonBody.value("transfer_description").toObject();
    transfer["signing_order"] = QJsonArray::fromStringList(signingOrder);
    transfer["signatures"]    = QJsonArray::fromStringList(whoHasSigned);
    transfer["stage"]         = "RECEIVED";
    transfer["status"]        = "NEW";
    transfer["tx_id"]         = "pending";
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 createdAt = 0;
    QJsonValue v = jsonBody.value("created_at");
    if (v.isDouble()) {
        createdAt = static_cast<qint64>(v.toDouble());
    } else if (v.isString()) {
        createdAt = v.toString().toLongLong();
    }

    transfer["created_at"]  = createdAt;
    transfer["received_at"]    = now;
    transfer["submitted_at"]    = 0;
    transfer["my_onion"]    = m_boundOnion;


    const QString myOnion = m_boundOnion;
    const QStringList walletPeers = peersForRefOnion(walletRef, myOnion);
    if (!walletPeers.contains(myOnion, Qt::CaseInsensitive)) {
            return false;
    }
    bool foundInOrder = false;
    for (const auto &s : signingOrder) {
        if (QString::compare(s, myOnion, Qt::CaseInsensitive)==0) { foundInOrder = true; break; }
    }
    if (!foundInOrder) {
            return false;
    }


    QJsonObject peersMap;
    for (const QString &peer : signingOrder) {
        if (peer.isEmpty()) continue;

        if (QString::compare(peer, myOnion, Qt::CaseInsensitive) == 0) {

            peersMap.insert(peer, QJsonArray{ "RECEIVED", true, false ,"" });
        } else {
            const bool signedIt = whoHasSigned.contains(peer, Qt::CaseInsensitive);
            peersMap.insert(peer, QJsonArray{
                                      signedIt ? "CHECKING_STATUS" : "UNKNOWN",
                                      signedIt,
                                      signedIt,
                                      "",
                                  });
        }
    }


    if (!peersMap.contains(myOnion))
        peersMap.insert(myOnion, QJsonArray{ "RECEIVED", true, false, "" });

    transfer["peers"] = peersMap;


    QJsonObject transfers = wobj.value("transfers").toObject();
    transfers.insert(transferRef, transfer);
    wobj["transfers"] = transfers;
    wallets[idx] = wobj;
    monero["wallets"] = wallets;
    root["monero"] = monero;

    const bool ok = m_acct->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return ok;
}

bool MultisigApiRouter::readSavedTransfer(const QString &walletRef,
                                          const QString &transferRef,
                                          QJsonObject *out) const
{
    if (!m_acct || !out) return false;

    QJsonDocument doc = QJsonDocument::fromJson(m_acct->loadAccountData().toUtf8());
    const QJsonObject root  = doc.object();
    const QJsonArray  wallets= root.value("monero").toObject().value("wallets").toArray();

    for (const QJsonValue &v : wallets) {
        const QJsonObject w = v.toObject();
        if (w.value("reference").toString()!=walletRef && w.value("name").toString()!=walletNameForRefOnion(walletRef, m_boundOnion)) continue;

        const QJsonObject transfers = w.value("transfers").toObject();
        if (transfers.contains(transferRef)) {
            *out = transfers.value(transferRef).toObject();
            return true;
        }
    }
    return false;
}


void MultisigApiRouter::pruneReplayCacheIfNeeded() const
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_postSeen.size() <= kReplayMaxItems)
        return;

    int removed = 0;
    for (auto it = m_postSeen.begin(); it != m_postSeen.end(); ) {
        if (now - it.value() > kReplayTtlSec) { it = m_postSeen.erase(it); ++removed; }
        else { ++it; }
    }
    if (m_postSeen.size() > kReplayMaxItems) {

        QVector<QPair<qint64, QString>> items; items.reserve(m_postSeen.size());
        for (auto it = m_postSeen.cbegin(); it != m_postSeen.cend(); ++it)
            items.push_back({it.value(), it.key()});
        std::sort(items.begin(), items.end(),
                  [](auto &a, auto &b){ return a.first < b.first; });
        const int toDrop = items.size() / 4;
        for (int i=0;i<toDrop;i++) m_postSeen.remove(items[i].second);
    }
}


bool MultisigApiRouter::seenPostAndRemember(const QByteArray &pub32,
                                            const QString &canonPath,
                                            const QByteArray &bodyHashHex) const
{

    const QString key = QString::fromLatin1(pub32.toHex())
                        + QLatin1Char('\x1f') + canonPath
                        + QLatin1Char('\x1f') + QString::fromLatin1(bodyHashHex);

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    auto it = m_postSeen.find(key);
    if (it != m_postSeen.end()) {

        if (now - it.value() <= kReplayTtlSec)
            return false;
        it.value() = now;
        return true;
    }
    pruneReplayCacheIfNeeded();
    m_postSeen.insert(key, now);
    return true;
}

bool MultisigApiRouter::cooldownAllow(const QString &key, int seconds) const
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 last = m_opCooldown.value(key, 0);
    if (now - last < seconds) return false;
    m_opCooldown.insert(key, now);
    return true;
}
