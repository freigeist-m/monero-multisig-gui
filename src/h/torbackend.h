#ifndef TORBACKEND_H
#define TORBACKEND_H
#include "torinstaller.h"

#include <QObject>
#include <QProcess>
#include <QTcpSocket>
#include <QDir>
#include <QLockFile>
#include <QTemporaryDir>
#include <QHash>
#include <QVector>


class AccountManager;
class MultiWalletController;
class MultisigManager;
class MultisigApiRouter;

class TorBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool    running       READ isRunning     NOTIFY runningChanged)
    Q_PROPERTY(bool    online        READ isRunning     NOTIFY runningChanged)
    Q_PROPERTY(QString onionAddress  READ onionAddress  NOTIFY onionAddressChanged)
    Q_PROPERTY(int socksPort   READ socksPort   CONSTANT)
    Q_PROPERTY(int controlPort READ controlPort CONSTANT)
    Q_PROPERTY(QStringList onionAddresses READ onionAddresses NOTIFY onionAddressesChanged)


public:
    explicit TorBackend(AccountManager *acctMgr, QObject *parent = nullptr);

    Q_INVOKABLE void start(bool forceDownload =  false);
    Q_INVOKABLE void stop();

    Q_INVOKABLE void startHiddenService() { start(); }
    Q_INVOKABLE void stopTorServer()      { stop();  }

    Q_INVOKABLE bool addNewService(const QString &label);
    Q_INVOKABLE bool setServiceOnline(const QString &onion, bool online);
    Q_INVOKABLE bool removeService(const QString &onion);
    Q_INVOKABLE void ensureDefaultService();
    Q_INVOKABLE void startIfAutoconnect();

    void setRouterDeps(MultiWalletController *wm, MultisigManager *msig);

    Q_PROPERTY(int bootstrapProgress READ bootstrapProgress NOTIFY bootstrapProgressChanged)
    Q_PROPERTY(QString currentStatus READ currentStatus NOTIFY currentStatusChanged)
    Q_PROPERTY(bool initializing READ isInitializing NOTIFY initializingChanged)
    Q_PROPERTY(bool installing READ isInstalling NOTIFY installingChanged)
    Q_PROPERTY(QVariantMap requestCounts READ requestCounts NOTIFY requestCountsChanged)
    Q_PROPERTY(QString downloadErrorCode READ downloadErrorCode NOTIFY downloadErrorCodeChanged)
    Q_PROPERTY(QString downloadErrorMsg READ downloadErrorMsg NOTIFY downloadErrorMsgChanged)


    Q_INVOKABLE QVariantMap requestCounts() const;
    Q_INVOKABLE void resetRequestCounts();
    Q_INVOKABLE void resetRequestCount(const QString &onion);
    Q_INVOKABLE void reset();



    bool    isRunning()    const { return m_running; }
    QString onionAddress() const { return m_onionAddress; }

    int     socksPort()    const { return m_socksPort; }
    int     controlPort()  const { return m_controlPort; }

    QStringList onionAddresses() const;

    std::unique_ptr<QTemporaryDir> m_tmpDir;

    int bootstrapProgress() const { return m_bootstrapProgress; }
    QString currentStatus() const { return m_currentStatus; }
    QString downloadErrorCode() const { return m_downloadErrorCode; }
    QString downloadErrorMsg() const { return m_downloadErrorMsg; }
    bool isInitializing() const { return m_initializing; }
    bool isInstalling() const { return m_installing; }

signals:
    void runningChanged();
    void onionAddressChanged(const QString &onion);
    void statusTorChanged(const QString &line);
    void started();
    void stopped();
    void logMessage(const QString &line);
    void errorOccurred(const QString &err);
    void onionAddressesChanged();

    void bootstrapProgressChanged();
    void currentStatusChanged();
    void downloadErrorCodeChanged();
    void downloadErrorMsgChanged();
    void initializingChanged();
    void installingChanged();

    void requestCountsChanged();
    void requestCountChanged(const QString &onion, quint64 count);


private slots:

    void onStdOut();
    void onStdErr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);


    void onCtlConnected();
    void onCtlReadyRead();



private:
    QString locateTorBinary() const;
    void launchTorWithBinary(const QString &torBin);
    void    writeTorrc(const QString &torrcPath,
                    const QString &hsDir,
                    const QString &controlCookieFile) const;
    void    initiateControlSession();
    void    sendAddOnionCmd();
    void    applyNewOnion(const QString &serviceId, const QString &privKeyBase64, const QString &preferredLabel);

    QStringList m_pendingNewLabels;
    bool        m_blockHadPrivateKey = false;

    AccountManager *m_acct = nullptr;
    QProcess        m_proc;
    QTcpSocket      m_ctl;
    QByteArray      m_ctlBuffer;
    quint16         m_socksPort   = 0;
    quint16         m_controlPort = 0;

    QString m_dataDir;
    QString m_onionAddress;
    QString m_privateKey;

    bool m_running          = false;
    bool m_bootstrapped100  = false;
    bool m_addOnionIssued   = false;

    int m_bootstrapProgress = 0;
    QString m_currentStatus;
    QString m_downloadErrorCode;
    QString m_downloadErrorMsg;
    bool m_initializing = false;
    TorInstaller    m_torInstaller;
    QThread* m_installThread = nullptr;
    bool     m_installing = false;
    bool m_stopping = false;



    struct LocalService {
        QString            onion;
        QString            label;
        quint16            port = 0;
        MultisigApiRouter *router = nullptr;
        bool               online = false;
    };

    QHash<QString, quint64> m_requestCounts;


    LocalService  createRouterForKnownOnion(const QString &onion);
    LocalService  createRouterForNewLabel(const QString &label);
    LocalService* ensureServiceForOnion(const QString &onion);
    void          stopAndDeleteService(const QString &onion);


    QHash<QString, LocalService> m_services;
    QVector<LocalService>        m_pendingNew;


    MultiWalletController *m_walletMgr = nullptr;
    MultisigManager       *m_msigMgr   = nullptr;

};

#endif


