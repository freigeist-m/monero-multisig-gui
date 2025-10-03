#include "win_compat.h"
#include "multisigmanager.h"
#include "accountmanager.h"
#include <QVariant>
#include <QVariantMap>


MultisigManager::MultisigManager(MultiWalletController *wm, TorBackend *tor, QObject *parent)
    : QObject(parent), m_wm(wm), m_tor(tor) {}


// ---- helpers ----
QString MultisigManager::canonOnion(QString o) {
    o = o.trimmed().toLower();
    if (!o.endsWith(".onion")) o.append(".onion");
    return o;
}
QString MultisigManager::makeKey(const QString &myOnion, const QString &ref) {
    return canonOnion(myOnion) + "|" + ref;
}
void MultisigManager::splitKey(const QString &key, QString *myOnionOut, QString *refOut) {
    const int p = key.indexOf('|');
    if (p <= 0) { if (myOnionOut) myOnionOut->clear(); if (refOut) refOut->clear(); return; }
    if (myOnionOut) *myOnionOut = key.left(p);
    if (refOut)     *refOut     = key.mid(p+1);
}

QString MultisigManager::startMultisig(const QString &ref,int m,int n,const QStringList &peers,
                                       const QString &walletName,const QString &walletPassword,
                                       const QString &myOnion, const QString &creator)
{
    const QString o = canonOnion(myOnion);
    const QString r = ref.trimmed();
    const QString k = makeKey(o, r);


    if (m_sessions.contains(k)) return r;

    if (m_notifiers.contains(k)) return QString();

    if (m_wm && !m_wm->walletNameForRef(r, o).isEmpty()) return QString();

    auto *s = new MultisigSession(m_wm, m_tor, r, m, n, peers,
                                  walletName, walletPassword, o, creator, this);


    connect(s, &MultisigSession::finished,
            this, [this, s](const QString &myOnion, const QString &ref, const QString &reason){
                onFinished(myOnion, ref, s, reason);
            });



    m_sessions.insert(k, s);
    m_keyBySession.insert(s, k);
    m_current = s;
    emit currentSessionChanged();
    emit sessionsChanged();

    s->start();
    emit sessionStarted(o, r);
    return r;
}

MultisigSession *MultisigManager::sessionFor(const QString &myOnion, const QString &ref) const
{
    return m_sessions.value(makeKey(myOnion, ref), nullptr);
}

void MultisigManager::onFinished(const QString &myOnion, const QString &ref, MultisigSession *s, const QString &result)
{

    stopNotifier(myOnion, ref);

    const QString key = m_keyBySession.take(s);
    if (!key.isEmpty()) {
        auto it = m_sessions.find(key);
        if (it!=m_sessions.end()) {
            it.value()->deleteLater();
            m_sessions.erase(it);
        }
    } else {

        m_sessions.remove(makeKey(myOnion, ref));
    }

    emit sessionFinished(canonOnion(myOnion), ref, result);

    if (m_current == s) {
        m_current=nullptr; emit currentSessionChanged();
    }
    emit sessionsChanged();
}

void MultisigManager::stopMultisig(const QString &myOnion, const QString &ref)
{

    stopNotifier(myOnion, ref);


    auto it = m_sessions.find(makeKey(myOnion, ref));
    if (it != m_sessions.end()) {
        it.value()->cancel();

    }
}

QString MultisigManager::startMultisigNotifier(const QString &ref, const QStringList &notifyPeers, const QString &myOnion)
{
    auto *s = sessionFor(myOnion, ref);
    if (!s || notifyPeers.isEmpty()) return QString();

    const QString k = makeKey(myOnion, ref);
    if (m_notifiers.contains(k)) return ref;


    QStringList allPeers;
    const QVariantList list = s->peerList().toList();
    for (const QVariant &v : list) {
        const auto m = v.toMap();
        const QString o = m.value(QStringLiteral("onion")).toString().trimmed().toLower();
        if (!o.isEmpty()) allPeers << o;
    }


    if (!myOnion.isEmpty()) allPeers << myOnion;
    allPeers.removeDuplicates();

    auto *n = new MultisigNotifier(m_wm, m_tor,
                                   ref,
                                   s->m(),
                                   s->n(),
                                   allPeers,
                                   notifyPeers,
                                   myOnion,
                                    false,
                                   this);


    connect(n, &MultisigNotifier::finished,
            this, [this, k](const QString &, const QString &){
                auto it = m_notifiers.find(k);
                if (it!=m_notifiers.end()) {
                    it.value()->deleteLater();
                    m_notifiers.erase(it);
                    emit sessionsChanged();
                }
            });

    m_notifiers.insert(k, n);
    m_keyByNotifier.insert(n, k);
    n->start();
    return ref;
}

QString MultisigManager::startStandaloneNotifier(const QString &ref, int m, int n,
                                                 const QStringList &allPeers,
                                                 const QStringList &notifyPeers,
                                                 const QString &myOnion)
{
    const QString o = canonOnion(myOnion);
    const QString r = ref.trimmed();
    const QString k = makeKey(o, r);


    if (m_notifiers.contains(k)) return r;


    QSet<QString> peerSet;
    for (const QString &p : allPeers) {
        const QString pn = canonOnion(p);
        if (!pn.isEmpty()) peerSet.insert(pn);
    }
    const QStringList peersNorm = peerSet.values();


    for (const QString &pn : peersNorm) {

        if (m_wm && !m_wm->walletNameForRef(r, pn).isEmpty()) {
            return QString();
        }

        if (m_sessions.contains(makeKey(pn, r))) {
            return QString();
        }

    }


    auto *new_notifier = new MultisigNotifier(m_wm, m_tor,
                                              r, m, n,
                                              peersNorm,
                                              notifyPeers,
                                              o,
                                              true,
                                              this);

    connect(new_notifier, &MultisigNotifier::finished,
            this, [this, k](const QString &, const QString &){
                auto it = m_notifiers.find(k);
                if (it != m_notifiers.end()) {
                    it.value()->deleteLater();
                    m_notifiers.erase(it);
                    emit sessionsChanged();
                }
            });

    m_notifiers.insert(k, new_notifier);
    m_keyByNotifier.insert(new_notifier, k);
    new_notifier->start();
    return r;
}


void MultisigManager::stopNotifier(const QString &myOnion, const QString &ref)
{
    auto it = m_notifiers.find(makeKey(myOnion, ref));
    if (it == m_notifiers.end()) return;

    it.value()->cancel();



}

MultisigNotifier *MultisigManager::notifierFor(const QString &myOnion, const QString &ref) const
{
    return m_notifiers.value(makeKey(myOnion, ref), nullptr);
}

bool MultisigManager::hasNotifier(const QString &myOnion, const QString &ref) const {
       return m_notifiers.contains(makeKey(myOnion, ref));
    }

void MultisigManager::reset()
{

    const auto sessKeys = m_sessions.keys();
    for (const QString &k : sessKeys) {
        QString o, r; splitKey(k, &o, &r);
        stopMultisig(o, r);
    }


    const auto notifKeys = m_notifiers.keys();
    for (const QString &k : notifKeys) {
        QString o, r; splitKey(k, &o, &r);
        stopNotifier(o, r);
    }


    m_sessions.clear();
    m_notifiers.clear();
    m_keyBySession.clear();
    m_keyByNotifier.clear();
    m_current = nullptr;

    emit currentSessionChanged();
    emit sessionsChanged();
}


