#pragma once
#include <QObject>
#include <QHash>
#include <QStringList>

#include "multisigsession.h"
#include "multiwalletcontroller.h"
#include "torbackend.h"
#include "multisignotifier.h"

class MultisigManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject*  currentSession READ currentSession NOTIFY currentSessionChanged)

    Q_PROPERTY(QStringList sessionsKeys  READ sessionsKeys  NOTIFY sessionsChanged)
    Q_PROPERTY(QStringList notifierKeys  READ notifierKeys  NOTIFY sessionsChanged)

public:
    explicit MultisigManager(MultiWalletController *wm,
                             TorBackend            *tor,
                             QObject               *parent=nullptr);

    Q_INVOKABLE QString startMultisig(const QString &ref,
                                      int m, int n,
                                      const QStringList &peers,
                                      const QString &walletName,
                                      const QString &walletPassword,
                                      const QString &myOnion,
                                      const QString &creator);

    Q_INVOKABLE MultisigSession *sessionFor(const QString &myOnion, const QString &ref) const;

    // Notifier API
    Q_INVOKABLE QString startMultisigNotifier(const QString &ref,
                                              const QStringList &notifyPeers, const QString &myOnion);


    Q_INVOKABLE void    stopNotifier(const QString &myOnion, const QString &ref);
    Q_INVOKABLE bool    hasNotifier(const QString &myOnion, const QString &ref) const;


    Q_INVOKABLE void    stopMultisig(const QString &myOnion, const QString &ref);


    Q_INVOKABLE QString startStandaloneNotifier(const QString &ref,
                                                int m, int n,
                                                const QStringList &allPeers,
                                                const QStringList &notifyPeers,
                                                const QString &myOnion);

    Q_INVOKABLE MultisigNotifier *notifierFor(const QString &myOnion, const QString &ref) const;
    Q_INVOKABLE void reset();

signals:
    void sessionStarted(QString myOnion,QString ref);
    void sessionFinished(QString myOnion, QString ref, QString result);
    void currentSessionChanged();
    void sessionsChanged();

private:
    MultisigSession* currentSession() const { return m_current; }

    QStringList      sessionsKeys()   const { return m_sessions.keys(); }
    QStringList      notifierKeys()   const { return m_notifiers.keys(); }

    void onFinished(const QString &myOnion, const QString &ref, MultisigSession *s, const QString &result);


    static QString canonOnion(QString o);
    static QString makeKey(const QString &myOnion, const QString &ref);
    static void    splitKey(const QString &key, QString *myOnionOut, QString *refOut);

    QHash<QString,MultisigSession*>  m_sessions;
    QHash<QString,MultisigNotifier*> m_notifiers;

    QHash<MultisigSession*,  QString> m_keyBySession;
    QHash<MultisigNotifier*, QString> m_keyByNotifier;


    MultisigSession                 *m_current = nullptr;
    MultiWalletController           *m_wm      = nullptr;
    TorBackend                      *m_tor     = nullptr;
};
