#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QUrl>
#include <QIcon>
#include "win_compat.h"
#include "accountmanager.h"
#include "bootstrap.h"
#include "wallet.h"
#include "multiwalletcontroller.h"
#include "torbackend.h"
#include "multisigmanager.h"
#include "transfermanager.h"
#include "transfertracker.h"
#include "thememanager.h"
#include <QCoreApplication>


int main(int argc, char *argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
    QGuiApplication app(argc, argv);

    QCoreApplication::setApplicationName("Monero-Multisig");

    #ifdef APP_VERSION
        QCoreApplication::setApplicationVersion(APP_VERSION);
    #endif

    qmlRegisterType<Wallet>("MoneroMultisigGui", 1, 0, "Wallet");
    qmlRegisterType<MultiWalletController>("MoneroMultisigGui", 1, 0, "WalletManager");
    qmlRegisterType<TransferTracker>("MoneroMultisigGui", 1, 0, "TransferTracker");

    app.setWindowIcon(QIcon(":/resources/icons/monero_rotated_blue.svg"));

    const QVariantMap core = Bootstrap::buildCore(&app);

    auto *accountManager   = core["account_manager"].value<AccountManager*>();
    auto *torBackend       = core["tor_server"].value<TorBackend*>();
    auto *walletManager    = core["wallet_manager"].value<MultiWalletController*>();
    auto *multisigManager  = core["multisig_manager"].value<MultisigManager*>();
    auto *transferManager  = core["transfer_manager"].value<TransferManager*>();
    auto *themeManager = core["theme_manager"].value<ThemeManager*>();


    qmlRegisterSingletonInstance("MoneroMultisigGui", 1, 0, "WalletManager", walletManager);
    qmlRegisterSingletonInstance("MoneroMultisigGui", 1, 0, "TransferManager", transferManager);
    qmlRegisterSingletonInstance("MoneroMultisigGui", 1, 0, "Theme", themeManager);


    QQmlApplicationEngine engine;

    qmlRegisterSingletonType(QUrl(u"qrc:/qt/qml/MoneroMultisigGui/qml/LockController.qml"_qs),
                             "MoneroMultisigGui", 1, 0, "LockController");

    engine.rootContext()->setContextProperty("accountManager",  accountManager);
    engine.rootContext()->setContextProperty("torServer",       torBackend);
    engine.rootContext()->setContextProperty("walletManager",   walletManager);
    engine.rootContext()->setContextProperty("multisigManager", multisigManager);
    engine.rootContext()->setContextProperty("transferManager", transferManager);
    engine.rootContext()->setContextProperty("themeManager", themeManager);

    engine.loadFromModule("MoneroMultisigGui", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
