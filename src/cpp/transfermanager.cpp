#include "win_compat.h"
#include "transfermanager.h"
#include "simpletransfer.h"
#include "transferinitiator.h"
#include "incomingtransfer.h"
#include "transfertracker.h"
#include "multisigimportsession.h"
#include "multiwalletcontroller.h"
#include "torbackend.h"
#include "accountmanager.h"
#include "wallet.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMetaObject>
#include <QDebug>
#include <QQmlEngine>
#include <algorithm>
#include <QSet>
#include <QVector>
#include <QPair>


TransferManager::TransferManager(MultiWalletController *wm,
                                 TorBackend            *tor,
                                 QObject               *parent)
    : QObject(parent), m_wm(wm), m_tor(tor)
{
    Q_ASSERT(wm && tor);

    if (auto obj = m_wm->property("accountManager").value<QObject*>())
    {
        m_acct = qobject_cast<AccountManager*>(obj);

        connect(m_acct, &AccountManager::isAuthenticatedChanged,
                this, [this](){
                    if (!m_acct->isAuthenticated()) return;
                    restoreAllSaved();
                    emit currentSessionChanged();
                }, Qt::QueuedConnection);
    }

    restoreAllSaved();


    m_msigImport = new MultisigImportSession(this);
    m_msigImport->initialize(m_wm, m_tor);
    setupMultisigImportWiring();

    if (m_acct) {
        connect(m_acct, &AccountManager::isAuthenticatedChanged,
                this, [this](){
                    if (m_acct->isAuthenticated()) {
                        m_msigImport->initialize(m_wm, m_tor);
                        maybeStartMultisigImport();
                    } else {
                        m_msigImport->stop();
                    }
                }, Qt::QueuedConnection);
    }

}

TransferManager::~TransferManager()
{
    for (auto *p : std::as_const(m_outgoing))  p->deleteLater();
    for (auto *p : std::as_const(m_incoming))  p->deleteLater();
    for (auto *p : std::as_const(m_trackers))  p->deleteLater();
}


static QList<TransferInitiator::Destination>
toAtomicDests(const QVariantList &destinations)
{
    QList<TransferInitiator::Destination> out;
    out.reserve(destinations.size());
    for (const QVariant &v : destinations) {
        const QVariantMap m = v.toMap();
        const QString addr  = m.value("address").toString().trimmed();
        if (addr.isEmpty()) continue;

        quint64 atomic = 0;
        const QVariant amountV = m.value("amount");
        if (amountV.metaType().id() == QMetaType::Double) {
            const double xmr = amountV.toDouble();
            if (!(xmr > 0.0)) continue;
            atomic = static_cast<quint64>(xmr * 1e12 + 0.5);
        } else {
            const qlonglong raw = amountV.toLongLong();
            if (!(raw > 0)) continue;
            atomic = static_cast<quint64>(raw);
        }
        out.push_back({addr, atomic});
    }
    return out;
}

static QList<int>
toFeeIndexList(const QVariantList &feeSplitVar, int destCount)
{
    QList<int> out;
    out.reserve(feeSplitVar.size());
    for (const QVariant &v : feeSplitVar) {
        bool ok = false;
        const int idx = v.toInt(&ok);
        if (!ok) continue;
        if (idx < 0) continue;
        if (destCount > 0 && idx >= destCount) continue;
        out.push_back(idx);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

QString TransferManager::startOutgoingTransfer(const QString &walletRef,
                                               const QVariantList &destinations,
                                               const QStringList &peers,
                                               const QStringList &signingOrder,
                                               int  threshold,
                                               int  feePriority,
                                               const QVariantList &feeSplitVar,
                                               bool inspectBeforeSending,
                                               const QString &myOnion)
{
    const auto dests = toAtomicDests(destinations);
    const QList<int> feeIdx = toFeeIndexList(feeSplitVar, dests.size());
    const QString onionNorm = normOnion(myOnion);

    const QString ref = makeTransferRef(walletRef, onionNorm);

    auto *init = new TransferInitiator(
        m_wm, m_tor,
        ref, walletRef,
        dests, peers, signingOrder,
        threshold, feePriority,
        feeIdx,
        inspectBeforeSending,
        onionNorm,
        this);

    m_outgoing.insert(ref, init);
    emit currentSessionChanged();

    connect(init, &TransferInitiator::submittedSuccessfully,
            this, [this](const QString &tref){
                resumeTracker(tref);
                restoreAllSaved();
                emit currentSessionChanged();
            }, Qt::QueuedConnection);

    connect(init, &TransferInitiator::finished,
            this,
            [this](const QString &tref, const QString &result){

                if (result == "abort") deleteSavedTransfer(tref);
                auto it = m_outgoing.find(tref);
                if (it != m_outgoing.end()) {
                    it.value()->deleteLater();
                    m_outgoing.erase(it);
                    restoreAllSaved();
                    emit currentSessionChanged();

                }
            },
            Qt::QueuedConnection);

    init->start();
    emit sessionStarted(ref);
    return ref;
}

/* ────────────────────────────────────────────────────────────────────────────
 *  Outgoing controls
 * ────────────────────────────────────────────────────────────────────────── */

QObject* TransferManager::getOutgoingSession(const QString &ref) const
{
    auto it = m_outgoing.find(ref);
    if (it == m_outgoing.end()) return nullptr;
    QQmlEngine::setObjectOwnership(it.value(), QQmlEngine::CppOwnership);
    return static_cast<QObject*>(it.value());
}

bool TransferManager::abortOutgoingTransfer(const QString &ref)
{
    auto it = m_outgoing.find(ref);
    if (it == m_outgoing.end()) return false;
    it.value()->cancel();
    return true;
}

bool TransferManager::validateOutgoingTransfer(const QString &ref)
{ return m_outgoing.contains(ref); }

bool TransferManager::proceedAfterApproval(const QString &ref)
{
    TransferInitiator *s = m_outgoing.value(ref, nullptr);
    if (s)
        QMetaObject::invokeMethod(s, "proceedAfterApproval", Qt::QueuedConnection);
    return s != nullptr;
}


QString TransferManager::saveIncomingTransfer(const QString &walletRef,
                                              const QString &transferRef,
                                              const QString &blobB64,
                                              const QStringList &order,
                                              const QStringList &signedBy,
                                              const QVariantMap &jsonBody)
{
    if (!m_acct) return {};

    QJsonDocument doc = QJsonDocument::fromJson(m_acct->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonObject monero= root.value("monero").toObject();
    QJsonArray  wallets= monero.value("wallets").toArray();

    bool updated=false;
    for (int i=0;i<wallets.size();++i) {
        QJsonObject w = wallets[i].toObject();
        if (w.value("reference").toString()!=walletRef) { wallets[i]=w; continue; }

        QJsonObject transfers = w.value("transfers").toObject();

        // peers map
        QJsonObject peersObj;
        for (const QString &o : order) {
            const bool present = signedBy.contains(o,Qt::CaseInsensitive);
            peersObj.insert(o, QJsonArray{
                                          present ? "CHECKING_STATUS" : "UNKNOWN",
                                          present, present, ""});
        }

        QString my_onion;
        {
            QStringList ours = m_acct ? m_acct->torOnions() : QStringList{};
            for (QString &o : ours) o = normOnion(o);
            for (auto it = peersObj.begin(); it != peersObj.end(); ++it) {
                const QString peerKey = normOnion(it.key());
                if (ours.contains(peerKey, Qt::CaseInsensitive)) {
                    my_onion = peerKey;
                    break;
                }
            }
        }


        QJsonObject entry{
            {"wallet_name", w.value("name").toString()},
            {"wallet_ref",  walletRef},
            {"destinations",
             QJsonArray::fromVariantList(
                 jsonBody.value("transfer_description").toMap()
                     .value("recipients").toList())},
            {"peers",       peersObj},
            {"signing_order", QJsonArray::fromStringList(order)},
            {"stage", "RECEIVED"},
            {"signatures",  QJsonArray::fromStringList(signedBy)},
            {"status","NEW"},
            {"transfer_blob", blobB64},
            {"transfer_description",
             QJsonObject::fromVariantMap(
                 jsonBody.value("transfer_description").toMap())},
            {"tx_id","pending"},
            {"created_at", static_cast<qint64>(QDateTime::currentSecsSinceEpoch())}
        };

        if (!my_onion.isEmpty())
            entry.insert("my_onion", my_onion);

        transfers.insert(transferRef, entry);


        w["transfers"]=transfers; wallets[i]=w; updated=true;
    }
    if (!updated) return {};

    monero["wallets"]=wallets; root["monero"]=monero;
    (void)m_acct->saveAccountData(
        QJsonDocument(root).toJson(QJsonDocument::Compact));

    restoreAllSaved();

    emit currentSessionChanged();
    return transferRef;
}


QString TransferManager::startIncomingTransfer(const QString &transferRef)
{
    if (m_incoming.contains(transferRef)) return transferRef;

    const QVariantMap saved = m_allSavedMap.value(transferRef);
    if (saved.isEmpty()) return {};

    const QString wref = saved.value("wallet_ref").toString();
    const QString my_onion = normOnion(saved.value("my_onion").toString());
    auto *sess = new IncomingTransfer(m_wm, m_tor, m_acct,
                                      wref, transferRef, my_onion, this);

    connect(sess, &IncomingTransfer::finished,
            this, [this](const QString &ref, const QString &res){
                m_incoming.remove(ref);
                restoreAllSaved();
                emit sessionFinished(ref, res);
                emit currentSessionChanged();
            }, Qt::QueuedConnection);

    connect(sess, &IncomingTransfer::submittedSuccessfully,
            this, [this](const QString &ref){
                resumeTracker(ref);
            }, Qt::QueuedConnection);

    m_incoming.insert(transferRef, sess);
    emit currentSessionChanged();
    sess->start();
    return transferRef;
}

QObject* TransferManager::getIncomingSession(const QString &ref) const
{
    auto it = m_incoming.find(ref);
    if (it == m_incoming.end()) return nullptr;
    QQmlEngine::setObjectOwnership(it.value(), QQmlEngine::CppOwnership);
    return static_cast<QObject*>(it.value());
}


QString TransferManager::makeTransferRef(const QString &walletRef,  const QString &myOnion) const
{
    const QString onionTag = normOnion(myOnion).left(10);
    const quint64 r = QRandomGenerator::global()->generate64();
    return QStringLiteral("%1_%2_%3_%4")
        .arg(walletRef, onionTag)
        .arg(QDateTime::currentMSecsSinceEpoch())
        .arg(QString::number(r, 16));
}

QString TransferManager::normOnion(QString s)
{
    s = s.trimmed().toLower();
    if (!s.endsWith(".onion") && !s.isEmpty()) s.append(".onion");
    return s;
}

static inline QVariantMap _getSaved(const QHash<QString, QVariantMap> &all,
                                    const QString &ref)
{
    auto it = all.find(ref);
    return (it == all.end()) ? QVariantMap{} : it.value();
}


bool TransferManager::isTerminalStage(const QString &stage)
{
    static const QSet<QString> kTerm{
        "COMPLETE","ABORTED","DECLINED","BROADCAST_SUCCESS","ERROR","FAILED"
    };
    return kTerm.contains(stage);
}


QVariantList TransferManager::openTransfersModel() const
{
    QSet<QString> seen;
    QVector<QPair<QString,QVariantMap>> list;
    list.reserve(m_allSavedMap.size());

    auto feed=[&](const QString &ref){
        if (seen.contains(ref)) return;
        const QVariantMap saved=_getSaved(m_allSavedMap,ref);
        if (saved.isEmpty()) return;
        if (isTerminalStage(saved.value("stage").toString())) return;
        seen.insert(ref); list.push_back({ref,saved});
    };

    for (const auto &r : m_outgoing.keys())  feed(r);
    for (const auto &r : m_trackers.keys())  feed(r);
    for (auto it=m_allSavedMap.begin(); it!=m_allSavedMap.end(); ++it) feed(it.key());

    std::sort(list.begin(), list.end(), [](const auto&a,const auto&b){
        return a.second.value("created_at").toLongLong() >
               b.second.value("created_at").toLongLong();
    });

    QVariantList out;
    out.reserve(list.size());
    for (const auto &p : list) {
        QVariantMap row=p.second;
        row["ref"]=p.first;
        const QVariantList rec=row.value("transfer_description").toMap()
                                     .value("recipients").toList();
        row["recipient_count"]=rec.size();
        row["peer_count"]=row.value("peers").toMap().size();
        out.push_back(row);
    }
    return out;
}


QString TransferManager::getOutgoingDetails(const QString &ref) const
{
    if (auto *s = m_outgoing.value(ref,nullptr))
        return s->getTransferDetailsJson();
    return {};
}

QVariantMap TransferManager::getTransferSummary(const QString &ref) const
{
    const QVariantMap saved=_getSaved(m_allSavedMap,ref);
    if (!saved.isEmpty()) return saved;

    const QString json=getOutgoingDetails(ref);
    if (!json.isEmpty()) {
        const auto doc=QJsonDocument::fromJson(json.toUtf8());
        if (doc.isObject()) return doc.object().toVariantMap();
    }

    const QString json_simple=getSimpleDetails(ref);
    if (!json_simple.isEmpty()) {
        const auto doc_ =QJsonDocument::fromJson(json_simple.toUtf8());
        if (doc_.isObject()) return doc_.object().toVariantMap();
    }


    return {};
}


QStringList TransferManager::outgoingSessions()          const { return m_outgoing.keys();  }
QStringList TransferManager::incomingSessions()          const { return m_incoming.keys();  }
QStringList TransferManager::activeTrackers()            const { return m_trackers.keys();  }
QStringList TransferManager::pendingIncomingTransfers()  const { return m_pendingIncomingMap.keys(); }
QStringList TransferManager::allSavedTransfers()         const { return m_allSavedMap.keys(); }


void TransferManager::restoreAllSaved()
{
    m_allSavedMap.clear();
    m_pendingIncomingMap.clear();

    const QJsonObject root = loadAccountRoot();
    const QJsonArray wallets = root.value("monero").toObject()
                                   .value("wallets").toArray();

    for (const auto &wv : wallets) {
        const QJsonObject w = wv.toObject();
        const QJsonObject transfers = w.value("transfers").toObject();

        for (auto it = transfers.begin(); it != transfers.end(); ++it) {
            const QString ref = it.key();
            const QJsonObject tr = it.value().toObject();

            m_allSavedMap.insert(ref, tr.toVariantMap());


            const QString stage  = tr.value("stage").toString();
            const QString status = tr.value("status").toString();
            if (stage.compare("RECEIVED", Qt::CaseInsensitive) == 0 &&
                status == "NEW") {
                m_pendingIncomingMap.insert(ref, tr.toVariantMap());
            }
        }
    }

    emit currentSessionChanged();
}


QJsonObject TransferManager::loadAccountRoot() const
{
    return m_acct
               ? QJsonDocument::fromJson(m_acct->loadAccountData().toUtf8()).object()
               : QJsonObject{};
}

void TransferManager::saveAccountRoot(const QJsonObject &root) const
{
    if (m_acct)
        (void)m_acct->saveAccountData(
            QJsonDocument(root).toJson(QJsonDocument::Compact));
}


bool TransferManager::deleteSavedTransfer(const QString &ref)
{
    if (!m_acct) return false;

    QJsonObject root = loadAccountRoot();
    QJsonObject mon  = root.value("monero").toObject();
    QJsonArray wallets = mon.value("wallets").toArray();
    bool removed=false;

    for (int i=0;i<wallets.size();++i){
        QJsonObject w=wallets[i].toObject();
        QJsonObject transfers=w.value("transfers").toObject();
        if (transfers.contains(ref)) {
            transfers.remove(ref); w["transfers"]=transfers;
            wallets[i]=w; removed=true; break;
        }
    }
    if (!removed) return false;

    mon["wallets"]=wallets; root["monero"]=mon;
    saveAccountRoot(root);
    restoreAllSaved();
    emit currentSessionChanged();
    return true;
}


void TransferManager::wireTracker(TransferTracker *t)
{
    connect(t,&TransferTracker::progress,
            this,[this](const QString&,const QVariantMap&){
                restoreAllSaved(); emit currentSessionChanged(); },
            Qt::QueuedConnection);

    connect(t,&TransferTracker::finished,
            this,[this,t](const QString &ref,const QString &res){
                m_trackers.remove(ref); t->deleteLater();
                restoreAllSaved(); emit currentSessionChanged();
                emit sessionFinished(ref,res);
            }, Qt::QueuedConnection);
}

bool TransferManager::resumeTracker(const QString &ref)
{
    if (m_trackers.contains(ref)) return true;

    QString wref, wname, myOnion;
    if (!resumeTrackerInternal(ref, &wref, &wname, &myOnion)) return false;

    auto *trk=new TransferTracker(wref,ref,wname,m_tor,m_acct, myOnion,this);
    wireTracker(trk);
    m_trackers.insert(ref,trk);
    trk->start();
    emit currentSessionChanged();
    return true;
}

bool TransferManager::resumeTrackerInternal(const QString &ref,
                                            QString *outWref,
                                            QString *outWname,
                                            QString *outMyOnion)
{
    const QJsonObject root=loadAccountRoot();
    const QJsonArray wallets=root.value("monero").toObject()
                                   .value("wallets").toArray();
    for (const auto &wv:wallets){
        const QJsonObject w=wv.toObject();
        const QJsonObject transfers=w.value("transfers").toObject();
        if (!transfers.contains(ref)) continue;
        const QString stage=transfers.value(ref).toObject().value("stage").toString();
        if (isTerminalStage(stage)) return false;
        if (outWref)  *outWref = w.value("reference").toString();
        if (outWname) *outWname= w.value("name").toString();
        if (outMyOnion) *outMyOnion = transfers.value(ref).toObject().value("my_onion").toString();
        return true;
    }
    return false;
}



bool TransferManager::stopTracker(const QString &ref)
{
    auto it = m_trackers.find(ref);
    if (it == m_trackers.end())
        return false;

    if (it.value()) {
        QMetaObject::invokeMethod(it.value(), "stop", Qt::QueuedConnection);

    }
    m_trackers.erase(it);
    emit currentSessionChanged();
    return true;
}


QObject* TransferManager::getTrackerSession(const QString &ref) const
{
    auto it = m_trackers.find(ref);
    if (it == m_trackers.end()) return nullptr;
    QQmlEngine::setObjectOwnership(it.value(), QQmlEngine::CppOwnership);
    return static_cast<QObject*>(it.value());
}

QString TransferManager::getSavedTransferDetails(const QString &transferRef) const
{

    if (auto *s = m_outgoing.value(transferRef, nullptr))
        return s->getTransferDetailsJson();

    const QJsonObject root = loadAccountRoot();
    const QJsonArray  wallets = root.value("monero").toObject()
                                   .value("wallets").toArray();

    for (const auto &wv : wallets) {
        const QJsonObject w = wv.toObject();
        const QJsonObject transfers = w.value("transfers").toObject();
        if (transfers.contains(transferRef)) {
            return QJsonDocument(transfers.value(transferRef).toObject())
            .toJson(QJsonDocument::Compact);
        }
    }
    return {};
}


void TransferManager::setupMultisigImportWiring()
{
    if (!m_msigImport) return;
    connect(m_msigImport, &MultisigImportSession::sessionStarted,
            this, &TransferManager::multisigImportSessionStarted, Qt::QueuedConnection);
    connect(m_msigImport, &MultisigImportSession::sessionStopped,
            this, &TransferManager::multisigImportSessionStopped, Qt::QueuedConnection);
    connect(m_msigImport, &MultisigImportSession::walletImportCompleted,
            this, &TransferManager::multisigImportWalletCompleted, Qt::QueuedConnection);
    connect(m_msigImport, &MultisigImportSession::peerInfoReceived,
            this, &TransferManager::multisigImportPeerInfoReceived, Qt::QueuedConnection);
}

void TransferManager::maybeStartMultisigImport()
{
    if (!m_msigImport) return;
    if (m_acct && m_acct->isAuthenticated())
        m_msigImport->start();
}


bool TransferManager::isMultisigImportSessionRunning() const
{ return m_msigImport ? m_msigImport->running() : false; }

void TransferManager::startMultisigImportSession()
{ if (m_msigImport) m_msigImport->start(); }

void TransferManager::stopMultisigImportSession()
{ if (m_msigImport) m_msigImport->stop(); }

QString TransferManager::getMultisigImportActivity() const
{ return m_msigImport ? m_msigImport->getImportActivity() : QStringLiteral("[]"); }

QString TransferManager::getMultisigImportPeerActivity() const
{ return m_msigImport ? m_msigImport->getPeerActivity() : QStringLiteral("[]"); }

int TransferManager::getMultisigImportActiveWalletCount() const
{ return m_msigImport ? m_msigImport->getActiveWalletCount() : 0; }

QString TransferManager::getMultisigImportWalletStatus(const QString &walletName) const
{ return m_msigImport ? m_msigImport->getWalletStatus(walletName) : QStringLiteral("idle"); }

void TransferManager::clearMultisigImportActivity()
{ if (m_msigImport) m_msigImport->clearActivity(); }


QString TransferManager::startSimpleTransfer(const QString &walletRef,
                                             const QVariantList &destinations,
                                             int feePriority,
                                             const QVariantList &feeSplitVar,
                                             bool inspectBeforeSending)
{

    QList<TransferInitiator::Destination> msigDests = toAtomicDests(destinations);

    QList<SimpleTransfer::Destination> dests;
    dests.reserve(msigDests.size());
    for (const auto &d : msigDests) dests.push_back({ d.address, d.amount });

    const QList<int> feeIdx = toFeeIndexList(feeSplitVar, dests.size());
    const QString ref = makeTransferRef(walletRef, "abcdeabcdeabcde");

    auto *sess = new SimpleTransfer(m_wm, m_acct,
                                    ref, walletRef,
                                    dests, feePriority, feeIdx,
                                    inspectBeforeSending, this);

    m_simple.insert(ref, sess);
    emit currentSessionChanged();

    connect(sess, &SimpleTransfer::submittedSuccessfully,
            this, [this](const QString &tref){

                restoreAllSaved();
                emit currentSessionChanged();
            }, Qt::QueuedConnection);

    connect(sess, &SimpleTransfer::finished,
            this, [this](const QString &tref, const QString &reason){
                if (reason == "abort") deleteSavedTransfer(tref);
                auto it = m_simple.find(tref);
                if (it != m_simple.end()) {
                    it.value()->deleteLater();
                    m_simple.erase(it);
                }
                restoreAllSaved();
                emit currentSessionChanged();
            }, Qt::QueuedConnection);

    sess->start();
    emit sessionStarted(ref);
    return ref;
}

bool TransferManager::abortSimpleTransfer(const QString &ref)
{
    auto it = m_simple.find(ref);
    if (it == m_simple.end()) return false;
    QMetaObject::invokeMethod(it.value(), "cancel", Qt::QueuedConnection);
    emit currentSessionChanged();
    return true;
}

bool TransferManager::validateSimpleTransfer(const QString &ref)
{ return m_simple.contains(ref); }

bool TransferManager::proceedSimpleAfterApproval(const QString &ref)
{
    if (auto *s = m_simple.value(ref, nullptr)) {
        QMetaObject::invokeMethod(s, "proceedAfterApproval", Qt::QueuedConnection);
        return true;
    }
    return false;
}

QObject* TransferManager::getSimpleSession(const QString &ref) const
{
    auto it = m_simple.find(ref);
    if (it == m_simple.end()) return nullptr;
    QQmlEngine::setObjectOwnership(it.value(), QQmlEngine::CppOwnership);
    return static_cast<QObject*>(it.value());
}

QString TransferManager::getSimpleDetails(const QString &ref) const
{
    if (auto *s = m_simple.value(ref, nullptr))
        return s->getTransferDetailsJson();
    return {};
}

QStringList TransferManager::simpleSessions() const {
    return m_simple.keys();
}


bool TransferManager::declineIncomingTransfer(const QString &transferRef)
{
    if (!m_acct) return false;

    auto incomingIt = m_incoming.find(transferRef);
    if (incomingIt != m_incoming.end()) {
        if (incomingIt.value()) {
            QMetaObject::invokeMethod(incomingIt.value(), "decline", Qt::QueuedConnection);
        }
    }

    QJsonDocument doc = QJsonDocument::fromJson(m_acct->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();
    QJsonObject monero= root.value("monero").toObject();
    QJsonArray  wallets= monero.value("wallets").toArray();

    bool updated = false;
    for (int i = 0; i < wallets.size(); ++i) {
        QJsonObject w = wallets[i].toObject();
        QJsonObject transfers = w.value("transfers").toObject();

        if (!transfers.contains(transferRef)) {
            wallets[i] = w;
            continue;
        }

        QJsonObject transfer = transfers.value(transferRef).toObject();

        const QString currentStage = transfer.value("stage").toString();
        if (isTerminalStage(currentStage)) {
            wallets[i] = w;
            continue;
        }

        transfer["stage"] = "DECLINED";
        transfer["status"] = "Transfer declined by user";

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        transfer["declined_at"] = now;

        QJsonObject peersObj = transfer.value("peers").toObject();
        QString targetOnion = normOnion(transfer.value("my_onion").toString());
        if (targetOnion.isEmpty()) {
            QStringList ours = m_acct ? m_acct->torOnions() : QStringList{};
            for (QString &o : ours) o = normOnion(o);
            for (auto it = peersObj.begin(); it != peersObj.end() && targetOnion.isEmpty(); ++it) {
                const QString peerKey = normOnion(it.key());
                if (ours.contains(peerKey, Qt::CaseInsensitive))
                    targetOnion = peerKey;
            }
        }

        if (!targetOnion.isEmpty() && peersObj.contains(targetOnion)) {
            QJsonArray peerArray = peersObj.value(targetOnion).toArray();
            if (peerArray.size() >= 4) {
                peerArray[0] = "DECLINED";
                peerArray[1] = true;
                peerArray[2] = false;
                peerArray[3] = "Declined";
                peersObj[targetOnion] = peerArray;
            }
        }
        transfer["peers"] = peersObj;

        transfers[transferRef] = transfer;
        w["transfers"] = transfers;
        wallets[i] = w;
        updated = true;
        break;
    }

    if (!updated) return false;

    monero["wallets"] = wallets;
    root["monero"] = monero;

    (void)m_acct->saveAccountData(
        QJsonDocument(root).toJson(QJsonDocument::Compact));

    restoreAllSaved();
    emit currentSessionChanged();

    return true;
}

void TransferManager::reset()
{

    for (auto it = m_outgoing.begin(); it != m_outgoing.end(); ++it) {
        if (it.value()) QMetaObject::invokeMethod(it.value(), "cancel", Qt::QueuedConnection);
    }

    for (auto it = m_incoming.begin(); it != m_incoming.end(); ++it) {
        if (it.value()) QMetaObject::invokeMethod(it.value(), "stop", Qt::QueuedConnection);

    }
    for (auto it = m_trackers.begin(); it != m_trackers.end(); ++it) {
        if (it.value()) QMetaObject::invokeMethod(it.value(), "stop", Qt::QueuedConnection);

    }
    for (auto it = m_simple.begin(); it != m_simple.end(); ++it) {
        if (it.value()) QMetaObject::invokeMethod(it.value(), "cancel", Qt::QueuedConnection);

    }

    m_outgoing.clear();
    m_incoming.clear();
    m_trackers.clear();
    m_simple.clear();
    m_pendingIncomingMap.clear();
    m_allSavedMap.clear();

    if (m_msigImport) {
        m_msigImport->stop();
        m_msigImport->clearActivity();
    }

    emit currentSessionChanged();
}



