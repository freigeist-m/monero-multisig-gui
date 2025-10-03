#pragma once


#include <QObject>
#include <QHash>
#include <QTimer>
#include <QJsonObject>
#include <QThreadPool>
#include <QStringList>

class MultiWalletController;
class TorBackend;
class AccountManager;
class Wallet;

class IncomingTransfer : public QObject
{
    Q_OBJECT
public:
    enum class Stage {
        START = 0,
        VALIDATING,
        SIGNING,
        SUBMITTING,
        BROADCASTING,
        CHECKING_STATUS,
        COMPLETE,
        DECLINED,
        ERROR
    };
    Q_ENUM(Stage)

    explicit IncomingTransfer(MultiWalletController *wm,
                              TorBackend            *tor,
                              AccountManager        *acct,
                              const QString&         walletRef,
                              const QString&         transferRef,
                              const QString&         myOnion,
                              QObject               *parent=nullptr);
    ~IncomingTransfer() override;

    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString getTransferDetailsJson() const;
    Q_INVOKABLE QString myOnion() const { return m_myOnion; }

signals:
    void stageChanged(QString stageName);
    void statusChanged(QString statusText);
    void peerStatusChanged();
    void submittedSuccessfully(QString transferRef);
    void finished(QString transferRef, QString result);

private slots:
    void validate();
    void onDescribeTransfer(QString walletName, QVariant detailsVar, QString operation_caller);

    void sign();
    void onSigned(QByteArray newBlob,
                  bool ,
                  QStringList ,
                  QString operation_caller);

    void maybeSubmit();
    void onHttpResult(QString onion, QString path, QJsonObject res, QString err);

    void retryRound();


    void onSubmitResult(bool ok, QString result, QString operation_caller);
    void onErrorOccurred(QString error, QString operation_caller = "");
    void decline();

private:

    void        saveToAccount();
    void        setStage(Stage s, const QString &msg={});
    void        stop(const QString &reason);
    QString     stageName(Stage s) const;
    QString     myOnionFQDN() const;

    Wallet*     walletByRef(const QString &ref ) const;
    QString     walletNameForRef(const QString &ref) const;
    QStringList walletPeersByRef(const QString &ref) const;


    void        submitToNextPeer();
    void        broadcastSelf();


    void        httpGetAsync (const QString &onion,const QString &path,bool signedFlag);
    void        httpPostAsync(const QString &onion,const QString &path,
                       const QJsonObject &json,bool signedFlag);
    QJsonObject httpRequestBlocking(const QString &onion,const QString &path,
                                    const QByteArray &method,bool signedFlag,
                                    const QJsonObject &json={});
    QByteArray  signPayload(const QJsonObject &payload) const;

private:
    MultiWalletController *m_wm {nullptr};
    TorBackend            *m_tor{nullptr};
    AccountManager        *m_acct{nullptr};

    QString m_walletRef;
    QString m_walletName;
    QString m_transferRef;

    Stage   m_stage {Stage::START};
    bool    m_stopFlag{false};


    QStringList  m_signingOrder;
    QStringList  m_signatures;
    QString      m_transferBlobB64;
    QJsonObject  m_description;
    QString      m_txId;
    QString      m_status;
    QString      m_myOnion;


    QTimer      m_retry;
    QThreadPool m_httpPool;

    QHash<QString,int> m_submitAttempts;


    QByteArray m_scalar, m_prefix, m_pubKey;
};
