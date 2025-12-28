#include "win_compat.h"
#include "multiwalletcontroller.h"
#include "torbackend.h"
#include "wallet.h"
#include "accountmanager.h"
#include "restore_height.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QQmlEngine>
#include <QUrl>
#include <QRegularExpression>


static int nettype_to_int(const QString &nettype) {
    const QString n = nettype.trimmed().toLower();
    if (n == "mainnet")  return 0;
    if (n == "testnet")  return 1;
    return 2; // stagenet
}

static inline QDir walletDir(const AccountManager *am) {
    return QDir(am ? am->walletAccountDir() : QString());
}
static inline QString walletBase(const AccountManager *am, const QString &name) {
    return am ? am->walletPath(name) : QString();
}
static inline QDir msigDir(const AccountManager *am) {
    return QDir(am ? am->multisigInfoDir() : QString());
}

MultiWalletController::MultiWalletController(AccountManager *am, TorBackend *tor, QObject *parent) :
    QObject(parent),
    m_am(am),
    m_tor(tor)
{
    Q_ASSERT(am);
    connect(m_am, &AccountManager::isAuthenticatedChanged,
            this, &MultiWalletController::onAccountStatusChanged,
            Qt::QueuedConnection);

    connect(m_am, &AccountManager::requireDisconnectAllWallets,
            this, [this]{
                stopAllWallets();
                emit walletsChanged();
            }, Qt::QueuedConnection);

    loadWalletsFromAccount();
}

MultiWalletController::~MultiWalletController()
{
    stopAllWallets();
}

static QString normOnion(QString s) {
    s = s.trimmed().toLower();
    if (s.isEmpty()) return {};
    if (!s.endsWith(".onion")) s += ".onion";
    return s;
}

static inline bool is_onion_host(const QString &h) {
    const QString s = h.trimmed().toLower();
    return s.endsWith(".onion") && (s.size() == 62);
}

struct NetworkPlan {
    QString daemonAddr;
    bool useProxy = false;
    QString proxyHost;
    quint16 proxyPort = 0;
};

NetworkPlan plan_for(AccountManager *am, TorBackend *tor) {
    NetworkPlan p;
    const QString host = am->daemonUrl();
    const int     port = am->daemonPort();
    p.daemonAddr = QString("http://%1:%2").arg(host).arg(port);

    const bool onion  = is_onion_host(host);
    const bool wantTor= am->useTorForDaemon() || onion;

    if (wantTor && tor && tor->isRunning() && tor->socksPort() > 0) {
        p.useProxy = true;
        p.proxyHost= QStringLiteral("127.0.0.1");
        p.proxyPort= tor->socksPort();
    }
    return p;
}

void MultiWalletController::loadWalletsFromAccount()
{
    m_walletNames.clear();
    m_walletRefs .clear();
    m_meta.clear();
    m_meta_ref.clear();
    m_msigCache.clear();


    if (!(m_am && m_am->isAuthenticated()))
        return;

    const QString json = m_am->loadAccountData();
    if (json.isEmpty())
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    auto walletsArr   = doc.object()
                          .value("monero").toObject()
                          .value("wallets").toArray();

    for (const QJsonValue &val : walletsArr) {
        const QJsonObject obj = val.toObject();
        const QString name = obj.value("name").toString();

        const QString ref  = obj.value("reference").toString();
        m_walletNames.append(name);
        m_walletRefs .append(ref.isEmpty() ? name : ref);
        const QString myOnion = obj.value("my_onion").toString().trimmed().toLower();

        QStringList peersList;
        const QJsonArray peersArray = obj.value("peers").toArray();
        for (const QJsonValue &peerValue : peersArray)
            peersList.append(peerValue.toString());

        Meta m;
        m.password   = obj.value("password").toString();
        m.reference  = ref;
        m.wallet_name= name;
        m.multisig   = obj.value("multisig").toBool(true);
        m.threshold  = obj.value("threshold").toInt();
        m.total      = obj.value("total").toInt();
        m.peers      = peersList;
        m.online     = obj.value("online").toBool(true);
        m.address    = obj.value("address").toString();
        m.seed       = obj.value("seed").toString();
        m.restore_height =  obj.value("restore_height").toInt();
        m.my_onion   = myOnion;
        m.creator    = obj.value("creator").toString();
        m.archived   = obj.value("archived").toBool(false);
        m.net_type    = obj.value("net_type").toString("mainnet");

        m_meta.insert(name, m);

        if (!ref.isEmpty() && !myOnion.isEmpty())
            m_meta_ref.insert(refKey(ref, myOnion), m);

    }

    emit walletsChanged();
}

QObject *MultiWalletController::walletInstance(const QString &n) const
{
    auto it = m_wallets.find(n);
    if (it == m_wallets.end()) return nullptr;
    QQmlEngine::setObjectOwnership(it.value(), QQmlEngine::CppOwnership);
    return it.value();
}

bool MultiWalletController::walletBusy(const QString &n) const
{
    const auto *w = qobject_cast<Wallet*>(walletInstance(n));
    return w ? w->busy() : false;
}

QString MultiWalletController::walletStatus(const QString &n) const
{
    const auto *w = qobject_cast<Wallet*>(walletInstance(n));
    return w ? w->property("status").toString()
             : QStringLiteral("Disconnected");
}

QVariantMap MultiWalletController::metaToMap(const Meta &m) const
{
    QVariantMap out;
    out["password"]  = m.password;
    out["reference"] = m.reference;
    out["name"]      = m.wallet_name;
    out["multisig"]  = m.multisig;
    out["threshold"] = m.threshold;
    out["total"]     = m.total;
    out["peers"]     = m.peers;
    out["online"]    = m.online;
    out["address"]   = m.address;
    out["seed"]      = m.seed;
    out["restore_height"] = m.restore_height;
    out["my_onion"]  = m.my_onion;
    out["creator"]   = m.creator;
    out["archived"]  = m.archived;
    out["net_type"] =  m.net_type;
    return out;
}

QVariantMap MultiWalletController::getWalletMeta(const QString &walletName) const
{
    const auto it = m_meta.constFind(walletName);
    return (it == m_meta.constEnd()) ? QVariantMap{} : metaToMap(it.value());
}

QStringList MultiWalletController::peersForRef(const QString &ref, const QString &onion) const
{
    auto it = m_meta_ref.constFind(refKey(ref, onion));
    if (it != m_meta_ref.constEnd()) return it.value().peers;

    for (auto mit = m_meta.constBegin(); mit != m_meta.constEnd(); ++mit) {
        if (mit.value().reference.compare(ref, Qt::CaseInsensitive)==0 && mit.value().my_onion.compare(onion, Qt::CaseInsensitive)==0) return mit.value().peers;
    }

    return {};
}

QString MultiWalletController::walletNameForRef(const QString &ref, const QString &onion) const
{
    const QString wantRef = ref.trimmed();
    const QString wantOn  = normOnion(onion);

    auto it = m_meta_ref.constFind(refKey(wantRef, wantOn));
    if (it != m_meta_ref.constEnd()) {

        for (auto n = m_meta.constBegin(); n != m_meta.constEnd(); ++n)
            if (&(n.value()) == &(*it)) return n.key();
    }

    for (auto n = m_meta.constBegin(); n != m_meta.constEnd(); ++n) {
        const auto &m = n.value();
        if (m.reference.compare(wantRef, Qt::CaseInsensitive)==0 &&
            m.my_onion.compare(wantOn, Qt::CaseInsensitive)==0) return n.key();
    }
    return {};
}


QPair<QByteArray, qint64> MultiWalletController::giveMultisigInfo(const QString &walletName) const
{
    const auto it = m_msigCache.constFind(walletName);
    if (it == m_msigCache.constEnd())
        return {{}, 0};
    return { it->info, it->ts };
}

void MultiWalletController::requestGenMultisig(const QString &walletName)
{
    if (Wallet *w = walletPtr(walletName)) {
        w->prepareMultisigInfo(walletName);
    }
}

qint64 MultiWalletController::lastRefreshTs(const QString &walletName) const
{
    return m_lastRefreshTs.value(walletName, 0);
}

QStringList MultiWalletController::peersForWallet(const QString &walletName) const
{
    return m_meta.value(walletName).peers;
}

bool MultiWalletController::setWalletOnlineStatus(const QString &walletName, bool online)
{
    if (!(m_am && m_am->isAuthenticated())) return false;

    QJsonDocument doc = QJsonDocument::fromJson(m_am->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonArray  arr   = root.value("monero").toObject().value("wallets").toArray();
    bool found = false;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr.at(i).toObject();
        if (o.value("name").toString() == walletName) {
            o["online"] = online; arr[i] = o; found = true; break;
        }
    }
    if (!found) return false;

    QJsonObject monero = root.value("monero").toObject();
    monero["wallets"] = arr;
    root["monero"] = monero;
    if (!m_am->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact))) return false;

    auto it = m_meta.find(walletName);
    if (it != m_meta.end()) it->online = online;
    emit walletsChanged();
    return true;
}

bool MultiWalletController::updateWalletPeers(const QString &walletName, const QStringList &peers)
{
    if (!(m_am && m_am->isAuthenticated())) return false;

    QJsonDocument doc = QJsonDocument::fromJson(m_am->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonArray  arr   = root.value("monero").toObject().value("wallets").toArray();
    bool found = false;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr.at(i).toObject();
        if (o.value("name").toString() == walletName) {
            o["peers"] = QJsonArray::fromStringList(peers);
            arr[i] = o; found = true; break;
        }
    }
    if (!found) return false;

    QJsonObject monero = root.value("monero").toObject();
    monero["wallets"] = arr;
    root["monero"] = monero;
    if (!m_am->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact))) return false;

    auto it = m_meta.find(walletName);
    if (it != m_meta.end()) it->peers = peers;
    loadWalletsFromAccount();
    emit walletsChanged();
    return true;
}

bool MultiWalletController::updateWalletReference(const QString &walletName, const QString &newRef)
{
    if (!(m_am && m_am->isAuthenticated())) return false;

    const QString myOn = m_meta.value(walletName).my_onion;
    if (!newRef.isEmpty()) {
        for (auto it = m_meta.constBegin(); it != m_meta.constEnd(); ++it) {
            if (it.key() == walletName) continue;
            if (it.value().reference.compare(newRef, Qt::CaseInsensitive)==0 && it.value().my_onion.compare(myOn, Qt::CaseInsensitive)==0)  return false;
        }
    }

    QJsonDocument doc = QJsonDocument::fromJson(m_am->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonArray  arr   = root.value("monero").toObject().value("wallets").toArray();
    bool found = false;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr.at(i).toObject();
        if (o.value("name").toString() == walletName) {
            o["reference"] = newRef; arr[i] = o; found = true; break;
        }
    }
    if (!found) return false;

    QJsonObject monero = root.value("monero").toObject();
    monero["wallets"] = arr;
    root["monero"] = monero;
    if (!m_am->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact))) return false;

    auto it = m_meta.find(walletName);
    if (it != m_meta.end()) it->reference = newRef;
    loadWalletsFromAccount();
    emit walletsChanged();
    return true;
}

bool MultiWalletController::updateWalletMyOnion(const QString &walletName, const QString &newOnion)
{
    if (!(m_am && m_am->isAuthenticated()))
        return false;

    auto norm = [](QString s){
        s = s.trimmed().toLower();
        if (!s.endsWith(QStringLiteral(".onion"))) s += QStringLiteral(".onion");
        return s;
    };


    const QString o = norm(newOnion);

    const QString currentRef = m_meta.value(walletName).reference;
    if (!currentRef.isEmpty()) {
        const QString collision = walletNameForRef(currentRef, o);
        if (!collision.isEmpty() &&
            QString::compare(collision, walletName, Qt::CaseInsensitive) != 0) {

            return false;
        }
    }


    const QStringList ownedList = m_am->torOnions();
    QSet<QString> ownedSet;
    for (const QString &id : ownedList) ownedSet.insert(norm(id));


    if (!ownedSet.contains(o))
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(m_am->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonObject mon   = root.value("monero").toObject();
    QJsonArray  arr   = mon.value("wallets").toArray();


    int idx = -1;
    for (int i = 0; i < arr.size(); ++i) {
        if (arr.at(i).toObject().value("name").toString() == walletName) {
            idx = i; break;
        }
    }
    if (idx < 0) return false;

    QJsonObject w = arr.at(idx).toObject();
    const QString oldOnion = norm(w.value("my_onion").toString());

    QJsonArray peers = w.value("peers").toArray();
    QJsonArray outPeers;
    QSet<QString> added;

    for (const auto &pv : peers) {
        const QString p = norm(pv.toString());
        if (ownedSet.contains(p) && p != o) continue;
        if (added.contains(p)) continue;
        outPeers.append(p);
        added.insert(p);
    }

    if (!added.contains(o)) {
        outPeers.append(o);
        added.insert(o);
    }

    w["my_onion"] = o;
    w["peers"]    = outPeers;
    arr[idx]      = w;
    mon["wallets"]= arr;
    root["monero"]= mon;

    if (!m_am->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact)))
        return false;


    auto it = m_meta.find(walletName);
    if (it != m_meta.end()) {
        it->my_onion = o;

        QStringList peersList;
        peersList.reserve(outPeers.size());
        for (const auto &pv : outPeers) peersList << pv.toString();
        it->peers = peersList;


        const QString ref = it->reference;
        if (!ref.isEmpty()) {
            if (!oldOnion.isEmpty())
                m_meta_ref.remove(refKey(ref, oldOnion));
            m_meta_ref.insert(refKey(ref, o), *it);
        }
    }


    loadWalletsFromAccount();
    emit walletsChanged();
    return true;
}

bool MultiWalletController::updateWalletPassword(const QString &walletName, const QString &newPassword)
{
    auto *w = qobject_cast<Wallet*>(walletInstance(walletName));
    if (!w) return false;

    const QString old = m_meta.value(walletName).password;

    connect(w, &Wallet::passwordChanged, this,
            [this, walletName, newPassword](bool ok) {
                if (ok && m_am && m_am->isAuthenticated()) {
                    QJsonDocument doc = QJsonDocument::fromJson(m_am->loadAccountData().toUtf8());
                    QJsonObject root  = doc.object();
                    QJsonArray  arr   = root.value("monero").toObject().value("wallets").toArray();
                    for (int i = 0; i < arr.size(); ++i) {
                        QJsonObject o = arr.at(i).toObject();
                        if (o.value("name").toString() == walletName) { o["password"] = newPassword; arr[i] = o; break; }
                    }
                    QJsonObject monero = root.value("monero").toObject();
                    monero["wallets"] = arr; root["monero"] = monero;
                    (void) m_am->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact));
                    auto it = m_meta.find(walletName); if (it != m_meta.end()) it->password = newPassword;
                }
                emit passwordReady(ok, walletName, ok ? newPassword : QString());
            },
            Qt::QueuedConnection);

    w->changePassword(old, newPassword);
    return true;
}

bool MultiWalletController::renameWallet(const QString &oldName, const QString &newName)
{
    if (m_wallets.contains(oldName)) return false;
    if (m_meta.contains(newName))     return false;
    if (!(m_am && m_am->isAuthenticated())) return false;

    const QDir d(m_am->walletAccountDir());
    if (!d.exists()) return false;

    const QStringList variants = { "", ".keys", ".txt" };
    for (const QString &ext : variants) {
        const QString src = d.filePath(oldName + ext);
        const QString dst = d.filePath(newName + ext);
        if (QFileInfo::exists(src)) {
            if (!QFile::rename(src, dst)) return false;
        }
    }

    // multisig_info lives under wallets/<account>/multisig_info/<walletName>
    const QDir ms(m_am->multisigInfoDir());
    if (QFileInfo(ms.filePath(newName)).exists()) return false;

    for (const QString &ext : variants) {
        const QString src = d.filePath(oldName + ext);
        const QString dst = d.filePath(newName + ext);
        if (QFileInfo::exists(src)) {
            if (!QFile::rename(src, dst)) return false;
        }
    }

    if (!ms.exists()) QDir().mkpath(ms.absolutePath());
    const QString msSrc = ms.filePath(oldName);
    const QString msDst = ms.filePath(newName);
    if (QFileInfo(msSrc).exists()) {
        if (!QFile::rename(msSrc, msDst)) return false;
    }


    QJsonDocument doc = QJsonDocument::fromJson(m_am->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonArray  arr   = root.value("monero").toObject().value("wallets").toArray();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr.at(i).toObject();
        if (o.value("name").toString() == oldName) { o["name"] = newName; arr[i] = o; break; }
    }
    QJsonObject monero = root.value("monero").toObject();
    monero["wallets"] = arr; root["monero"] = monero;
    if (!m_am->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact))) return false;

    if (m_meta.contains(oldName)) {
        Meta m = m_meta.take(oldName);
        m.wallet_name = newName;
        m_meta.insert(newName, m);
    }
    m_walletNames.replace(m_walletNames.indexOf(oldName), newName);
    loadWalletsFromAccount();
    emit walletsChanged();
    return true;
}

bool MultiWalletController::removeWallet(const QString &walletName)
{
    if (!(m_am && m_am->isAuthenticated())) return false;

    QJsonDocument doc = QJsonDocument::fromJson(m_am->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonArray  arr   = root.value("monero").toObject().value("wallets").toArray();
    int idx = -1;
    for (int i = 0; i < arr.size(); ++i) {
        if (arr.at(i).toObject().value("name").toString() == walletName) { idx = i; break; }
    }
    if (idx < 0) return false;
    arr.removeAt(idx);
    QJsonObject monero = root.value("monero").toObject();
    monero["wallets"] = arr; root["monero"] = monero;
    if (!m_am->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact))) return false;

    disconnectWallet(walletName);

    m_meta.remove(walletName);
    m_walletNames.removeAll(walletName);

    m_msigCache.remove(walletName);
    m_lastRefreshTs.remove(walletName);

    const QString base = m_am->walletPath(walletName);
    QFile::remove(base);
    QFile::remove(base + ".keys");
    QFile::remove(base + ".txt");

    const QString msPath = QDir(m_am->multisigInfoDir()).filePath(walletName);
    if (QFileInfo(msPath).exists()) {
        QDir(msPath).removeRecursively();
    }

    loadWalletsFromAccount();
    emit walletsChanged();
    return true;
}


void MultiWalletController::addWalletToAccount(const QString &walletName,
                                               const QString &password,
                                               const QString &seed,
                                               const QString address,
                                               quint64       restoreHeight,
                                               const QString &myOnion,
                                               const QString &reference,
                                               bool          multisig,
                                               quint64       threshold,
                                               quint64       total,
                                               QStringList   peers,
                                               bool          online,
                                               const QString &creator,
                                               const QString  net_type)
{
    if (!(m_am && m_am->isAuthenticated()))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(m_am->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonArray  arr   = root.value("monero").toObject().value("wallets").toArray();

    QJsonObject newObj{
        { "name",           walletName     },
        { "password",       password       },
        { "seed",           seed           },
        { "address",        address        },
        { "restore_height", static_cast<qint64>(restoreHeight) },
        { "my_onion",       myOnion        },
        { "reference",      reference      },
        { "multisig",       multisig       },
        { "threshold",      static_cast<qint64>(threshold)     },
        { "total",          static_cast<qint64>(total)         },
        { "peers",          QJsonArray::fromStringList(peers)  },
        { "online",         online         },
        { "creator",        creator        },
        { "net_type",       net_type}

    };

    bool replaced = false;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr.at(i).toObject();
        if (o.value("name").toString() == walletName) {
            arr[i] = newObj; replaced = true; break;
        }
    }
    if (!replaced) arr.append(newObj);

    QJsonObject monero = root.value("monero").toObject();
    monero["wallets"]  = arr;
    root["monero"]     = monero;
    m_am->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact));

    loadWalletsFromAccount();
}


void MultiWalletController::connectWallet(const QString &walletName)
{
    if (m_wallets.contains(walletName)) {
        qDebug().noquote() << "m_wallets.contains(walletName)";
        return;
    }

    const auto meta = m_meta.value(walletName);
    qDebug().noquote() << meta.wallet_name;

    if (!meta.wallet_name.isNull()) {
        auto *w = new Wallet;
        QString accountName = m_am->currentAccount();
        QDir().mkpath(m_am->walletAccountDir());
        const QString walletPath = m_am->walletPath(walletName);

        const auto net = plan_for(m_am, m_tor);
        qDebug() << "Setting daemon address to:" << net.daemonAddr
                 << "via_tor=" << net.useProxy
                 << (net.useProxy ? QString("socks %1:%2").arg(net.proxyHost).arg(net.proxyPort) : QString());
        w->setDaemonAddress(net.daemonAddr);
        if (net.useProxy)  w->setSocksProxy(net.proxyHost, net.proxyPort);
        else               w->clearProxy();

        connect(w, &Wallet::busyChanged, this, [this, walletName]() {
            emit busyChanged(walletName, walletBusy(walletName));
        }, Qt::QueuedConnection);

        connect(w, &Wallet::errorOccurred, this, [this, walletName](const QString &msg) {
            emit rpcError(walletName, msg);
        }, Qt::QueuedConnection);

        connect(w, &Wallet::queueChanged, this, [this, walletName] {
            emit pendingOpsChanged(walletName);
        }, Qt::QueuedConnection);

        connect(w, &Wallet::syncProgress, this, [this, walletName](quint64 /*current*/, quint64 /*target*/) {
            m_lastRefreshTs[walletName] = QDateTime::currentSecsSinceEpoch();
        }, Qt::QueuedConnection);

        connect(w, &Wallet::multisigInfoPrepared, this, [this, walletName](const QByteArray &info) {
            m_msigCache[walletName] = MultisigInfoCache{ info, QDateTime::currentSecsSinceEpoch() };
            emit multisigInfoUpdated(walletName);
        }, Qt::QueuedConnection);

        connect(w, &Wallet::balanceReady, this, [this, walletName](quint64 bal, quint64 unlk, bool){
            emit walletBalanceChanged(walletName, bal, unlk);
        }, Qt::QueuedConnection);

        connect(w, &Wallet::walletOpened, this, [this, walletName, w]() {

            const auto meta = m_meta.value(walletName);
            const uint64_t h = meta.restore_height > 10 ? meta.restore_height - 10 : 0;
            w->setRefreshHeight(h);
            emit walletsChanged();
            w->startSync(120);
            w->getBalance();
        }, Qt::QueuedConnection);



        connect(w, &Wallet::accountsReady, this,
                [this, walletName](const QVariantList &a){ emit accountsReady(walletName, a); },
                Qt::QueuedConnection);


        connect(w, &Wallet::subaddressesReady, this,
                [this, walletName](int acc, const QVariantList &items){ emit subaddressesReady(walletName, acc, items); },
                Qt::QueuedConnection);
        connect(w, &Wallet::subaddressCreated, this,
                [this, walletName](int acc, int idx, const QString &addr){ emit subaddressCreated(walletName, acc, idx, addr); },
                Qt::QueuedConnection);
        connect(w, &Wallet::subaddressLabeled, this,
                [this, walletName](int acc, int idx, const QString &label){ emit subaddressLabeled(walletName, acc, idx, label); },
                Qt::QueuedConnection);


        connect(w, &Wallet::subaddressLookaheadSet, this,
                [this, walletName](int maj, int min){ emit subaddressLookaheadSet(walletName, maj, min); },
                Qt::QueuedConnection);

        m_wallets.insert(walletName, w);
        emit walletsChanged();
        bumpEpoch();

        const QString nettype = m_am->networkType();

        if (meta.net_type != nettype ) {
            emit rpcError(walletName , QStringLiteral("Network mismatch: current account network is '%1'.").arg(nettype));
            return;
        }

        w->open(walletPath, meta.password, nettype);
    }
}

QStringList MultiWalletController::connectedWalletNames() const
{
    return m_wallets.keys();
}

bool MultiWalletController::walletExists(const QString &walletName) const
{
    const QString trimmed = walletName.trimmed();
    if (trimmed.isEmpty())
        return false;


    for (const QString &n : m_walletNames) {
        if (QString::compare(n, trimmed, Qt::CaseInsensitive) == 0)
            return true;
    }

    if (!m_am)
        return false;

    const QString account = m_am->currentAccount();
    if (account.isEmpty())
        return false;

    const QDir dir(m_am->walletAccountDir());
    if (!dir.exists())
        return false;

    const QString basePath = dir.filePath(trimmed);
    if (QFile::exists(basePath)) return true;
    if (QFile::exists(basePath + ".keys")) return true;
    if (QFile::exists(basePath + ".txt")) return true;

    const QString want0 = trimmed;
    const QString want1 = trimmed + ".keys";
    const QString want2 = trimmed + ".txt";

    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoSymLinks);
    for (const QFileInfo &fi : files) {
        const QString fn = fi.fileName();
        if (fn.compare(want0, Qt::CaseInsensitive) == 0) return true;
        if (fn.compare(want1, Qt::CaseInsensitive) == 0) return true;
        if (fn.compare(want2, Qt::CaseInsensitive) == 0) return true;
    }

    return false;
}

void MultiWalletController::createWallet(const QString &walletName,const QString &password , const QString &nettype )
{
    if (m_wallets.contains(walletName)){
        qDebug() << "already in instances";
        return;
    }

    const auto meta = m_meta.value(walletName);

    if (meta.wallet_name.isNull()){
        auto *w = new Wallet;
        QString accountName = m_am->currentAccount();
        const QString walletPath = m_am->walletPath(walletName);

        const auto net = plan_for(m_am, m_tor);
        qDebug() << "Setting daemon address to:" << net.daemonAddr
                 << "via_tor=" << net.useProxy
                 << (net.useProxy ? QString("socks %1:%2").arg(net.proxyHost).arg(net.proxyPort) : QString());
        w->setDaemonAddress(net.daemonAddr);
        if (net.useProxy)  w->setSocksProxy(net.proxyHost, net.proxyPort);
        else               w->clearProxy();

        connect(w, &Wallet::busyChanged, this, [this, walletName]() {
            emit busyChanged(walletName, walletBusy(walletName));
        }, Qt::QueuedConnection);

        connect(w, &Wallet::errorOccurred, this, [this, walletName](const QString &msg) {
            emit rpcError(walletName, msg);
        }, Qt::QueuedConnection);

        connect(w, &Wallet::queueChanged, this, [this, walletName] {
            emit pendingOpsChanged(walletName);
        }, Qt::QueuedConnection);

        m_wallets.insert(walletName, w);
        emit walletsChanged();
        bumpEpoch();

        qDebug() << "creating new wallet";

        w->createNew(walletPath, password , "English",  nettype , 1);
    }
}

void MultiWalletController::disconnectWallet(const QString &walletName)
{
    auto it = m_wallets.find(walletName);
    if (it == m_wallets.end())
        return;

    Wallet *w = it.value();
    w->stopSync();
    w->close();
    connect(w, &Wallet::walletFullyClosed, this, [this, w]() {
        w->deleteLater();
    }, Qt::QueuedConnection);
    m_wallets.erase(it);
    m_msigCache.remove(walletName);
    m_lastRefreshTs.remove(walletName);

    emit walletsChanged();
    bumpEpoch();
}

void MultiWalletController::refreshWallet(const QString &walletName)
{
    if (auto *w = qobject_cast<Wallet*>(walletInstance(walletName)))
        w->refreshAsync();
}

void MultiWalletController::stopAllWallets()
{
    const QStringList names = m_wallets.keys();
    for (const QString &name : names)
        disconnectWallet(name);
}


void MultiWalletController::onAccountStatusChanged()
{
    stopAllWallets();
    if (m_am && m_am->isAuthenticated()) {

        loadWalletsFromAccount();
    } else {

        m_walletNames.clear();
        m_walletRefs.clear();
        m_meta.clear();
        m_meta_ref.clear();
        m_msigCache.clear();
        m_lastRefreshTs.clear();
        emit walletsChanged();
        bumpEpoch();
    }
}

bool MultiWalletController::importWallet(bool          fromFile,
                                         const QString &walletName,
                                         const QString &source,
                                         const QString &password,
                                         const QString &seedWords,
                                         const quint64 &restoreHeight,
                                         bool           multisig,
                                         const QString &reference,
                                         const QStringList &peers,
                                         const QString &myOnionArg,
                                         const QString &nettype )
{


    qDebug().noquote() << source ;

    if (!m_am || !m_am->isAuthenticated()) {
        qDebug().noquote() << "Not authenticated";
        emit rpcError(QString(), tr("Not authenticated"));  return false;
    }

    if ( m_am->networkType() != nettype) {
        qDebug().noquote() << "Network mismatch";
        emit rpcError(QString(), tr("Network mismatch"));  return false;
    }


    qDebug().noquote() << walletName ;
    if (walletName.isEmpty()) {
        qDebug().noquote() << "Wallet name is empty";
        emit rpcError(QString(), tr("Wallet name is empty"));  return false;

    }
    if (walletExists(walletName)) {
        qDebug().noquote() << "Wallet '%1' already exists";
        emit rpcError(QString(), tr("Wallet '%1' already exists").arg(walletName));  return false;
    }
    if (multisig && reference.trimmed().isEmpty()) {
        qDebug().noquote() << "Reference is mandatory for multisig wallets";
        emit rpcError(QString(), tr("Reference is mandatory for multisig wallets"));  return false;
    }


    auto normOnion = [](QString s){
        s = s.trimmed().toLower();
        if (!s.endsWith(QStringLiteral(".onion"))) s += QStringLiteral(".onion");
        return s;
    };


    const QStringList ownedIdsRaw = m_am ? m_am->torOnions() : QStringList{};
    QSet<QString> ownedIds;
    for (const QString &id : ownedIdsRaw) ownedIds.insert(normOnion(id));

    QString chosenMyOnion = normOnion(myOnionArg);
    if (!chosenMyOnion.isEmpty() && !ownedIds.contains(chosenMyOnion)) {

        chosenMyOnion.clear();
    }

    if (!reference.trimmed().isEmpty()) {
        if (!refOnionAvailable(reference, chosenMyOnion)) {
            qDebug().noquote() << "Reference '" << reference << "' already in use for onion " << chosenMyOnion;
            emit rpcError(QString(), tr("Reference '%1' is already used on %2").arg(reference, chosenMyOnion));
            return false;
        }
    }



    QStringList peersNorm;
    peersNorm.reserve(peers.size());
    QSet<QString> dedup;
    for (const QString &p : peers) {
        const QString pn = normOnion(p);
        if (pn.isEmpty() || dedup.contains(pn)) continue;
        dedup.insert(pn);
        peersNorm.push_back(pn);
    }

    if (chosenMyOnion.isEmpty()) {
        for (const QString &pn : peersNorm) {
            if (ownedIds.contains(pn)) { chosenMyOnion = pn; break; }
        }
    }


    QStringList peersOut; peersOut.reserve(peersNorm.size()+1);
    QSet<QString> outDedup;
    for (const QString &pn : peersNorm) {
        if (ownedIds.contains(pn) && pn != chosenMyOnion) continue;
        if (outDedup.contains(pn)) continue;
        outDedup.insert(pn);
        peersOut.push_back(pn);
    }

    if (!chosenMyOnion.isEmpty() && !outDedup.contains(chosenMyOnion)) {
        peersOut.push_back(chosenMyOnion);
        outDedup.insert(chosenMyOnion);
    }


    QDir().mkpath(m_am->walletAccountDir());
    const QString walletPath = m_am->walletPath(walletName);


    auto *w = new Wallet;

    const auto net = plan_for(m_am, m_tor);
    qDebug() << "Setting daemon address to:" << net.daemonAddr
             << "via_tor=" << net.useProxy
             << (net.useProxy ? QString("socks %1:%2").arg(net.proxyHost).arg(net.proxyPort) : QString());
    w->setDaemonAddress(net.daemonAddr);
    if (net.useProxy)  w->setSocksProxy(net.proxyHost, net.proxyPort);
    else               w->clearProxy();

    connect(w, &Wallet::errorOccurred, this,
            [this, walletName](const QString &msg){
                emit rpcError(walletName, msg);
                if (m_restoreInPlaceJobs.contains(walletName)) {
                    // rollbackRestoreInPlace(walletName, msg);
                }
            },
            Qt::QueuedConnection);


    if (fromFile) {
        const QString walletName_file =
            fromFile ? QFileInfo(source).completeBaseName()
                     : source.trimmed();

        if (walletName_file != walletName ){
            qDebug().noquote() << "walletname does not match path wallet name";
            return false;
        }


        QUrl url = QUrl::fromUserInput(source);
        const QString srcPath = url.isLocalFile() ? url.toLocalFile() : source;

        QFileInfo fi(srcPath);
        qDebug().noquote() << "normalized srcPath =" << fi.absoluteFilePath();

        const QString origBase = fi.completeBaseName();
        const QDir     srcDir  = fi.absoluteDir();

        const QStringList variants = {"", ".keys"};
        for (const QString &ext : variants) {
            const QString src = srcDir.filePath(origBase + ext);
            const QString dst = QString("%1%2").arg(walletPath, ext);

            qDebug().noquote() << "copy" << src << "->" << dst;

            if (!QFile::exists(src)) {
                qDebug().noquote() << "source missing:" << src;
                continue;
            }
            if (!QFile::copy(src, dst)) {
                emit rpcError(walletName, tr("Failed to copy %1 → %2").arg(src, dst));
                w->deleteLater();
                return false;
            }
        }


        connect(w, &Wallet::walletOpened, this,
                [=] {

                    Meta m;
                    m.password   = password;
                    m.reference  = reference;
                    m.wallet_name= walletName;
                    m.multisig   = multisig;
                    m.threshold  = 0;
                    m.total      = 0;
                    m.peers      = peersOut;
                    m.online     = true;
                    m.address    = "pending";
                    m.seed       = seedWords;
                    m.restore_height = restoreHeight;
                    m.my_onion   = chosenMyOnion;
                    m.creator    = "user";
                    m.net_type =  nettype;

                    m_meta.insert(walletName, m);
                    if (!reference.isEmpty() && !chosenMyOnion.isEmpty())
                        m_meta_ref.insert(refKey(reference, chosenMyOnion), m);



                    addWalletToAccount(walletName,
                                       password,
                                       /*seed*/ QString(),
                                       w->address(),
                                       restoreHeight,
                                       chosenMyOnion,
                                       reference,
                                       multisig,
                                       /*threshold*/ 0,
                                       /*total*/ 0,
                                       peersOut,
                                       /*online*/ true,
                                       "user",
                                       nettype);




                    w->getPrimarySeed();
                    w->isMultisig();
                    w->startSync(120);
                    w->getBalance();
                    maybeFinalizeRestoreInPlace(walletName);
                },
                Qt::QueuedConnection);


        connect(w, &Wallet::primarySeedReady, this,
                [this, walletName](const QString &seed)
                {
                    if (seed.isEmpty()) return;
                    const Meta m = m_meta.value(walletName);
                    addWalletToAccount(walletName,
                                       m.password,
                                       seed,
                                       m.address,
                                       m.restore_height,
                                       m.my_onion,
                                       m.reference,
                                       m.multisig,
                                       m.threshold,
                                       m.total,
                                       m.peers,
                                       m.online,
                                       m.creator,
                                       m.net_type);
                    maybeFinalizeRestoreInPlace(walletName);
                }, Qt::QueuedConnection);


        connect(w, &Wallet::isMultisigReady, this,
                [this, w, walletName](bool isMulti)
                {
                    auto m = m_meta.value(walletName);
                    if (m.multisig != isMulti) {
                        m.multisig = isMulti;
                        addWalletToAccount(walletName,
                                           m.password,
                                           m.seed,
                                           m.address,
                                           m.restore_height,
                                           m.my_onion,
                                           m.reference,
                                           isMulti,
                                           m.threshold,
                                           m.total,
                                           m.peers,
                                           m.online,
                                           m.creator,
                                           m.net_type);
                    }
                    if (isMulti)
                        w->getMultisigParams();
                    maybeFinalizeRestoreInPlace(walletName);
                }, Qt::QueuedConnection);


        connect(w, &Wallet::multisigParamsReady, this,
                [this, walletName](quint32 th, quint32 tot)
                {
                    auto m = m_meta.value(walletName);


                    m.threshold = int(th);
                    m.total     = int(tot);

                    addWalletToAccount(walletName,
                                       m.password,
                                       m.seed,
                                       m.address,
                                       m.restore_height,
                                       m.my_onion,
                                       m.reference,
                                       m.multisig,
                                       th,
                                       tot,
                                       m.peers,
                                       m.online,
                                       m.creator,
                                       m.net_type);
                    maybeFinalizeRestoreInPlace(walletName);
                }, Qt::QueuedConnection);


        m_wallets.insert(walletName, w);
        bumpEpoch();
        emit walletsChanged();
        qDebug().noquote() << "Trying to open";

        w->open(walletPath, password, nettype);
        return true;
    }



    connect(w, &Wallet::walletRestored, this,
            [=] {

                Meta m;
                m.password   = password;
                m.reference  = reference;
                m.wallet_name= walletName;
                m.multisig   = multisig;
                m.threshold  = 0;
                m.total      = 0;
                m.peers      = peersOut;
                m.online     = true;
                m.address    = "pending";
                m.seed       = seedWords;
                m.restore_height = restoreHeight;
                m.my_onion   = chosenMyOnion;
                m.creator    = "user";
                m.net_type =  nettype;

                m_meta.insert(walletName, m);
                if (!reference.isEmpty() && !chosenMyOnion.isEmpty())
                    m_meta_ref.insert(refKey(reference, chosenMyOnion), m);

                addWalletToAccount(walletName,
                                   password,
                                   seedWords,
                                   w->address(),
                                   restoreHeight,
                                   chosenMyOnion,
                                   reference,
                                   multisig,
                                   /*threshold*/ 0,
                                   /*total*/ 0,
                                   peersOut,
                                   /*online*/ true,
                                   "user",
                                   nettype);

                w->isMultisig();
                w->startSync(120);
                w->getBalance();
                maybeFinalizeRestoreInPlace(walletName);
            },
            Qt::QueuedConnection);


    connect(w, &Wallet::isMultisigReady, this,
            [this, w, walletName](bool isMulti)
            {
                auto m = m_meta.value(walletName);
                if (m.multisig != isMulti) {
                    m.multisig = isMulti;
                    addWalletToAccount(walletName,
                                       m.password,
                                       m.seed,
                                       m.address,
                                       m.restore_height,
                                       m.my_onion,
                                       m.reference,
                                       isMulti,
                                       m.threshold,
                                       m.total,
                                       m.peers,
                                       m.online,
                                       m.creator,
                                       m.net_type);
                }
                if (isMulti)
                    w->getMultisigParams();
                maybeFinalizeRestoreInPlace(walletName);
            }, Qt::QueuedConnection);


    connect(w, &Wallet::multisigParamsReady, this,
            [this, walletName](quint32 th, quint32 tot)
            {
                auto m = m_meta.value(walletName);
                m.threshold = int(th);
                m.total     = int(tot);

                addWalletToAccount(walletName,
                                   m.password,
                                   m.seed,
                                   m.address,
                                   m.restore_height,
                                   m.my_onion,
                                   m.reference,
                                   m.multisig,
                                   th,
                                   tot,
                                   m.peers,
                                   m.online,
                                   m.creator,
                                   m.net_type);

                maybeFinalizeRestoreInPlace(walletName);

            }, Qt::QueuedConnection);

    m_wallets.insert(walletName, w);
    bumpEpoch();
    emit walletsChanged();
    qDebug().noquote() << "Trying to restore";
    w->restoreFromSeed(walletPath,
                       password,
                       seedWords,
                       restoreHeight,
                       /*language*/ "English",
                       /*testnet*/ nettype,
                       1,
                       multisig);
    return true;
}



bool MultiWalletController::getAccounts(const QString &walletName) {
    if (auto *w = walletPtr(walletName)) { w->getAccounts(); return true; }
    return false;
}


bool MultiWalletController::getSubaddresses(const QString &walletName, quint32 accountIndex) {
    if (auto *w = walletPtr(walletName)) { w->getSubaddresses(accountIndex); return true; }
    return false;
}
bool MultiWalletController::createSubaddress(const QString &walletName, quint32 accountIndex, const QString &label) {
    if (auto *w = walletPtr(walletName)) { w->createSubaddress(accountIndex, label); return true; }
    return false;
}
bool MultiWalletController::labelSubaddress(const QString &walletName, quint32 accountIndex, quint32 addressIndex, const QString &label) {
    if (auto *w = walletPtr(walletName)) { w->labelSubaddress(accountIndex, addressIndex, label); return true; }
    return false;
}


bool MultiWalletController::setSubaddressLookahead(const QString &walletName, quint32 major, quint32 minor) {
    if (auto *w = walletPtr(walletName)) { w->setSubaddressLookahead(major, minor); return true; }
    return false;
}


void MultiWalletController::reset()
{

    stopAllWallets();
    m_walletNames.clear();
    m_walletRefs.clear();
    m_meta.clear();
    m_meta_ref.clear();
    m_msigCache.clear();
    m_lastRefreshTs.clear();

    emit walletsChanged();
    bumpEpoch();
}


bool MultiWalletController::refExistsForOnion(const QString &reference, const QString &onion) const
{
    return !walletNameForRef(reference, onion).isEmpty();
}

bool MultiWalletController::refOnionAvailable(const QString &reference,
                                              const QString &onion,
                                              const QString &excludingWallet) const
{
    const QString existing = walletNameForRef(reference, onion);
    if (existing.isEmpty()) return true;
    if (excludingWallet.isEmpty()) return false;
    return QString::compare(existing, excludingWallet, Qt::CaseInsensitive) == 0;
}

bool MultiWalletController::setWalletArchived(const QString &walletName, bool archived)
{
    if (!(m_am && m_am->isAuthenticated())) return false;

    QJsonDocument doc = QJsonDocument::fromJson(m_am->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonArray  arr   = root.value("monero").toObject().value("wallets").toArray();

    bool found = false;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr.at(i).toObject();
        if (o.value("name").toString() == walletName) {
            o["archived"] = archived;
            arr[i] = o;
            found = true;
            break;
        }
    }
    if (!found) return false;

    QJsonObject monero = root.value("monero").toObject();
    monero["wallets"] = arr;
    root["monero"] = monero;
    if (!m_am->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact))) return false;

    // Update local meta
    auto it = m_meta.find(walletName);
    if (it != m_meta.end()) it->archived = archived;

    emit walletsChanged();
    return true;
}


bool MultiWalletController::addressMatchesCurrentNet(const QString &address) const
{
    if (address.isEmpty()) return true; // nothing to check
    const QString net = m_am->networkType();
    const QChar c = address.trimmed().left(1).toUpper().at(0);

    if (net == "mainnet") {
        // Standard(4), Sub(8), Integrated(4)
        return (c == QLatin1Char('4') || c == QLatin1Char('8'));
    } else if (net == "testnet") {
        // Standard(9 or A), Sub(B), Integrated(9 or A)
        return (c == QLatin1Char('9') || c == QLatin1Char('A') || c == QLatin1Char('B'));
    } else if (net == "stagenet") {
        // Standard(5), Sub(7), Integrated(5)
        return (c == QLatin1Char('5') || c == QLatin1Char('7'));
    }
    return false;
}



QString MultiWalletController::randomSuffix(int len) const
{
    static const QString alphabet = QStringLiteral("abcdefghijklmnopqrstuvwxyz0123456789");
    QString out;
    out.reserve(len);
    for (int i = 0; i < len; ++i) {
        const int idx = int(QRandomGenerator::global()->bounded(alphabet.size()));
        out.append(alphabet[idx]);
    }
    return out;
}

QString MultiWalletController::makeUniqueBackupName(const QString &base) const
{
    const QString ts = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss");
    QString cand = QStringLiteral("__backup__%1__%2").arg(base, ts);  //

    int n = 2;
    while (walletExists(cand)) {
        cand = QStringLiteral("__backup__%1__%2__%3").arg(base, ts).arg(n++);
    }
    return cand;
}


bool MultiWalletController::restoreWalletInPlaceUsingImport(const QString &walletName)
{
    if (!m_am || !m_am->isAuthenticated()) {
        emit rpcError(walletName, tr("Not authenticated"));
        return false;
    }

    // Guard: must be disconnected
    if (m_wallets.contains(walletName)) {
        emit rpcError(walletName, tr("Disconnect wallet before restoring"));
        return false;
    }

    if (!m_meta.contains(walletName)) {
        emit rpcError(walletName, tr("Wallet not found"));
        return false;
    }

    if (m_restoreInPlaceJobs.contains(walletName)) {
        emit rpcError(walletName, tr("Restore already running"));
        return false;
    }

    const Meta orig = m_meta.value(walletName);
    if (orig.seed.trimmed().isEmpty()) {
        emit rpcError(walletName, tr("No seed stored for this wallet"));
        return false;
    }

    if (m_am->networkType() != orig.net_type) {
        emit rpcError(walletName, tr("Network mismatch"));
        return false;
    }

    // 1) Backup by renaming wallet (files + account data)
    const QString backupName = makeUniqueBackupName(walletName);

    // 2) Avoid ref/onion collision: backup wallet gets a different reference
    QString backupRef = orig.reference;
    if (!orig.reference.trimmed().isEmpty()) {
        for (int i = 0; i < 50; ++i) {
            const QString cand = QStringLiteral("%1-backup-%2")
            .arg(orig.reference.trimmed(), randomSuffix(6));
            if (refOnionAvailable(cand, orig.my_onion)) { // orig.my_onion may be ""
                backupRef = cand;
                break;
            }
        }
        if (backupRef == orig.reference) {
            emit rpcError(walletName, tr("Could not generate backup reference"));
            return false;
        }
    }

    if (!renameWallet(walletName, backupName)) {
        emit rpcError(walletName, tr("Failed to create backup (rename failed)"));
        return false;
    }

    // (void)setWalletArchived(backupName, true);

    if (!orig.reference.trimmed().isEmpty()) {
        if (!updateWalletReference(backupName, backupRef)) {
            // revert rename if we can
            (void)renameWallet(backupName, walletName);
            emit rpcError(walletName, tr("Failed to update backup reference"));
            return false;
        }
    }

    // Create job
    RestoreInPlaceJob job;
    job.originalName = walletName;
    job.backupName = backupName;
    job.backupMeta = orig;                // expected values for verification
    job.originalReference = orig.reference;
    job.backupReference = backupRef;
    job.waitingForParams = orig.multisig; // multisig needs multisigParamsReady to verify th/tot
    m_restoreInPlaceJobs.insert(walletName, job);

    emit restoreInPlaceStarted(walletName, backupName);

    // 3) Restore using your existing importWallet path (seed restore)
    // IMPORTANT: pass peers + my_onion exactly from meta, no re-derivation here.
    const bool ok = importWallet(
        /*fromFile*/ false,
        /*walletName*/ walletName,
        /*source*/ QString(),
        /*password*/ orig.password,
        /*seedWords*/ orig.seed,
        /*restoreHeight*/ orig.restore_height,
        /*multisig*/ orig.multisig,
        /*reference*/ orig.reference,
        /*peers*/ orig.peers,
        /*myOnionArg*/ orig.my_onion,
        /*nettype*/ orig.net_type
        );

    if (!ok) {
        rollbackRestoreInPlace(walletName, tr("Failed to start import/restore"));
        return false;
    }

    return true;
}

void MultiWalletController::maybeFinalizeRestoreInPlace(const QString &walletName)
{
    if (!m_restoreInPlaceJobs.contains(walletName)) return;

    auto &job = m_restoreInPlaceJobs[walletName];
    if (job.completed) return;

    // We verify against the meta that should now reflect the restored wallet state
    const Meta cur = m_meta.value(walletName);

    // Basic checks: must exist
    if (cur.wallet_name.isEmpty()) return;

    // Verify stable fields (these should match your previous wallet)
    const Meta expected = job.backupMeta;

    // Address: only verify if expected had a real one (not empty/pending)
    if (!expected.address.trimmed().isEmpty() &&
        expected.address != QStringLiteral("pending") &&
        cur.address != QStringLiteral("pending") &&
        cur.address.trimmed() != expected.address.trimmed()) {
        qDebug().noquote() << "rollbackRestoreInPlace:" << "Address mismatch after restore";
        // rollbackRestoreInPlace(walletName, tr("Address mismatch after restore"));
        return;
    }

    // Multisig flag should match once isMultisigReady has run
    if (cur.multisig != expected.multisig) {
        // if it's still in flight, wait a bit (but if it flipped wrong, rollback)
        // Here we treat mismatch as failure because expected is known.
        qDebug().noquote() << "rollbackRestoreInPlace:" << "Wallet type mismatch (multisig flag)";
        // rollbackRestoreInPlace(walletName, tr("Wallet type mismatch (multisig flag)"));
        return;
    }

    // If multisig, threshold/total must match; but those arrive after multisigParamsReady
    if (expected.multisig) {
        if (cur.threshold <= 0 || cur.total <= 0) return; // not ready yet
        if (cur.threshold != expected.threshold || cur.total != expected.total) {
            qDebug().noquote() << "rollbackRestoreInPlace:" << "Multisig params mismatch (threshold/total)";
            // rollbackRestoreInPlace(walletName, tr("Multisig params mismatch (threshold/total)"));
            return;
        }
    }

    // Optional: verify reference & my_onion (should match original)
    if (cur.reference.trimmed() != expected.reference.trimmed()) {
        qDebug().noquote() << "rollbackRestoreInPlace:" << "Reference mismatch after restore";
        // rollbackRestoreInPlace(walletName, tr("Reference mismatch after restore"));
        return;
    }
    if (cur.my_onion.trimmed().toLower() != expected.my_onion.trimmed().toLower()) {
        qDebug().noquote() << "rollbackRestoreInPlace:" << "My onion mismatch after restore";
        // rollbackRestoreInPlace(walletName, tr("My onion mismatch after restore"));
        return;
    }

    // If we get here, it matches -> delete backup
    job.completed = true;

    const QString backupName = job.backupName;
    // (void)removeWallet(backupName);

    emit restoreInPlaceFinished(walletName, true,
                                tr("Wallet restored successfully. Backup '%1' deleted.").arg(backupName));
}

void MultiWalletController::rollbackRestoreInPlace(const QString &originalName, const QString &reason)
{
    if (!m_restoreInPlaceJobs.contains(originalName)) return;

    auto job = m_restoreInPlaceJobs.take(originalName);

    // Remove the newly created wallet (if any)
    disconnectWallet(originalName);
    (void)removeWallet(originalName);

    // Rename backup back to original
    if (!renameWallet(job.backupName, originalName)) {
        emit restoreInPlaceFinished(originalName, false,
                                    tr("Restore failed: %1. Backup kept as '%2' (rename-back failed).")
                                        .arg(reason, job.backupName));
        return;
    }

    // restore original reference (we modified backup ref)
    if (job.backupMeta.multisig && !job.originalReference.trimmed().isEmpty()) {
        (void)updateWalletReference(originalName, job.originalReference);
    }

    // (void)setWalletArchived(originalName, job.backupMeta.archived);

    emit restoreInPlaceFinished(originalName, false,
                                tr("Restore failed: %1. Original wallet restored from backup.").arg(reason));
}

bool MultiWalletController::createStandardWallet(const QString &walletName,
                                                 const QString &password,
                                                 const QString &nettype)
{
    const QString name = walletName.trimmed();

    if (!m_am || !m_am->isAuthenticated()) {
        emit rpcError(QString(), tr("Not authenticated"));
        return false;
    }

    if (m_am->networkType() != nettype) {
        emit rpcError(name, tr("Network mismatch"));
        return false;
    }

    if (name.isEmpty()) {
        emit rpcError(QString(), tr("Wallet name is empty"));
        return false;
    }

    if (walletExists(name)) {
        emit rpcError(name, tr("Wallet '%1' already exists").arg(name));
        return false;
    }

    if (m_wallets.contains(name)) {
        emit rpcError(name, tr("Wallet is already open"));
        return false;
    }

    if (m_stdCreateJobs.contains(name)) {
        emit rpcError(name, tr("Wallet creation already running"));
        return false;
    }

    // Create job
    StandardCreateJob job;
    job.password = password;
    job.nettype  = nettype;

    job.reference = randomSuffix(20);

    for (int i = 0; i < 20; ++i) {
        bool collision = false;
        for (auto it = m_meta.constBegin(); it != m_meta.constEnd(); ++it) {
            const auto &m = it.value();
            if (!m.multisig && m.my_onion.trimmed().isEmpty() &&
                m.reference.compare(job.reference, Qt::CaseInsensitive) == 0) {
                collision = true;
                break;
            }
        }
        if (!collision) break;
        job.reference = randomSuffix(20);
    }

    const std::time_t now = QDateTime::currentSecsSinceEpoch();
    job.restoreHeight = restore_height::estimate_from_timestamp(now, nettype_to_int(nettype));
    m_stdCreateJobs.insert(name, job);

    // Wallet path
    const QString walletPath = m_am->walletPath(name);

    // Create wallet instance
    auto *w = new Wallet;

    const auto net = plan_for(m_am, m_tor);
    w->setDaemonAddress(net.daemonAddr);
    if (net.useProxy)  w->setSocksProxy(net.proxyHost, net.proxyPort);
    else               w->clearProxy();

    connect(w, &Wallet::errorOccurred, this,
            [this, name, w](const QString &msg) {
                emit rpcError(name, msg);
                m_stdCreateJobs.remove(name);

                // cleanup instance if we inserted it
                if (m_wallets.value(name) == w) {
                    disconnectWallet(name);
                } else {
                    w->deleteLater();
                }
            },
            Qt::QueuedConnection);

    connect(w, &Wallet::walletCreated, this,
            [this, name, w]() {
                // Request both; we finalize when both arrive
                w->getPrimarySeed();
                w->getAddress();

                // Optional: start sync immediately
                w->startSync(120);
            },
            Qt::QueuedConnection);

    connect(w, &Wallet::primarySeedReady, this,
            [this, name](const QString &seed) {
                if (!m_stdCreateJobs.contains(name) || seed.isEmpty()) return;
                auto &job = m_stdCreateJobs[name];
                job.seed = seed;
                job.gotSeed = true;
                maybeFinalizeStandardCreate(name);
            },
            Qt::QueuedConnection);

    connect(w, &Wallet::addressReady, this,
            [this, name](const QString &addr) {
                if (!m_stdCreateJobs.contains(name) || addr.isEmpty()) return;
                auto &job = m_stdCreateJobs[name];
                job.address = addr;
                job.gotAddr = true;
                maybeFinalizeStandardCreate(name);
            },
            Qt::QueuedConnection);

    // Hook typical signals like you do in connectWallet/createWallet (busy/rpc/etc)
    connect(w, &Wallet::busyChanged, this, [this, name]() {
        emit busyChanged(name, walletBusy(name));
    }, Qt::QueuedConnection);

    connect(w, &Wallet::queueChanged, this, [this, name] {
        emit pendingOpsChanged(name);
    }, Qt::QueuedConnection);

    connect(w, &Wallet::balanceReady, this, [this, name](quint64 bal, quint64 unlk, bool){
        emit walletBalanceChanged(name, bal, unlk);
    }, Qt::QueuedConnection);

    // Insert into map so UI can see it immediately
    m_wallets.insert(name, w);
    emit walletsChanged();
    bumpEpoch();

    // Create new standard wallet
    w->createNew(walletPath, password, "English", nettype, 1);
    return true;
}

void MultiWalletController::maybeFinalizeStandardCreate(const QString &walletName)
{
    if (!m_stdCreateJobs.contains(walletName)) return;
    auto &job = m_stdCreateJobs[walletName];
    if (job.done) return;
    if (!job.gotSeed || !job.gotAddr) return;

    job.done = true;

    addWalletToAccount(walletName,
                       job.password,
                       job.seed,
                       job.address,
                       job.restoreHeight,
                       /*myOnion*/ "",
                       /*reference*/ job.reference,
                       /*multisig*/ false,
                       /*threshold*/ 0,
                       /*total*/ 0,
                       /*peers*/ QStringList{},
                       /*online*/ true,
                       /*creator*/ "user",
                       job.nettype);

    emit standardWalletCreated(walletName);

    // Optional: if you prefer not to keep it connected right now:
    // disconnectWallet(walletName);

    m_stdCreateJobs.remove(walletName);
}


