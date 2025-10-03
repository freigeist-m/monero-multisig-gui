#ifndef ACCOUNTMANAGER_H
#define ACCOUNTMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QMutex>
#include <QDir>
#include <QLockFile>
#include <QSaveFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <memory>
#include "cryptoutils.h"


class AccountManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool    is_authenticated      READ isAuthenticated   NOTIFY isAuthenticatedChanged)
    Q_PROPERTY(QString current_account       READ currentAccount    NOTIFY currentAccountChanged)
    Q_PROPERTY(bool    has_logged_out        READ hasLoggedOut      NOTIFY isAuthenticatedChanged)
    Q_PROPERTY(QString current_account_onion READ currentAccountOnion NOTIFY currentAccountChanged)
    Q_PROPERTY(bool    inspect_guard         READ inspectGuard      NOTIFY settingsChanged)
    Q_PROPERTY(QString daemon_url            READ daemonUrl         NOTIFY settingsChanged)
    Q_PROPERTY(int     daemon_port           READ daemonPort        NOTIFY settingsChanged)
    Q_PROPERTY(bool    use_tor_for_daemon    READ useTorForDaemon   NOTIFY settingsChanged)
    Q_PROPERTY(bool    tor_daemon            READ torDaemon         NOTIFY settingsChanged)
    Q_PROPERTY(bool    tor_autoconnect       READ torAutoconnect    NOTIFY settingsChanged)
    Q_PROPERTY(int     lock_timeout_minutes  READ lockTimeoutMinutes NOTIFY settingsChanged)

public:
    explicit AccountManager(QObject *parent = nullptr);
    ~AccountManager() override;


    bool    isAuthenticated()   const { return m_isAuthenticated; }
    QString currentAccount()    const { return m_currentAccount;  }
    bool    hasLoggedOut()      const { return m_hasLoggedOut;    }
    QString currentAccountOnion() const { return m_currentAccountOnion; }
    bool    inspectGuard()      const { return m_inspectGuard;    }
    QString daemonUrl()         const { return m_daemonUrl;       }
    int     daemonPort()        const { return m_daemonPort;      }
    bool    useTorForDaemon()   const { return m_useTorForDaemon; }
    bool    torDaemon()         const { return m_torDaemon;       }
    bool    torAutoconnect()    const { return m_torAutoconnect;  }
    int     lockTimeoutMinutes() const { return m_lockTimeoutMinutes; }


    Q_INVOKABLE QVariantList getTorIdentities() const;
    Q_INVOKABLE QStringList  torOnions() const;
    QString                  torPrivKeyFor(const QString &onion) const;


    Q_INVOKABLE bool addTorIdentity(const QString &label);
    Q_INVOKABLE bool renameTorIdentity(const QString &onion, const QString &label);
    Q_INVOKABLE bool setTorIdentityOnline(const QString &onion, bool online);
    Q_INVOKABLE bool removeTorIdentity(const QString &onion);
    Q_INVOKABLE bool removePlaceholderIdentityByLabel(const QString &label);
    Q_INVOKABLE bool darkModePref() const;
    Q_INVOKABLE bool setDarkModePref(bool dark);


    void storeTorIdentity(const QString &onion,
                          const QString &priv,
                          const QString &label,
                          bool online);


    QString  torOnion()  const;
    QString  torPrivKey() const;
    void     storeTorPair(const QString &onion, const QString &priv);

public slots:

    Q_INVOKABLE bool verifyPassword(const QString &password) const;
    bool login(const QString &filePath, const QString &password);
    void logout();
    bool updatePassword(const QString &oldPassword, const QString &newPassword);
    bool createAccount(const QString &accountName, const QString &password);


    QString loadAccountData();
    bool    saveAccountData(const QString &content);


    QVariantList getAvailableAccounts();
    QVariantList getAddressBook();
    bool addAddressBookEntry(const QString &label, const QString &onion);
    bool removeAddressBookEntry(const QString &onion);
    bool updateAddressBookEntry(const QString &oldOnion,
                                const QString &newLabel,
                                const QString &newOnion);

    QVariantList getXMRAddressBook();
    bool addXMRAddressBookEntry(const QString &label, const QString &xmrAddress);
    bool removeXMRAddressBookEntry(const QString &xmrAddress);

    QString getTrustedPeers();
    Q_INVOKABLE bool addTrustedPeer   (const QString &onion, const QString &label, int maxN, int minThreshold, bool active,const QStringList &allowedIdentities , int max_number_wallets);
    Q_INVOKABLE bool updateTrustedPeer(const QString &onion, const QString &label, int maxN, int minThreshold ,const QStringList &allowedIdentities, int max_number_wallets);
    bool setTrustedPeerActive(const QString &onion, bool active);
    bool removeTrustedPeer(const QString &onion);

    Q_INVOKABLE bool resetTrustedPeerWalletCount(const QString &onion);
    Q_INVOKABLE bool incrementTrustedPeerWalletCount(const QString &onion);

    bool updateSettings(bool inspectGuard, const QString &daemonUrl, int daemonPort, bool useTorForDaemon, bool torAutoConnect, int  lockTimeoutMinutes);
    bool setPlaceholderOnlineByLabel(const QString &label, bool online);

    Q_INVOKABLE bool importTorIdentity(const QString &label,
                                       const QString &onion,
                                       const QString &privateKey,
                                       bool online);


    Q_INVOKABLE QVariantList getDaemonAddressBook() const;
    Q_INVOKABLE bool addDaemonAddressBookEntry(const QString &label,
                                               const QString &url,
                                               int port);
    Q_INVOKABLE bool removeDaemonAddressBookEntry(const QString &url,
                                                  int port);

signals:

    void loginSuccess(const QString &accountName);
    void loginFailed (const QString &error);
    void accountCreated(const QString &accountName);
    void logoutOccurred();
    void isAuthenticatedChanged();
    void currentAccountChanged();
    void passwordUpdated(bool success);


    void settingsChanged();
    void addressBookChanged();
    void addressEntryAdded   (const QString &label, const QString &onion);
    void addressEntryRemoved (const QString &onion);
    void addressXMRBookChanged();
    void addressXMREntryAdded(const QString &label, const QString &xmrAddress);
    void addressXMREntryRemoved(const QString &xmrAddress);
    void trustedPeersChanged();
    void trustedPeerAdded   (const QString &onion, const QString &label);
    void trustedPeerRemoved (const QString &onion);
    void trustedPeerUpdated (const QString &onion, const QString &label);
    void errorOccurred(const QString &error);
    void addressEntryUpdated(const QString &oldOnion,
                             const QString &newOnion,
                             const QString &label);


    void addressDaemonBookChanged();
    void addressDaemonEntryAdded(const QString &label, const QString &url, int port);
    void addressDaemonEntryRemoved(const QString &url, int port);


    void torIdentitiesChanged();

private:

    bool passwordIsCorrect(const QString &password) const;
    bool isOnionAddress(const QString &url) const;
    bool validateDaemonUrl(const QString &url) const;
    void loadSettingsFromJson(const QJsonObject &obj);
    QVariantMap sanitizeTrustedPeers(const QVariantMap &raw) const;
    bool persistUnlocked();
    void resetState();
    QString prettyJson(const QJsonObject &obj) const;


    QString pickCurrentOnionFrom(const QJsonArray &arr) const;
    void    recomputeCurrentOnionLocked();


    QJsonObject     m_accountData;
    QByteArray      m_key;
    QByteArray      m_salt;
    bool            m_isAuthenticated = false;
    bool            m_hasLoggedOut    = false;
    QString         m_currentAccount;
    QString         m_currentFilePath;
    QString         m_currentAccountOnion;


    bool    m_inspectGuard = true;
    QString m_daemonUrl    = QStringLiteral("127.0.0.1");
    int     m_daemonPort   = 18081;
    bool    m_useTorForDaemon = false;
    bool    m_torDaemon       = false;
    bool    m_torAutoconnect  = false;
    int     m_lockTimeoutMinutes = 30;

    QVariantMap m_trustedPeers;



    std::unique_ptr<QLockFile> m_lock;
    mutable QMutex             m_mutex;
};

#endif // ACCOUNTMANAGER_H
