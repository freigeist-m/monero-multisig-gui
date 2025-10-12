#include "win_compat.h"
#include "accountmanager.h"
#include <cryptoutils_extras.h>
#include <sodium.h>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QtEndian>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QMutexLocker>
#include <QSaveFile>
#include <QThread>
#include <QDebug>



using namespace CryptoUtils;


AccountManager::AccountManager(QObject *parent) : QObject(parent)
{

    QDir().mkpath("accounts");
}

AccountManager::~AccountManager()
{

    logout();
}


QString AccountManager::prettyJson(const QJsonObject &obj) const
{
    return QString(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

bool AccountManager::isOnionAddress(const QString &url) const
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9]{56}\\.onion$"),
                                       QRegularExpression::CaseInsensitiveOption);
    const bool ok = re.match(url.trimmed()).hasMatch();
    // if (!url.isEmpty())  qDebug() << "[AccountManager] isOnionAddress(" << url << ") ->" << ok;
    return ok;
}

bool AccountManager::validateDaemonUrl(const QString &url) const
{
    if (url.isEmpty()) return false;

    QString hostToCheck = url.trimmed();


    if (hostToCheck.startsWith("http://", Qt::CaseInsensitive)) {
        hostToCheck = hostToCheck.mid(7);

        int slashPos = hostToCheck.indexOf('/');
        if (slashPos != -1) {
            hostToCheck = hostToCheck.left(slashPos);
        }

        int colonPos = hostToCheck.indexOf(':');
        if (colonPos != -1) {
            hostToCheck = hostToCheck.left(colonPos);
        }
    } else if (hostToCheck.startsWith("https://", Qt::CaseInsensitive)) {
        hostToCheck = hostToCheck.mid(8);

        int slashPos = hostToCheck.indexOf('/');
        if (slashPos != -1) {
            hostToCheck = hostToCheck.left(slashPos);
        }

        int colonPos = hostToCheck.indexOf(':');
        if (colonPos != -1) {
            hostToCheck = hostToCheck.left(colonPos);
        }
    }


    if (isOnionAddress(hostToCheck)) return true;

    static const QRegularExpression ipv4(QStringLiteral(R"(^((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)$)"));
    static const QRegularExpression domain(QStringLiteral(R"((?:[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\.)+[A-Za-z]{2,}$)"));

    const bool ok = ipv4.match(hostToCheck).hasMatch() || domain.match(hostToCheck).hasMatch() || hostToCheck.compare("localhost", Qt::CaseInsensitive) == 0;

    return ok;
}

void AccountManager::loadSettingsFromJson(const QJsonObject &obj)
{
    const auto s = obj.value("settings").toObject();
    m_inspectGuard    = s.value("inspect_guard").toBool(true);
    m_daemonUrl       = s.value("daemon_url").toString(QStringLiteral("127.0.0.1"));
    m_daemonPort      = s.value("daemon_port").toInt(18081);
    m_useTorForDaemon = s.value("use_tor_for_daemon").toBool(false);
    m_torAutoconnect  = s.value("tor_autoconnect").toBool(false);
    m_torDaemon       = isOnionAddress(m_daemonUrl);
    m_lockTimeoutMinutes = s.value("lock_timeout_minutes").toInt(30);
    if (m_torDaemon) m_useTorForDaemon = true;
    m_networkType = s.value("network_type").toString("mainnet");


}

QVariantMap AccountManager::sanitizeTrustedPeers(const QVariantMap &raw) const
{
    QVariantMap clean;
    for (auto it = raw.begin(); it != raw.end(); ++it) {
        const QVariantMap src = it.value().toMap();
        QVariantMap dst;
        dst["label"]         = src.value("label").toString();
        dst["max_n"]         = src.value("max_n", 1).toInt();
        dst["min_threshold"] = src.value("min_threshold", 1).toInt();
        dst["active"]        = src.value("active", true).toBool();
        dst["current_number_wallets"]         = src.value("current_number_wallets", 0).toInt();
        dst["max_number_wallets"]         = src.value("max_number_wallets", 0).toInt();


        QVariantList allowedOut;
        const QVariant vAllowed = src.value("allowed_identities");
        const QVariantList in = vAllowed.canConvert<QVariantList>() ? vAllowed.toList() : QVariantList{};
        for (const QVariant &vv : in) {
            const QString s = vv.toString().trimmed().toLower();
            if (isOnionAddress(s)) allowedOut << s;
        }
        dst["allowed_identities"] = allowedOut;

        clean[it.key()] = dst;
    }
    return clean;
}

bool AccountManager::resetTrustedPeerWalletCount(const QString &onion)
{
    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated) return false;

    const QString peerOn = onion.trimmed().toLower();
    QJsonObject peers = m_accountData.value("trusted_peers").toObject();
    if (!peers.contains(peerOn)) { emit errorOccurred(tr("Trusted peer not found")); return false; }

    QJsonObject obj = peers.value(peerOn).toObject();
    obj["current_number_wallets"] = 0;
    peers[peerOn] = obj;
    m_accountData["trusted_peers"] = peers;

    if (!persistUnlocked()) return false;

    if (m_trustedPeers.contains(peerOn)) {
        QVariantMap ref = m_trustedPeers[peerOn].toMap();
        ref["current_number_wallets"] = 0;
        m_trustedPeers[peerOn] = ref;
    }
    emit trustedPeersChanged();
    return true;
}

bool AccountManager::incrementTrustedPeerWalletCount(const QString &onion)
{
    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated) return false;

    const QString peerOn = onion.trimmed().toLower();
    QJsonObject peers = m_accountData.value("trusted_peers").toObject();
    if (!peers.contains(peerOn)) { emit errorOccurred(tr("Trusted peer not found")); return false; }

    QJsonObject obj = peers.value(peerOn).toObject();
    const int cur = obj.value("current_number_wallets").toInt(0);
    const int max = obj.value("max_number_wallets").toInt(0);

    if (max > 0 && cur >= max) {

        return false;
    }

    obj["current_number_wallets"] = cur + 1;
    peers[peerOn] = obj;
    m_accountData["trusted_peers"] = peers;

    if (!persistUnlocked()) return false;

    if (m_trustedPeers.contains(peerOn)) {
        QVariantMap ref = m_trustedPeers[peerOn].toMap();
        ref["current_number_wallets"] = cur + 1;
        m_trustedPeers[peerOn] = ref;
    }
    emit trustedPeersChanged();
    return true;
}

void AccountManager::resetState()
{

    m_isAuthenticated     = false;
    m_hasLoggedOut        = false;
    m_key.clear();
    m_salt.clear();
    m_accountData = QJsonObject();
    m_currentAccount.clear();
    m_currentFilePath.clear();
    m_currentAccountOnion.clear();
    m_trustedPeers.clear();

    m_inspectGuard = true;
    m_daemonUrl    = QStringLiteral("127.0.0.1");
    m_daemonPort   = 18081;
    m_useTorForDaemon = false;
    m_torDaemon       = false;
    m_torAutoconnect  = false;
    m_lockTimeoutMinutes = 30;

    if (m_lock) {
        m_lock->unlock();
        m_lock.reset();
    }
}


bool AccountManager::persistUnlocked()
{

    QSaveFile out(m_currentFilePath);
    if (!out.open(QIODevice::WriteOnly)) {

        return false;
    }


    quint16 saltLen = static_cast<quint16>(m_salt.size());
    QByteArray lenBuf(2, 0);
    qToBigEndian<quint16>(saltLen, lenBuf.data());
    out.write(lenBuf);
    out.write(m_salt);


    QByteArray nonce;
    QByteArray cipher = CryptoUtils::encrypt(QJsonDocument(m_accountData)
                                                 .toJson(QJsonDocument::Compact),
                                             m_key, nonce);
    out.write(cipher);
    const bool ok = out.commit();

    return ok;
}


bool AccountManager::login(const QString &filePath, const QString &password)
{


    bool ok  = false;
    QString acct;
    {

        QMutexLocker locker(&m_mutex);
        resetState();

        if (!QFileInfo::exists(filePath)) {

            emit loginFailed(tr("File does not exist: %1").arg(filePath));
            return false;
        }

        auto lockfile = std::make_unique<QLockFile>(filePath + QStringLiteral(".lock"));
        if (!lockfile->tryLock()) {

            emit loginFailed(tr("Account is already in use"));
            return false;
        }

        QFile in(filePath);
        if (!in.open(QIODevice::ReadOnly)) {

            emit loginFailed(tr("Cannot open account file"));
            return false;
        }

        // --- read header ---
        const QByteArray lenBuf = in.read(2);
        if (lenBuf.size() != 2) {

            emit loginFailed(tr("Corrupted account file"));
            return false;
        }
        const quint16 saltLen = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(lenBuf.constData()));
        m_salt = in.read(saltLen);
        if (m_salt.size() != saltLen) {

            emit loginFailed(tr("Corrupted account file"));
            return false;
        }

        const QByteArray cipher = in.readAll();
        in.close();

        try {
            m_key = CryptoUtils::deriveKey(password, m_salt);

        } catch (const std::exception &e) {

            emit loginFailed(QString::fromLatin1(e.what()));
            return false;
        }

        QByteArray plain;
        if (!CryptoUtils::decrypt(cipher, m_key, plain)) {

            emit loginFailed(tr("Invalid password for this account"));
            return false;
        }


        const QJsonDocument doc = QJsonDocument::fromJson(plain);
        if (!doc.isObject()) {

            emit loginFailed(tr("Account data is not valid JSON"));
            return false;
        }

        m_accountData = doc.object();
        loadSettingsFromJson(m_accountData);

        const QJsonArray ids = m_accountData.value("tor_identities").toArray();
        m_currentAccountOnion = pickCurrentOnionFrom(ids);


        m_trustedPeers = sanitizeTrustedPeers(
            m_accountData.value("trusted_peers").toObject().toVariantMap());

        m_isAuthenticated   = true;
        m_hasLoggedOut      = false;
        m_currentFilePath   = filePath;
        m_currentAccount    = QFileInfo(filePath).baseName();
        acct = m_currentAccount;
        m_lock.swap(lockfile);
        ok = true;

    }
    if (!ok) return false;
    emit isAuthenticatedChanged();
    emit currentAccountChanged();
    emit settingsChanged();
    emit loginSuccess(acct);

    return true;
}

void AccountManager::logout()
{
    bool wasAuth = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_isAuthenticated) return;
        resetState();
        m_hasLoggedOut = true;
        wasAuth = true;
    } // <-- unlock here

    if (wasAuth) {
        emit isAuthenticatedChanged();
        emit currentAccountChanged();
        emit logoutOccurred();
    }
}

bool AccountManager::passwordIsCorrect(const QString &password) const
{

    QFile in(m_currentFilePath);
    if (!in.open(QIODevice::ReadOnly)) return false;

    QByteArray lenBuf = in.read(2);
    if (lenBuf.size() != 2) return false;
    quint16 saltLen = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(lenBuf.constData()));
    QByteArray salt = in.read(saltLen);
    QByteArray cipher = in.readAll();
    in.close();

    QByteArray key;
    try { key = CryptoUtils::deriveKey(password, salt); }
    catch (...) { return false; }

    QByteArray plain;
    if (!CryptoUtils::decrypt(cipher, key, plain)) return false;
    return QJsonDocument::fromJson(plain).isObject();
}

bool AccountManager::verifyPassword(const QString &password) const
{
    QMutexLocker locker(&m_mutex);
    const bool ok = m_isAuthenticated && passwordIsCorrect(password);

    return ok;
}

bool AccountManager::updatePassword(const QString &oldPassword,
                                    const QString &newPassword)
{

    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated)              return false;
    if (!passwordIsCorrect(oldPassword)) {

        return false;
    }

    QByteArray newSalt = CryptoUtils::generateSalt();
    QByteArray newKey;
    try { newKey = CryptoUtils::deriveKey(newPassword, newSalt); }
    catch (...) { qDebug() << "[AccountManager] updatePassword: deriveKey failed"; return false; }

    m_salt = newSalt;
    m_key  = newKey;
    const bool ok = persistUnlocked();


    if (!ok) return false;
    emit passwordUpdated(true);
    return true;
}

// -----------------------------------------------------------
// create account
// -----------------------------------------------------------
bool AccountManager::createAccount(const QString &accountName,
                                   const QString &password)
{

    bool ok = false;
    QString safe;

    {


        QMutexLocker locker(&m_mutex);
        resetState();

        safe = accountName;;
        safe.remove(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")));
        if (safe.isEmpty()) {

            emit errorOccurred(tr("Invalid account name"));
            return false;
        }

        QDir().mkpath(QStringLiteral("wallets/") + safe + QStringLiteral("/multisig_info"));

        const QString path = QStringLiteral("accounts/") + safe + QStringLiteral(".enc");
        if (QFileInfo::exists(path)) {

            emit errorOccurred(tr("Account '%1' already exists").arg(safe));
            return false;
        }

        QJsonObject data{
            {"tor_identities", QJsonArray()},
            {"monero",          QJsonObject{ {"wallets", QJsonArray()} }},
            {"address_book",    QJsonArray() },
            {"xmr_address_book",QJsonArray() },
            {"trusted_peers",   QJsonObject() },

            {"daemon_address_book", QJsonObject{
                                                {"label", "local daemon"},
                                                {"url" , "127.0.0.1"},
                                                {"port", 18081}} },

            {"settings",        QJsonObject{
                             {"inspect_guard", true},
                             {"daemon_url",    "127.0.0.1"},
                             {"daemon_port",   18081},
                             {"use_tor_for_daemon", false},
                             {"dark_mode",     true},
                             {"tor_autoconnect", false},
                             {"lock_timeout_minutes", 30},
                             {"network_type",  "mainnet"}
                         }}
        };
        m_accountData.swap(data);

        m_salt = CryptoUtils::generateSalt();
        try { m_key = CryptoUtils::deriveKey(password, m_salt); }
        catch (const std::exception &e) {

            emit errorOccurred(e.what());
            return false;
        }

        m_currentFilePath = path;
        if (!persistUnlocked()) {

            emit errorOccurred(tr("Failed to write new account file"));
            return false;

        }


        ok =  true;

    }

    if (!ok) return false;

    emit settingsChanged();
    emit accountCreated(safe);

    return true;
}


QString AccountManager::loadAccountData()
{
    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated) return {};

    return prettyJson(m_accountData);
}

bool AccountManager::saveAccountData(const QString &content)
{

    bool ok = false;

    {
        QMutexLocker locker(&m_mutex);
        if (!m_isAuthenticated) return false;

        const QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
        if (!doc.isObject()) {

            emit errorOccurred(tr("JSON is not an object"));
            return false;
        }
        m_accountData = doc.object();
        ok = persistUnlocked();
        if (!ok) {

            emit errorOccurred(tr("Could not save account data"));
            return false;
        }
        recomputeCurrentOnionLocked();

    }

    emit torIdentitiesChanged();
    emit currentAccountChanged();
    return true;
}


QVariantList AccountManager::getAvailableAccounts()
{
    QVariantList out;
    QDir dir(QStringLiteral("accounts"));
    dir.setNameFilters(QStringList{QStringLiteral("*.enc")});
    for (const QFileInfo &fi : dir.entryInfoList()) {
        out << QVariantMap{
            {"name", fi.baseName()},
            {"path", fi.absoluteFilePath()}
        };
    }

    return out;
}

QVariantList AccountManager::getTorIdentities() const {
    QMutexLocker lk(&m_mutex);
    QVariantList out;
    const QJsonArray arr = m_accountData.value("tor_identities").toArray();
    for (auto v : arr) {
        const QJsonObject o = v.toObject();
        out << QVariantMap{
            {"onion",  o.value("onion_address").toString()},
            {"label",  o.value("label").toString()},
            {"online", o.value("online").toBool()}
        };
    }

    return out;
}

QStringList AccountManager::torOnions() const {
    QMutexLocker lk(&m_mutex);
    QStringList xs;
    for (auto v : m_accountData.value("tor_identities").toArray())
        xs << v.toObject().value("onion_address").toString();

    return xs;
}

QString AccountManager::torPrivKeyFor(const QString &onion) const {
    QMutexLocker lk(&m_mutex);
    const QJsonArray arr = m_accountData.value("tor_identities").toArray();
    for (auto v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value("onion_address").toString().compare(onion, Qt::CaseInsensitive) == 0) {
            const QString key = o.value("private_key").toString();

            return key;
        }
    }

    return {};
}


QString AccountManager::torOnion() const {
    QMutexLocker lk(&m_mutex);
    const QJsonArray arr = m_accountData.value("tor_identities").toArray();
    for (auto v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value("online").toBool())
            return o.value("onion_address").toString();
    }
    if (!arr.isEmpty()) return arr.first().toObject().value("onion_address").toString();
    return {};
}
QString AccountManager::torPrivKey() const {
    const QString on = torOnion();
    return torPrivKeyFor(on);
}


void AccountManager::storeTorIdentity(const QString &onion,
                                      const QString &priv,
                                      const QString &label,
                                      bool online)
{
    QJsonArray arr;
    bool ok = false;
    QString currentOnion;
    {
        QMutexLocker lk(&m_mutex);
        if (!m_isAuthenticated) return;


        arr = m_accountData.value("tor_identities").toArray();
        bool updated = false;


        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr.at(i).toObject();
            if (o.value("onion_address").toString().compare(onion, Qt::CaseInsensitive) == 0) {
                if (!priv.isEmpty())  o["private_key"] = priv;
                if (!label.isEmpty()) o["label"]       = label;
                o["online"] = online;
                arr[i] = o;
                updated = true;

                break;
            }
        }


        if (!updated && !label.isEmpty()) {
            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject o = arr.at(i).toObject();
                const bool isPlaceholder = o.value("onion_address").toString().isEmpty();
                if (isPlaceholder &&
                    o.value("label").toString().compare(label, Qt::CaseInsensitive) == 0) {
                    o["onion_address"] = onion;
                    if (!priv.isEmpty()) o["private_key"] = priv;
                    o["online"] = online;
                    arr[i] = o;
                    updated = true;

                    break;
                }
            }
        }


        if (!updated) {
            auto labelExists = [](const QJsonArray &arr, const QString &want)->bool {
                for (const auto &v : arr) {
                    const QString l = v.toObject().value("label").toString();
                    if (l.compare(want, Qt::CaseInsensitive) == 0) return true;
                }
                return false;
            };
            auto uniqueLabelFrom = [&](const QJsonArray &arr, const QString &base)->QString {
                if (base.trimmed().isEmpty())
                    return QStringLiteral("id-%1").arg(arr.size() + 1);
                if (!labelExists(arr, base)) return base;
                int n = 2;
                while (true) {
                    const QString cand = QStringLiteral("%1-%2").arg(base).arg(n++);
                    if (!labelExists(arr, cand)) return cand;
                }
            };

            const QString finalLabel = uniqueLabelFrom(arr, label);

            arr.append(QJsonObject{
                {"onion_address", onion},
                {"private_key",   priv},
                {"label",         finalLabel},
                {"online",        online}
            });
        }

        m_accountData["tor_identities"] = arr;
        recomputeCurrentOnionLocked();
        ok = persistUnlocked();
        currentOnion = m_currentAccountOnion;

    }

    emit torIdentitiesChanged();
    emit currentAccountChanged();
}


bool AccountManager::setPlaceholderOnlineByLabel(const QString &label, bool online)
{

    bool ok = false;
    {
        QMutexLocker lk(&m_mutex);
        if (!m_isAuthenticated) return false;

        const QString want = label.trimmed();
        if (want.isEmpty()) return false;

        QJsonArray arr = m_accountData.value("tor_identities").toArray();
        bool changed = false;
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr.at(i).toObject();
            if (!o.value("onion_address").toString().isEmpty()) continue;
            if (o.value("label").toString().compare(want, Qt::CaseInsensitive) != 0) continue;
            o["online"] = online;
            arr[i] = o;
            changed = true;

            break;
        }
        if (!changed) {

            return false;
        }

        m_accountData["tor_identities"] = arr;
        recomputeCurrentOnionLocked();
        ok = persistUnlocked();

    }

    if (ok) { emit torIdentitiesChanged(); emit currentAccountChanged(); }
    return ok;
}

bool AccountManager::removePlaceholderIdentityByLabel(const QString &label)
{

    bool ok = false;

    {
        QMutexLocker lk(&m_mutex);
        if (!m_isAuthenticated) return false;

        const QString want = label.trimmed();
        if (want.isEmpty()) { emit errorOccurred(tr("Label cannot be empty")); return false; }

        QJsonArray arr = m_accountData.value("tor_identities").toArray();

        int idx = -1;
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonObject o = arr.at(i).toObject();
            const bool isPlaceholder = o.value("onion_address").toString().isEmpty();
            if (isPlaceholder &&
                o.value("label").toString().compare(want, Qt::CaseInsensitive) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {

            emit errorOccurred(tr("Placeholder not found"));
            return false;
        }

        arr.removeAt(idx);
        m_accountData["tor_identities"] = arr;
        recomputeCurrentOnionLocked();
        ok = persistUnlocked();

    }

    if (ok) { emit torIdentitiesChanged(); emit currentAccountChanged(); }
    return ok;
}


void AccountManager::storeTorPair(const QString &onion, const QString &priv)
{
    storeTorIdentity(onion, priv, QString(), true);
}


bool AccountManager::addTorIdentity(const QString &label)
{

    bool ok = false;
    int countAfter = 0;

    {
        QMutexLocker lk(&m_mutex);
        if (!m_isAuthenticated) return false;

        const QString clean = label.trimmed();
        if (clean.isEmpty()) { emit errorOccurred(tr("Label cannot be empty")); return false; }

        QJsonArray arr = m_accountData.value("tor_identities").toArray();
        for (const auto &v : arr) {
            const QJsonObject o = v.toObject();
            if (o.value("label").toString().compare(clean, Qt::CaseInsensitive)==0) {

                emit errorOccurred(tr("Label already exists"));
                return false;
            }
        }

        arr.append(QJsonObject{
            {"onion_address", ""},
            {"private_key",   ""},
            {"label",         clean},
            {"online",        false}
        });
        m_accountData["tor_identities"] = arr;

        ok = persistUnlocked();
        countAfter = arr.size();

    }

    if (ok) emit torIdentitiesChanged();
    return ok;
}


bool AccountManager::renameTorIdentity(const QString &onion, const QString &label)
{

    bool ok = false;

    {
        QMutexLocker lk(&m_mutex);
        if (!m_isAuthenticated) return false;

        const QString clean = label.trimmed();
        if (clean.isEmpty()) { emit errorOccurred(tr("Label cannot be empty")); return false; }

        QJsonArray arr = m_accountData.value("tor_identities").toArray();

        int idx = -1;
        for (int i=0;i<arr.size();++i) {
            const QJsonObject o = arr.at(i).toObject();
            if (o.value("onion_address").toString().compare(onion, Qt::CaseInsensitive)==0) {
                idx = i; break;
            }
        }
        if (idx < 0) { qDebug() << "[AccountManager] renameTorIdentity: not found"; emit errorOccurred(tr("Identity not found")); return false; }

        for (int i=0;i<arr.size();++i) {
            if (i==idx) continue;
            const QJsonObject o = arr.at(i).toObject();
            if (o.value("label").toString().compare(clean, Qt::CaseInsensitive)==0) {

                emit errorOccurred(tr("Label already exists"));
                return false;
            }
        }

        QJsonObject o = arr.at(idx).toObject();
        o["label"] = clean;
        arr[idx] = o;
        m_accountData["tor_identities"] = arr;

        ok = persistUnlocked();

    }

    if (ok) emit torIdentitiesChanged();
    return ok;
}


bool AccountManager::setTorIdentityOnline(const QString &onion, bool online)
{

    bool ok = false;
    QString currentOnion;

    {
        QMutexLocker lk(&m_mutex);
        if (!m_isAuthenticated) return false;

        QJsonArray arr = m_accountData.value("tor_identities").toArray();
        bool found = false;
        for (int i=0;i<arr.size();++i) {
            QJsonObject o = arr.at(i).toObject();
            if (o.value("onion_address").toString().compare(onion, Qt::CaseInsensitive)==0) {
                o["online"] = online;
                arr[i] = o;
                found = true;

                break;
            }
        }
        if (!found) { qDebug() << "[AccountManager] setTorIdentityOnline: NOT FOUND"; emit errorOccurred(tr("Identity not found")); return false; }

        m_accountData["tor_identities"] = arr;
        recomputeCurrentOnionLocked();
        ok = persistUnlocked();
        currentOnion = m_currentAccountOnion;

    }

    if (ok) { emit torIdentitiesChanged(); emit currentAccountChanged(); }
    return ok;
}


bool AccountManager::removeTorIdentity(const QString &onion)
{

    bool ok = false;
    int countAfter = 0;
    QString currentOnion;

    {
        QMutexLocker lk(&m_mutex);
        if (!m_isAuthenticated) return false;

        QJsonArray arr = m_accountData.value("tor_identities").toArray();
        QJsonArray out;
        bool removed = false;
        for (const auto &v : arr) {
            const QJsonObject o = v.toObject();
            if (o.value("onion_address").toString().compare(onion, Qt::CaseInsensitive)==0) {
                removed = true; continue;
            }
            out.append(o);
        }
        if (!removed) { qDebug() << "[AccountManager] removeTorIdentity: NOT FOUND"; emit errorOccurred(tr("Identity not found")); return false; }

        m_accountData["tor_identities"] = out;
        recomputeCurrentOnionLocked();
        ok = persistUnlocked();
        countAfter = out.size();
        currentOnion = m_currentAccountOnion;

    }

    if (ok) { emit torIdentitiesChanged(); emit currentAccountChanged(); }
    return ok;
}


QVariantList AccountManager::getAddressBook()
{
    QMutexLocker locker(&m_mutex);
    QJsonArray book = m_accountData.value("address_book").toArray();
    QVariantList out;
    for (const QJsonValue &v : book)
        out << v.toObject().toVariantMap();

    return out;
}

bool AccountManager::addAddressBookEntry(const QString &label,
                                         const QString &onion)
{

    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated) return false;

    QJsonArray book = m_accountData["address_book"].toArray();
    bool found = false;
    for (QJsonValueRef v : book) {
        QJsonObject obj = v.toObject();
        if (obj["onion"].toString() == onion) {
            obj["label"] = label;
            v = obj;
            found = true;
            break;
        }
    }
    if (!found)
        book.append(QJsonObject{{"label", label}, {"onion", onion}});

    m_accountData["address_book"] = book;
    const bool ok = persistUnlocked();

    if (!ok) return false;

    emit addressBookChanged();
    emit addressEntryAdded(label, onion);
    return true;
}

bool AccountManager::removeAddressBookEntry(const QString &onion)
{

    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated) return false;

    QJsonArray old = m_accountData["address_book"].toArray();
    QJsonArray nw;
    bool removed = false;
    for (const QJsonValue &v : old) {
        if (v.toObject()["onion"].toString() == onion) {
            removed = true;
            continue;
        }
        nw.append(v);
    }
    if (!removed) return false;

    m_accountData["address_book"] = nw;
    const bool ok = persistUnlocked();

    if (!ok) return false;

    emit addressBookChanged();
    emit addressEntryRemoved(onion);
    return true;
}

bool AccountManager::updateAddressBookEntry(const QString &oldOnion,
                                            const QString &newLabel,
                                            const QString &newOnion)
{

    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated) return false;

    const QString oldOn = oldOnion.trimmed().toLower();
    const QString newOn = newOnion.trimmed().toLower();

    if (newLabel.trimmed().isEmpty()) {
        emit errorOccurred(tr("Label cannot be empty"));
        return false;
    }
    if (!isOnionAddress(newOn)) {
        emit errorOccurred(tr("Invalid onion address"));
        return false;
    }

    QJsonArray book = m_accountData["address_book"].toArray();

    int foundIdx = -1;
    for (int i = 0; i < book.size(); ++i) {
        const QJsonObject obj = book.at(i).toObject();
        const QString on = obj.value("onion").toString();
        if (on == oldOn) foundIdx = i;
        if (on == newOn && on != oldOn) {
            emit errorOccurred(tr("Address already exists"));
            return false;
        }
    }
    if (foundIdx < 0) {
        emit errorOccurred(tr("Address not found"));
        return false;
    }

    QJsonObject updated = book.at(foundIdx).toObject();
    updated["label"] = newLabel;
    updated["onion"] = newOn;
    book[foundIdx] = updated;

    m_accountData["address_book"] = book;
    const bool ok = persistUnlocked();

    if (!ok) return false;

    emit addressBookChanged();
    emit addressEntryUpdated(oldOn, newOn, newLabel);
    return true;
}


QVariantList AccountManager::getXMRAddressBook()
{
    QMutexLocker locker(&m_mutex);
    QVariantList out;
    const QJsonArray arr = m_accountData.value("xmr_address_book").toArray();
    for (const QJsonValue &v : arr)
        out << v.toObject().toVariantMap();

    return out;
}

bool AccountManager::addXMRAddressBookEntry(const QString &label,
                                            const QString &xmrAddress)
{

    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated) return false;

    QJsonArray arr = m_accountData["xmr_address_book"].toArray();
    bool found = false;
    for (QJsonValueRef v : arr) {
        QJsonObject obj = v.toObject();
        if (obj["xmr_address"].toString() == xmrAddress) {
            obj["label"] = label;
            v = obj;
            found = true;
            break;
        }
    }
    if (!found)
        arr.append(QJsonObject{{"label", label}, {"xmr_address", xmrAddress}});

    m_accountData["xmr_address_book"] = arr;
    const bool ok = persistUnlocked();

    if (!ok) return false;

    emit addressXMRBookChanged();
    emit addressXMREntryAdded(label, xmrAddress);
    return true;
}

bool AccountManager::removeXMRAddressBookEntry(const QString &xmrAddress)
{

    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated) return false;

    QJsonArray old = m_accountData["xmr_address_book"].toArray();
    QJsonArray nw;
    bool removed = false;
    for (const QJsonValue &v : old) {
        if (v.toObject()["xmr_address"].toString() == xmrAddress)
            removed = true;
        else
            nw.append(v);
    }
    if (!removed) return false;

    m_accountData["xmr_address_book"] = nw;
    const bool ok = persistUnlocked();

    if (!ok) return false;

    emit addressXMRBookChanged();
    emit addressXMREntryRemoved(xmrAddress);
    return true;
}


QString AccountManager::getTrustedPeers()
{
    QMutexLocker locker(&m_mutex);
    const QString s = QString(QJsonDocument(QJsonObject::fromVariantMap(m_trustedPeers))
                                  .toJson(QJsonDocument::Compact));

    return s;
}

bool AccountManager::addTrustedPeer(const QString &onion,
                                    const QString &label,
                                    int maxN,
                                    int minThreshold,
                                    bool active,
                                    const QStringList &allowedIdentities,
                                    int max_number_wallets)
{
    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated)                return false;
    const QString peerOn = onion.trimmed().toLower();
    if (!isOnionAddress(peerOn))           { emit errorOccurred(tr("Invalid onion address")); return false; }
    if (maxN <= 0 || minThreshold <= 0)    { emit errorOccurred(tr("Max participants and min threshold must be positive")); return false; }
    if (minThreshold > maxN)               { emit errorOccurred(tr("Min threshold cannot exceed max participants")); return false; }
    if (max_number_wallets <= 0 || max_number_wallets >= 10001)    { emit errorOccurred(tr("Max number of wallets is too high")); return false; }

    QJsonObject peers = m_accountData["trusted_peers"].toObject();
    if (peers.contains(peerOn))            { emit errorOccurred(tr("Trusted peer already exists")); return false; }


    QStringList myOnions;
    for (const auto &v : m_accountData.value("tor_identities").toArray())
        myOnions << v.toObject().value("onion_address").toString().trimmed().toLower();


    QStringList allowedNorm;
    for (const QString &s : allowedIdentities) {
        const QString a = s.trimmed().toLower();
        if (!isOnionAddress(a)) continue;
        if (!myOnions.contains(a)) {
            emit errorOccurred(tr("Allowed identity is not one of your onions: %1").arg(a));
            return false;
        }
        if (!allowedNorm.contains(a)) allowedNorm << a;
    }

    QJsonArray allowedArr;
    for (const QString &a : allowedNorm) allowedArr.append(a);

    QJsonObject obj{
        {"label",               label},
        {"max_n",               maxN},
        {"min_threshold",       minThreshold},
        {"active",              active},
        {"allowed_identities",  allowedArr},
        {"max_number_wallets",  max_number_wallets},
        {"current_number_wallets",  0 }
    };
    peers[peerOn] = obj;
    m_accountData["trusted_peers"] = peers;

    if (!persistUnlocked()) return false;


    QVariantMap vm = obj.toVariantMap();
    m_trustedPeers[peerOn] = vm;

    emit trustedPeersChanged();
    emit trustedPeerAdded(peerOn, label);
    return true;
}

bool AccountManager::updateTrustedPeer(const QString &onion,
                                       const QString &label,
                                       int maxN,
                                       int minThreshold,
                                       const QStringList &allowedIdentities,
                                       int max_number_wallets)
{
    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated)                return false;
    const QString peerOn = onion.trimmed().toLower();
    if (maxN <= 0 || minThreshold <= 0)    { emit errorOccurred(tr("Max participants and min threshold must be positive")); return false; }
    if (minThreshold > maxN)               { emit errorOccurred(tr("Min threshold cannot exceed max participants")); return false; }
    if (max_number_wallets <= 0 || max_number_wallets >= 10001)    { emit errorOccurred(tr("Max number of wallets is too high")); return false; }


    QJsonObject peers = m_accountData["trusted_peers"].toObject();
    if (!peers.contains(peerOn))           { emit errorOccurred(tr("Trusted peer not found")); return false; }


    QStringList myOnions;
    for (const auto &v : m_accountData.value("tor_identities").toArray())
        myOnions << v.toObject().value("onion_address").toString().trimmed().toLower();

    QStringList allowedNorm;
    for (const QString &s : allowedIdentities) {
        const QString a = s.trimmed().toLower();
        if (!isOnionAddress(a)) continue;
        if (!myOnions.contains(a)) {
            emit errorOccurred(tr("Allowed identity is not one of your onions: %1").arg(a));
            return false;
        }
        if (!allowedNorm.contains(a)) allowedNorm << a;
    }

    QJsonArray allowedArr;
    for (const QString &a : allowedNorm) allowedArr.append(a);

    QJsonObject obj = peers[peerOn].toObject();
    obj["label"]               = label;
    obj["max_n"]               = maxN;
    obj["min_threshold"]       = minThreshold;
    obj["allowed_identities"]  = allowedArr;
    obj["max_number_wallets"] =  max_number_wallets;

    peers[peerOn] = obj;
    m_accountData["trusted_peers"] = peers;

    if (!persistUnlocked()) return false;

    if (m_trustedPeers.contains(peerOn)) {
        QVariantMap ref = m_trustedPeers[peerOn].toMap();
        ref["label"]               = label;
        ref["max_n"]               = maxN;
        ref["min_threshold"]       = minThreshold;
        ref["max_number_wallets"]  = max_number_wallets;
        ref["current_number_wallets"]  = obj["current_number_wallets"].toInt();
        QVariantList qvAllowed; for (const auto &a : allowedNorm) qvAllowed << a;
        ref["allowed_identities"]  = qvAllowed;
        m_trustedPeers[peerOn]     = ref;
    }

    emit trustedPeersChanged();
    emit trustedPeerUpdated(peerOn, label);
    return true;
}

bool AccountManager::setTrustedPeerActive(const QString &onion, bool active)
{

    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated)                return false;

    QJsonObject peers = m_accountData["trusted_peers"].toObject();
    if (!peers.contains(onion))            { emit errorOccurred(tr("Trusted peer not found")); return false; }

    QJsonObject obj = peers[onion].toObject();
    obj["active"] = active;
    peers[onion] = obj;
    m_accountData["trusted_peers"] = peers;

    if (!persistUnlocked())                return false;

    if (m_trustedPeers.contains(onion)) {
        QVariantMap ref = m_trustedPeers[onion].toMap();
        ref["active"] = active;
        m_trustedPeers[onion] = ref;
    }
    emit trustedPeersChanged();
    return true;
}

bool AccountManager::removeTrustedPeer(const QString &onion)
{

    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated)                return false;

    QJsonObject peers = m_accountData["trusted_peers"].toObject();
    if (!peers.contains(onion))            return false;

    peers.remove(onion);
    m_accountData["trusted_peers"] = peers;

    if (!persistUnlocked())                return false;

    m_trustedPeers.remove(onion);
    emit trustedPeersChanged();
    emit trustedPeerRemoved(onion);
    return true;
}


bool AccountManager::updateSettings(bool inspectGuard,
                                    const QString &daemonUrl,
                                    int daemonPort,
                                    bool useTorForDaemon,
                                    bool torAutoConnect,
                                    int  lockTimeoutMinutes,
                                    const QString &networkTypeStr)
{
    bool ok = false;
    bool disconnectWallets =  false;
    QString newNetNormalized;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_isAuthenticated) return false;

        const QString url = daemonUrl.trimmed();
        if (!validateDaemonUrl(url))         { emit errorOccurred(tr("Invalid daemon URL"));  return false; }
        if (daemonPort < 1 || daemonPort > 65535) { emit errorOccurred(tr("Invalid daemon port")); return false; }
        if (lockTimeoutMinutes < 0 || lockTimeoutMinutes > 1440) {
            emit errorOccurred(tr("Auto-lock minutes must be between 0 and 1440"));
            return false;
        }


        if (networkTypeStr != m_networkType) {
            disconnectWallets = true;

        }



        bool isOnion = isOnionAddress(url);

        QJsonObject settings = m_accountData["settings"].toObject();

        if (settings["daemon_url"] != url || settings["daemon_port"] != daemonPort || settings["use_tor_for_daemon"] != useTorForDaemon) disconnectWallets  = true ;

        settings["inspect_guard"]      = inspectGuard;
        settings["daemon_url"]         = url;
        settings["daemon_port"]        = daemonPort;
        settings["use_tor_for_daemon"] = useTorForDaemon;
        settings["tor_autoconnect"]    = torAutoConnect;
        settings["lock_timeout_minutes"]    = lockTimeoutMinutes;
        settings["network_type"]        = networkTypeStr;

        m_accountData["settings"]      = settings;

        if (!persistUnlocked())  return false;

        m_inspectGuard    = inspectGuard;
        m_daemonUrl       = url;
        m_daemonPort      = daemonPort;
        m_useTorForDaemon = useTorForDaemon;
        m_torDaemon       = isOnion;
        m_torAutoconnect  = torAutoConnect;
        m_lockTimeoutMinutes  = lockTimeoutMinutes;
        m_networkType = networkTypeStr;

        ok = true;
    }

    if (disconnectWallets) emit requireDisconnectAllWallets();

    emit settingsChanged();
    return ok;
}

QString AccountManager::pickCurrentOnionFrom(const QJsonArray &arr) const
{
    QString result;
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value("online").toBool()) {
            result = o.value("onion_address").toString();
            break;
        }
    }
    if (result.isEmpty() && !arr.isEmpty())
        result = arr.first().toObject().value("onion_address").toString();

    return result;
}

void AccountManager::recomputeCurrentOnionLocked()
{
    const QJsonArray ids = m_accountData.value("tor_identities").toArray();
    m_currentAccountOnion = pickCurrentOnionFrom(ids);

}


bool AccountManager::darkModePref() const {
    QMutexLocker lk(&m_mutex);
    return m_accountData.value("settings").toObject()
        .value("dark_mode").toBool(false);
}

bool AccountManager::setDarkModePref(bool dark) {
    bool changed = false;

    {
        QMutexLocker lk(&m_mutex);
        if (!m_isAuthenticated) return false;

        QJsonObject settings = m_accountData["settings"].toObject();
        if (settings.value("dark_mode").toBool(false) == dark)
            return true;

        settings["dark_mode"] = dark;
        m_accountData["settings"] = settings;

        if (!persistUnlocked()) return false;
        changed = true;
    }

    if (changed) emit settingsChanged();
    return true;
}


bool AccountManager::importTorIdentity(const QString &label,
                                       const QString &onion,
                                       const QString &privateKey,
                                       bool online)
{

    const QString onNorm = onion.trimmed().toLower();
    if (!isOnionAddress(onNorm)) {
        emit errorOccurred(tr("Invalid onion address"));
        return false;
    }

    const QString pk = privateKey.trimmed();

    if (!pk.startsWith(QStringLiteral("ED25519-V3:"), Qt::CaseInsensitive)) {
        emit errorOccurred(tr("Private key must be in Tor ControlPort format: ED25519-V3:<base64>"));
        return false;
    }

    {
        const int colon = pk.indexOf(':');
        const QByteArray b64 = pk.mid(colon + 1).toLatin1().trimmed();
        QByteArray raw = QByteArray::fromBase64(b64, QByteArray::Base64Encoding);
        if (raw.size() < 32) {
            emit errorOccurred(tr("Malformed ED25519 key (bad base64)"));
            return false;
        }
    }

    bool ok = false;
    {
        QMutexLocker lk(&m_mutex);
        if (!m_isAuthenticated) return false;

        QJsonArray arr = m_accountData.value("tor_identities").toArray();

        auto labelExists = [](const QJsonArray &arr, const QString &want)->bool {
            for (const auto &v : arr)
                if (v.toObject().value("label").toString().compare(want, Qt::CaseInsensitive) == 0)
                    return true;
            return false;
        };
        auto uniqueLabelFrom = [&](const QJsonArray &arr, const QString &base)->QString {
            const QString baseClean = base.trimmed();
            if (baseClean.isEmpty()) return QStringLiteral("id-%1").arg(arr.size() + 1);
            if (!labelExists(arr, baseClean)) return baseClean;
            int n = 2;
            while (true) {
                const QString cand = QStringLiteral("%1-%2").arg(baseClean).arg(n++);
                if (!labelExists(arr, cand)) return cand;
            }
        };

        bool updated = false;
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr.at(i).toObject();
            if (o.value("onion_address").toString().compare(onNorm, Qt::CaseInsensitive) == 0) {
                if (!label.trimmed().isEmpty()) o["label"] = label.trimmed();
                o["private_key"] = pk;
                o["online"]      = online;
                arr[i] = o;
                updated = true;
                break;
            }
        }

        if (!updated) {
            const QString finalLabel = uniqueLabelFrom(arr, label);
            arr.append(QJsonObject{
                {"onion_address", onNorm},
                {"private_key",   pk},
                {"label",         finalLabel},
                {"online",        online}
            });
        }

        m_accountData["tor_identities"] = arr;
        recomputeCurrentOnionLocked();
        ok = persistUnlocked();
    }

    if (ok) {
        emit torIdentitiesChanged();
        emit currentAccountChanged();
    } else {
        emit errorOccurred(tr("Could not save account data"));
    }
    return ok;
}

static QJsonArray toDaemonArray(const QJsonValue &v) {
    if (v.isArray()) return v.toArray();
    if (v.isObject()) return QJsonArray{ v.toObject() };
    return QJsonArray{};
}

static bool endpointsEqual(const QString &aUrl, int aPort,
                           const QString &bUrl, int bPort)
{
    return aPort == bPort &&
           aUrl.trimmed().compare(bUrl.trimmed(), Qt::CaseInsensitive) == 0;
}


QVariantList AccountManager::getDaemonAddressBook() const
{
    QMutexLocker locker(&m_mutex);
    QVariantList out;

    QJsonArray arr = toDaemonArray(m_accountData.value("daemon_address_book"));
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        QVariantMap m;
        m["label"] = o.value("label").toString();
        m["url"]   = o.value("url").toString();
        m["port"]  = o.value("port").toInt();
        if (!m["url"].toString().trimmed().isEmpty() && m["port"].toInt() > 0)
            out << m;
    }
    return out;
}


bool AccountManager::addDaemonAddressBookEntry(const QString &label,
                                               const QString &url,
                                               int port)
{
    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated) return false;

    const QString u = url.trimmed();
    if (!validateDaemonUrl(u)) { emit errorOccurred(tr("Invalid daemon URL")); return false; }
    if (port < 1 || port > 65535) { emit errorOccurred(tr("Invalid daemon port")); return false; }

    QJsonArray arr = toDaemonArray(m_accountData.value("daemon_address_book"));

    bool updated = false;
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr.at(i).toObject();
        if (endpointsEqual(o.value("url").toString(), o.value("port").toInt(), u, port)) {
            o["label"] = label.trimmed();
            arr[i] = o;
            updated = true;
            break;
        }
    }
    if (!updated) {
        arr.append(QJsonObject{
            {"label", label.trimmed()},
            {"url",   u},
            {"port",  port}
        });
    }

    m_accountData["daemon_address_book"] = arr;
    if (!persistUnlocked()) return false;

    if (updated)
        emit addressDaemonEntryAdded(label.trimmed(), u, port);


    return true;
}

bool AccountManager::removeDaemonAddressBookEntry(const QString &url, int port)
{
    QMutexLocker locker(&m_mutex);
    if (!m_isAuthenticated) return false;

    const QString u = url.trimmed();
    QJsonArray oldArr = toDaemonArray(m_accountData.value("daemon_address_book"));
    QJsonArray newArr;
    bool removed = false;

    for (const QJsonValue &v : oldArr) {
        const QJsonObject o = v.toObject();
        if (endpointsEqual(o.value("url").toString(), o.value("port").toInt(), u, port)) {
            removed = true; continue;
        }
        newArr.append(o);
    }
    if (!removed) return false;

    m_accountData["daemon_address_book"] = newArr;
    if (!persistUnlocked()) return false;

    emit addressDaemonEntryRemoved(u, port);
    return true;
}



