#pragma once

#include <QObject>
#include <QHash>
#include <QVariantMap>
#include <QStringList>
#include <QTimer>
#include <QJsonObject>
#include <QThreadPool>

class MultiWalletController;
class TorBackend;
class AccountManager;
class Wallet;

class TransferInitiator : public QObject
{
    Q_OBJECT
public:
    enum class Stage {
        INIT = 0,
        MULTISIG_IMPORT_CHECK,
        CHECKING_PEERS,
        COLLECTING_INFO,
        CREATING_TRANSFER,
        VALIDATING,
        APPROVING,
        SUBMITTING,
        CHECKING_STATUS,
        COMPLETE,
        ERROR,
        DECLINED
    };
    Q_ENUM(Stage)

    struct Destination {
        QString address;
        quint64 amount;
    };

    struct PeerState {
        QString onion;
        bool    online{false};
        bool    ready{false};
        bool    receivedTransfer{false};
        bool    hasSigned{false};
        QString stageName;
        QByteArray  multisigInfo;
        qint64  multisigInfoTs{0};
        qint64  lastSeen{0};
        QString status{""};
    };

public:
    explicit TransferInitiator(MultiWalletController *wm,
                               TorBackend            *tor,
                               const QString         &transferRef,
                               const QString         &walletRef,
                               const QList<Destination> &destinationsAtomic,
                               const QStringList     &peersOnionLower,
                               const QStringList     &signingOrderLower,
                               int                    threshold,
                               int                    feePriority,
                               const QList<int>      &feeSplitIndices,
                               bool                   inspectBeforeSending,
                               const QString          &myOnion,
                               QObject               *parent=nullptr);
    ~TransferInitiator() override;

    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void proceedAfterApproval();

    Q_INVOKABLE QString getTransferDetailsJson() const;

signals:
    void stageChanged(QString stageName);
    void statusChanged(QString statusText);
    void peerStatusChanged();
    void finished(QString transferRef, QString result);
    void _httpResult(QString onion, QString path, QJsonObject result, QString error);
    void submittedSuccessfully(QString transferRef);


private slots:
    void pingRound();
    void retryRound();

    void createOwnMultisigInfo();
    void onMultisigInfoPrepared(QByteArray info, QString operation_caller);
    void importAllMultisigInfos();
    void onMultisigInfosImported(int nImported, bool needsRescan,QString operation_caller);

    void createMultisigTransfer();
    void onUnsignedMultisigReady(QByteArray txset,
                                 quint64 feeAtomic,
                                 QVariantList normalizedDestinations,
                                 QString warningOrEmpty ,QString operation_caller);

    void onErrorOccurred(QString error, QString operation_caller = "");

    void describeTransfer();
    void onDescribeTransferResultVariant(QString walletName, QVariant detailsVar,QString operation_caller);
    void onDescribeTransferResultParsed(QString walletName, QJsonObject details);

    void submitToNextPeer();
    void checkPeerStatus();

private:
    void saveToAccount();
    void setStage(Stage s, const QString &statusMsg = {});
    void stop(const QString &reason);

    void httpGetAsync(const QString &onion, const QString &path, bool signedFlag);
    void httpPostAsync(const QString &onion, const QString &path, const QJsonObject &json, bool signedFlag);
    QJsonObject httpRequestBlocking(const QString &onion,
                                    const QString &path,
                                    const QByteArray &method,
                                    bool signedFlag,
                                    const QJsonObject &json = {});

    QByteArray signPayload(const QJsonObject &payload) const;
    QByteArray pubKey() const;
    QString   myOnionFQDN() const;

    Wallet*  walletByRef(const QString &ref) const;
    QString  walletNameForRef(const QString &ref) const;
    QString  walletAddressForRef(const QString &ref) const;
    QStringList walletPeersByRef(const QString &ref) const;

    void checkPeersOnline();
    void pollMultisigInfo();

private:
    MultiWalletController *m_wm{nullptr};
    TorBackend            *m_tor{nullptr};
    AccountManager        *m_acct{nullptr};

    QString m_transferRef;
    QString m_walletRef;
    QString m_walletName;
    QString m_walletMainAddr;

    int m_threshold{0};
    int m_feePriority{0};
    QList<int> m_feeSplit;
    bool m_inspectBeforeSend{true};

    Stage m_stage{Stage::INIT};
    bool  m_stopFlag{false};

    QList<Destination> m_destinations;
    QList<Destination> m_destinationsInit;

    QHash<QString, PeerState> m_peers;
    QStringList               m_signingOrder;
    QStringList               m_signatures;

    QTimer m_ping;
    QTimer m_retry;

    QThreadPool m_httpPool;

    QString              m_transferBlob;
    quint64              m_feeAtomic{0};
    QString              m_feeStr;
    QString              m_paymentId;
    QString              m_txId;
    QJsonObject          m_transferDescription;
    QHash<QString, int>  m_submitAttempts;
    qint64               m_submittedAt{0};
    QString              m_status;
    QString              m_unlock_time;
    qint64               m_created_at{0};
    QString              m_myOnionSelected;


    QByteArray m_scalar, m_prefix, m_pubKey;
};
