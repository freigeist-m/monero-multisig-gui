#ifndef MULTISIGNOTIFIER_H
#define MULTISIGNOTIFIER_H

#include <QObject>
#include <QTimer>
#include <QThreadPool>
#include <QHash>
#include <QJsonObject>
#include <QStringList>

class MultiWalletController;
class TorBackend;


class MultisigNotifier : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString stage READ stageName NOTIFY stageChanged)
     Q_PROPERTY(QString myOnion READ myOnionAddress NOTIFY stageChanged)

public:
    enum class Stage { INIT, POSTING, COMPLETE, ERROR };
    Q_ENUM(Stage)

    explicit MultisigNotifier(MultiWalletController *wm,
                              TorBackend            *tor,
                              const QString         &ref,
                              int                    m,
                              int                    n,
                              const QStringList     &allPeers,
                              const QStringList     &notifyPeers,
                              const QString         &myOnion,
                              bool                   isStandaloneNotifier = false,
                              const QString         &nettype =  "mainnet",
                              QObject               *parent=nullptr);



    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();

    Q_INVOKABLE QVariantList getPeerStatus() const;
    Q_INVOKABLE int getCompletedCount() const;
    Q_INVOKABLE int getTotalCount() const;

    QString stageName() const;
    QString myOnionAddress() const {return m_myOnion;}
    QString m_myOnion;

    ~MultisigNotifier() override;

    QJsonObject httpRequestBlocking(const QString &onion,
                                    const QString &path,
                                    bool           signedFlag,
                                    const QByteArray &method,
                                    const QJsonObject &jsonBody);

signals:
    void finished(QString myOnion, QString ref, QString result);
    void stageChanged(QString stageName, QString myOnion, QString ref);
    void peerStatusChanged(QString myOnion, QString ref);

    void _httpResult(QString onion, QString path, QJsonObject res, QString err);

private:
    struct Peer {
        QString onion;
        bool    ok{false};
        int     trials{0};
        qint64  last{0};
    };

    static QString stageToString(Stage s);

    void retryRound();
    void httpPostAsync(const QString &onion,
                       const QString &path,
                       bool           signedFlag,
                       const QJsonObject &body);
    void onHttp(QString onion, QString path, QJsonObject res, QString err);

    QByteArray sign(const QJsonObject &payload) const;


    MultiWalletController *m_wm {nullptr};
    TorBackend            *m_tor{nullptr};

    QString      m_ref;
    int          m_m{0};
    int          m_n{0};
    QStringList  m_allPeers;
    QString      m_nettype;

    bool m_isStandaloneNotifier{false};

    QHash<QString, Peer> m_peers;

    Stage        m_stage{Stage::INIT};
    bool         m_stop{false};

    QTimer       m_retry;
    QThreadPool  m_httpPool;

    QByteArray   m_scalar, m_prefix, m_pubKey;

    int m_inFlight{0};

    static constexpr int RETRY_MS = 5'000;
    static constexpr int MAX_TRIES_PER_PEER = 3600;
};

#endif
