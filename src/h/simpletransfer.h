#pragma once
#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>
#include <QStringList>

class MultiWalletController;
class AccountManager;
class Wallet;

class SimpleTransfer : public QObject
{
    Q_OBJECT
public:
    enum class Stage {
        INIT = 0,
        CREATING_TRANSFER,
        VALIDATING,
        APPROVING,
        SUBMITTING,
        CHECKING_STATUS,
        COMPLETE,
        ERROR
    };
    Q_ENUM(Stage)

    struct Destination {
        QString address;
        quint64 amount;
    };

    explicit SimpleTransfer(MultiWalletController *wm,
                            AccountManager        *acct,
                            const QString         &transferRef,
                            const QString         &walletRef,
                            const QList<Destination> &destinationsAtomic,
                            int                    feePriority,
                            const QList<int>      &feeSplitIndices,
                            bool                   inspectBeforeSending,
                            QObject               *parent = nullptr);
    ~SimpleTransfer() override;

    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void proceedAfterApproval();

    Q_INVOKABLE QString getTransferDetailsJson() const;

signals:
    void stageChanged(QString stageName);
    void statusChanged(QString text);
    void finished(QString transferRef, QString result);
    void submittedSuccessfully(QString transferRef);

private slots:
    void onPrepared(QString ref, quint64 feeAtomic, QVariantList normalizedDests, QString warning);
    void onDescribe(QString ref, QVariant details);
    void onCommit(QString ref, bool ok, QString txidsOrError);

private:

    void setStage(Stage s, const QString &msg = {});
    void stop(const QString &reason);
    void saveToAccount();
    QString stageName(Stage s) const;


    Wallet*  walletByRef(const QString &ref) const;
    QString  walletNameForRef(const QString &ref) const;
    QString  resolveWalletName(const QString &walletRef) const;


    static QVariantList destinationsToVariant(const QList<Destination> &d);

private:
    MultiWalletController *m_wm{nullptr};
    AccountManager        *m_acct{nullptr};

    QString m_transferRef;
    QString m_walletRef;
    QString m_walletName;

    QList<Destination> m_destinationsInit;
    QList<Destination> m_destinations;
    int                m_feePriority{0};
    QList<int>         m_feeSplit;
    bool               m_inspect{true};

    Stage              m_stage{Stage::INIT};
    bool               m_stop{false};


    quint64            m_feeAtomic{0};
    QString            m_txId{"pending"};
    QString            m_paymentId;
    QJsonObject        m_transferDescription;
};
