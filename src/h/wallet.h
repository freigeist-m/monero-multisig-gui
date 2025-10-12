#pragma once
#include "win_compat.h"
#include "wallet/wallet2.h"
#ifdef QT_TRANSLATE_NOOP
#undef QT_TRANSLATE_NOOP
#endif

#include <QObject>
#include <QTimer>
#include <QQueue>
#include <QMutex>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <memory>
#include <functional>
#include <cstdint>

namespace tools      { class wallet2; }
namespace cryptonote { enum network_type : std::uint8_t; }

class Wallet : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString  address          READ address          NOTIFY walletChanged)
    Q_PROPERTY(quint64  balance          READ balance          NOTIFY walletChanged)
    Q_PROPERTY(quint64  unlockedBalance  READ unlockedBalance  NOTIFY walletChanged)
    Q_PROPERTY(QString  daemonAddress    READ daemonAddress    WRITE setDaemonAddress NOTIFY walletChanged)
    Q_PROPERTY(bool     busy             READ busy             NOTIFY busyChanged)
    Q_PROPERTY(QVariantList pendingOps   READ pendingOps       NOTIFY queueChanged)
    Q_PROPERTY(bool activeSyncTimer READ activeSyncTimer NOTIFY walletChanged)
    Q_PROPERTY(bool has_multisig_partial_key_images   READ hasMultisigPartialKeyImages  NOTIFY walletChanged)
    Q_PROPERTY(quint64  wallet_height          READ wallet_height          NOTIFY walletChanged)
    Q_PROPERTY(quint64  daemon_height          READ daemon_height          NOTIFY walletChanged)

public:
    explicit Wallet(QObject *parent = nullptr);
    ~Wallet() override;

    QString address()         const { return m_addressCache;  }
    quint64 balance()         const { return m_balanceCache;  }
    quint64 unlockedBalance() const { return m_unlockedCache; }
    QString daemonAddress()   const { return m_daemonAddress; }
    bool    busy()            const { return m_busy;          }
    quint64 wallet_height()         const { return m_walletHeightCache;  }
    quint64 daemon_height()         const { return m_daemonHeightCache;  }
    QVariantList pendingOps() const;
    bool activeSyncTimer() const { return m_syncTimer.isActive(); }
    bool hasMultisigPartialKeyImages() const { return m_hasMsigPartialKeyImages; }


    Q_INVOKABLE void getBalance();
    Q_INVOKABLE void getHeight();
    Q_INVOKABLE void setRefreshHeight(uint64_t h);
    Q_INVOKABLE void changePassword(const QString &oldPass, const QString &newPass);

    Q_INVOKABLE void firstKexMsg();
    Q_INVOKABLE void prepareMultisigInfo(QString operation_caller);
    Q_INVOKABLE void makeMultisig(const QList<QByteArray> &kexMsgs, quint32 threshold, const QString &password = {});
    Q_INVOKABLE void exchangeMultisigKeys(const QList<QByteArray> &kexMsgs, const QString &password = {});
    Q_INVOKABLE void isMultisig();
    Q_INVOKABLE void getMultisigParams();
    Q_INVOKABLE void getAddress();
    Q_INVOKABLE void seedMulti();
    Q_INVOKABLE void getTransfers();
    Q_INVOKABLE void importMultisigInfos(const QList<QByteArray>  &infos, QString operation_caller);
    Q_INVOKABLE void importMultisigInfosBulk(const QList<QByteArray>  &infos,QString operation_caller);
    Q_INVOKABLE void createUnsignedMultisigTransfer(QVariantList destinations,
                                                    int feePriority,
                                                    QList<int> subtractFeeFromIndices, QString operation_caller);
    Q_INVOKABLE void signMultisigBlob(const QByteArray &txsetBlob, QString operation_caller);
    Q_INVOKABLE void submitSignedMultisig(const QByteArray &signedTxset, QString operation_caller);

    Q_INVOKABLE void describeTransfer(const QString &multisigTxset,QString operation_caller);

    Q_INVOKABLE void restoreFromSeed(const QString &path,
                                     const QString &password,
                                     const QString &seedWords,
                                     quint64        restoreHeight = 0,
                                     const QString &language      = {},
                                     const QString &nettype      = "mainnet",
                                     quint64        kdfRounds     = 1,
                                     bool           isMultisg    = false);
    Q_INVOKABLE void getPrimarySeed();
    Q_INVOKABLE void refreshHasMultisigPartialKeyImages();


    Q_INVOKABLE void prepareSimpleTransfer(const QString &ref,
                                           QVariantList destinations,
                                           int feePriority,
                                           QList<int> subtractFeeFromIndices);
    Q_INVOKABLE void describePreparedSimpleTransfer(const QString &ref);
    Q_INVOKABLE void commitPreparedSimpleTransfer(const QString &ref);
    Q_INVOKABLE void discardPreparedSimpleTransfer(const QString &ref);


    Q_INVOKABLE void getAccounts();


    Q_INVOKABLE void getSubaddresses(int accountIndex = 0);
    Q_INVOKABLE void createSubaddress(int accountIndex, const QString &label);
    Q_INVOKABLE void labelSubaddress (int accountIndex, int addressIndex, const QString &label);

    Q_INVOKABLE void setSubaddressLookahead(int major, int minor);

    void setSocksProxy(const QString &host, quint16 port);
    void clearProxy();

    void apply_proxy_if_needed(tools::wallet2 *w,  const QString &host, quint16 port,  bool useProxy);

public slots:

    void createNew(const QString &path,
                   const QString &password,
                   const QString &language = "English",
                   const QString &nettype   = "mainnet",
                   quint64       kdfRounds = 1);

    void open      (const QString &path,
              const QString &password,
              const QString &nettype   = "mainnet",
              quint64       kdfRounds = 1);

    void close();
    void save();


    void setDaemonAddress(const QString &addr);


    void startSync(int intervalSeconds = 30);
    void stopSync();
    void refreshAsync();

signals:
    void walletCreated();
    void walletOpened();
    void walletClosed();
    void walletSaved();
    void walletChanged();
    void walletRestored();
    void primarySeedReady(QString seedWords);

    void syncProgress(quint64 currentHeight, quint64 targetHeight);

    void busyChanged();
    void queueChanged();
    void errorOccurred(const QString &message, QString operation_caller = "");
    void activeSyncTimerChanged();
    void walletFullyClosed();


    void balanceReady(quint64 balance, quint64 unlocked, bool needsImport);
    void heightReady(quint64 height);
    void restoreHeightSet();
    void transfersReady(QVariantList transfers);
    void passwordChanged(bool ok);

    void firstKexMsgReady(QByteArray blob);
    void multisigInfoPrepared(QByteArray info, QString operation_caller);
    void makeMultisigDone(QByteArray info);
    void exchangeMultisigKeysDone(QByteArray info);
    void isMultisigReady(bool isMulti);
    void addressReady(QString address);
    void seedMultiReady(QString seed);
    void multisigParamsReady(quint32 threshold, quint32 total);
    void multisigInfosImported(int nImported, bool needsRescan, QString operation_caller);
    void unsignedMultisigReady(QByteArray txset, quint64 feeAtomic,
                               QVariantList normalizedDestinations,
                               QString warningOrEmpty, QString operation_caller);
    void multisigSigned(QByteArray signedTxset, bool readyToSubmit,
                        QStringList partialTxIds, QString operation_caller);
    void multisigSubmitResult(bool ok, QString txidOrError,QString operation_caller);


    void describeTransferResult(QString walletName, QVariant details, QString operation_caller);


    void simpleTransferPrepared(QString ref, quint64 feeAtomic,
                                QVariantList normalizedDestinations,
                                QString warningOrEmpty );
    void simpleDescribeResult(QString ref, QVariant details);
    void simpleSubmitResult(QString ref, bool ok, QString txidsOrError);

    void accountsReady(const QVariantList &accounts);
    void subaddressesReady(int accountIndex, const QVariantList &items);
    void subaddressCreated(int accountIndex, int addressIndex, const QString &address);
    void subaddressLabeled(int accountIndex, int addressIndex, const QString &label);
    void subaddressLookaheadSet(int major, int minor);



private:
    struct Operation {
        QString                     name;
        std::function<void ()>      func;
    };

    void enqueue(const QString &name, std::function<void ()> func);
    void runNext();


    QString    m_addressCache;
    quint64    m_balanceCache   = 0;
    quint64    m_unlockedCache  = 0;
    bool m_hasMsigPartialKeyImages = false;
    quint64    m_walletHeightCache   = 0;
    quint64    m_daemonHeightCache   = 0;


    std::unique_ptr<tools::wallet2> m_wallet;
    cryptonote::network_type        m_netType {};

    QHash<QString, std::vector<tools::wallet2::pending_tx>> m_simplePrepared;


    QQueue<Operation>         m_queue;
    bool                      m_busy       = false;
    QMutex                    m_mutex;

    // misc
    QString                   m_daemonAddress = "http://127.0.0.1:18081";
    QTimer                    m_syncTimer;

    QString m_proxyHost = "127.0.0.1";
    quint16 m_proxyPort = 0;
    bool    m_useProxy  = false;


};
