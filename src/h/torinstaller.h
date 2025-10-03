#ifndef TORINSTALLER_H
#define TORINSTALLER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QDir>
#include <QLockFile>
#include <QTemporaryDir>
#include <QVersionNumber>

class TorInstaller : public QObject
{
    Q_OBJECT
public:
    explicit TorInstaller(QObject *parent = nullptr);

    QString ensureTorPresent(QString *err = nullptr);

    void setInstallRoot(const QString &path) { m_installRoot = path; }
    QString installRoot() const;

    void setRequireGpg(bool on) { m_requireGpg = on; }
    void setAllowedSignerFingerprints(const QStringList &fps) { m_allowedFingerprints = fps; }

    enum class GpgCheckResult {
        Verified,          // signature verified and fingerprint pinned
        VerifiedPinnedKeyring, // verified with gpgv + pinned keyring
        NotInstalled,      // gpg/gpgv not found
        NoKey,             // tool present but Tor key missing and could not be bootstrapped
        BadSignature,      // signature present but invalid
        UnexpectedSigner,  // valid signature but fingerprint not in allow-list
        TimeoutOrError,    // gpg invocation failed/timed out
        NotAttempted,      // (fallback path) couldn't even try
    };
    Q_ENUM(GpgCheckResult)

signals:

    void gpgWarning(const QString &code,
                    const QString &msg ) const;

private:

    bool alreadyInstalled(QString &torBin) const;
    bool downloadAndInstall(QString &torBin, QString *err);

    QString osToken() const;          // windows | macos | linux
    QString archToken() const;        // x86_64 | aarch64 | i686 (fallback)


    QByteArray httpGet(const QUrl &url, QString *err, int timeoutMs = 120000);
    bool       httpGetToFile(const QUrl &url, const QString &dstFile, QString *err, int timeoutMs = 300000);

    bool findLatestTorBundle(QString *outVersion,
                             QString *outFileNamePattern,
                             QUrl    *outBaseUrl,
                             QString *err);

    bool parseSha256Sums(const QByteArray &sumsTxt,
                         const QString &wantedFileName,
                         QByteArray *outSha256Hex) const;

    bool verifyFileSha256(const QString &filePath, const QByteArray &wantHex, QString *err) const;


    bool verifyChecksumsWithGpg(const QString &sumsPath,
                                const QString &ascPath,
                                QString *err) const;

    bool bootstrapTorKeyViaWkd(QString *err) const;


    bool extractArchive(const QString &archivePath, const QString &dstDir, QString *err) const;
    QString findTorBinaryIn(const QString &root) const;

private:
    QNetworkAccessManager m_nam;
    bool m_requireGpg = false;
    QStringList m_allowedFingerprints;
    QString m_installRoot;
};

#endif
