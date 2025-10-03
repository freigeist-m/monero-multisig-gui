// torinstallworker.h
#pragma once
#include <QObject>
#include "torinstaller.h"

class TorInstallWorker : public QObject {
    Q_OBJECT
public:
    explicit TorInstallWorker(QObject *parent=nullptr) : QObject(parent) {}

    void setInstallRoot(const QString &p) { m_installRoot = p; }
    void setRequireGpg(bool on) { m_requireGpg = on; }
    void setAllowedFingerprints(const QStringList &fps) { m_fingerprints = fps; }

signals:
    void progress(int percent, const QString &step);    // optional
    void finished(bool ok, const QString &torBin, const QString &error);
    void gpgWarning(const QString &code, const QString &msg);



public slots:
    void run() {
        TorInstaller inst;
        if (!m_installRoot.isEmpty()) inst.setInstallRoot(m_installRoot);
        inst.setRequireGpg(m_requireGpg);
        inst.setAllowedSignerFingerprints(m_fingerprints);
        QObject::connect(&inst, &TorInstaller::gpgWarning,
                         this,  &TorInstallWorker::gpgWarning);

        QString err;
        const QString bin = inst.ensureTorPresent(&err);
        emit finished(!bin.isEmpty(), bin, err);
    }

private:
    QString m_installRoot;
    bool m_requireGpg = false;
    QStringList m_fingerprints;
};
