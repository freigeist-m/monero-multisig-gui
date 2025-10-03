#pragma once

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include <QJsonObject>

class MultiWalletController;
class TorBackend;
class AccountManager;

class TransferInitiator;
class TransferTracker;
class IncomingTransfer;
class MultisigImportSession;
class SimpleTransfer;

class TransferManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QStringList outgoingSessions   READ outgoingSessions   NOTIFY currentSessionChanged)
    Q_PROPERTY(QStringList incomingSessions   READ incomingSessions   NOTIFY currentSessionChanged)
    Q_PROPERTY(QStringList activeTrackers     READ activeTrackers     NOTIFY currentSessionChanged)
    Q_PROPERTY(QStringList pendingIncomingTransfers READ pendingIncomingTransfers NOTIFY currentSessionChanged)
    Q_PROPERTY(QStringList allSavedTransfers  READ allSavedTransfers  NOTIFY currentSessionChanged)
    Q_PROPERTY(QVariantList openTransfersModel READ openTransfersModel NOTIFY currentSessionChanged)
    Q_PROPERTY(QStringList simpleSessions READ simpleSessions NOTIFY currentSessionChanged)



public:
    explicit TransferManager(MultiWalletController *wm,
                             TorBackend            *tor,
                             QObject               *parent=nullptr);
    ~TransferManager() override;

    // ──────────────────────────────────────────────────  Outgoing
    Q_INVOKABLE QString startOutgoingTransfer(const QString &walletRef,
                                              const QVariantList &destinations,
                                              const QStringList &peers,
                                              const QStringList &signingOrder,
                                              int  threshold,
                                              int  feePriority,
                                              const QVariantList &feeSplitVar,
                                              bool inspectBeforeSending,
                                              const QString &myOnion);

    Q_INVOKABLE bool abortOutgoingTransfer   (const QString &transferRef);
    Q_INVOKABLE bool validateOutgoingTransfer(const QString &transferRef);
    Q_INVOKABLE bool proceedAfterApproval    (const QString &transferRef);

    Q_INVOKABLE QObject* getOutgoingSession  (const QString &transferRef) const;
    Q_INVOKABLE QString  getOutgoingDetails  (const QString &transferRef) const;

    // ──────────────────────────────────────────────────  Incoming
    Q_INVOKABLE QString saveIncomingTransfer(const QString &walletRef,
                                             const QString &transferRef,
                                             const QString &transferBlob,
                                             const QStringList &signingOrder,
                                             const QStringList &whoHasSigned,
                                             const QVariantMap &jsonBody);

    Q_INVOKABLE QString startIncomingTransfer(const QString &transferRef);
    Q_INVOKABLE QObject* getIncomingSession  (const QString &transferRef) const;


    Q_INVOKABLE bool    resumeTracker   (const QString &transferRef);
    Q_INVOKABLE QString getSavedTransferDetails(const QString &transferRef) const;


    Q_INVOKABLE void    restoreAllSaved();
    Q_INVOKABLE bool    deleteSavedTransfer(const QString &transferRef);

    Q_INVOKABLE bool    isMultisigImportSessionRunning() const;
    Q_INVOKABLE void    startMultisigImportSession();
    Q_INVOKABLE void    stopMultisigImportSession();
    Q_INVOKABLE QString getMultisigImportActivity() const;
    Q_INVOKABLE QString getMultisigImportPeerActivity() const;
    Q_INVOKABLE int     getMultisigImportActiveWalletCount() const;
    Q_INVOKABLE QString getMultisigImportWalletStatus(const QString &walletName) const;
    Q_INVOKABLE void    clearMultisigImportActivity();



    Q_INVOKABLE QString startSimpleTransfer(const QString &walletRef,
                                            const QVariantList &destinations,
                                            int  feePriority,
                                            const QVariantList &feeSplitVar,
                                            bool inspectBeforeSending);

    Q_INVOKABLE bool    abortSimpleTransfer(const QString &transferRef);
    Q_INVOKABLE bool    validateSimpleTransfer(const QString &transferRef);
    Q_INVOKABLE bool    proceedSimpleAfterApproval(const QString &transferRef);
    Q_INVOKABLE QObject* getSimpleSession(const QString &transferRef) const;
    Q_INVOKABLE QString  getSimpleDetails(const QString &transferRef) const;

    Q_INVOKABLE bool stopTracker(const QString &transferRef);
    Q_INVOKABLE QObject* getTrackerSession(const QString &transferRef) const;

    Q_INVOKABLE bool declineIncomingTransfer(const QString &transferRef);
    Q_INVOKABLE bool hasLiveSession(const QString &transferRef) const {
        return m_outgoing.contains(transferRef) ||
               m_incoming.contains(transferRef) ||
               m_trackers.contains(transferRef) ||
               m_simple.contains(transferRef);
    }

    Q_INVOKABLE void reset();

    QStringList outgoingSessions()          const;
    QStringList incomingSessions()          const;
    QStringList activeTrackers()            const;
    QStringList pendingIncomingTransfers()  const;
    QStringList allSavedTransfers()         const;
    QStringList simpleSessions()            const;

    Q_INVOKABLE QVariantList openTransfersModel() const;
    Q_INVOKABLE QVariantMap  getTransferSummary(const QString &transferRef) const;

signals:
    void sessionStarted (QString transferRef);
    void sessionFinished(QString transferRef, QString result);
    void currentSessionChanged();
    void session_finished(QString transferRef, QString result);

    void multisigImportSessionStarted();
    void multisigImportSessionStopped();
    void multisigImportWalletCompleted(QString walletName);
    void multisigImportPeerInfoReceived(QString walletName, QString peerOnion);

private:

    void    wireTracker  (TransferTracker *trk);
    QString makeTransferRef(const QString &walletRef, const QString &myOnion) const;
    static QString normOnion(QString s);


    QJsonObject loadAccountRoot() const;
    void        saveAccountRoot(const QJsonObject &root) const;

    bool upsertTransfer(const QString &walletRef,
                        const QString &walletName,
                        const QString &transferRef,
                        const QJsonObject &entry);
    bool persistOutgoingPrepared (const QVariantMap &snapshot);
    bool persistOutgoingSubmitted(const QVariantMap &snapshot);


    void restorePendingIncoming();
    void restoreTrackers();
    bool resumeTrackerInternal(const QString &transferRef,
                               QString *outWalletRef,
                               QString *outWalletName,
                               QString *outMyOnion);

    static bool isTerminalStage(const QString &stage);


    MultiWalletController *m_wm  {nullptr};
    TorBackend            *m_tor {nullptr};
    AccountManager        *m_acct{nullptr};

    QHash<QString, TransferInitiator*> m_outgoing;
    QHash<QString, IncomingTransfer*>   m_incoming;
    QHash<QString, TransferTracker*>    m_trackers;
    QHash<QString, SimpleTransfer*>     m_simple;

    QHash<QString, QVariantMap> m_pendingIncomingMap;
    QHash<QString, QVariantMap> m_allSavedMap;

    MultisigImportSession *m_msigImport {nullptr};
    void setupMultisigImportWiring();
    void maybeStartMultisigImport();
};
