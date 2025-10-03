#include "torbackend.h"
#include "accountmanager.h"

#include <QStandardPaths>
#include <QCoreApplication>
#include <QTextStream>
#include <QFile>
#include <QTimer>
#include <QRegularExpression>
#include <QSysInfo>
#include <QDirIterator>
#include <QTcpServer>
#include <QThread>
#include <QDebug>
#include <iostream>
#include "routerhandler.h"
#include "multisigapirouter.h"
#include "torinstaller.h"
#include "torinstallworker.h"

Q_DECLARE_METATYPE(TorBackend*)
extern RouterHandler *router;


static quint16 findFreePort()
{
    QTcpServer srv;
    if (!srv.listen(QHostAddress::LocalHost, 0))
        return 0;
    const quint16 p = srv.serverPort();
    srv.close();
    return p;
}

// ──────────────────────────────────────────────────────────────────────────────
TorBackend::TorBackend(AccountManager *acctMgr, QObject *parent)
    : QObject(parent),
    m_acct(acctMgr),
    m_bootstrapProgress(0),
    m_currentStatus(""),
    m_initializing(false)
{


    connect(&m_proc, &QProcess::readyReadStandardOutput,
            this,      &TorBackend::onStdOut);
    connect(&m_proc, &QProcess::readyReadStandardError,
            this,      &TorBackend::onStdErr);
    connect(&m_proc, qOverload<int,QProcess::ExitStatus>(&QProcess::finished),
            this,      &TorBackend::onProcessFinished);


    connect(&m_ctl, &QTcpSocket::connected,    this, &TorBackend::onCtlConnected);
    connect(&m_ctl, &QTcpSocket::readyRead,    this, &TorBackend::onCtlReadyRead);
    connect(&m_ctl, &QTcpSocket::errorOccurred,
            this, [this](QAbstractSocket::SocketError){

                emit errorOccurred(tr("Control-port error: %1").arg(m_ctl.errorString()));
            });


    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    if (base.isEmpty()) {
        qWarning() << "[TorBackend] AppDataLocation is empty; falling back to TempLocation";
        base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    if (base.isEmpty()) {
        qWarning() << "[TorBackend] TempLocation is empty; falling back to QDir::tempPath()";
        base = QDir::tempPath();
    }

    // ensure parent exists; QTemporaryDir will not create it
    if (!QDir().mkpath(base)) {
        qWarning() << "[TorBackend] Could not create base dir" << base
                   << "; falling back to QDir::tempPath()";
        base = QDir::tempPath();
        QDir().mkpath(base); // best effort
    }

    // try under base
    m_tmpDir = std::make_unique<QTemporaryDir>(base + "/tor-XXXXXX");

    // last-resort fallback: pure system temp
    if (!m_tmpDir->isValid()) {
        qWarning() << "[TorBackend] Failed to create tor temp dir under" << base
                   << "; retrying in system temp";
        m_tmpDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/tor-XXXXXX");
    }

    if (!m_tmpDir->isValid()) {
        emit errorOccurred(tr("Cannot create temp dir for Tor (base=%1)").arg(base));
        // optionally: return; (and handle disabled state) instead of aborting the whole app
        return;
    }

    m_dataDir = m_tmpDir->path();

    if (!m_tmpDir->isValid())
        qFatal("Cannot create temp dir for Tor");
    m_dataDir = m_tmpDir->path();




    m_socksPort   = findFreePort();
    m_controlPort = findFreePort();

    if (m_socksPort == 0 || m_controlPort == 0 || m_socksPort == m_controlPort)
        qWarning() << "[TorBackend] could not reserve random ports, will fall back to defaults";
}

void TorBackend::setRouterDeps(MultiWalletController *wm, MultisigManager *msig)
{
    m_walletMgr = wm;
    m_msigMgr   = msig;
}

// ──────────────────────────────────────────────────────────────────────────────
QString TorBackend::locateTorBinary() const
{
#if defined(Q_OS_WIN)
    const QString exeName = "tor.exe";
#else
    const QString exeName = "tor";
#endif


    const QByteArray env = qgetenv("TOR_BINARY");
    if (!env.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(env)))
        return QString::fromLocal8Bit(env);


#if defined(Q_OS_WIN)
    const QString osToken = "windows";
#elif defined(Q_OS_MACOS)
    const QString osToken = "macos";
#else
    const QString osToken = "linux";
#endif
    const QString cpu = QSysInfo::currentCpuArchitecture().toLower();
    const QString archToken =
        ((cpu.contains("x86") || cpu.contains("amd")) && cpu.contains("64")) ? "x86_64"
        : (cpu.contains("arm") || cpu.contains("aarch"))                      ? "aarch64"
                                                                             : "i686";

    auto pickBest = [&](const QString &root)->QString {
        if (!QDir(root).exists()) return {};
        QString best;
        QDirIterator it(root, QStringList() << exeName, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString cand = it.next();
#ifndef Q_OS_WIN
            if (!QFileInfo(cand).isExecutable()) continue;
#endif \

            if (cand.contains("/bin/") || cand.contains("/Tor/")) return cand;
            if (best.isEmpty()) best = cand;
            if (cand.contains("/release/")) best = cand;
            if (best.contains("/debug/") && !cand.contains("/debug/")) best = cand;
        }
        return best;
    };

    QStringList roots;
    const QString appDir = QCoreApplication::applicationDirPath();

    roots << appDir + "/tor/" + osToken + "-" + archToken;
    roots << QFileInfo(appDir).absolutePath() + "/tor/" + osToken + "-" + archToken;

    roots << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + "/tor-bin/" + osToken + "-" + archToken;

    for (const QString &r : roots) {
        const QString cand = pickBest(r);
        if (!cand.isEmpty()) return cand;
    }


    return QStandardPaths::findExecutable(exeName);
}

// ──────────────────────────────────────────────────────────────────────────────
void TorBackend::writeTorrc(const QString &torrcPath,
                            const QString &hsDir,
                            const QString &cookieFile) const
{

    QFile f(torrcPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error("Cannot write torrc");
    QTextStream ts(&f);
    ts << "SOCKSPort "      << m_socksPort    << "\n"
       << "ControlPort "    << m_controlPort  << "\n"
       << "CookieAuthentication 1\n"
       << "CookieAuthFile " << cookieFile     << "\n"
       << "DataDirectory "  << m_dataDir      << "\n"
       << "Log notice stdout\n"
       << "SafeLogging 0\n";
    f.close();

}

// ──────────────────────────────────────────────────────────────────────────────
void TorBackend::start(bool forceDownload)
{


    m_initializing = true; emit initializingChanged();

    m_bootstrapped100 = false;
    m_addOnionIssued  = false;
    m_ctlBuffer.clear();
    if (m_ctl.state() != QAbstractSocket::UnconnectedState)
        m_ctl.abort();

    quint16 socks   = findFreePort();
    quint16 ctlPort = findFreePort();
    if (socks == 0 || ctlPort == 0 || socks == ctlPort) {
        qWarning() << "[TorBackend] could not reserve random ports, will fall back to previous";
    } else {
        m_socksPort   = socks;
        m_controlPort = ctlPort;
    }

    if (m_running) { qDebug() << "[TorBackend] start: already running"; return; }
    emit statusTorChanged(tr("Launching Tor…"));

    QString torBin;
    if (!forceDownload) {

        torBin = locateTorBinary();
        qInfo() << "torBin" << torBin ;
    }

    if (!forceDownload && !torBin.isEmpty()) {
        qDebug() << "[TorBackend]: launchTorWithBinary(torBin)";
        launchTorWithBinary(torBin);
        return;
    }

    if (m_installing) {
        emit statusTorChanged(tr("Downloading Tor…"));
        qInfo() << "Downloading Tor…";

        return;
    }

    m_installing = true;
    emit installingChanged();
    m_installThread = new QThread(this);
    auto *worker   = new TorInstallWorker();

    const QString appDir = QCoreApplication::applicationDirPath();

    worker->setInstallRoot(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/tor-bin"
        );


    worker->setRequireGpg(false);
    worker->setAllowedFingerprints({ "EF6E286DDA85EA2A4BA7DE684E2C6E8793298290" });

    worker->moveToThread(m_installThread);

    // connect progress if you add it later inside TorInstaller
    // connect(worker, &TorInstallWorker::progress, this, &TorBackend::statusTorChanged);

    connect(m_installThread, &QThread::started, worker, &TorInstallWorker::run);

    connect(worker, &TorInstallWorker::finished, this,
            [this](bool ok, const QString &bin, const QString &err){
                m_installing = false;
                emit installingChanged();
                if (!ok) {
                    emit errorOccurred(tr("Tor setup failed: %1").arg(err));
                    emit statusTorChanged(tr("Failed to obtain Tor binaries"));
                    return;
                }
                qInfo() << "[TorInstaller] Tor ready at" << bin;
                launchTorWithBinary(bin);
            },
            Qt::QueuedConnection);

    connect(worker, &TorInstallWorker::gpgWarning, this,
            [this](const QString &code, const QString &msg){
                qInfo() << "GPG warning:" << code << msg ;
                m_downloadErrorCode =  code;
                m_downloadErrorMsg = msg;
                emit downloadErrorCodeChanged();
                emit downloadErrorMsgChanged();

            });


    connect(worker, &TorInstallWorker::finished, m_installThread, &QThread::quit);


    connect(m_installThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(m_installThread, &QThread::finished, m_installThread, [this]{
        m_installThread->deleteLater();
        m_installThread = nullptr;
    });

    m_installThread->start();
    qInfo() << "Downloading Tor…";


    m_bootstrapped100 = false;
    m_addOnionIssued  = false;


    m_bootstrapProgress = 0;
    m_currentStatus = tr("Launching Tor…");



    emit bootstrapProgressChanged();
    emit currentStatusChanged();


}


TorBackend::LocalService TorBackend::createRouterForKnownOnion(const QString &onion) {
    LocalService svc;
    svc.onion  = onion.trimmed().toLower();
    svc.router = new MultisigApiRouter(m_msigMgr, m_acct, svc.onion, qApp);
    if (!svc.router->start(0)) {
        qWarning() << "[TorBackend] Router start failed for" << onion << ":" << svc.router->errorString();
        svc.router->deleteLater();
        svc.router=nullptr; return svc;
    }
    connect(svc.router, &MultisigApiRouter::requestReceived, this,
            [this](const QString &on, const QString &, const QString &) {
                const QString key = on.trimmed().toLower();
                if (key.isEmpty()) return;
                m_requestCounts[key] += 1;
                emit requestCountChanged(key, m_requestCounts[key]);
                emit requestCountsChanged();
            });

    svc.port   = svc.router->port();
    svc.online = true;
    return svc;
}

TorBackend::LocalService TorBackend::createRouterForNewLabel(const QString &label) {
    LocalService svc;
    svc.label = label;
    svc.router = new MultisigApiRouter(m_msigMgr, m_acct, QString(), qApp);
    if (!svc.router->start(0)) {
        qWarning() << "[TorBackend] Router start failed (NEW) label=" << label << ":" << svc.router->errorString();
        svc.router->deleteLater();
        svc.router=nullptr;
        return svc;
    }
    connect(svc.router, &MultisigApiRouter::requestReceived, this,
            [this](const QString &on, const QString &, const QString &) {
                const QString key = on.trimmed().toLower();
                if (key.isEmpty()) return;
                m_requestCounts[key] += 1;
                emit requestCountChanged(key, m_requestCounts[key]);
                emit requestCountsChanged();
            });

    svc.port = svc.router->port();
    return svc; }

TorBackend::LocalService* TorBackend::ensureServiceForOnion(const QString &onion) {
    const QString key = onion.trimmed().toLower();
    if (m_services.contains(key)) return &m_services[key];
    LocalService svc = createRouterForKnownOnion(key);
    if (!svc.router) return nullptr;
    m_services.insert(key, svc);
    return &m_services[key];
}

void TorBackend::stopAndDeleteService(const QString &onion) {
    const QString key = onion.trimmed().toLower();
    auto it = m_services.find(key);
    if (it==m_services.end()) return;
    if (it->router) { it->router->close();
        it->router->deleteLater();
        it->router=nullptr;
    }
    m_services.erase(it);

}


void TorBackend::launchTorWithBinary(const QString &torBin)
{
    const QString hsDir     = m_dataDir + "/hidden_service";
    const QString torrcPath = m_dataDir + "/torrc";
    const QString cookie    = m_dataDir + "/control_auth_cookie";
    QDir().mkpath(hsDir);

    try { writeTorrc(torrcPath, hsDir, cookie); }
    catch (const std::exception &e) { emit errorOccurred(QString::fromLatin1(e.what())); return; }


    const QString binDir = QFileInfo(torBin).dir().absolutePath();
    QString bundleRoot   = binDir;

    if (QDir(binDir + "/../lib").exists())
        bundleRoot = QFileInfo(binDir + "/..").absoluteFilePath();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

#if defined(Q_OS_LINUX)
    const QString libDir = bundleRoot + "/lib";
    if (QDir(libDir).exists()) {
        const QString cur = env.value("LD_LIBRARY_PATH");
        env.insert("LD_LIBRARY_PATH", libDir + (cur.isEmpty() ? "" : ":" + cur));
    }
#elif defined(Q_OS_MACOS)
    const QString libDir = bundleRoot + "/lib";
    if (QDir(libDir).exists()) {
        const QString cur = env.value("DYLD_LIBRARY_PATH");
        env.insert("DYLD_LIBRARY_PATH", libDir + (cur.isEmpty() ? "" : ":" + cur));
    }
#elif defined(Q_OS_WIN)
    // On Windows, shipped DLLs are usually next to tor.exe; still extend PATH with root and lib
    const QString libDir = bundleRoot + "\\lib";
    const QString cur = env.value("PATH");
    env.insert("PATH", bundleRoot + ";" + libDir + (cur.isEmpty() ? "" : ";" + cur));
#endif


    m_proc.setProcessEnvironment(env);
    m_proc.setWorkingDirectory(bundleRoot);


    connect(&m_proc, &QProcess::errorOccurred, this, [this, torBin](QProcess::ProcessError pe){
        qWarning() << "[TorBackend] QProcess error" << pe << m_proc.errorString()
        << "bin=" << torBin
        << "cwd=" << m_proc.workingDirectory();
        emit errorOccurred(tr("Failed to launch Tor"));
        emit statusTorChanged(tr("Failed to launch Tor – check permissions / libraries"));
        m_initializing = false; emit initializingChanged();
    });

    m_proc.start(torBin, { "-f", torrcPath }, QIODevice::ReadWrite | QIODevice::Text);
}


void TorBackend::stop()
{

    if (m_running == true || m_initializing == true){

        m_addOnionIssued  = false;
        m_bootstrapped100 = false;
        m_ctlBuffer.clear();

        m_ctl.abort();
        m_proc.terminate();
        m_proc.waitForFinished(3000);
        if (m_proc.state() != QProcess::NotRunning)
            m_proc.kill();

        m_bootstrapProgress = 0;
        m_currentStatus = "";
        m_initializing = false;
        m_running = false;
        emit bootstrapProgressChanged();
        emit currentStatusChanged();
        emit initializingChanged();
        return;
    }

    return;

}


void TorBackend::onStdOut()
{
    const QString chunk = QString::fromLocal8Bit(m_proc.readAllStandardOutput());


    emit logMessage(chunk);
    emit statusTorChanged(chunk);

    m_currentStatus = chunk;
    emit currentStatusChanged();


    for (const QString &line : chunk.split('\n', Qt::SkipEmptyParts)) {
        // qDebug() << "[TorBackend][stdout]" << line;
    }

    static QRegularExpression re("Bootstrapped.*?(\\d+)%");
    QRegularExpressionMatch match = re.match(chunk);
    if (match.hasMatch()) {
        int newProgress = match.captured(1).toInt();
        if (newProgress != m_bootstrapProgress) {
            m_bootstrapProgress = newProgress;
            emit bootstrapProgressChanged();
        }

        bool wasInitializing = m_initializing;
        m_initializing = (m_bootstrapProgress < 100);
        if (m_initializing != wasInitializing) {
            emit initializingChanged();
        }

        if (m_bootstrapProgress == 100) {
            m_running         = true;
            m_initializing = false;
            emit initializingChanged();
            emit runningChanged();
        }
    }

    if (!m_bootstrapped100 &&
        chunk.contains(QStringLiteral("Bootstrapped 100%"))) {

        m_bootstrapped100 = true;
        m_initializing = false;
        emit initializingChanged();
        QTimer::singleShot(500, this, [this]{ initiateControlSession(); });
        emit onionAddressesChanged();
    }


    if (chunk.contains("Hidden-service online")) {
        m_initializing = false;
        m_running      = true;
        emit initializingChanged();
        emit runningChanged();
    }
    if (chunk.toLower().contains("failed") || chunk.toLower().contains("error")) {
        m_initializing = false;
        m_running         = false;
        emit initializingChanged();
        emit runningChanged();
    }
}

void TorBackend::onStdErr()
{
    const QString chunk = QString::fromLocal8Bit(m_proc.readAllStandardError());
    emit logMessage(chunk);
    emit statusTorChanged(chunk);
    for (const QString &line : chunk.split('\n', Qt::SkipEmptyParts))
        qDebug() << "[TorBackend][stderr]" << line;
}

void TorBackend::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{

    m_running = false;
    m_bootstrapProgress = 0;
    m_currentStatus = "";
    m_initializing = false;
    m_addOnionIssued  = false;
    m_bootstrapped100 = false;

    emit runningChanged();
    emit bootstrapProgressChanged();
    emit currentStatusChanged();
    emit initializingChanged();
    emit stopped();
}

// ──────────────────────────────────────────────────────────────────────────────
void TorBackend::initiateControlSession()
{

    if (m_ctl.state() != QAbstractSocket::UnconnectedState)
        m_ctl.abort();
    m_ctl.connectToHost(QHostAddress::LocalHost, m_controlPort);
}

void TorBackend::onCtlConnected()
{

    QFile cookie(m_dataDir + QLatin1String("/control_auth_cookie"));
    if (!cookie.open(QIODevice::ReadOnly)) {

        emit errorOccurred(tr("Cannot open control cookie"));
        return;
    }
    const QByteArray hex = cookie.readAll().toHex();

    m_ctl.write("AUTHENTICATE " + hex + "\r\n");
}

void TorBackend::onCtlReadyRead()
{
    const QByteArray incoming = m_ctl.readAll();
    m_ctlBuffer += incoming;
    const QList<QByteArray> lines = m_ctlBuffer.split('\n');


    if (!m_ctlBuffer.endsWith('\n'))
        m_ctlBuffer = lines.last();
    else
        m_ctlBuffer.clear();

    for (QByteArray raw : lines) {
        raw = raw.trimmed();
        if (raw.isEmpty()) continue;

        const QString line = QString::fromLatin1(raw);

        emit logMessage(QString::fromLatin1("[CTL] %1").arg(line));


        if (line == "250 OK" && !m_addOnionIssued) {

            m_addOnionIssued = true;
            sendAddOnionCmd();
            continue;
        }


        static QRegularExpression rxSvc("^250-ServiceID=(\\w+)$");
        static QRegularExpression rxKey(QLatin1String(R"(^250-PrivateKey=(.+)$)"));

        QRegularExpressionMatch m;

        if ((m = rxSvc.match(line)).hasMatch()) {
            m_onionAddress = m.captured(1) + QLatin1String(".onion");

            m_blockHadPrivateKey = false;
            continue;
        }
        if ((m = rxKey.match(line)).hasMatch()) {
            m_privateKey = m.captured(1);

            m_blockHadPrivateKey = true;
            continue;
        }

        if (line == "250 OK") {
            if (!m_onionAddress.isEmpty()) {

                QString labelForThis;
                if (m_blockHadPrivateKey && !m_pendingNewLabels.isEmpty())
                    labelForThis = m_pendingNewLabels.takeFirst();

                int idx = -1;
                for (int i=0;i<m_pendingNew.size();++i) {
                    if (m_pendingNew[i].label.compare(labelForThis, Qt::CaseInsensitive)==0)
                    { idx=i; break; }
                }
                if (idx<0 && !m_pendingNew.isEmpty()) idx = 0;

                if (idx>=0) {
                    LocalService svc = m_pendingNew.takeAt(idx);
                    if (svc.router) svc.router->setBoundOnion(m_onionAddress);
                    svc.onion = m_onionAddress;
                    svc.online = true;
                    m_services.insert(m_onionAddress.toLower(), std::move(svc));
                }

                applyNewOnion(m_onionAddress, m_privateKey, labelForThis);

                m_onionAddress.clear();
                m_privateKey.clear();
                m_blockHadPrivateKey = false;

                emit onionAddressesChanged();
            } else {
                emit onionAddressesChanged();
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
void TorBackend::sendAddOnionCmd()
{

    if (!m_acct) return;
    const QVariantList ids = m_acct->getTorIdentities();

    QByteArray batch;

    for (const QVariant &v : ids) {
        const QVariantMap m = v.toMap();
        const bool online   = m.value("online").toBool();
        const QString label = m.value("label").toString();
        if (!online) {

            continue;
        }

        const QString onion = m.value("onion").toString().trimmed();
        const QString key   = m_acct->torPrivKeyFor(onion);

        if (!key.isEmpty()) {

            // Ensure dedicated router/port for this onion
            LocalService *svc = ensureServiceForOnion(onion);
            if (!svc || !svc->router) continue;
            QByteArray cmd = "ADD_ONION ";
            cmd += key.toLatin1();
            cmd += " Port=80,127.0.0.1:";
            cmd += QByteArray::number(svc->port);
            cmd += " Flags=DiscardPK\r\n";
            batch += cmd;




        } else {

            // NEW: pre-create a router to obtain a dedicated local port
            LocalService svc = createRouterForNewLabel(label);
            if (!svc.router) continue;
            QByteArray cmd = "ADD_ONION NEW:ED25519-V3 Port=80,127.0.0.1:";
            cmd += QByteArray::number(svc.port);
            cmd += "\r\n";
            batch += cmd;
            m_pendingNewLabels << label;
            m_pendingNew.push_back(std::move(svc));


        }
    }

    if (!batch.isEmpty()) {
        m_ctl.write(batch);
    } else {
        qDebug() << "[TorBackend] sendAddOnionCmd: nothing to do";
    }
}

void TorBackend::applyNewOnion(const QString &serviceId,
                               const QString &privKey,
                               const QString &preferredLabel)
{

    m_onionAddress = serviceId;
    emit onionAddressChanged(serviceId);
    emit statusTorChanged(tr("Hidden-service online: %1").arg(serviceId));
    emit started();


    if (!m_acct) return;

    m_acct->storeTorIdentity(serviceId, privKey, preferredLabel, true);


}

QStringList TorBackend::onionAddresses() const
{
    QStringList out;
    if (!m_acct) return out;

    const QVariantList ids = m_acct->getTorIdentities();
    for (const QVariant &v : ids) {
        const QVariantMap m = v.toMap();
        const QString onion = m.value("onion").toString();
        const bool online   = m.value("online").toBool();
        if (!onion.isEmpty() && online)
            out << onion;
    }

    return out;
}

bool TorBackend::addNewService(const QString &label)
{

    if (!m_acct) return false;

    const QString clean = label.trimmed();

    QString finalLabel = clean;
    const QVariantList ids = m_acct->getTorIdentities();
    auto labelExists = [&](const QString &l)->bool {
        for (const auto &v : ids) {
            if (v.toMap().value("label").toString().compare(l, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    };
    if (!finalLabel.isEmpty() && labelExists(finalLabel)) {
        bool hadPlaceholder = false;
        for (const auto &v : ids) {
            const auto m = v.toMap();
            if (m.value("label").toString().compare(finalLabel, Qt::CaseInsensitive) == 0
                && m.value("onion").toString().isEmpty()) {
                hadPlaceholder = true;
                break;
            }
        }
        if (!hadPlaceholder) {
            int n = 2;
            while (labelExists(QString("%1-%2").arg(finalLabel).arg(n))) ++n;
            finalLabel = QString("%1-%2").arg(finalLabel).arg(n);
        }
    }


    if (!finalLabel.isEmpty()) {
        m_acct->addTorIdentity(finalLabel);
        m_acct->setPlaceholderOnlineByLabel(finalLabel, true);
    } else {

        m_acct->storeTorIdentity(QString(), QString(), QString(),  true);
    }

    if (m_running && (m_ctl.state() == QAbstractSocket::ConnectedState)) {
        LocalService svc = createRouterForNewLabel(finalLabel);
        if (svc.router) {
            QByteArray cmd = "ADD_ONION NEW:ED25519-V3 Port=80,127.0.0.1:";
            cmd += QByteArray::number(svc.port);
            cmd += "\r\n";
            m_ctl.write(cmd);
            m_pendingNewLabels << finalLabel;
            m_pendingNew.push_back(std::move(svc));

        }


    } else {
        qDebug() << "[TorBackend] addNewService: deferred until Tor auth";
    }

    return true;
}

bool TorBackend::setServiceOnline(const QString &onion, bool online)
{

    if (!m_acct) return false;

    const QString svc = onion.trimmed();
    if (svc.isEmpty()) return false;

    if (!m_acct->setTorIdentityOnline(svc, online))
        return false;
    qDebug() << "[TorBackend] m_running" << m_running << "m_ctl.state()"<< m_ctl.state();
    if (m_running && (m_ctl.state() == QAbstractSocket::ConnectedState)) {
        if (online) {
            const QString key = m_acct->torPrivKeyFor(svc);
            if (key.isEmpty()) {
                LocalService pending = createRouterForNewLabel(QString());
                if (pending.router) {
                    QByteArray cmd = "ADD_ONION NEW:ED25519-V3 Port=80,127.0.0.1:";
                    cmd += QByteArray::number(pending.port);
                    cmd += "\r\n";
                    m_ctl.write(cmd);
                    m_pendingNewLabels << QString();
                    m_pendingNew.push_back(std::move(pending));

                }
            } else {
                LocalService *ls = ensureServiceForOnion(svc);
                if (ls && ls->router) {
                    QByteArray cmd = "ADD_ONION ";
                    cmd += key.toLatin1();
                    cmd += " Port=80,127.0.0.1:";
                    cmd += QByteArray::number(ls->port);
                    cmd += " Flags=DiscardPK\r\n";
                    m_ctl.write(cmd);
                }
            }

        } else {
            QString sid = svc;
            if (sid.endsWith(".onion", Qt::CaseInsensitive))
                sid.chop(6);
            QByteArray cmd = "DEL_ONION ";
            cmd += sid.toLatin1();
            cmd += "\r\n";
            m_ctl.write(cmd);
            stopAndDeleteService(svc);


        }
    } else {
        qDebug() << "[TorBackend] setServiceOnline: Tor not connected; persisted only";
    }

    return true;
}

void TorBackend::ensureDefaultService()
{
    if (!m_acct) return;

    const QVariantList ids = m_acct->getTorIdentities();
    if (!ids.isEmpty()) {

        return;
    }

    addNewService(QStringLiteral("main"));
}

bool TorBackend::removeService(const QString &onion)
{

    if (!m_acct) return false;

    const QString svc = onion.trimmed();
    if (svc.isEmpty()) return false;

    if (m_running && (m_ctl.state() == QAbstractSocket::ConnectedState)) {
        QString sid = svc;
        if (sid.endsWith(".onion", Qt::CaseInsensitive))
            sid.chop(6);
        QByteArray cmd = "DEL_ONION ";
        cmd += sid.toLatin1();
        cmd += "\r\n";
        m_ctl.write(cmd);

    } else {
        qDebug() << "[TorBackend] removeService: Tor not connected; removing from storage only";
    }

    const bool ok = m_acct->removeTorIdentity(svc);
    stopAndDeleteService(svc);
    return ok;
}

QVariantMap TorBackend::requestCounts() const {
    QVariantMap out;
    for (auto it = m_requestCounts.constBegin(); it != m_requestCounts.constEnd(); ++it)
        out[it.key()] = static_cast<qulonglong>(it.value());
    return out;
}

void TorBackend::resetRequestCounts() {
    m_requestCounts.clear();
    emit requestCountsChanged();
}
void TorBackend::resetRequestCount(const QString &onion) {
    const QString key = onion.trimmed().toLower();
    m_requestCounts.remove(key);
    emit requestCountsChanged();
}


void TorBackend::startIfAutoconnect()
{
    if (!m_acct) return;
    if (m_acct->isAuthenticated() && m_acct->torAutoconnect()) {
        start();
    }
}

void TorBackend::reset()
{

    stop();

    for (auto it = m_services.begin(); it != m_services.end(); ++it) {
        if (it->router) {
            it->router->close();
        }
    }
    m_services.clear();
    m_pendingNew.clear();
    m_pendingNewLabels.clear();

    m_requestCounts.clear();
    m_onionAddress.clear();
    m_running = false;
    m_bootstrapProgress = 0;
    m_currentStatus.clear();
    m_initializing = false;
    m_downloadErrorCode.clear();
    m_downloadErrorMsg.clear();

    emit onionAddressChanged(QString());
    emit onionAddressesChanged();
    emit requestCountsChanged();
    emit runningChanged();
    emit bootstrapProgressChanged();
    emit currentStatusChanged();
    emit initializingChanged();
    emit downloadErrorCodeChanged();
    emit downloadErrorMsgChanged();
}



