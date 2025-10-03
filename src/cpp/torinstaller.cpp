#include "torinstaller.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QProcess>
#include <QFileInfo>
#include <QDirIterator>
#include <QSysInfo>
#include <QTextStream>
#include <QSaveFile>
#include <QDebug>

static QString appGnupgHome()
{
#ifdef Q_OS_WIN
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
#else
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
    const QString hd = base + "/gnupg";
    QDir().mkpath(hd);
#ifndef Q_OS_WIN
    QFile::setPermissions(hd, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif
    return hd;
}

static QString findGpgLike()
{
    // Prefer full gpg (has WKD), then gpgv
    QString bin = QStandardPaths::findExecutable("gpg");
    if (bin.isEmpty()) bin = QStandardPaths::findExecutable("gpgv");

#ifdef Q_OS_WIN
    auto tryPath = [](const QString &p){ return QFileInfo(p).isFile() ? p : QString(); };
    if (bin.isEmpty()) {
        bin = tryPath("C:/Program Files/GnuPG/bin/gpg.exe");
        if (bin.isEmpty()) bin = tryPath("C:/Program Files/GnuPG/bin/gpgv.exe");
        if (bin.isEmpty()) bin = tryPath("C:/Program Files (x86)/GnuPG/bin/gpg.exe");
        if (bin.isEmpty()) bin = tryPath("C:/Program Files (x86)/GnuPG/bin/gpgv.exe");
    }
#endif
    const QByteArray envBin = qgetenv("MYAPP_GPG_BIN");
    if (!envBin.isEmpty() && QFileInfo(QString::fromUtf8(envBin)).isFile())
        bin = QString::fromUtf8(envBin);

    return bin; // may be empty
}

static QString appDataTorRoot()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/tor-bin";
    qDebug() << "TorInstaller: AppData tor root:" << root;
    return root;
}

static bool exportPinnedKeyring(const QString &hd,
                                const QStringList &allowedFprs,
                                QString *err)
{
    const QString gpg = QStandardPaths::findExecutable("gpg");
    if (gpg.isEmpty()) { if (err) *err = "gpg not found for export"; return false; }

    QStringList args = {"--homedir", hd, "--output", hd + "/tor.keyring", "--export"};
    if (!allowedFprs.isEmpty()) args << allowedFprs;

    QProcess p;
    p.start(gpg, args);
    if (!p.waitForFinished(15000) || p.exitCode()!=0) {
        if (err) *err = "gpg --export failed: " + QString::fromUtf8(p.readAllStandardError());
        return false;
    }
    return QFileInfo::exists(hd + "/tor.keyring");
}

TorInstaller::TorInstaller(QObject *parent) : QObject(parent)
{
    qDebug() << "";
}

QString TorInstaller::installRoot() const
{
    if (!m_installRoot.isEmpty())
        return m_installRoot;
    return appDataTorRoot();
}

QString TorInstaller::osToken() const
{
    QString os;
#if defined(Q_OS_WIN)
    os = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    os = QStringLiteral("macos");
#else
    os = QStringLiteral("linux");
#endif
    qDebug() << "TorInstaller: Detected OS token:" << os;
    return os;
}

QString TorInstaller::archToken() const
{
    const QString cpu = QSysInfo::currentCpuArchitecture().toLower();
    qDebug() << "TorInstaller: Raw CPU architecture:" << cpu;

    QString arch;
    if (cpu.contains("64") && (cpu.contains("x86") || cpu.contains("amd")))
        arch = QStringLiteral("x86_64");
    else if (cpu.contains("arm") || cpu.contains("aarch"))
        arch = QStringLiteral("aarch64");
    else
        arch = QStringLiteral("i686");

    qDebug() << "TorInstaller: Mapped to arch token:" << arch;
    return arch;
}

bool TorInstaller::alreadyInstalled(QString &torBin) const
{
#ifdef Q_OS_WIN
    const QString exeName = "tor.exe";
#else
    const QString exeName = "tor";
#endif
    qDebug() << "TorInstaller: Checking if already installed, looking for:" << exeName;

    const QString root = installRoot();
    QDir d(root);
    if (!d.exists()) {
        qDebug() << "TorInstaller: Install root directory does not exist:" << root;
        return false;
    }

    const QString os = osToken();
    const QString arch = archToken();
    const QString base = root + "/" + os + "-" + arch;
    qDebug() << "TorInstaller: Looking in base directory:" << base;

    QDir baseDir(base);
    if (!baseDir.exists()) {
        qDebug() << "TorInstaller: Platform-specific directory does not exist:" << base;
        return false;
    }

    QDirIterator it(base, QStringList() << exeName, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        qDebug() << "TorInstaller: Found potential tor binary:" << p;

#ifndef Q_OS_WIN
        QFile f(p);
        if (!QFileInfo(f).isExecutable()) {
            qDebug() << "TorInstaller: File not executable, skipping:" << p;
            continue;
        }
#endif
        torBin = p;
        qDebug() << "TorInstaller: Using existing tor binary:" << torBin;
        return true;
    }
    qDebug() << "TorInstaller: No existing tor binary found";
    return false;
}

QByteArray TorInstaller::httpGet(const QUrl &url, QString *err, int timeoutMs)
{
    qDebug() << "TorInstaller: HTTP GET:" << url.toString() << "timeout:" << timeoutMs << "ms";

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *rep = m_nam.get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, [&]{
        qDebug() << "TorInstaller: HTTP request timed out for:" << url.toString();
        if (rep) rep->abort();
    });
    connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    QByteArray data;
    if (rep->error() != QNetworkReply::NoError) {
        QString errorMsg = QString("GET %1 failed: %2").arg(url.toString(), rep->errorString());
        qDebug() << "TorInstaller: HTTP error:" << errorMsg;
        if (err) *err = errorMsg;
    } else {
        data = rep->readAll();
        qDebug() << "TorInstaller: HTTP GET successful, received" << data.size() << "bytes";
    }
    rep->deleteLater();
    return data;
}

bool TorInstaller::httpGetToFile(const QUrl &url, const QString &dstFile, QString *err, int timeoutMs)
{
    qDebug() << "TorInstaller: HTTP GET to file:" << url.toString() << "-> " << dstFile;

    QByteArray payload = httpGet(url, err, timeoutMs);
    if (payload.isEmpty()) {
        qDebug() << "TorInstaller: HTTP GET returned empty payload";
        return false;
    }

    QSaveFile f(dstFile);
    if (!f.open(QIODevice::WriteOnly)) {
        QString errorMsg = QString("Cannot open %1 for write").arg(dstFile);
        qDebug() << "TorInstaller: File open error:" << errorMsg;
        if (err) *err = errorMsg;
        return false;
    }
    f.write(payload);
    if (!f.commit()) {
        QString errorMsg = QString("Cannot commit %1").arg(dstFile);
        qDebug() << "TorInstaller: File commit error:" << errorMsg;
        if (err) *err = errorMsg;
        return false;
    }
    qDebug() << "TorInstaller: Successfully saved" << payload.size() << "bytes to" << dstFile;
    return true;
}

bool TorInstaller::findLatestTorBundle(QString *outVersion,
                                       QString *outFileName,
                                       QUrl    *outBaseUrl,
                                       QString *err)
{
    const QUrl root("https://dist.torproject.org/torbrowser/");
    const QByteArray index = httpGet(root, err, 120000);
    if (index.isEmpty()) return false;

    QRegularExpression rxHref(R"(href="([0-9]+\.[0-9]+\.[0-9]+(?:\.[0-9]+)?)\/")");
    QSet<QString> versions;
    for (auto it = rxHref.globalMatch(QString::fromUtf8(index)); it.hasNext();) {
        versions.insert(it.next().captured(1));
    }
    if (versions.isEmpty()) { if (err) *err = "No torbrowser versions found."; return false; }

    QVersionNumber best;
    for (const QString &v : versions) best = qMax(best, QVersionNumber::fromString(v));
    const QString ver = best.toString();
    if (ver.isEmpty()) { if (err) *err = "Could not pick a latest version."; return false; }

    const QString os   = osToken();     // windows | macos | linux
    const QString arch = archToken();   // x86_64 | aarch64 | i686

    // tor-expert-bundle-<os>-<arch>-<ver>.tar.gz
    const QString fileName =
        QString("tor-expert-bundle-%1-%2-%3.tar.gz").arg(os, arch, ver);

    *outVersion  = ver;
    *outFileName = fileName;
    *outBaseUrl  = root.resolved(QUrl(ver + "/"));
    return true;
}

bool TorInstaller::parseSha256Sums(const QByteArray &sumsTxt,
                                   const QString &wantedFileName,
                                   QByteArray *outSha256Hex) const
{
    qDebug() << "TorInstaller: Parsing SHA256 sums for file:" << wantedFileName;
    qDebug() << "TorInstaller: SHA256 sums content size:" << sumsTxt.size() << "bytes";

    QString text = QString::fromUtf8(sumsTxt);
    QTextStream ts(&text);

    static const QRegularExpression rx(
        R"(^\s*([A-Fa-f0-9]{64})\s+\*?(.+)\s*$)"
        );

    int lineNum = 0;
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        lineNum++;
        qDebug() << "TorInstaller: SHA256 line" << lineNum << ":" << line;

        const auto m = rx.match(line);
        if (!m.hasMatch()) {
            qDebug() << "TorInstaller: Line does not match expected format";
            continue;
        }

        const QString hashHex = m.captured(1);
        const QString file    = m.captured(2);

        qDebug() << "TorInstaller: Parsed - file:" << file << "hash:" << hashHex;

        if (file.endsWith(wantedFileName)) {
            *outSha256Hex = hashHex.toLatin1().toLower();
            qDebug() << "TorInstaller: Found matching hash for" << wantedFileName << ":" << *outSha256Hex;
            return true;
        }
    }
    qDebug() << "TorInstaller: No matching file found in SHA256 sums";
    return false;
}

bool TorInstaller::verifyFileSha256(const QString &filePath, const QByteArray &wantHex, QString *err) const
{
    qDebug() << "TorInstaller: Verifying SHA256 for:" << filePath;
    qDebug() << "TorInstaller: Expected hash:" << wantHex;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        QString errorMsg = QString("Cannot open %1 for hashing").arg(filePath);
        qDebug() << "TorInstaller: File open error:" << errorMsg;
        if (err) *err = errorMsg;
        return false;
    }

    QCryptographicHash h(QCryptographicHash::Sha256);
    qint64 totalBytes = 0;
    while (!f.atEnd()) {
        QByteArray chunk = f.read(1 << 20);
        totalBytes += chunk.size();
        h.addData(chunk);
    }
    qDebug() << "TorInstaller: Hashed" << totalBytes << "bytes";

    const QByteArray gotHex = h.result().toHex();
    qDebug() << "TorInstaller: Computed hash:" << gotHex;

    if (!wantHex.isEmpty() && !qstrcmp(gotHex.constData(), wantHex.constData())) {
        qDebug() << "TorInstaller: SHA256 verification successful";
        return true;
    }

    QString errorMsg = QString("SHA256 mismatch for %1 (got %2, want %3)")
                           .arg(filePath, QString::fromLatin1(gotHex), QString::fromLatin1(wantHex));
    qDebug() << "TorInstaller: SHA256 verification failed:" << errorMsg;
    if (err) *err = errorMsg;
    return false;
}

bool TorInstaller::verifyChecksumsWithGpg(const QString &sumsPath,
                                          const QString &ascPath,
                                          QString *err) const
{
    qDebug() << "TorInstaller: Verifying GPG signature";
    const QString tool = findGpgLike();
    const QString hd   = appGnupgHome();

    auto warn = [&](const GpgCheckResult code, const QString &msg){
        qWarning() << "GPG warn:" << msg ;
        emit gpgWarning(QString::number(static_cast<int>(code)), msg); // <-- signal should be 'const'
        if (err && !msg.isEmpty()) *err = msg;
    };

    if (tool.isEmpty()) {
        warn(GpgCheckResult::NotInstalled,
             "OpenPGP tool not found. Install GnuPG (Linux: package 'gnupg'; Windows: Gpg4win) to enable signature verification. Download will proceed relying only on hash verification vs file in tor project website." );
        return !m_requireGpg;
    }

    const bool isGpgv = QFileInfo(tool).fileName().toLower().startsWith("gpgv");

    // Path A: gpgv + pinned keyring (and try to bootstrap it if missing)
    const QString pinnedRing = hd + "/tor.keyring";
    if (isGpgv) {
        if (!QFileInfo::exists(pinnedRing)) {
            // Try one-time bootstrap: gpg + WKD -> export pinned keyring
            QString wkderr;
            if (bootstrapTorKeyViaWkd(&wkderr)) {
                QString expErr;
                if (!exportPinnedKeyring(hd, m_allowedFingerprints, &expErr)) {
                    warn(GpgCheckResult::NotAttempted,
                         "Cannot verify: gpgv found; failed to create pinned keyring. Download will proceed relying only on hash verification vs file in tor project website.");
                    return !m_requireGpg;
                }
            } else {
                warn(GpgCheckResult::NoKey,
                     "Cannot verify: gpgv found; no pinned keyring and WKD unavailable. Download will proceed relying only on hash verification vs file in tor project website.");
                return !m_requireGpg;
            }
        }

        QProcess p;
        p.start(tool, {"--keyring", pinnedRing, ascPath, sumsPath});
        if (!p.waitForFinished(60000) || p.exitCode()!=0) {
            const QString out = QString::fromUtf8(p.readAllStandardError())
            + QString::fromUtf8(p.readAllStandardOutput());
            warn(GpgCheckResult::BadSignature, "Signature verification failed. Download will proceed relying only on hash verification vs file in tor project website.");
            return !m_requireGpg;
        }
        qDebug() << "TorInstaller: GPGV verification successful with pinned keyring";
        return true; // <-- IMPORTANT
    }

    // Path B: full gpg (WKD + verify + fingerprint pin)
    {
        QString wkderr;
        if (!bootstrapTorKeyViaWkd(&wkderr)) {
            warn(GpgCheckResult::NoKey, "Download will proceed relying on file hash verification vs file in tor project website.");
            return !m_requireGpg;
        }

        QProcess p;
        p.start(tool, {"--homedir", hd, "--batch", "--status-fd", "1",
                       "--verify", ascPath, sumsPath});
        if (!p.waitForFinished(60000) || p.exitStatus()!=QProcess::NormalExit) {
            warn(GpgCheckResult::TimeoutOrError, "gpg --verify failed or timed out. Download will proceed relying only on hash verification vs file in tor project website.");
            return !m_requireGpg;
        }
        const QString out = QString::fromUtf8(p.readAllStandardError())
                            + QString::fromUtf8(p.readAllStandardOutput());

        if (!out.contains("GOODSIG")) {
            warn(GpgCheckResult::BadSignature, "Signature not valid. Download will proceed relying only on hash verification vs file in tor project website.");
            return !m_requireGpg;
        }

        // ---- Parse VALIDSIG: accept subkey OR primary (robust to rotations)
        QString sigFpr, primaryFpr;
        for (const QString &line : out.split('\n')) {
            if (!line.startsWith("[GNUPG:] VALIDSIG ")) continue;
            const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 3) sigFpr = parts[2].toUpper();

            // Last 40-hex token is typically the primary
            static const QRegularExpression hex40(R"(^[0-9A-Fa-f]{40}$)");
            for (int i = parts.size() - 1; i >= 2; --i) {
                if (hex40.match(parts[i]).hasMatch()) {
                    primaryFpr = parts[i].toUpper();
                    break;
                }
            }
            break;
        }

        // Normalize allowed list to uppercase for comparison
        QStringList allowed;
        allowed.reserve(m_allowedFingerprints.size());
        for (const QString &fp : m_allowedFingerprints) allowed << fp.toUpper();

        const bool okSigner = allowed.isEmpty()
                                  ? true
                                  : (allowed.contains(primaryFpr) || allowed.contains(sigFpr));
        if (!okSigner) {
            warn(GpgCheckResult::UnexpectedSigner,
                 QString("Signature made by unexpected key. Got signer=%1 primary=%2 Allowed: %3. Download will proceed relying only on hash verification vs file in tor project website.")
                     .arg(sigFpr, primaryFpr, allowed.join(", ")));
            return !m_requireGpg;
        }

        qDebug() << "TorInstaller: GPG verification successful; signer" << (primaryFpr.isEmpty() ? sigFpr : primaryFpr);

        // Best-effort: export a pinned keyring so future runs can use gpgv offline
        QString expErr;
        (void)exportPinnedKeyring(hd, allowed, &expErr);

        return true; // <-- IMPORTANT
    }

    // Should never reach here; keep as defensive fallback
    warn(GpgCheckResult::NotAttempted,
         "Cannot verify (unexpected fall-through). Internal flow error.");
    return !m_requireGpg;
}



bool TorInstaller::extractArchive(const QString &archivePath, const QString &dstDir, QString *err) const
{
    if (!QDir().mkpath(dstDir)) {
        if (err) *err = QString("Failed to create directory: %1").arg(dstDir);
        return false;
    }

    // Prefer a concrete tar on Windows; otherwise PATH.
    QString tarBin = QStandardPaths::findExecutable("tar");
#ifdef Q_OS_WIN
    const QString systemTar = qEnvironmentVariable("SystemRoot") + "/System32/tar.exe";
    if (QFileInfo::exists(systemTar)) tarBin = systemTar;
#endif
    if (tarBin.isEmpty()) {
        if (err) *err = "Could not find 'tar' in PATH (required to extract .tar.gz).";
        return false;
    }

    // Probe tar flavor
    QProcess probe;
    probe.start(tarBin, {"--version"});
    probe.waitForFinished(3000);
    const QString verOut = QString::fromUtf8(probe.readAllStandardOutput())
                           + QString::fromUtf8(probe.readAllStandardError());
    const bool isGnuTar   = verOut.contains("GNU tar", Qt::CaseInsensitive);
    const bool isBsdtar   = verOut.contains("bsdtar", Qt::CaseInsensitive)
                          || verOut.contains("libarchive", Qt::CaseInsensitive);

    // Normalize paths (avoid backslash escapes)
    auto fwd = [](const QString &p){ return QDir::fromNativeSeparators(p); };
    QString file = fwd(archivePath);
    QString out  = fwd(dstDir);

#ifdef Q_OS_WIN
    // If we're using MSYS/Git GNU tar, convert C:/... -> /c/...
    const bool looksLikeMsys = tarBin.contains("/usr/bin/tar", Qt::CaseInsensitive)
                               || tarBin.contains("\\usr\\bin\\tar", Qt::CaseInsensitive)
                               || isGnuTar; // conservative: treat GNU tar as MSYS-ish on Windows
    if (looksLikeMsys && file.size() > 1 && file[1] == ':') {
        auto toMsys = [](QString p){
            p = QDir::fromNativeSeparators(p);    // C:/Users/...
            return "/" + p.left(1).toLower() + p.mid(2); // -> /c/Users/...
        };
        file = toMsys(file);
        out  = toMsys(out);
    }
#endif

    // Build args depending on tar flavor
    QStringList args;
#ifdef Q_OS_WIN
    if (isGnuTar) {
        // GNU tar needs -z for .tar.gz and supports --force-local
        args << "-xzf" << file << "-C" << out << "--no-same-owner" << "--no-same-permissions" << "--force-local";
    } else {
        // bsdtar (Windows tar.exe) auto-detects gzip and doesn't support --force-local
        args << "-xf" << file << "-C" << out << "--no-same-owner" << "--no-same-permissions";
    }
#else
    // On Linux/macOS, -z is fine everywhere
    args << "-xzf" << file << "-C" << out;
#endif

    QProcess p;
    p.start(tarBin, args);
    if (!p.waitForFinished(10 * 60 * 1000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        const QString so = QString::fromUtf8(p.readAllStandardOutput());
        const QString se = QString::fromUtf8(p.readAllStandardError());
        if (err) *err = QString("tar extraction failed (code %1)\nstdout:\n%2\nstderr:\n%3")
                       .arg(p.exitCode()).arg(so, se);
        return false;
    }
    return true;
}


QString TorInstaller::findTorBinaryIn(const QString &root) const
{
#ifdef Q_OS_WIN
    const QString exe = "tor.exe";
#else
    const QString exe = "tor";
#endif

    auto isBadPath = [](const QString &p){
        return p.contains("/debug/") || p.contains("\\debug\\")
        || p.contains("/.build-id/") || p.contains("/usr/lib/debug/")
            || p.endsWith(".debug");
    };

    {
        QDirIterator it(root, QStringList() << exe, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            if (!p.contains("/bin/") && !p.contains("\\bin\\")) continue;
#ifndef Q_OS_WIN
            if (!QFileInfo(p).isExecutable()) continue;
#endif
            if (isBadPath(p)) continue;
            qDebug() << "TorInstaller: Using tor binary:" << p;
            return p;
        }
    }

    QDirIterator it(root, QStringList() << exe, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
#ifndef Q_OS_WIN
        if (!QFileInfo(p).isExecutable()) continue;
#endif
        if (isBadPath(p)) continue;
        qDebug() << "TorInstaller: Using tor binary:" << p;
        return p;
    }

    qDebug() << "TorInstaller: No tor binary found in:" << root;
    return QString();
}

bool TorInstaller::downloadAndInstall(QString &torBin, QString *err)
{
    qDebug() << "TorInstaller: Starting download and install process";

    QString version, wantedFile, sumsFileName = "sha256sums-unsigned-build.txt";
    QUrl baseUrl;
    if (!findLatestTorBundle(&version, &wantedFile, &baseUrl, err)) {
        qDebug() << "TorInstaller: Failed to find latest bundle";
        return false;
    }

    qDebug() << "TorInstaller: Target bundle - version:" << version << "file:" << wantedFile;

    QUrl sumsUrl = baseUrl.resolved(QUrl("sha256sums-unsigned-build.txt"));
    qDebug() << "TorInstaller: Downloading checksums from:" << sumsUrl.toString();

    QByteArray sumsTxt = httpGet(sumsUrl, err);

    if (sumsTxt.isEmpty()) {
        sumsUrl = baseUrl.resolved(QUrl("sha256sums-signed-build.txt"));
        qDebug() << "TorInstaller: Failed to download checksums";
        sumsTxt = httpGet(sumsUrl, err);
        if (sumsTxt.isEmpty()) return false;
    }

    const QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/tor-dl";
    qDebug() << "TorInstaller: Using temporary directory:" << tmpDir;

    QDir().mkpath(tmpDir);
    const QString sumsPath = tmpDir + "/sha256sums.txt";
    const QString ascPath  = tmpDir + "/sha256sums.txt.asc";

    qDebug() << "TorInstaller: Saving checksums to:" << sumsPath;
    {
        QSaveFile f(sumsPath);
        f.open(QIODevice::WriteOnly);
        f.write(sumsTxt);
        f.commit();
    }

    QUrl ascUrl = sumsUrl;
    ascUrl.setPath(ascUrl.path() + ".asc");
    qDebug() << "TorInstaller: Attempting to download signature from:" << ascUrl.toString();

    QString gerr;
    if (httpGetToFile(ascUrl, ascPath, &gerr)) {
        qDebug() << "TorInstaller: Signature downloaded, verifying...";
        QString gpgErr;
        if (!verifyChecksumsWithGpg(sumsPath, ascPath, &gpgErr)) {
            qDebug() << "TorInstaller: GPG verification failed:" << gpgErr;
            if (m_requireGpg) {
                if (err) *err = gpgErr;
                return false;
            }
        }
    } else {
        qDebug() << "TorInstaller: Signature download failed:" << gerr;
        if (m_requireGpg) {
            if (err) *err = "Checksum signature file not available but GPG verification required.";
            return false;
        }
    }

    QByteArray wantShaHex;
    if (!parseSha256Sums(sumsTxt, wantedFile, &wantShaHex)) {
        QString errorMsg = QString("Could not find %1 in sha256sums.txt").arg(wantedFile);
        emit gpgWarning( "4", errorMsg);
        qDebug() << "TorInstaller: Checksum parsing failed:" << errorMsg;
        if (err) *err = errorMsg;
        return false;
    }

    const QUrl bundleUrl = baseUrl.resolved(QUrl(wantedFile));
    const QString archivePath = tmpDir + "/" + wantedFile;
    qDebug() << "TorInstaller: Downloading bundle from:" << bundleUrl.toString();
    qDebug() << "TorInstaller: Saving to:" << archivePath;

    if (!httpGetToFile(bundleUrl, archivePath, err)) {
        qDebug() << "TorInstaller: Bundle download failed";
        emit gpgWarning( "4",  "TorInstaller: Bundle download failed");
        return false;
    }

    qDebug() << "TorInstaller: Verifying downloaded file...";
    if (!verifyFileSha256(archivePath, wantShaHex, err)) {
        qDebug() << "TorInstaller: File verification failed";
        emit gpgWarning( "4",  "TorInstaller: File verification failed");
        return false;
    }

    const QString os = osToken(), arch = archToken();
    const QString dstBase = installRoot() + "/" + os + "-" + arch;
    qDebug() << "TorInstaller: Installing to:" << dstBase;

    QDir().mkpath(dstBase);
    QLockFile lock(dstBase + "/.lock");
    lock.setStaleLockTime(5000);
    if (!lock.tryLock(60000)) {
        qDebug() << "TorInstaller: Failed to acquire lock";
        if (err) *err = "Another install process is running";
        emit gpgWarning( "4",  "TorInstaller: Another install process is running");
        return false;
    }
    qDebug() << "TorInstaller: Lock acquired";

    qDebug() << "TorInstaller: Cleaning old installation...";
    QDir dst(dstBase);
    for (const auto &entry : dst.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        if (entry == ".lock") continue;
        qDebug() << "TorInstaller: Removing old entry:" << entry;
        QFileInfo fi(dstBase + "/" + entry);
        if (fi.isDir()) QDir(fi.absoluteFilePath()).removeRecursively();
        else QFile::remove(fi.absoluteFilePath());
    }

    if (!extractArchive(archivePath, dstBase, err)) {
        qDebug() << "TorInstaller: Archive extraction failed";
        emit gpgWarning( "4",  "TorInstaller: Archive extraction failed -- try to shutdown & delete old tor manually first");
        return false;
    }

    torBin = findTorBinaryIn(dstBase);
    if (torBin.isEmpty()) {
        qDebug() << "TorInstaller: Could not locate tor binary after extraction";
        if (err) *err = "Extracted bundle but could not locate tor binary";
        emit gpgWarning( "4",  "TorInstaller: Could not locate tor binary after extraction");
        return false;
    }

#ifndef Q_OS_WIN
    qDebug() << "TorInstaller: Setting executable permissions on:" << torBin;
    QFile torFile(torBin);
    torFile.setPermissions(torFile.permissions()
                           | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
#endif

    const QString markerPath = dstBase + "/.installed";
    qDebug() << "TorInstaller: Writing installation marker to:" << markerPath;
    {
        QSaveFile f(markerPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << "version=" << version << "\n"
               << "file=" << wantedFile << "\n"
               << "sha256=" << wantShaHex << "\n";
            f.commit();
            qDebug() << "TorInstaller: Installation marker written successfully";
        } else {
            qDebug() << "TorInstaller: Failed to write installation marker";
        }
    }

    qDebug() << "TorInstaller: Installation completed successfully, tor binary at:" << torBin;
    return true;
}

QString TorInstaller::ensureTorPresent(QString *err)
{
    qDebug() << "TorInstaller: ensureTorPresent() called";
    QString torBin;
    // if (alreadyInstalled(torBin)) {
    //     qDebug() << "TorInstaller: Tor already installed at:" << torBin;
    //     return torBin;
    // }

    QString rootDir = installRoot();
    qDebug() << "TorInstaller: Creating root directory:" << rootDir;
    QDir().mkpath(rootDir);

    QString e;
    if (!downloadAndInstall(torBin, &e)) {
        qDebug() << "TorInstaller: Download and install failed:" << e;
        if (err) *err = e;
        return QString();
    }
    qDebug() << "TorInstaller: ensureTorPresent() completed successfully, tor at:" << torBin;
    return torBin;
}


bool TorInstaller::bootstrapTorKeyViaWkd(QString *err) const
{
    const QString gpg = QStandardPaths::findExecutable("gpg");
    if (gpg.isEmpty()) { if (err) *err = "gpg not found for WKD fetch"; return false; }

    const QString hd = appGnupgHome();
    QProcess p;
    p.start(gpg, {
                     "--homedir", hd,
                     "--batch", "--quiet",
                     "--auto-key-locate", "nodefault,wkd",
                     "--locate-keys", "torbrowser@torproject.org"
                 });
    if (!p.waitForFinished(60000) || p.exitStatus()!=QProcess::NormalExit || p.exitCode()!=0) {
        if (err) *err = QString("WKD fetch failed: %1").arg(QString::fromUtf8(p.readAllStandardError()));
        return false;
    }
    return true;
}

