#include "win_compat.h"
#include "simpletransfer.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QMetaObject>
#include <QSet>
#include <QDebug>
#include <QMetaEnum>

#include "multiwalletcontroller.h"
#include "wallet.h"
#include "accountmanager.h"


namespace {
static QString stageName(SimpleTransfer::Stage s) {
    using S = SimpleTransfer::Stage;
    switch (s) {
    case S::INIT:               return "INIT";
    case S::CREATING_TRANSFER:  return "CREATING_TRANSFER";
    case S::VALIDATING:         return "VALIDATING";
    case S::APPROVING:          return "APPROVING";
    case S::SUBMITTING:         return "SUBMITTING";
    case S::CHECKING_STATUS:    return "CHECKING_STATUS";
    case S::COMPLETE:           return "COMPLETE";
    case S::ERROR:              return "ERROR";
    }
    return {};
}

static QList<SimpleTransfer::Destination> toAtomicDests(const QList<SimpleTransfer::Destination> &in) {
    return in;
}

static QJsonArray toJsonRecipients(const QList<SimpleTransfer::Destination> &d) {
    QJsonArray arr;
    for (const auto &x : d) {
        arr.push_back(QJsonObject{
            {"address", x.address},
            {"amount",  QString::number(x.amount)}
        });
    }
    return arr;
}
}

SimpleTransfer::SimpleTransfer(MultiWalletController *wm,
                               AccountManager        *acct,
                               const QString         &transferRef,
                               const QString         &walletRef,
                               const QList<Destination> &destsAtomic,
                               int feePriority,
                               const QList<int> &feeSplitIndices,
                               bool inspectBeforeSending,
                               QObject *parent)
    : QObject(parent)
    , m_wm(wm)
    , m_acct(acct)
    , m_transferRef(transferRef)
    , m_walletRef(walletRef)
    , m_walletName(resolveWalletName(walletRef))
    , m_destinationsInit(destsAtomic)
    , m_destinations(destsAtomic)
    , m_feePriority(feePriority)
    , m_feeSplit(feeSplitIndices)
    , m_inspect(inspectBeforeSending)
{
    Q_ASSERT(wm && acct);
}

SimpleTransfer::~SimpleTransfer()
{
    stop("dtor");
}

void SimpleTransfer::start()
{
    if (m_stage != Stage::INIT) return;
    Wallet *w = walletByRef(m_walletRef);
    if (!w) { setStage(Stage::ERROR, "Wallet not found"); return; }

    // connect once
    connect(w, &Wallet::simpleTransferPrepared,
            this, &SimpleTransfer::onPrepared, Qt::QueuedConnection);
    connect(w, &Wallet::simpleDescribeResult,
            this, &SimpleTransfer::onDescribe, Qt::QueuedConnection);
    connect(w, &Wallet::simpleSubmitResult,
            this, &SimpleTransfer::onCommit, Qt::QueuedConnection);

    setStage(Stage::CREATING_TRANSFER, QStringLiteral("Building transfer…"));
    w->prepareSimpleTransfer(m_transferRef,
                             destinationsToVariant(m_destinationsInit),
                             m_feePriority,
                             m_feeSplit);

}

void SimpleTransfer::cancel()
{
    Wallet *w = walletByRef(m_walletRef);
    if (w) w->discardPreparedSimpleTransfer(m_transferRef);
    setStage(Stage::ERROR, "aborted");

    stop("abort");
}

void SimpleTransfer::proceedAfterApproval()
{
    if (m_stage != Stage::APPROVING) return;
    Wallet *w = walletByRef(m_walletRef);
    if (!w) { setStage(Stage::ERROR, "Wallet not found"); return; }
    setStage(Stage::SUBMITTING, QStringLiteral("Broadcasting…"));
    w->commitPreparedSimpleTransfer(m_transferRef);
}

void SimpleTransfer::onPrepared(QString ref, quint64 feeAtomic, QVariantList normalizedDests, QString warning)
{
    if (ref != m_transferRef || m_stop) return;

    m_feeAtomic = feeAtomic;

    m_destinations.clear();
    for (const QVariant &v : normalizedDests) {
        const QVariantMap m = v.toMap();
        SimpleTransfer::Destination d;
        d.address = m.value("address").toString();
        d.amount  = static_cast<quint64>(m.value("amount").toULongLong());
        m_destinations.push_back(d);
    }

    if (!warning.isEmpty())
        emit statusChanged(warning);

    Wallet *w = walletByRef(m_walletRef);
    if (!w) { setStage(Stage::ERROR, "Wallet not found"); return; }
    setStage(Stage::VALIDATING, QStringLiteral("Describing transfer…"));
    w->describePreparedSimpleTransfer(m_transferRef);
}

void SimpleTransfer::onDescribe(QString ref, QVariant detailsVar)
{
    if (ref != m_transferRef || m_stop) return;

    QJsonObject details;
    if (detailsVar.metaType().id() == QMetaType::QJsonObject)
        details = detailsVar.toJsonObject();
    else if (detailsVar.canConvert<QVariantMap>())
        details = QJsonObject::fromVariantMap(detailsVar.toMap());
    else if (detailsVar.canConvert<QString>()) {
        QJsonParseError pe{};
        auto doc = QJsonDocument::fromJson(detailsVar.toString().toUtf8(), &pe);
        if (pe.error == QJsonParseError::NoError && doc.isObject())
            details = doc.object();
    }

    const QJsonArray descArr = details.value(QStringLiteral("desc")).toArray();
    if (descArr.isEmpty()) {
        setStage(Stage::ERROR, "Invalid describe payload");

        return;
    }
    const QJsonObject first = descArr.first().toObject();
    m_transferDescription = QJsonObject{
        { "recipients",  first.value("recipients").toArray() },
        { "payment_id",  first.value("payment_id").toString() },
        { "fee",         first.value("fee") },
        { "unlock_time", first.value("unlock_time") }
    };
    m_paymentId = first.value("payment_id").toString();


    quint64 sum = 0;
    for (const auto &d : m_destinations) sum += d.amount;
    const bool feeHigh = (sum > 0 && (double(m_feeAtomic)/double(sum)) > 0.005);
    if (m_inspect  || feeHigh && m_acct->inspectGuard() ) m_inspect  = true;

    if (m_inspect) {
        setStage(Stage::APPROVING, QStringLiteral("Awaiting your approval"));

    } else {
        setStage(Stage::APPROVING, QStringLiteral("Awaiting your approval"));
        proceedAfterApproval();
    }
}

void SimpleTransfer::onCommit(QString ref, bool ok, QString txidsOrError)
{
    if (ref != m_transferRef || m_stop) return;

    if (!ok) {
        setStage(Stage::ERROR, QStringLiteral("Broadcast failed: %1").arg(txidsOrError));

        return;
    }

    m_txId = txidsOrError;
    setStage(Stage::COMPLETE, QStringLiteral("Submitted"));
    saveToAccount();

    emit submittedSuccessfully(m_transferRef);
    stop("success");
}

void SimpleTransfer::setStage(Stage s, const QString &msg)
{
    m_stage = s;
    emit stageChanged(stageName(m_stage));
    if (!msg.isEmpty()) emit statusChanged(msg);
}

void SimpleTransfer::stop(const QString &reason)
{
    if (m_stop) return;
    m_stop = true;
    emit finished(m_transferRef, reason);
}

void SimpleTransfer::saveToAccount()
{
    if (!m_acct) return;

    QJsonDocument doc = QJsonDocument::fromJson(m_acct->loadAccountData().toUtf8());
    QJsonObject root  = doc.object();

    QJsonObject mon   = root.value("monero").toObject();
    QJsonArray wallets= mon.value("wallets").toArray();

    for (int i=0;i<wallets.size();++i) {
        QJsonObject w = wallets[i].toObject();
        const bool match =
            (w.value("reference").toString()==m_walletRef) ||
            (w.value("name").toString()==m_walletName);
        if (!match) continue;

        QJsonObject transfers = w.value("transfers").toObject();
        QJsonObject entry     = transfers.value(m_transferRef).toObject();

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (!entry.contains("created_at")) entry["created_at"] = now;

        entry["type"]        = QStringLiteral("SIMPLE");
        entry["wallet_name"] = m_walletName;
        entry["wallet_ref"]  = m_walletRef;
        entry["destinations"]= toJsonRecipients(m_destinations);
        entry["peers"]       = QJsonObject{};
        entry["stage"]       = stageName(m_stage);
        entry["status"]      = QStringLiteral("updated");
        entry["tx_id"]       = m_txId.isEmpty() ? QString("pending") : m_txId;
        entry["transfer_description"] = m_transferDescription;

        if (m_stage == Stage::SUBMITTING || m_stage == Stage::CHECKING_STATUS) {
            if (!entry.contains("submitted_at")) entry["submitted_at"] = now;
        }

        transfers.insert(m_transferRef, entry);
        w["transfers"] = transfers;
        wallets[i] = w;
        break;
    }

    mon["wallets"] = wallets;
    root["monero"] = mon;
    (void)m_acct->saveAccountData(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QString SimpleTransfer::getTransferDetailsJson() const
{
    QJsonObject out{
        {"wallet_name", m_walletName},
        {"ref",        m_walletRef},
        {"stage",      stageName(m_stage)},
        {"fee",        QString::number(m_feeAtomic)},
        {"tx_id",      m_txId},
        {"payment_id", m_paymentId},
        {"destinations", toJsonRecipients(m_destinations)}
    };
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
}

Wallet* SimpleTransfer::walletByRef(const QString &ref) const
{
    Q_UNUSED(ref);
    if (m_walletName.isEmpty()) return nullptr;
    return qobject_cast<Wallet*>(m_wm->walletInstance(m_walletName));
}

QString SimpleTransfer::walletNameForRef(const QString &ref) const
{
    Q_UNUSED(ref);
    return m_walletName;
}

QVariantList SimpleTransfer::destinationsToVariant(const QList<Destination> &d)
{
    QVariantList lst;
    for (const auto &x : d) {
        QVariantMap m;
        m["address"] = x.address;
        m["amount"]  = static_cast<qulonglong>(x.amount);
        lst.push_back(m);
    }
    return lst;
}


QString SimpleTransfer::stageName(SimpleTransfer::Stage s) const
{
    const int idx = staticMetaObject.indexOfEnumerator("Stage");
    if (idx < 0) return {};
    const QMetaEnum me = staticMetaObject.enumerator(idx);
    const char *key = me.valueToKey(static_cast<int>(s));
    return key ? QString::fromLatin1(key) : QString();
}


QString SimpleTransfer::resolveWalletName(const QString &walletRef) const
{

    if (m_acct) {
        const auto root = QJsonDocument::fromJson(m_acct->loadAccountData().toUtf8()).object();
        const auto mon  = root.value(QStringLiteral("monero")).toObject();
        const auto arr  = mon.value(QStringLiteral("wallets")).toArray();
        for (const auto &wv : arr) {
            const auto w = wv.toObject();
            if (w.value(QStringLiteral("reference")).toString() == walletRef)
                return w.value(QStringLiteral("name")).toString();
        }
    }
    return walletRef;
}
