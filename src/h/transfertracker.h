#pragma once
#include <QObject>
#include <QVariantMap>
#include <QJsonObject>
#include <QByteArray>
#include <QHash>
#include <QStringList>

class TorBackend;
class AccountManager;

class TransferTracker : public QObject {
    Q_OBJECT
public:
    explicit TransferTracker(const QString &walletRef,
                             const QString &transferRef,
                             const QString &walletName,
                             TorBackend    *tor,
                             AccountManager* acct,
                             const QString &myOnion,
                             QObject       *parent=nullptr);

    QString walletRef()   const { return m_walletRef; }
    QString transferRef() const { return m_transferRef; }
    QString walletName()  const { return m_walletName; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setBackoffMs(int ms);
    Q_INVOKABLE QString myOnion() const { return m_myOnion; }

signals:
    void progress(QString transferRef, QVariantMap status);
    void finished(QString transferRef, QString result);

private:

    void        tick();
    bool        loadOnceFromAccount();


    QJsonObject httpGetBlockingSigned(const QString &onion, const QString &path);
    QByteArray  sign(const QJsonObject &payload) const;


    bool        persistPeerStageIfChanged(const QString &onion,
                                   const QString &stage,
                                   const QString &txid,
                                   qint64 time);
    bool        persistAggregateIfChanged(const QString &stageCandidate,
                                   const QString &txidCandidate,
                                   qint64 timeCandidate);
    bool        writeBack();

    bool        persistPeerInfoIfChanged(const QString &onion,
                                  const QString &stage,
                                  bool receivedTransfer,
                                  bool hasSigned,
                                  const QString &status);
    bool        ensureSignatureListed(const QString &onion);



    static      int stageRank(const QString &s);
    bool        isTerminal(const QString &stage) const;
    QString     lastBestStageFromAccount() const;

private:

    QString         m_walletRef, m_transferRef, m_walletName;


    TorBackend     *m_tor  = nullptr;
    AccountManager *m_acct = nullptr;


    QByteArray      m_scalar, m_prefix, m_pubKey;


    bool            m_running   = false;
    int             m_backoffMs = 2000;


    bool            m_loaded = false;
    QJsonObject     m_root;
    int             m_walletIdx = -1;
    QStringList     m_peersToPoll;
    QString         m_myOnion;


    QHash<QString, QVariantMap> m_peerStageCache;

    QHash<QString, QVariantList> m_peerInfoCache;


    QString         m_lastAggregateStage;
    QString         m_lastTxId;
    qint64          m_lastTime = 0;

    int             m_inFlight = 0;
    bool            m_cancelRequested = false;
    bool            m_finishedSignaled = false;
    QString         m_pendingFinishResult;

};
