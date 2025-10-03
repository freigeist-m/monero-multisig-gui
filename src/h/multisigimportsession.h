#pragma once

#include <QObject>
#include <QTimer>
#include <QThreadPool>
#include <QHash>
#include <QJsonObject>
#include <QDateTime>
#include <QStringList>
#include <QMetaType>

class MultiWalletController;
class TorBackend;

class MultisigImportSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit MultisigImportSession(QObject *parent=nullptr);
    ~MultisigImportSession() override;

    void initialize(MultiWalletController *wm, TorBackend *tor);

    Q_INVOKABLE bool running() const { return m_running; }


    Q_INVOKABLE QString getImportActivity() const;
    Q_INVOKABLE QString getPeerActivity() const;
    Q_INVOKABLE int     getActiveWalletCount() const;
    Q_INVOKABLE QString getWalletStatus(const QString &walletName) const;
    Q_INVOKABLE void    clearActivity();


    void        httpGetAsync(const QString &onion, const QString &path, bool signedFlag ,const QString &walletName);
    QJsonObject httpGetBlocking(const QString &onion, const QString &path, bool signedFlag ,const QString &walletName);

public slots:
    void start();
    void stop();

signals:
    void runningChanged();
    void statusChanged(QString);
    void sessionStarted();
    void sessionStopped();
    void walletImportCompleted(QString walletName);
    void peerInfoReceived(QString walletName, QString peerOnion);



    void _httpResult(QString onion, QString path, QJsonObject res, QString err, QString walletName);

private slots:
    void _checkAllWallets();
    void onHttp(QString onion, QString path, QJsonObject res, QString err, QString walletName);
    void _onImportMultisigResult(const QString &walletName, int actualImported = 0);

private:

    bool _isReady() const;
    void _loadSigningKeys();


    void _processWalletImport(const QString &walletName);
    bool _walletNeedsImport(const QString &walletName) const;


    QString     _accountCacheDir() const;
    QJsonObject _loadCachedInfos(const QString &walletName) const;
    void        _saveCachedInfo(const QString &walletName,
                         const QString &peerOnion,
                         const QByteArray &infoRaw);
    void        _markPeersAsImported(const QString &walletName,
                              const QStringList &peers);
    QStringList _filterStalePeers(const QStringList &peers,
                                  const QJsonObject &cached) const;

    void        _attemptImport(const QString &walletName, const QJsonObject &cached);


    struct KeyMat { QByteArray scalar, prefix, pub; };
    QHash<QString, KeyMat> m_keysByOnion;
    QStringList            m_allOnions;


    QByteArray  _signPayload(const QJsonObject &msg, const KeyMat &km) const;
    QByteArray  _pubKey() const;



    struct PendingImportEntry {
        QString peer;
         QByteArray  infoRaw;
    };


    void _recordImportAttempt(const QString &walletName, const QStringList &peers);
    void _recordPeerInfoReceived(const QString &walletName, const QString &peerOnion);
    void _recordImportCompleted(const QString &walletName, const QList<PendingImportEntry> &entries, int actualImported = 0);


private:
    MultiWalletController *m_wm  {nullptr};
    TorBackend            *m_tor {nullptr};

    bool   m_running = false;
    qint64 m_cacheExpirySecs = 120;

    QTimer       m_checkTimer;
    QThreadPool  m_httpPool;


    QByteArray m_scalar, m_prefix, m_pub;
    QString    m_ownOnion;

    QHash<QString, QMetaObject::Connection> m_importConnections;





    QHash<QString, QList<PendingImportEntry>> m_pendingImports;


    QString  _resolveOwnOnionForWallet(const QString &walletName) const;
    bool     _resolveKeysForWallet(const QString &walletName, KeyMat *outKey, QString *outOnion = nullptr) const;


    QHash<QString, QJsonObject> m_importActivity;

    QList<QJsonObject>          m_peerActivity;

    int      m_inFlight{0};
    bool     m_stopRequested = false;
    bool     m_stoppedSignaled = false;


};
