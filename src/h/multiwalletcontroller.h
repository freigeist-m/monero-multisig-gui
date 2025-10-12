#pragma once

#include <QObject>
#include <QStringList>
#include <QHash>
#include <memory>
#include <QByteArray>
#include <QPair>
#include <QDateTime>
#include <QQmlEngine>

class AccountManager;
class Wallet;
class TorBackend;

class MultiWalletController : public QObject
{
    Q_OBJECT


    Q_PROPERTY(QStringList walletNames READ walletNames NOTIFY walletsChanged)
    Q_PROPERTY(QStringList walletRefs  READ walletRefs  NOTIFY walletsChanged)
    Q_PROPERTY(int         walletCount READ walletCount NOTIFY walletsChanged)
    Q_PROPERTY(int         walletConnectedCount READ walletConnectedCount NOTIFY walletsChanged)

    Q_PROPERTY(int epoch READ epoch NOTIFY epochChanged)

public:
    explicit MultiWalletController(AccountManager *am, TorBackend *tor = nullptr,
                                   QObject *parent = nullptr);
    ~MultiWalletController() override;


    QStringList walletNames() const { return m_walletNames; }
    QStringList walletRefs () const { return m_walletRefs;  }
    int         walletCount() const { return m_walletNames.size(); }
    int         walletConnectedCount() const { return m_wallets.keys().size(); }
    int         epoch()       const { return m_epoch; }

    Q_INVOKABLE QString walletNameForRef(const QString &ref, const QString &onion) const;

    Q_INVOKABLE QVariantMap getRow(int index) const {
        QVariantMap out;
        if (index < 0 || index >= m_walletNames.size()) return out;
        const QString name = m_walletNames.at(index);
        QObject* w = walletInstance(name);
        if (w) QQmlEngine::setObjectOwnership(w, QQmlEngine::CppOwnership);
        out["name"]   = name;
        out["wallet"] = QVariant::fromValue(w);
        return out;
    }

    QPair<QByteArray, qint64> giveMultisigInfo(const QString &walletName) const;
    qint64 lastRefreshTs(const QString &walletName) const;

    Q_INVOKABLE QObject *walletInstance(const QString &walletName) const;
    Q_INVOKABLE bool     walletBusy    (const QString &walletName) const;
    Q_INVOKABLE QString  walletStatus  (const QString &walletName) const;
    Q_INVOKABLE bool     walletExists  (const QString &walletName) const;
    Q_INVOKABLE QStringList peersForRef(const QString &ref, const QString &onion) const;
    Q_INVOKABLE QStringList peersForWallet(const QString &walletName) const;
    Q_INVOKABLE qint64 lastRefreshTsQml(const QString &walletName) const {
        return lastRefreshTs(walletName);
    }
    Q_INVOKABLE QVariantMap multisigInfoForWallet(const QString &walletName) const {
        const auto it = m_msigCache.constFind(walletName);
        return (it == m_msigCache.constEnd())
                   ? QVariantMap{ {"ts", 0}, {"info", QString()} }
                   : QVariantMap{ {"ts", it->ts}, {"info", QString::fromUtf8(it->info)} };
    }

    Q_INVOKABLE bool getAccounts(const QString &walletName);

    Q_INVOKABLE bool getSubaddresses(const QString &walletName, quint32 accountIndex = 0);
    Q_INVOKABLE bool createSubaddress(const QString &walletName, quint32 accountIndex, const QString &label);
    Q_INVOKABLE bool labelSubaddress(const QString &walletName, quint32 accountIndex, quint32 addressIndex, const QString &label);

    Q_INVOKABLE bool setSubaddressLookahead(const QString &walletName, quint32 major, quint32 minor);

    Q_INVOKABLE void reset();

    Q_INVOKABLE bool refExistsForOnion(const QString &reference, const QString &onion) const;
    Q_INVOKABLE bool refOnionAvailable(const QString &reference,
                                       const QString &onion,
                                       const QString &excludingWallet = {}) const;



    struct Meta {
        QString     password;
        QString     reference;
        QString     wallet_name;
        bool        multisig = true;
        int         threshold = 0;
        int         total = 0;
        QStringList peers;
        bool        online = true;
        QString     my_onion;
        QString     address;
        QString     seed;
        int         restore_height = 0 ;
        QString     creator =  "user";
        bool        archived = false;
        QString     net_type =  "mainnet";

    };

    void onAccountStatusChanged();

public slots:

    void addWalletToAccount(const QString &walletName,
                            const QString &password,
                            const QString &seed          = {},
                            const QString address        = {},
                            quint64       restoreHeight  = 0,
                            const QString &myOnion       = {},
                            const QString &reference     = {},
                            bool          multisig       = true,
                            quint64       threshold      = 0,
                            quint64       total          = 0,
                            QStringList   peers          = {},
                            bool          online         = true,
                            const QString &creator       = "user",
                            const QString net_type       = "mainnet");

    void connectWallet     (const QString &walletName);
    void disconnectWallet  (const QString &walletName);
    void refreshWallet     (const QString &walletName);
    void createWallet      (const QString &walletName, const QString &password, const QString &nettype);
    void stopAllWallets();
    void requestGenMultisig(const QString &walletName);

    Q_INVOKABLE QVariantMap getWalletMeta(const QString &walletName) const;
    Q_INVOKABLE bool setWalletOnlineStatus(const QString &walletName, bool online);
    Q_INVOKABLE bool updateWalletPeers(const QString &walletName, const QStringList &peers);
    Q_INVOKABLE bool updateWalletReference(const QString &walletName, const QString &newRef);
    Q_INVOKABLE bool renameWallet(const QString &oldName, const QString &newName);
    Q_INVOKABLE bool updateWalletPassword(const QString &walletName, const QString &newPassword);
    Q_INVOKABLE bool removeWallet(const QString &walletName);
    Q_INVOKABLE bool updateWalletMyOnion(const QString &walletName, const QString &newOnion);
    Q_INVOKABLE bool setWalletArchived(const QString &walletName, bool archived);

    Q_INVOKABLE bool importWallet(bool         fromFile,
                                  const QString &walletName,
                                  const QString &source,
                                  const QString &password,
                                  const QString &seedWords     = {},
                                  const quint64 &restoreHeight  = 0,
                                  bool          multisig       = false,
                                  const QString &reference     = {},
                                  const QStringList &peers     = {},
                                  const QString &myOnion        = {},
                                  const QString &nettype =  "mainnet" );
    Q_INVOKABLE QStringList connectedWalletNames() const;


signals:

    void walletsChanged();

    void epochChanged();


    void busyChanged   (const QString &walletName, bool busy);
    void rpcError      (const QString &walletName, const QString &payload);
    void pendingOpsChanged(const QString &walletName);
    void passwordReady(bool success, const QString &walletName, const QString &newPassword);
    void multisigInfoUpdated(const QString &walletName);

    void walletBalanceChanged(const QString &walletName,
                              quint64 balanceAtomic,
                              quint64 unlockedAtomic);


    void accountsReady(const QString &walletName, QVariantList accounts);


    void subaddressesReady(const QString &walletName, quint32 accountIndex, QVariantList items);
    void subaddressCreated(const QString &walletName, quint32 accountIndex, quint32 addressIndex, QString address);
    void subaddressLabeled(const QString &walletName, quint32 accountIndex, quint32 addressIndex, QString label);


    void subaddressLookaheadSet(const QString &walletName, quint32 major, quint32 minor);

private:

    void loadWalletsFromAccount();
    QVariantMap metaToMap(const Meta &m) const;

    Wallet *walletPtr(const QString &walletName) const {
        auto it = m_wallets.find(walletName);
        return (it != m_wallets.end()) ? it.value() : nullptr;
    }

    void bumpEpoch() { ++m_epoch; emit epochChanged(); }

    struct MultisigInfoCache {
        QByteArray info;
        qint64     ts = 0;
    };

    static QString refKey(const QString &ref, const QString &onion) {
        auto norm = [](QString s){ return s.trimmed().toLower(); };
        return norm(ref) + QLatin1Char('|') + norm(onion);
    }

    bool addressMatchesCurrentNet(const QString &addr) const;

    AccountManager               *m_am = nullptr;
    QStringList                   m_walletNames;
    QStringList                   m_walletRefs;
    QHash<QString, Meta>          m_meta;
    QHash<QString, Meta>          m_meta_ref;
    QHash<QString, Wallet*>       m_wallets;
    QHash<QString, MultisigInfoCache> m_msigCache;
    QHash<QString, qint64>            m_lastRefreshTs;

    TorBackend *m_tor = nullptr;

    int m_epoch = 0;
};

Q_DECLARE_METATYPE(MultiWalletController*)
