#ifndef MULTISIGSESSION_H
#define MULTISIGSESSION_H

#include <QObject>
#include <QHash>
#include <QTimer>
#include <QThreadPool>
#include <QSet>
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QVariant>

class MultiWalletController;
class TorBackend;
class Wallet;

class MultisigSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString  referenceCode READ referenceCode CONSTANT)
    Q_PROPERTY(QString  walletName    READ walletName    CONSTANT)
    Q_PROPERTY(QString  stage         READ stageNameQml  NOTIFY stageChanged)
    Q_PROPERTY(int m READ m CONSTANT)
    Q_PROPERTY(int n READ n CONSTANT)
    Q_PROPERTY(QVariant peerList READ peerList NOTIFY peerStatusChanged)

public:
    enum class Stage { INIT, WAIT_PEERS, KEX, ACK, PENDING, COMPLETE, ERROR };
    Q_ENUM(Stage)

    static QString stageName(Stage s);

    // HttpTask uses these:
    void        httpGetAsync(const QString &onion, const QString &path, bool signedFlag);
    QJsonObject httpGetBlocking(const QString &onion, const QString &path, bool signedFlag);

    bool isPeer(const QString &onion) const;

    explicit MultisigSession(MultiWalletController *wm,
                             TorBackend            *tor,
                             const QString         &reference,
                             int                    m,
                             int                    n,
                             const QStringList     &peerOnions,
                             const QString         &walletName,
                             const QString         &walletPassword,
                             const QString         &myOnion,
                             const QString         &creator,
                             const QString         &nettype,
                             QObject               *parent = nullptr);
    ~MultisigSession() override;

    Q_INVOKABLE QString walletName() const { return m_walletName; }
    Q_INVOKABLE QString walletPassword() const { return m_walletPassword; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();

    Q_INVOKABLE Stage   currentStage() const { return m_stage; }
    Q_INVOKABLE QString referenceCode() const { return m_ref; }

    Q_INVOKABLE int m() const { return m_m; }
    Q_INVOKABLE int n() const { return m_n; }
    Q_INVOKABLE QString  net_type() const { return m_nettype; }

    Q_INVOKABLE QByteArray blobForStage(const QString &stage, int round) const;
    Q_INVOKABLE void onFirstKexMsg(QByteArray blob);
    Q_INVOKABLE void registerPeerPendingConfirmation(const QString &onion);
    Q_INVOKABLE QString myOnion() const { return m_myOnion; }

signals:
    void stageChanged(QString stageName, QString myOnion, QString ref);
    void peerStatusChanged(QString myOnion, QString ref);
    void walletAddressChanged(QString address, QString myOnion, QString ref);
    void finished(QString myOnion, QString ref, QString reason);

    void _httpResult(QString onion, QString path, QJsonObject res, QString err);

private:
    QString stageNameQml() const { return stageName(m_stage); }
    QJsonObject httpGetDaemon(const QString &url, bool useTorProxy = true, int timeoutMs = 5000);


    void pingRound();
    void retryRound();


    void onHttp(QString onion, QString path, QJsonObject res, QString err);

    bool allPeersOnline() const;
    bool allKex(int round) const;
    bool allAck()  const;
    bool allPending() const;
    void checkStageCompletion();
    void advanceHandshake();


    void createWallet();
    void runMakeMultisig_Round1();
    void runExchange_WithPassword();


    void probeReadiness();
    void runAckPhase();
    void onAddressReady(QString addr);
    void onSeedReady(QString seed);


    void onWalletCreated();
    void onMakeMultisigDone(QByteArray next);
    void onExchangeMultisigKeysDone(QByteArray next);

    void onIsMultisigReady(bool ready);
    bool validateAckObject(const QJsonObject &obj, QString *whyNot = nullptr) const;


    void transitionToPending();
    void finalize();

    bool beginOp(const char *op, int round, const QList<QByteArray>  &infos);
    void endOp(const char *op, int round);

    QSet<QString>              m_inflightOps;
    QSet<QString>              m_oncePerRoundOps;
    QHash<QString, QByteArray> m_lastOpKey;

    QByteArray sign(const QJsonObject &payload) const;
    void stop(const QString &reason);


    MultiWalletController          *m_wm   {nullptr};
    TorBackend                     *m_tor  {nullptr};

    QString      m_ref;
    int          m_m {0};
    int          m_n {0};
    QString      m_walletName;
    QString      m_walletPassword;

    QByteArray   m_scalar;
    QByteArray   m_prefix;
    QByteArray   m_pubKey;

    Stage        m_stage {Stage::INIT};
    bool         m_stopFlag {false};

    QTimer       m_ping;
    QTimer       m_retry;
    QThreadPool  m_httpPool;

    int          m_inFlight{0};
    bool         m_finishedSignaled{false};
    QString      m_pendingFinishReason;

    QString      m_address;
    QString      m_seed;

    QString     m_myOnion;
    QString     m_creator;
    QString     m_nettype;

    struct PeerState {
        QString    onion;
        bool       online            {false};
        qint64     lastSeen          {0};
        QMap<int,QByteArray> kex;
        QByteArray ack;
        bool       pendingComplete   {false};
        QJsonObject details;
        bool       detailsMatch      {false};
    };
    QHash<QString, PeerState> m_peers;

    QMap<int,QByteArray> m_kex;
    QByteArray           m_ack, m_pending;

    int m_currentRound {0};

    quint64 get_chain_height_robust();



public:
    Q_INVOKABLE QVariant peerList() const;
};

#endif
