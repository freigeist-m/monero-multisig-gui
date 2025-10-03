#pragma once
#include "routerhandler.h"
#include "multisigmanager.h"
#include <QJsonObject>
#include <QMap>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QDateTime>

class AccountManager;
class MultiWalletController;

class MultisigApiRouter : public RouterHandler
{
    Q_OBJECT
public:

    explicit MultisigApiRouter(MultisigManager *mgr,
                               AccountManager  *acct,
                               const QString   &boundOnion = QString(),
                               QObject         *parent=nullptr);


    void    setBoundOnion(const QString &on) { m_boundOnion = on.trimmed().toLower(); }
    QString boundOnion() const               { return m_boundOnion; }



signals:

    void requestReceived(const QString &onion, const QString &method, const QString &path);



protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    MultisigManager      *m_mgr  = nullptr;
    AccountManager       *m_acct = nullptr;
    QString               m_boundOnion;

    void sendPlain(QTcpSocket *sock,int status,const QByteArray &body);
    void sendJson (QTcpSocket *sock,int status,const QJsonObject &obj);

    bool cooldownAllow(const QString &key, int seconds) const;
    mutable QHash<QString, qint64> m_opCooldown;


    bool handlePing (const QByteArray &method,const QByteArray &path,
                    const QMap<QByteArray,QByteArray> &headers, QTcpSocket *sock);
    bool handleBlob (const QByteArray &method,const QByteArray &path,
                    const QMap<QByteArray,QByteArray> &headers, QTcpSocket *sock);
    bool handleNew  (const QByteArray &method,const QByteArray &path,
                   const QMap<QByteArray,QByteArray> &headers,
                   const QByteArray &body, QTcpSocket *sock);


    bool handleTransferPing(const QByteArray &method,const QByteArray &path,
                            const QMap<QByteArray,QByteArray> &headers, QTcpSocket *sock);
    bool handleTransferRequestInfo(const QByteArray &method,const QByteArray &path,
                                   const QMap<QByteArray,QByteArray> &headers, QTcpSocket *sock);
    bool handleTransferSubmit(const QByteArray &method,const QByteArray &path,
                              const QMap<QByteArray,QByteArray> &headers,
                              const QByteArray &body, QTcpSocket *sock);
    bool handleTransferStatus(const QByteArray &method,const QByteArray &path,
                              const QMap<QByteArray,QByteArray> &headers, QTcpSocket *sock);

    bool enforceDestinationOnion(const QString &ref, QTcpSocket *sock) const;


    bool verifySignedGet(const QByteArray &rawPath,
                         const QMap<QByteArray,QByteArray> &headers,
                         const QString &canonPath,
                         QString *outCallerOnion,
                         QString *whyNot) const;

    bool verifySignedPost(const QByteArray &rawPath,
                          const QMap<QByteArray,QByteArray> &headers,
                          const QByteArray &bodyCompact,
                          const QString &canonPath,
                          QString *outCallerOnion,
                          QString *whyNot) const;


    MultiWalletController* wm() const;
    QString walletNameForRefOnion(const QString &ref, const QString &onion) const;
    QStringList peersForRefOnion(const QString &ref, const QString &onion) const;
    bool refExistsForOnion(const QString &ref, const QString &onion) const;


    bool saveIncomingTransfer(const QString &walletRef,
                              const QString &walletName,
                              const QString &transferRef,
                              const QJsonObject &jsonBody,
                              const QStringList &signingOrder,
                              const QStringList &whoHasSigned);

    bool readSavedTransfer(const QString &walletRef,
                           const QString &transferRef,
                           QJsonObject *out) const;



    mutable QHash<QString, qint64> m_postSeen;
    static constexpr qint64 kReplayTtlSec   = 5 * 60;
    static constexpr int    kReplayMaxItems = 4096;
    bool seenPostAndRemember(const QByteArray &pub32,
                             const QString &canonPath,
                             const QByteArray &bodyHashHex) const;
    void pruneReplayCacheIfNeeded() const;
};
