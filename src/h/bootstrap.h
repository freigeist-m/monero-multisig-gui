#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include <QObject>
#include <QVariantMap>
#include <QCoreApplication>

class AccountManager;
class TorBackend;


class Bootstrap : public QObject
{
    Q_OBJECT
public:
    explicit Bootstrap(QObject *parent = nullptr);
    static QVariantMap buildCore(QCoreApplication *app = nullptr);

private:
    static void setupSignalConnections(const QVariantMap &core);
};

#endif // BOOTSTRAP_H
