#include "win_compat.h"
#include "wallet.h"
#include <wallet/wallet2.h>
#include <cryptonote_basic/cryptonote_basic.h>
#include <cryptonote_basic/cryptonote_basic_impl.h>
#include <cryptonote_config.h>
#include <cryptonote_basic/cryptonote_format_utils.h>

#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/tx_extra.h"

#include <QMetaObject>
#include <QDebug>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <QRegularExpression>
#include <mnemonics/electrum-words.h>
#include <crypto/crypto.h>

#include <set>
#include <list>
#include <tuple>
#include <boost/optional/optional.hpp>
#include <net/http_client.h>



using namespace tools;
using namespace cryptonote;


static inline std::string toString(const QString &q) { return q.toStdString(); }

static inline qulonglong sum_dests(const std::vector<cryptonote::tx_destination_entry> &d) {
    qulonglong s = 0; for (const auto &e : d) s += static_cast<qulonglong>(e.amount); return s; }


static QVariantMap toVariant(const tools::wallet2::transfer_details &td)
{
    QVariantMap m;
    m[QStringLiteral("txid")]        = QString::fromStdString(epee::string_tools::pod_to_hex(td.m_txid));
    m[QStringLiteral("amount")]      = static_cast<qulonglong>(td.m_amount);
    m[QStringLiteral("height")]      = static_cast<qulonglong>(td.m_block_height);
    m[QStringLiteral("direction")]   = td.m_spent ? QStringLiteral("out") : QStringLiteral("in");
    m[QStringLiteral("subaddr_major")] = static_cast<int>(td.m_subaddr_index.major);
    m[QStringLiteral("subaddr_minor")] = static_cast<int>(td.m_subaddr_index.minor);
    m[QStringLiteral("key_image")]   = td.m_key_image_known ? QString::fromStdString(epee::string_tools::pod_to_hex(td.m_key_image)) : QString();
    m[QStringLiteral("frozen")]      = td.m_frozen;


#ifdef HAS_WALLET2_TRANSFER_DETAILS_TIMESTAMP
    m[QStringLiteral("timestamp")] = static_cast<qulonglong>(td.m_timestamp);
#else
    m[QStringLiteral("timestamp")] = static_cast<qulonglong>(0);
#endif

    return m;
}


namespace {

QByteArray tryB64(const QByteArray &in) {
    QByteArray dec = QByteArray::fromBase64(in, QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    if (!dec.isEmpty()) return dec;
    dec = QByteArray::fromBase64(in);
    return dec.isEmpty() ? in : dec;
}


bool makeDest(const QString &addrStr, quint64 amount,
              cryptonote::network_type net,
              cryptonote::tx_destination_entry &out,
              QString *why = nullptr)
{
    cryptonote::address_parse_info info;
    if (!cryptonote::get_account_address_from_str(info, net, addrStr.toStdString())) {
        if (why) *why = QStringLiteral("invalid-address: %1").arg(addrStr);
        return false;
    }
    if (amount == 0) {
        if (why) *why = QStringLiteral("zero-amount");
        return false;
    }
    out = cryptonote::tx_destination_entry(amount, info.address, info.is_subaddress);
    return true;
}


void summarizeTxs(const std::vector<tools::wallet2::pending_tx> &ptx,
                  cryptonote::network_type net,
                  quint64 &feeSum,
                  QVariantList &normalizedDests)
{
    feeSum = 0;
    QMap<QString, qulonglong> byAddr;
    for (const auto &p : ptx) {
        feeSum += p.fee;
        for (const auto &d : p.dests) {
            const QString a = QString::fromStdString(cryptonote::get_account_address_as_str(net, d.is_subaddress, d.addr));
            byAddr[a] += static_cast<qulonglong>(d.amount);
        }
    }
    for (auto it = byAddr.cbegin(); it != byAddr.cend(); ++it)
        normalizedDests.append(QVariantMap{{"address", it.key()}, {"amount", static_cast<qulonglong>(it.value())}});
}
}

static inline QString subaddr_label(tools::wallet2 *w, uint32_t major, uint32_t minor) {
    try { return QString::fromStdString(w->get_subaddress_label({major, minor})); }
    catch (...) { return {}; }
}
static inline QString subaddr_str(tools::wallet2 *w, cryptonote::network_type net, uint32_t major, uint32_t minor) {
    try { return QString::fromStdString(w->get_subaddress_as_str({major, minor})); }
    catch (...) { return {}; }
}


Wallet::Wallet(QObject *parent) : QObject(parent)
{
    connect(&m_syncTimer, &QTimer::timeout, this, &Wallet::refreshAsync);
}

Wallet::~Wallet()
{
    stopSync();
    close();
}


void Wallet::setDaemonAddress(const QString &addr)
{
    if (addr == m_daemonAddress) return;
    m_daemonAddress = addr;
    emit walletChanged();
}

void Wallet::createNew(const QString &path,
                       const QString &password,
                       const QString &language,
                       const QString &nettype,
                       quint64       kdfRounds)
{
    enqueue(QStringLiteral("createNew"), [=]() {
        try {
            const std::string pathStr      = path.toStdString();
            epee::wipeable_string passWipe = epee::wipeable_string{password.toStdString().c_str()};

            if (nettype== "testnet"){
                m_netType =  network_type::TESTNET;
            }
            else if (nettype == "stagenet") {
                m_netType =  network_type::STAGENET;
            }
            else {m_netType =  network_type::MAINNET;
            }

            m_wallet.reset(new wallet2(m_netType, kdfRounds, /*unattended=*/true));

            std::string proxyStr;
            if (m_useProxy && !m_proxyHost.isEmpty() && m_proxyPort > 0) {
                proxyStr = QString("%1:%2").arg(m_proxyHost).arg(m_proxyPort).toStdString();
            }

            crypto::secret_key nullSkey{};
            {
                QMutexLocker locker(&m_mutex);
                m_wallet->generate(pathStr, passWipe, nullSkey, false, true, false);
                m_wallet->set_seed_language(language.toStdString());

                m_wallet->init(toString(m_daemonAddress),
                               boost::none,  // daemon_login
                               proxyStr,     // proxy_address
                               0,           // upper_transaction_weight_limit
                               false);
            }

            QMetaObject::invokeMethod(this, [this] {
                emit walletCreated();
                emit walletChanged();
            }, Qt::QueuedConnection);
        }
        catch (const std::exception &e) {
            QMetaObject::invokeMethod(this, [=]() {
                emit errorOccurred(QString::fromUtf8(e.what()));
            }, Qt::QueuedConnection);
        }
    });
}

void Wallet::open(const QString &path,
                  const QString &password,
                  const QString &nettype,
                  quint64       kdfRounds)
{
    enqueue(QStringLiteral("open"), [=]() {
        try {
            if (nettype== "testnet"){
                m_netType =  network_type::TESTNET;
            }
            else if (nettype == "stagenet") {
                m_netType =  network_type::STAGENET;
            }
            else {m_netType =  network_type::MAINNET;
            }


            m_wallet.reset(new wallet2(m_netType, kdfRounds, /*unattended=*/true));

            std::string proxyStr;
            if (m_useProxy && !m_proxyHost.isEmpty() && m_proxyPort > 0) {
                proxyStr = QString("%1:%2").arg(m_proxyHost).arg(m_proxyPort).toStdString();
            }

            qDebug() << "proxyStr:" << proxyStr;

            {
                QMutexLocker locker(&m_mutex);
                m_wallet->load(toString(path), toString(password));

                m_wallet->init(toString(m_daemonAddress),
                               boost::none,  // daemon_login
                               proxyStr,     // proxy_address
                               0,           // upper_transaction_weight_limit
                               false); // ssl_options
            }

            QMetaObject::invokeMethod(this, [=]() {
                m_addressCache = QString::fromStdString(
                    m_wallet->get_account().get_public_address_str(m_netType));
                emit walletOpened();
                emit walletChanged();
                // qDebug().noquote() << "managed to open wallet";
            }, Qt::QueuedConnection);
        }
        catch (const std::exception &e) {
            QMetaObject::invokeMethod(this, [=]() {
                qDebug().noquote() << QStringLiteral("fail open: %1").arg(e.what());
                emit errorOccurred(QString::fromUtf8(e.what()));
            }, Qt::QueuedConnection);
        }
    });
}

void Wallet::close()
{
    enqueue(QStringLiteral("close"), [=]() {
        if (!m_wallet) return;
        try {
            {
                QMutexLocker locker(&m_mutex);
                m_wallet->stop();
                m_wallet->store();
            }
            m_wallet.reset();
            QMetaObject::invokeMethod(this, &Wallet::walletClosed, Qt::QueuedConnection);
            QMetaObject::invokeMethod(this, &Wallet::walletFullyClosed, Qt::QueuedConnection);
        }
        catch (...) {
            QMetaObject::invokeMethod(this, &Wallet::walletFullyClosed, Qt::QueuedConnection);
        }
    });
}

void Wallet::save()
{
    enqueue(QStringLiteral("save"), [=]() {
        if (!m_wallet) return;
        try {
            QMutexLocker locker(&m_mutex);
            m_wallet->store();
            QMetaObject::invokeMethod(this, &Wallet::walletSaved, Qt::QueuedConnection);
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
    });
}

void Wallet::restoreFromSeed(const QString &path,
                             const QString &password,
                             const QString &seedWords,
                             quint64 restoreHeight,
                             const QString &language,
                             const QString &nettype,
                             quint64 kdfRounds,
                             bool isMultisig)
{
    enqueue(QStringLiteral("restoreFromSeed"), [=] {
        try {

            if (nettype== "testnet"){
                m_netType =  network_type::TESTNET;
            }
            else if (nettype == "stagenet") {
                m_netType =  network_type::STAGENET;
            }
            else {m_netType =  network_type::MAINNET;
            }

            m_wallet.reset(new wallet2(m_netType, kdfRounds, /*unattended=*/true));

            std::string proxyStr;
            if (m_useProxy && !m_proxyHost.isEmpty() && m_proxyPort > 0) {
                proxyStr = QString("%1:%2").arg(m_proxyHost).arg(m_proxyPort).toStdString();
            }

            epee::wipeable_string pwd(password.toStdString().c_str());
            if (isMultisig) {


                epee::wipeable_string multisig_data;

                multisig_data.resize(seedWords.toStdString().size() / 2);


                if (!epee::from_hex::to_buffer(epee::to_mut_byte_span(multisig_data), seedWords.toStdString())) {

                    throw std::runtime_error("Multisig seed is not a valid hexadecimal string");
                }

                m_wallet->set_seed_language(language.isEmpty() ? "English" : language.toStdString());

                m_wallet->generate(path.toStdString(), pwd, multisig_data, /*recover=*/false);

                m_wallet->enable_multisig(true);

            } else {

                crypto::secret_key recoveryKey;
                std::string seedLang;
                if (!crypto::ElectrumWords::words_to_bytes(seedWords.toStdString(), recoveryKey, seedLang)) {
                    throw std::runtime_error("Electrum seed failed verification");
                }

                if (!language.isEmpty()) {
                    seedLang = language.toStdString();
                } else if (seedLang == crypto::ElectrumWords::old_language_name && language.isEmpty()) {
                    throw std::runtime_error("Wallet uses old seed language; specify a new seed language");
                }

                m_wallet->set_seed_language(seedLang);
                m_wallet->generate(path.toStdString(), pwd, recoveryKey, /*recover=*/true, /*two_random=*/false, /*create_address_file=*/false);
            }


            m_wallet->set_refresh_from_block_height(restoreHeight);


            m_wallet->init(toString(m_daemonAddress),
                           boost::none,  // daemon_login
                           proxyStr,     // proxy_address
                           0,           // upper_transaction_weight_limit
                           false);

            QMetaObject::invokeMethod(this, [=] {
                m_addressCache = QString::fromStdString(
                    m_wallet->get_account().get_public_address_str(m_netType));
                emit walletRestored();
                emit walletChanged();
                qDebug().noquote() << "Managed to restore wallet" << (isMultisig ? "(multisig)" : "(standard)");
            }, Qt::QueuedConnection);
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
    });
}


void Wallet::getPrimarySeed()
{
    enqueue(QStringLiteral("getPrimarySeed"), [=] {
        QString seed;
        try {
            if (!m_wallet) throw std::runtime_error("wallet not loaded");

            QMutexLocker lock(&m_mutex);

            epee::wipeable_string words;
            if (m_wallet->get_multisig_seed(words)) {
                seed = QString::fromStdString(
                    std::string(words.data(), words.size()));
            } else if (m_wallet->is_deterministic() &&
                       m_wallet->get_seed(words)) {
                seed = QString::fromStdString(
                    std::string(words.data(), words.size()));
            }
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(
            this, [=]{ emit primarySeedReady(seed); },
            Qt::QueuedConnection);
    });
}

void Wallet::startSync(int intervalSeconds)
{
    if (intervalSeconds < 5) intervalSeconds = 5;
    if (!m_syncTimer.isActive()) m_syncTimer.start(intervalSeconds * 1000);
    emit activeSyncTimerChanged();
    refreshAsync();
}

void Wallet::stopSync()
{
    m_syncTimer.stop();
    emit activeSyncTimerChanged();
}

void Wallet::refreshAsync()
{
    enqueue(QStringLiteral("refresh"), [=]() {
        if (!m_wallet) return;

        quint64 current  = 0;
        quint64 target   = 0;
        quint64 bal      = 0;
        quint64 unlocked = 0;
        bool needsImport=false;

        try {
            {
                QMutexLocker locker(&m_mutex);
                m_wallet->refresh(/*trusted=*/false);
                current  = m_wallet->get_blockchain_current_height();

                std::string err;
                target  = m_wallet->get_daemon_blockchain_height(err);

                emit syncProgress(current, target);
                bal      = m_wallet->balance(0, false);
                unlocked = m_wallet->unlocked_balance(0, false);
                const auto st = m_wallet->get_multisig_status();
                needsImport = st.multisig_is_active && m_wallet->has_multisig_partial_key_images();
            }

            QMetaObject::invokeMethod(this, [=]() {
                m_balanceCache  = bal;
                m_unlockedCache = unlocked;
                m_walletHeightCache = current;
                m_daemonHeightCache = target;

                if (m_hasMsigPartialKeyImages != needsImport) {
                    m_hasMsigPartialKeyImages = needsImport;
                }
                emit walletChanged();
                emit syncProgress(current, target);
            }, Qt::QueuedConnection);
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
    });
}

void Wallet::getBalance()
{
    enqueue(QStringLiteral("getBalance"), [=]() {
        if (!m_wallet) return;

        quint64 bal=0, unlocked=0; bool needsImport=false;
        try {
            QMutexLocker locker(&m_mutex);
            bal      = m_wallet->balance_all(/*strict=*/false);
            unlocked = m_wallet->unlocked_balance_all(/*strict=*/false, /*blocks_to_unlock*/ nullptr, /*time_to_unlock*/ nullptr);
            const auto st = m_wallet->get_multisig_status();
            needsImport = st.multisig_is_active && m_wallet->has_multisig_partial_key_images();
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;

        }

        QMetaObject::invokeMethod(this, [=]() {
            m_balanceCache  = bal;
            m_unlockedCache = unlocked;

            if (m_hasMsigPartialKeyImages != needsImport) {
                m_hasMsigPartialKeyImages = needsImport;
                emit walletChanged();
            }

            emit balanceReady(bal, unlocked, needsImport);
        }, Qt::QueuedConnection);
    });
}

void Wallet::getHeight()
{
    enqueue(QStringLiteral("getHeight"), [=]() {
        if (!m_wallet) return;
        quint64 h = 0;
        try {
            QMutexLocker locker(&m_mutex);
            h = m_wallet->get_blockchain_current_height();
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(this, [=]() { emit heightReady(h); }, Qt::QueuedConnection);
    });
}

void Wallet::setRefreshHeight(uint64_t h)
{
    enqueue(QStringLiteral("setRefreshHeight"), [=]() {
        if (!m_wallet) return;
        try {
            QMutexLocker locker(&m_mutex);
            m_wallet->set_refresh_from_block_height(h);
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(this, [=]() { emit restoreHeightSet(); }, Qt::QueuedConnection);
    });
}


void Wallet::changePassword(const QString &oldPass, const QString &newPass)
{
    enqueue(QStringLiteral("changePassword"), [=]() {
        if (!m_wallet) return;
        try {
            m_wallet->change_password(m_wallet->get_wallet_file(),
                                      epee::wipeable_string(oldPass.toStdString().c_str()),
                                      epee::wipeable_string(newPass.toStdString().c_str()));
            QMetaObject::invokeMethod(this, [=]() { emit passwordChanged(true); }, Qt::QueuedConnection);
        } catch (const std::exception &e) {

            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
                emit passwordChanged(false);
            }, Qt::QueuedConnection);
            return;

        }
    });
}

void Wallet::firstKexMsg()
{
    enqueue(QStringLiteral("firstKex"), [=]{
        QByteArray blob;
        try {
            QMutexLocker l(&m_mutex);
            blob = QByteArray::fromStdString(m_wallet->get_multisig_first_kex_msg());
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(this, [=]{ emit firstKexMsgReady(blob); }, Qt::QueuedConnection);
    });
}

void Wallet::prepareMultisigInfo(QString operation_caller)
{
    qDebug().noquote() << "[Wallet] preparing multisig";
    enqueue(QStringLiteral("prepareMultisig"), [=]() {
        if (!m_wallet) return;
        QByteArray info;
        try {
            QMutexLocker locker(&m_mutex);
            cryptonote::blobdata blob = m_wallet->export_multisig();
            info = QByteArray::fromStdString(blob);
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg, operation_caller]() {
                emit errorOccurred(msg, operation_caller);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [=]() { emit multisigInfoPrepared(info, operation_caller); }, Qt::QueuedConnection);
    });
}

void Wallet::makeMultisig(const QList<QByteArray> &kexMsgs,
                          quint32 threshold,
                          const QString &password)
{
    enqueue(QStringLiteral("makeMultisig"), [=]() {
        if (!m_wallet) return;
        QByteArray result;
        try {

            std::vector<std::string> msgs;
            for (const QByteArray &ba : kexMsgs)
                msgs.emplace_back(ba.constData(), ba.size());
            QMutexLocker locker(&m_mutex);
            std::string ret = m_wallet->make_multisig(epee::wipeable_string(password.toStdString().c_str()),
                                                      msgs, threshold);
            result = QByteArray::fromStdString(ret);
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(this, [=]() { emit makeMultisigDone(result); }, Qt::QueuedConnection);
    });
}

void Wallet::exchangeMultisigKeys(const QList<QByteArray> &kexMsgs, const QString &password)
{
    enqueue(QStringLiteral("exchangeMultisigKeys"), [=]() {
        if (!m_wallet) return;
        QByteArray result;
        try {
            std::vector<std::string> msgs;
            for (const QByteArray &ba : kexMsgs) msgs.emplace_back(ba.constData(), ba.size());
            QMutexLocker locker(&m_mutex);
            std::string ret = m_wallet->exchange_multisig_keys(epee::wipeable_string(password.toStdString().c_str()), msgs, false);
            result = QByteArray::fromStdString(ret);
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(this, [=]() { emit exchangeMultisigKeysDone(result); }, Qt::QueuedConnection);
    });
}


void Wallet::isMultisig()
{
    enqueue(QStringLiteral("isMultisig"), [=]() {
        bool ready = false;
        try {
            QMutexLocker l(&m_mutex);
            if (m_wallet) {
                const auto st = m_wallet->get_multisig_status();
                ready = st.multisig_is_active && st.is_ready;
            }
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;


        }
        QMetaObject::invokeMethod(this, [=]() { emit isMultisigReady(ready); }, Qt::QueuedConnection);
    });
}

void Wallet::getMultisigParams()
{
    enqueue(QStringLiteral("multisigParams"), [=]() {
        quint32 threshold = 0, total = 0;
        try {
            QMutexLocker l(&m_mutex);
            if (m_wallet) {
                const auto st = m_wallet->get_multisig_status();
                if (st.multisig_is_active) { threshold = st.threshold; total = st.total; }
            }
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(this, [=]() { emit multisigParamsReady(threshold, total); }, Qt::QueuedConnection);
    });
}

void Wallet::getAddress()
{
    enqueue(QStringLiteral("getAddress"), [=]() {
        QString addr;
        try {
            QMutexLocker locker(&m_mutex);
            addr = QString::fromStdString(m_wallet->get_address_as_str());
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;

        }
        QMetaObject::invokeMethod(this, [=]() { emit addressReady(addr); }, Qt::QueuedConnection);
    });
}

void Wallet::seedMulti()
{
    enqueue(QStringLiteral("seedMulti"), [=]() {
        QString seed;
        try {
            epee::wipeable_string words;
            QMutexLocker locker(&m_mutex);
            if (m_wallet->get_multisig_seed(words))
                seed = QString::fromStdString(std::string(words.data(), words.size()));
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;

        }
        QMetaObject::invokeMethod(this, [=]() { emit seedMultiReady(seed); }, Qt::QueuedConnection);
    });
}

void Wallet::importMultisigInfos(const QList<QByteArray> &infos,QString operation_caller)
{
    qDebug().noquote() << "[msig] importMultisigInfos() called; items=" << infos.size();

    enqueue(QStringLiteral("importMultisigInfos"), [=]() {
        if (!m_wallet) {
            qDebug().noquote() << "[msig][abort] m_wallet is null";
            return;
        }

        int  totalImported = 0;
        bool needsRescan   = false;

        try {

            auto decodeOne = [](const QByteArray &in) -> QByteArray {
                bool looksBinary = false;
                for (unsigned char c : in) {
                    if (c == 0 || c > 0x7F) { looksBinary = true; break; }
                }
                if (looksBinary) return in;

                static const QRegularExpression kHexRe(QStringLiteral("^[0-9A-Fa-f]+$"));
                if ((in.size() % 2) == 0 && kHexRe.match(QString::fromLatin1(in)).hasMatch()) {
                    QByteArray raw = QByteArray::fromHex(in);
                    if (!raw.isEmpty() || in.isEmpty()) return raw;
                }

                QByteArray raw = QByteArray::fromBase64(in, QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
                if (raw.isEmpty()) raw = QByteArray::fromBase64(in);
                if (!raw.isEmpty()) return raw;

                return in;
            };

            QMutexLocker locker(&m_mutex);

            const auto st = m_wallet->get_multisig_status();

            // qDebug().noquote() << "[msig] wallet status:"
            //                    << "active="    << (st.multisig_is_active ? "true" : "false")
            //                    << "ready="     << (st.is_ready ? "true" : "false")
            //                    << "threshold=" << st.threshold
            //                    << "total="     << st.total;

            if (!st.multisig_is_active || !st.is_ready)
                throw std::runtime_error("wallet is not ready multisig");

            if (int(infos.size()) + 1 < int(st.threshold)) {
                // qDebug().noquote() << "[msig][warn] not enough multisig infos yet:"
                //                    << "haveFromPeers=" << infos.size()
                //                    << "needAtLeast="  << (st.threshold - 1);
            }

            for (int i = 0; i < infos.size(); ++i) {
                const QByteArray &src = infos.at(i);
                const QByteArray  raw = decodeOne(src);
                // qDebug().noquote() << "[msig] info[" << i << "]:"
                //                    << "in.len="  << src.size()
                //                    << "raw.len=" << raw.size()
                //                    << "raw.hex.prefix=" << QString(raw.left(16).toHex());

                std::vector<cryptonote::blobdata> vec(1);
                vec[0].assign(raw.constData(), raw.constData() + raw.size());

                try {
                    const size_t n = m_wallet->import_multisig(vec);
                    totalImported += static_cast<int>(n);
                    qDebug().noquote() << "[msig] import_multisig[" << i << "] ->" << n;
                } catch (const std::exception &e) {
                    qDebug().noquote() << "[msig][ERROR] import_multisig[" << i << "] failed:" << e.what();
                }
            }

            if (m_wallet->is_trusted_daemon()) {
                try {
                    m_wallet->rescan_spent();
                    qDebug().noquote() << "[msig] rescan_spent ok";
                } catch (const std::exception &e) {
                    qDebug().noquote() << "[msig][warn] rescan_spent failed:" << e.what();
                }
            } else {
                qDebug().noquote() << "[msig][note] daemon untrusted; skipping rescan_spent";
            }

            needsRescan = m_wallet->has_unknown_key_images();

            m_wallet->store();

            const bool needImportAgain = m_wallet->has_multisig_partial_key_images();

            QMetaObject::invokeMethod(this, [=]() {
                if (m_hasMsigPartialKeyImages != needImportAgain) {
                    m_hasMsigPartialKeyImages = needImportAgain;
                    emit walletChanged();
                }
            }, Qt::QueuedConnection);
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg, operation_caller]() {
                emit errorOccurred(msg, operation_caller);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [=]() {
            emit multisigInfosImported(totalImported, needsRescan, operation_caller);
        }, Qt::QueuedConnection);
    });
}

void Wallet::importMultisigInfosBulk(const QList<QByteArray> &infos, QString operation_caller)
{


    enqueue(QStringLiteral("importMultisigInfosBulk"), [=]() {
        if (!m_wallet) {
            return;
        }

        size_t importedCount = 0;
        bool needsRescan     = false;

        try {
            auto decodeOne = [](const QByteArray &in) -> QByteArray {
                bool looksBinary = false;
                for (unsigned char c : in) {
                    if (c == 0 || c > 0x7F) { looksBinary = true; break; }
                }
                if (looksBinary) return in;

                static const QRegularExpression kHexRe(QStringLiteral("^[0-9A-Fa-f]+$"));
                if ((in.size() % 2) == 0 && kHexRe.match(QString::fromLatin1(in)).hasMatch()) {
                    QByteArray raw = QByteArray::fromHex(in);
                    if (!raw.isEmpty() || in.isEmpty()) return raw;
                }

                QByteArray raw = QByteArray::fromBase64(in, QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
                if (raw.isEmpty()) raw = QByteArray::fromBase64(in);
                if (!raw.isEmpty()) return raw;

                return in;
            };

            QMutexLocker locker(&m_mutex);

            const auto st = m_wallet->get_multisig_status();

            // qDebug().noquote() << "[msig] wallet status:"
            //                    << "active="    << (st.multisig_is_active ? "true" : "false")
            //                    << "ready="     << (st.is_ready ? "true" : "false")
            //                    << "threshold=" << st.threshold
            //                    << "total="     << st.total;

            if (!st.multisig_is_active || !st.is_ready)
                throw std::runtime_error("wallet is not ready multisig");

            if (int(infos.size()) + 1 < int(st.threshold)) {
                // qDebug().noquote() << "[msig][warn] not enough multisig infos yet:"
                //                    << "haveFromPeers=" << infos.size()
                //                    << "needAtLeast="  << (st.threshold - 1);

            }

            std::vector<cryptonote::blobdata> vec;
            vec.reserve(std::max<size_t>(1, static_cast<size_t>(infos.size())));

            int added = 0;
            for (int i = 0; i < infos.size(); ++i) {
                const QByteArray &src = infos.at(i);
                const QByteArray  raw = decodeOne(src);

                if (raw.isEmpty()) {
                    continue;
                }

                cryptonote::blobdata b;
                b.assign(raw.constData(), raw.constData() + raw.size());
                vec.emplace_back(std::move(b));
                ++added;

            }

            if (vec.empty()) {
                qDebug().noquote() << "[msig][warn] nothing to import after decoding";
            } else {

                importedCount = m_wallet->import_multisig(vec);

            }

            if (m_wallet->is_trusted_daemon()) {
                try {
                    m_wallet->rescan_spent();
                    qDebug().noquote() << "[msig] rescan_spent ok";
                } catch (const std::exception &e) {
                    qDebug().noquote() << "[msig][warn] rescan_spent failed:" << e.what();
                }
            } else {
                qDebug().noquote() << "[msig][note] daemon untrusted; skipping rescan_spent";
            }

            needsRescan = m_wallet->has_unknown_key_images();

            m_wallet->store();

            const bool needImportAgain = m_wallet->has_multisig_partial_key_images();

            QMetaObject::invokeMethod(this, [=]() {
                if (m_hasMsigPartialKeyImages != needImportAgain) {
                    m_hasMsigPartialKeyImages = needImportAgain;
                    emit walletChanged();
                }
            }, Qt::QueuedConnection);
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg, operation_caller]() {
                emit errorOccurred(msg, operation_caller);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [=]() {
            emit multisigInfosImported(static_cast<int>(importedCount), needsRescan, operation_caller);
        }, Qt::QueuedConnection);
    });
}


void Wallet::createUnsignedMultisigTransfer(QVariantList destinations,
                                            int feePriority,
                                            QList<int> subtractFeeFromIndices,QString operation_caller)
{
    enqueue(QStringLiteral("createUnsignedMultisigTransfer"), [=]() {
        if (!m_wallet) return;

        QByteArray txset;
        quint64 feeAtomic = 0;
        QVariantList normalized;
        QString warning;

        try {

            tools::fee_priority prio;
            uint64_t mixin = 0;
            {
                QMutexLocker l(&m_mutex);
                const auto st = m_wallet->get_multisig_status();
                if (!st.multisig_is_active || !st.is_ready)
                    throw std::runtime_error("wallet is not ready multisig");

                if (m_wallet->has_multisig_partial_key_images()) {
                    qDebug().noquote() << "[msig] need to import multisig info before signing";
                    throw std::runtime_error("need_import_multisig_info_before_signing");
                }

                prio  = m_wallet->adjust_priority(static_cast<uint32_t>(feePriority));
                mixin = m_wallet->adjust_mixin(/*requested*/ 0);
            }

            std::vector<cryptonote::tx_destination_entry> dsts;
            dsts.reserve(static_cast<size_t>(destinations.size()));
            for (const QVariant &v : destinations) {
                const QVariantMap m = v.toMap();
                const QString addr = m.value("address").toString();
                quint64 amount = 0;
                if (m.value("amount").typeId() == QMetaType::Double) {
                    const double xmr = m.value("amount").toDouble();
                    amount = static_cast<quint64>(xmr * 1e12 + 0.5);
                } else {
                    amount = static_cast<quint64>(m.value("amount").toULongLong());
                }

                cryptonote::tx_destination_entry de;
                QString why;
                if (!makeDest(addr, amount, m_netType, de, &why))
                    throw std::runtime_error(QStringLiteral("bad destination (%1)").arg(why).toStdString());
                dsts.emplace_back(std::move(de));
            }

            tools::wallet2::unique_index_container subtractSet;
            for (int idx : subtractFeeFromIndices) {
                if (idx >= 0 && idx < destinations.size())
                    subtractSet.insert(static_cast<uint32_t>(idx));
            }

            {
                QMutexLocker l(&m_mutex);
                prio = m_wallet->adjust_priority(static_cast<uint32_t>(feePriority));
            }

            std::vector<tools::wallet2::pending_tx> ptx;
            {
                QMutexLocker locker(&m_mutex);
                const std::vector<uint8_t> extra;
                const uint32_t subaddr_account = 0;
                const std::set<uint32_t> subaddr_indices;

                ptx = m_wallet->create_transactions_2(
                    dsts, static_cast<size_t>(mixin), prio, extra,
                    subaddr_account, subaddr_indices, subtractSet);

                if (ptx.empty())
                    throw std::runtime_error("create_transactions_2 returned empty");

                if (!m_wallet->sanity_check(ptx, dsts, subtractSet))
                    throw std::runtime_error("sanity_check failed");

                std::string txsetStr = m_wallet->save_multisig_tx(ptx);
                txset = QByteArray::fromStdString(txsetStr);

                summarizeTxs(ptx, m_netType, feeAtomic, normalized);

                if (ptx.size() > 1)
                    warning = QStringLiteral("Transaction split into %1 parts (multiple ptx).").arg(static_cast<int>(ptx.size()));
            }
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg, operation_caller]() {
                emit errorOccurred(msg, operation_caller);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [=]() {
            emit unsignedMultisigReady(txset, feeAtomic, normalized, warning,operation_caller);
        }, Qt::QueuedConnection);
    });
}



void Wallet::signMultisigBlob(const QByteArray &txsetBlob,QString operation_caller)
{

    enqueue(QStringLiteral("signMultisigBlob"), [=]() {

        if (!m_wallet) {
            return;
        }

        QByteArray outTxset;
        bool fullySigned = false;
        QStringList txidsOut;

        try {
            tools::wallet2::multisig_tx_set mset;

            const bool looksAscii = std::all_of(txsetBlob.cbegin(), txsetBlob.cend(),
                                                [](char c){ return (c >= 32 && c <= 126) || c=='\n' || c=='\r' || c=='\t'; });

            {
                QMutexLocker locker(&m_mutex);
                qDebug().noquote() << "[msig] mutex acquired; parsing txset";

                const auto st = m_wallet->get_multisig_status();

                const std::string s = txsetBlob.toStdString();
                const bool parsed = m_wallet->parse_multisig_tx_from_str(s, mset);


                if (!parsed) {

                    throw std::runtime_error("parse_multisig_tx_from_str failed");
                }


                for (int i = 0; i < int(mset.m_ptx.size()); ++i) {
                    const auto &p = mset.m_ptx[size_t(i)];
                }

                std::vector<crypto::hash> txids;
                const bool signedOK = m_wallet->sign_multisig_tx(mset, txids);

                if (!signedOK)
                    throw std::runtime_error("sign_multisig_tx failed");

                fullySigned = !txids.empty();
                for (const auto &h : txids) {
                    const QString id = QString::fromStdString(epee::string_tools::pod_to_hex(h));
                    txidsOut << id;
                }

                std::string out = m_wallet->save_multisig_tx(mset);
                outTxset = QByteArray::fromStdString(out);
            }
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg, operation_caller]() {
                emit errorOccurred(msg, operation_caller);
            }, Qt::QueuedConnection);
            return;
        }


        QMetaObject::invokeMethod(this, [=]() {
            emit multisigSigned(outTxset, /*readyToSubmit=*/fullySigned, txidsOut,operation_caller);
        }, Qt::QueuedConnection);
    });
}



static QString to_tx_hex(const tools::wallet2::pending_tx &ptx) {
    const cryptonote::blobdata blob = cryptonote::tx_to_blob(ptx.tx);
    return QString::fromStdString(epee::string_tools::buff_to_hex_nodelimer(blob));
}

void Wallet::submitSignedMultisig(const QByteArray &signedTxset,QString operation_caller)
{
    enqueue(QStringLiteral("submitSignedMultisig"), [=]() {
        if (!m_wallet) return;

        bool ok = false;
        QString result;

        try {
            qDebug().noquote() << " before tools::wallet2::multisig_tx_set mset;";
            tools::wallet2::multisig_tx_set mset;
            qDebug().noquote() << "after tools::wallet2::multisig_tx_set mset";

            {
                QMutexLocker locker(&m_mutex);

                const std::string s = signedTxset.toStdString();

                if (!m_wallet->parse_multisig_tx_from_str(s, mset)) {
                    cryptonote::blobdata blob(s.begin(), s.end());

                    if (!m_wallet->load_multisig_tx(blob, mset, nullptr)) {
                        throw std::runtime_error("unable to parse/load multisig tx set");

                    }
                }

                for (size_t i = 0; i < mset.m_ptx.size(); ++i) {
                    const auto &p = mset.m_ptx[i];
                }

                QStringList ids;
                for (auto &ptx : mset.m_ptx) {

                    if (ptx.tx.vin.empty())
                    {
                        throw std::runtime_error("not fully signed (tx missing inputs)");
                    }

                    const QString txhex = to_tx_hex(ptx);


                    try {

                        m_wallet->commit_tx(ptx);

                    } catch (...) {
                        try { throw; }
                        catch (const tools::error::tx_rejected &e) {
                            qDebug().noquote() << "[commit] tx_rejected what=" << e.what();
                        }
                        catch (const std::exception &e) {
                            qDebug().noquote() << "[commit] exception type=" << typeid(e).name()
                            << "what=" << e.what();
                        }
                        // Show a curl you can run to see the daemon's full JSON error:
                        qDebug().noquote() << "[commit] To inspect daemon reply, run:";
                        qDebug().noquote()
                            << "curl -s -X POST " << m_daemonAddress
                            << "/sendrawtransaction -d '{\"tx_as_hex\":\"" << txhex
                            << "\",\"do_not_relay\":false,\"do_sanity_checks\":true}'";
                        throw; // rethrow so your outer catch emits multisigSubmitResult(false, ...)
                    }

                    const crypto::hash h = get_transaction_hash(ptx.tx);
                    ids << QString::fromStdString(epee::string_tools::pod_to_hex(h));

                }

                ok = true;
                result = ids.join(QLatin1Char(','));
            }
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg, operation_caller]() {
                emit errorOccurred(msg, operation_caller);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [=]() {
            emit multisigSubmitResult(ok, result,operation_caller);
        }, Qt::QueuedConnection);
    });
}

void Wallet::describeTransfer(const QString &multisigTxset, QString operation_caller)
{
    enqueue(QStringLiteral("describeTransfer"), [=]() {
        if (!m_wallet) return;

        QJsonObject detailsRoot;

        try {
            tools::wallet2::multisig_tx_set mset;

            QByteArray in = QByteArray::fromStdString(multisigTxset.toStdString());
            QByteArray raw = tryB64(in);
            if (raw.isEmpty()) {
                    QByteArray maybeHex = QByteArray::fromHex(in);
                    if (!maybeHex.isEmpty()) raw = maybeHex;
                    else raw = in;
                }
            cryptonote::blobdata blob(raw.begin(), raw.end());
            {
                    QMutexLocker locker(&m_mutex);
                        if (!m_wallet->parse_multisig_tx_from_str(std::string(blob.begin(), blob.end()), mset)) {
                            if (!m_wallet->load_multisig_tx(blob, mset, nullptr))
                                throw std::runtime_error("unable to parse/load multisig tx set");
                        }
                }


            quint64 feeSum = 0;
            QVariantList normalized;
            summarizeTxs(mset.m_ptx, m_netType, feeSum, normalized);

            QJsonArray recipientsJson;
            for (const QVariant &v : normalized) {
                const QVariantMap m = v.toMap();
                const QString addr = m.value(QStringLiteral("address")).toString();
                const qulonglong amt = m.value(QStringLiteral("amount")).toULongLong();
                recipientsJson.push_back(QJsonObject{
                  { QStringLiteral("address"), addr },

                  { QStringLiteral("amount"),  QString::number(amt) }
                  });
            }


            QString  paymentIdStr;
            uint64_t unlockTime = 0;


            auto toHex = [](const void *data, size_t len) -> QString {
                return QString::fromLatin1(QByteArray::fromRawData(
                                               reinterpret_cast<const char*>(data), int(len)).toHex());
            };

            for (const auto &p : mset.m_ptx) {

                unlockTime = std::max<uint64_t>(unlockTime, p.tx.unlock_time);

                if (paymentIdStr.isEmpty()) {
                    std::vector<cryptonote::tx_extra_field> tx_extra_fields;
                    if (cryptonote::parse_tx_extra(p.tx.extra, tx_extra_fields)) {
                        cryptonote::tx_extra_nonce extra_nonce;
                        if (cryptonote::find_tx_extra_field_by_type(tx_extra_fields, extra_nonce)) {
                            crypto::hash  pid  = crypto::null_hash;
                            crypto::hash8 pid8 = crypto::null_hash8;

                            if (cryptonote::get_payment_id_from_tx_extra_nonce(extra_nonce.nonce, pid)) {

                                paymentIdStr = toHex(&pid, sizeof(pid));
                            } else if (cryptonote::get_encrypted_payment_id_from_tx_extra_nonce(extra_nonce.nonce, pid8)) {
                                paymentIdStr = toHex(&pid8, sizeof(pid8));
                            }
                        }
                    }
                }
            }

            QJsonObject item{                    
                { QStringLiteral("recipients"),  recipientsJson },
                { QStringLiteral("payment_id"),  paymentIdStr },
                { QStringLiteral("fee"),         QString::number(feeSum) },
                { QStringLiteral("unlock_time"), QString::number(unlockTime) }
            };

            detailsRoot.insert(QStringLiteral("desc"), QJsonArray{ item });
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg, operation_caller]() {
                emit errorOccurred(msg, operation_caller);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [=]() {
            QString name;
            try {
                QMutexLocker locker(&m_mutex);
                name = QString::fromStdString(m_wallet->get_wallet_file());
            } catch (...) {
                name.clear();
            }
            emit describeTransferResult(name, detailsRoot, operation_caller);
        }, Qt::QueuedConnection);
    });
}

void Wallet::getTransfers()
{
    enqueue(QStringLiteral("getTransfers"), [=]() {
        if (!m_wallet) return;

        QVariantList list;

        auto push_incoming = [&](const crypto::hash &pid,
                                 const tools::wallet2::payment_details &pd,
                                 uint64_t chain_h, uint64_t last_reward)
        {
            QVariantMap m;
            m["txid"]        = QString::fromStdString(epee::string_tools::pod_to_hex(pd.m_tx_hash));
            m["payment_id"]  = QString::fromStdString(epee::string_tools::pod_to_hex(pid));
            m["direction"]   = QStringLiteral("in");
            m["amount"]      = static_cast<qulonglong>(pd.m_amount);
            m["fee"]         = static_cast<qulonglong>(pd.m_fee);
            m["height_b"]    = static_cast<qulonglong>(pd.m_block_height);
            m["timestamp"]   = static_cast<qulonglong>(pd.m_timestamp);
            m["unlock_time"] = static_cast<qulonglong>(pd.m_unlock_time);
            m["subaddr_major"]= static_cast<int>(pd.m_subaddr_index.major);
            m["subaddr_minor"]= static_cast<int>(pd.m_subaddr_index.minor);
            m["address"]     = QString::fromStdString(m_wallet->get_subaddress_as_str(pd.m_subaddr_index));
            m["unlocked"]    = m_wallet->is_transfer_unlocked(pd.m_unlock_time, pd.m_block_height);

            const quint64 h = m["height_b"].toULongLong();
            const quint64 conf = (h >= chain_h) ? 0 : (chain_h - h);
            m["confirmations"] = conf;
            m["suggested_conf_threshold"] =
                last_reward ? ( (m["amount"].toULongLong() + last_reward - 1) / last_reward ) : 0;

            list.append(m);
        };

        auto push_outgoing_c = [&](const crypto::hash &txid,
                                   const tools::wallet2::confirmed_transfer_details &pd,
                                   uint64_t chain_h, uint64_t last_reward)
        {
            const uint64_t fee    = pd.m_amount_in - pd.m_amount_out;
            const uint64_t change = pd.m_change == (uint64_t)-1 ? 0 : pd.m_change;
            const uint64_t amt    = pd.m_amount_in - change - fee;

            QVariantMap m;
            m["txid"]        = QString::fromStdString(epee::string_tools::pod_to_hex(txid));
            m["payment_id"]  = QString::fromStdString(epee::string_tools::pod_to_hex(pd.m_payment_id));
            m["direction"]   = QStringLiteral("out");
            m["amount"]      = static_cast<qulonglong>(amt);
            m["fee"]         = static_cast<qulonglong>(fee);
            m["height_b"]    = static_cast<qulonglong>(pd.m_block_height);
            m["timestamp"]   = static_cast<qulonglong>(pd.m_timestamp);
            m["unlock_time"] = static_cast<qulonglong>(pd.m_unlock_time);
            m["subaddr_major"]= static_cast<int>(pd.m_subaddr_account);
            m["subaddr_minor"]= 0;
            m["address"]     = QString::fromStdString(m_wallet->get_subaddress_as_str({pd.m_subaddr_account,0}));
            m["unlocked"]    = m_wallet->is_transfer_unlocked(pd.m_unlock_time, pd.m_block_height);

            const quint64 h = m["height_b"].toULongLong();
            const quint64 conf = (h >= chain_h) ? 0 : (chain_h - h);
            m["confirmations"] = conf;
            m["suggested_conf_threshold"] =
                last_reward ? ( (m["amount"].toULongLong() + last_reward - 1) / last_reward ) : 0;

            list.append(m);
        };

        auto push_outgoing_u = [&](const crypto::hash &txid,
                                   const tools::wallet2::unconfirmed_transfer_details &pd,
                                   bool failed,
                                   uint64_t chain_h, uint64_t last_reward)
        {
            const uint64_t fee    = pd.m_amount_in - pd.m_amount_out;
            const uint64_t change = pd.m_change;
            const uint64_t amt    = (change == (uint64_t)-1 ? 0 : (pd.m_amount_in - change - fee));

            QVariantMap m;
            m["txid"]        = QString::fromStdString(epee::string_tools::pod_to_hex(txid));
            m["payment_id"]  = QString::fromStdString(epee::string_tools::pod_to_hex(pd.m_payment_id));
            m["direction"]   = failed ? QStringLiteral("failed") : QStringLiteral("pending");
            m["amount"]      = static_cast<qulonglong>(amt);
            m["fee"]         = static_cast<qulonglong>(fee);
            m["height_b"]    = 0;
            m["timestamp"]   = static_cast<qulonglong>(pd.m_timestamp);
            m["unlock_time"] = static_cast<qulonglong>(pd.m_tx.unlock_time);
            m["subaddr_major"]= static_cast<int>(pd.m_subaddr_account);
            m["subaddr_minor"]= 0;
            m["address"]     = QString::fromStdString(m_wallet->get_subaddress_as_str({pd.m_subaddr_account,0}));
            m["unlocked"]    = false;

            m["confirmations"] = 0;
            m["suggested_conf_threshold"] =
                last_reward ? ( (m["amount"].toULongLong() + last_reward - 1) / last_reward ) : 0;

            list.append(m);
        };

        auto push_pool = [&](const crypto::hash &pid,
                             const tools::wallet2::pool_payment_details &ppd,
                             uint64_t chain_h, uint64_t last_reward)
        {
            const auto &pd = ppd.m_pd;
            QVariantMap m;
            m["txid"]        = QString::fromStdString(epee::string_tools::pod_to_hex(pd.m_tx_hash));
            m["payment_id"]  = QString::fromStdString(epee::string_tools::pod_to_hex(pid));
            m["direction"]   = QStringLiteral("in");     // FIXED: Changed from "pool" to "in"
            m["amount"]      = static_cast<qulonglong>(pd.m_amount);
            m["fee"]         = static_cast<qulonglong>(pd.m_fee);
            m["height_b"]    = 0;
            m["timestamp"]   = static_cast<qulonglong>(pd.m_timestamp);
            m["unlock_time"] = static_cast<qulonglong>(pd.m_unlock_time);
            m["subaddr_major"]= static_cast<int>(pd.m_subaddr_index.major);
            m["subaddr_minor"]= static_cast<int>(pd.m_subaddr_index.minor);
            m["address"]     = QString::fromStdString(m_wallet->get_subaddress_as_str({pd.m_subaddr_index.major, pd.m_subaddr_index.minor}));
            m["unlocked"]    = false;

            m["confirmations"] = 0;
            m["suggested_conf_threshold"] =
                last_reward ? ( (m["amount"].toULongLong() + last_reward - 1) / last_reward ) : 0;

            list.append(m);
        };

        try {
            uint64_t chain_h = 0, last_reward = 0;
            std::vector<std::tuple<cryptonote::transaction, crypto::hash, bool>> process_txs;
            {
                QMutexLocker l(&m_mutex);
                chain_h     = m_wallet->get_blockchain_current_height();
                last_reward = m_wallet->get_last_block_reward();


                m_wallet->update_pool_state(process_txs);
                if (!process_txs.empty())
                    m_wallet->process_pool_state(process_txs);
            }


            QSet<QString> processedTxIds;


            {
                std::list<std::pair<crypto::hash, tools::wallet2::payment_details>> payments;
                {
                    QMutexLocker l(&m_mutex);
                    m_wallet->get_payments(payments, /*min*/0, /*max*/(uint64_t)-1, boost::none, {});
                }
                for (const auto &p : payments) {
                    QString txId = QString::fromStdString(epee::string_tools::pod_to_hex(p.second.m_tx_hash));
                    if (!processedTxIds.contains(txId)) {
                        push_incoming(p.first, p.second, chain_h, last_reward);
                        processedTxIds.insert(txId);
                    }
                }
            }


            {
                std::list<std::pair<crypto::hash, tools::wallet2::confirmed_transfer_details>> payments_out;
                {
                    QMutexLocker l(&m_mutex);
                    m_wallet->get_payments_out(payments_out, /*min*/0, /*max*/(uint64_t)-1, boost::none, {});
                }
                for (const auto &p : payments_out) {
                    QString txId = QString::fromStdString(epee::string_tools::pod_to_hex(p.first));
                    if (!processedTxIds.contains(txId)) {
                        push_outgoing_c(p.first, p.second, chain_h, last_reward);
                        processedTxIds.insert(txId);
                    }
                }
            }

            {
                std::list<std::pair<crypto::hash, tools::wallet2::pool_payment_details>> pool;
                {
                    QMutexLocker l(&m_mutex);
                    m_wallet->get_unconfirmed_payments(pool, boost::none, {});
                }
                for (const auto &pp : pool) {
                    QString txId = QString::fromStdString(epee::string_tools::pod_to_hex(pp.second.m_pd.m_tx_hash));
                    if (!processedTxIds.contains(txId)) {
                        push_pool(pp.first, pp.second, chain_h, last_reward);
                        processedTxIds.insert(txId);
                    }
                }
            }

            {
                std::list<std::pair<crypto::hash, tools::wallet2::unconfirmed_transfer_details>> upayments;
                {
                    QMutexLocker l(&m_mutex);
                    m_wallet->get_unconfirmed_payments_out(upayments, boost::none, {});
                }
                for (const auto &u : upayments) {
                    QString txId = QString::fromStdString(epee::string_tools::pod_to_hex(u.first));
                    if (!processedTxIds.contains(txId)) {
                        const bool failed = (u.second.m_state == tools::wallet2::unconfirmed_transfer_details::failed);
                        push_outgoing_u(u.first, u.second, failed, chain_h, last_reward);
                        processedTxIds.insert(txId);
                    }
                }
            }


            std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b){
                const auto A = a.toMap(), B = b.toMap();
                const quint64 ta = A.value("timestamp").toULongLong();
                const quint64 tb = B.value("timestamp").toULongLong();
                if (ta != tb) return ta > tb;
                return A.value("height_b").toULongLong() > B.value("height_b").toULongLong();
            });
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [=]() { emit transfersReady(list); }, Qt::QueuedConnection);
    });
}


void Wallet::refreshHasMultisigPartialKeyImages()
{
    enqueue(QStringLiteral("refreshHasMsigPartialKeyImages"), [=]() {
        if (!m_wallet) {
            return;
        }

        bool val = false;
        try {
            QMutexLocker locker(&m_mutex);
            val = m_wallet->has_multisig_partial_key_images();
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;

        }

        QMetaObject::invokeMethod(this, [=]() {
            if (m_hasMsigPartialKeyImages != val) {
                m_hasMsigPartialKeyImages = val;
                emit walletChanged();
            }
        }, Qt::QueuedConnection);
    });
}



void Wallet::enqueue(const QString &name, std::function<void ()> func)
{
    if (QThread::currentThread() != this->thread()) {

        QMetaObject::invokeMethod(this,
                                  [this, name, func]() { enqueue(name, func); },
                                  Qt::QueuedConnection);
        return;
    }

    if (!m_queue.isEmpty() && m_queue.back().name == name) return;
    m_queue.enqueue({name, std::move(func)});
    emit queueChanged();
    if (!m_busy) runNext();
}

void Wallet::runNext()
{
    if (m_queue.isEmpty()) {
        if (m_busy) { m_busy = false; emit busyChanged(); }
        return;
    }

    m_busy = true;
    emit busyChanged();

    const Operation op = m_queue.dequeue();
    emit queueChanged();

    auto *watcher = new QFutureWatcher<void>(this);
    QFuture<void> fut = QtConcurrent::run([=]() { op.func(); });

    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        runNext();
    });

    watcher->setFuture(fut);
}

QVariantList Wallet::pendingOps() const
{
    QVariantList list;
    for (const Operation &op : m_queue) list.append(op.name);
    return list;
}

void Wallet::prepareSimpleTransfer(const QString &ref,
                                   QVariantList destinations,
                                   int feePriority,
                                   QList<int> subtractFeeFromIndices)
{
    enqueue(QStringLiteral("prepareSimpleTransfer"), [=]() {
        if (!m_wallet) return;

        quint64 feeAtomic = 0;
        QVariantList normalized;
        QString warning;

        try {

            std::vector<cryptonote::tx_destination_entry> dsts;
            dsts.reserve(size_t(destinations.size()));
            for (const QVariant &v : destinations) {
                const QVariantMap m = v.toMap();
                const QString addr = m.value("address").toString();
                quint64 amount = (m.value("amount").typeId() == QMetaType::Double)
                                     ? static_cast<quint64>(m.value("amount").toDouble() * 1e12 + 0.5)
                                     : static_cast<quint64>(m.value("amount").toULongLong());

                cryptonote::tx_destination_entry de;
                QString why;
                if (!makeDest(addr, amount, m_netType, de, &why))
                    throw std::runtime_error(QStringLiteral("bad destination (%1)").arg(why).toStdString());
                dsts.emplace_back(std::move(de));
            }


            tools::wallet2::unique_index_container subtractSet;
            for (int idx : subtractFeeFromIndices) {
                if (idx >= 0 && idx < destinations.size())
                    subtractSet.insert(static_cast<uint32_t>(idx));
            }


            tools::fee_priority prio;
            uint64_t mixin = 0;
            {
                QMutexLocker l(&m_mutex);
                prio  = m_wallet->adjust_priority(static_cast<uint32_t>(feePriority));
                mixin = m_wallet->adjust_mixin(/*requested*/ 0);
            }


            std::vector<tools::wallet2::pending_tx> ptx;
            {
                QMutexLocker locker(&m_mutex);
                const std::vector<uint8_t> extra;
                const uint32_t subaddr_account = 0;
                const std::set<uint32_t> subaddr_indices;

                ptx = m_wallet->create_transactions_2(
                    dsts, static_cast<size_t>(mixin), prio, extra,
                    subaddr_account, subaddr_indices, subtractSet);

                if (ptx.empty())
                    throw std::runtime_error("create_transactions_2 returned empty");

                if (!m_wallet->sanity_check(ptx, dsts, subtractSet))
                    throw std::runtime_error("sanity_check failed");


                m_simplePrepared[ref] = ptx;

                summarizeTxs(ptx, m_netType, feeAtomic, normalized);

                if (ptx.size() > 1)
                    warning = QStringLiteral("Transaction split into %1 parts.").arg(int(ptx.size()));
            }
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg, ref]() {
                emit errorOccurred(msg, ref);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [=]() {
            emit simpleTransferPrepared(ref, feeAtomic, normalized, warning);
        }, Qt::QueuedConnection);
    });
}

void Wallet::describePreparedSimpleTransfer(const QString &ref)
{
    enqueue(QStringLiteral("describePreparedSimpleTransfer"), [=]() {
        if (!m_wallet) return;
        QJsonObject root;

        try {
            std::vector<tools::wallet2::pending_tx> ptx;

            {
                QMutexLocker l(&m_mutex);
                auto it = m_simplePrepared.find(ref);
                if (it == m_simplePrepared.end())
                    throw std::runtime_error("no prepared transfer for ref");
                ptx = it.value();
            }

            quint64 feeSum = 0;
            QVariantList normalized;
            summarizeTxs(ptx, m_netType, feeSum, normalized);

            // recipients
            QJsonArray recipients;
            for (const QVariant &v : normalized) {
                const QVariantMap m = v.toMap();
                recipients.push_back(QJsonObject{
                    {"address", m.value("address").toString()},
                    {"amount",  QString::number(m.value("amount").toULongLong())}
                });
            }

            QString  paymentId;
            uint64_t unlockTime = 0;
            for (const auto &p : ptx) {
                unlockTime = std::max<uint64_t>(unlockTime, p.tx.unlock_time);

                if (paymentId.isEmpty()) {
                    std::vector<cryptonote::tx_extra_field> f;
                    if (cryptonote::parse_tx_extra(p.tx.extra, f)) {
                        cryptonote::tx_extra_nonce n;
                        if (cryptonote::find_tx_extra_field_by_type(f, n)) {
                            crypto::hash  pid  = crypto::null_hash;
                            crypto::hash8 pid8 = crypto::null_hash8;
                            auto hex = [](const void* d, size_t len){ return QString::fromLatin1(QByteArray::fromRawData(reinterpret_cast<const char*>(d), int(len)).toHex()); };
                            if (cryptonote::get_payment_id_from_tx_extra_nonce(n.nonce, pid))      paymentId = hex(&pid,  sizeof(pid));
                            else if (cryptonote::get_encrypted_payment_id_from_tx_extra_nonce(n.nonce, pid8)) paymentId = hex(&pid8, sizeof(pid8));
                        }
                    }
                }
            }

            root.insert(QStringLiteral("desc"),
                        QJsonArray{ QJsonObject{
                            {"recipients",  recipients},
                            {"payment_id",  paymentId},
                            {"fee",         QString::number(feeSum)},
                            {"unlock_time", static_cast<qint64>(unlockTime)}
                        }});
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg, ref]() {
                emit errorOccurred(msg, ref);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [=]() {
            emit simpleDescribeResult(ref, root);
        }, Qt::QueuedConnection);
    });
}

static QString txhex_from_ptx(const tools::wallet2::pending_tx &p) {
    const cryptonote::blobdata blob = cryptonote::tx_to_blob(p.tx);
    return QString::fromStdString(epee::string_tools::buff_to_hex_nodelimer(blob));
}

void Wallet::commitPreparedSimpleTransfer(const QString &ref)
{
    enqueue(QStringLiteral("commitPreparedSimpleTransfer"), [=]() {
        if (!m_wallet) return;

        bool ok = false;
        QString result;

        try {
            std::vector<tools::wallet2::pending_tx> ptx;
            {
                QMutexLocker l(&m_mutex);
                auto it = m_simplePrepared.find(ref);
                if (it == m_simplePrepared.end())
                    throw std::runtime_error("no prepared transfer for ref");
                ptx = it.value();
            }

            QStringList ids;
            {
                QMutexLocker locker(&m_mutex);
                for (auto &one : ptx) {
                    if (one.tx.vin.empty())
                        throw std::runtime_error("prepared tx missing inputs");
                    const QString txhex = txhex_from_ptx(one);
                    Q_UNUSED(txhex);
                    m_wallet->commit_tx(one);
                    const crypto::hash h = get_transaction_hash(one.tx);
                    ids << QString::fromStdString(epee::string_tools::pod_to_hex(h));
                }
            }

            {
                QMutexLocker l(&m_mutex);
                m_simplePrepared.remove(ref);
            }

            ok = true;
            result = ids.join(QLatin1Char(','));
        }
        catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg, ref]() {
                emit errorOccurred(msg, ref);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [=]() {
            emit simpleSubmitResult(ref, ok, result);
        }, Qt::QueuedConnection);
    });
}

void Wallet::discardPreparedSimpleTransfer(const QString &ref)
{
    enqueue(QStringLiteral("discardPreparedSimpleTransfer"), [=]() {
        QMutexLocker l(&m_mutex);
        m_simplePrepared.remove(ref);
    });
}

void Wallet::getAccounts()
{
    enqueue(QStringLiteral("getAccounts"), [=](){
        if (!m_wallet) return;
        QVariantList out;
        try {
            QMutexLocker l(&m_mutex);
            const uint32_t n = m_wallet->get_num_subaddress_accounts();
            for (uint32_t a = 0; a < n; ++a) {
                const QString label = subaddr_label(m_wallet.get(), a, 0);
                const QString base  = subaddr_str  (m_wallet.get(), m_netType, a, 0);
                const quint64 bal   = m_wallet->balance(a, /*strict=*/false);
                const quint64 unlk  = m_wallet->unlocked_balance(a, /*strict=*/false);
                out.append(QVariantMap{

                    { "account_index", static_cast<int>(a) },
                    { "label",         label },
                    { "balance",       static_cast<qint64>(bal)   },
                    { "unlocked",      static_cast<qint64>(unlk)  },
                    { "base_address",  base  }
                });
            }
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(this, [=](){ emit accountsReady(out); }, Qt::QueuedConnection);
    });
}


void Wallet::getSubaddresses(int accountIndex)
{
    enqueue(QStringLiteral("getSubaddresses"), [=](){
        if (!m_wallet) return;
        QVariantList out;
        try {
            QMutexLocker l(&m_mutex);
            const uint32_t n = m_wallet->get_num_subaddresses(static_cast<uint32_t>(accountIndex));

            for (uint32_t i = 0; i < n; ++i) {
                const QString addr  = subaddr_str(m_wallet.get(), m_netType, static_cast<uint32_t>(accountIndex), i);
                const QString label = subaddr_label(m_wallet.get(), static_cast<uint32_t>(accountIndex), i);

                const auto perBal  = m_wallet->balance_per_subaddress(static_cast<uint32_t>(accountIndex), false);
                const auto perUnlk = m_wallet->unlocked_balance_per_subaddress(static_cast<uint32_t>(accountIndex), false);

                const auto itB = perBal.find(i);
                const quint64 bal = (itB != perBal.end()) ? itB->second : 0;

                const auto itU = perUnlk.find(i);
                const quint64 unlk = (itU != perUnlk.end()) ? itU->second.first : 0;

                out.append(QVariantMap{
                    { "account_index",  accountIndex },
                    { "address_index",  static_cast<int>(i) },
                    { "address",        addr },
                    { "label",          label },
                    { "balance",        static_cast<qint64>(bal) },
                    { "unlocked",       static_cast<qint64>(unlk) }
                });
            }
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(this, [=](){ emit subaddressesReady(accountIndex, out); }, Qt::QueuedConnection);
    });
}
void Wallet::createSubaddress(int accountIndex, const QString &label)
{
    enqueue(QStringLiteral("createSubaddress"), [=](){
        if (!m_wallet) return;
        int idx = 0; QString addr;
        try {
            QMutexLocker l(&m_mutex);
            m_wallet->add_subaddress(static_cast<uint32_t>(accountIndex), label.toStdString());
            idx = static_cast<int>(m_wallet->get_num_subaddresses(static_cast<uint32_t>(accountIndex)) - 1);
            addr = subaddr_str(m_wallet.get(), m_netType, static_cast<uint32_t>(accountIndex), static_cast<uint32_t>(idx));
            m_wallet->store();
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(this, [=](){ emit subaddressCreated(accountIndex, idx, addr); }, Qt::QueuedConnection);
    });
}

void Wallet::labelSubaddress(int accountIndex, int addressIndex, const QString &label)
{
    enqueue(QStringLiteral("labelSubaddress"), [=](){
        if (!m_wallet) return;
        try {
            QMutexLocker l(&m_mutex);
            m_wallet->set_subaddress_label({static_cast<uint32_t>(accountIndex), static_cast<uint32_t>(addressIndex)}, label.toStdString());
            m_wallet->store();
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(this, [=](){ emit subaddressLabeled(accountIndex, addressIndex, label); }, Qt::QueuedConnection);
    });
}

void Wallet::setSubaddressLookahead(int major, int minor)
{
    enqueue(QStringLiteral("setSubaddrLookahead"), [=](){
        if (!m_wallet) return;
        try {
            QMutexLocker l(&m_mutex);
            m_wallet->set_subaddress_lookahead(static_cast<uint32_t>(major), static_cast<uint32_t>(minor));
            m_wallet->store();
        } catch (const std::exception &e) {
            const QString msg = QString::fromUtf8(e.what());
            qDebug().noquote() << "[msig][ERROR]:" << e.what();
            QMetaObject::invokeMethod(this, [this, msg]() {
                emit errorOccurred(msg);
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(this, [=](){ emit subaddressLookaheadSet(major, minor); }, Qt::QueuedConnection);
    });
}


void Wallet::setSocksProxy(const QString &host, quint16 port)
{
    m_proxyHost = host.trimmed();
    m_proxyPort = port;
    m_useProxy  = !m_proxyHost.isEmpty() && (m_proxyPort > 0);
}

void Wallet::clearProxy()
{
    m_proxyHost.clear();
    m_proxyPort = 0;
    m_useProxy  = false;
}


