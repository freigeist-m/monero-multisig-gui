#include "win_compat.h"
#include "bootstrap.h"
#include "accountmanager.h"
#include "thememanager.h"
#include "torbackend.h"
#include "routerhandler.h"
#include "multiwalletcontroller.h"
#include "multisigmanager.h"
#include "multisigapirouter.h"
#include "transfermanager.h"
#include <QCoreApplication>
#include <QVariantMap>
#include <QDebug>
#include <iostream>

RouterHandler *router = nullptr;

Bootstrap::Bootstrap(QObject *parent) : QObject(parent) {}

QVariantMap Bootstrap::buildCore(QCoreApplication *app)
{
    if (!app) {
        app = QCoreApplication::instance();
        if (!app) {
            qWarning() << "Bootstrap: QCoreApplication not available";
            return {};
        }
    }

    /* ---- singletons ---------------------------------------------------- */
    auto *themeManager = new ThemeManager(app);
    auto *accountManager  = new AccountManager(app);
    auto *torBackend      = new TorBackend(accountManager);
    auto *walletManager   = new MultiWalletController(accountManager, torBackend , app);
    auto *multisigManager = new MultisigManager(walletManager, torBackend , app);

    walletManager->setProperty("accountManager", QVariant::fromValue(static_cast<QObject*>(accountManager)));


    RouterHandler::setWalletManager(walletManager);

    auto *transferManager = new TransferManager(walletManager, torBackend, app);

    torBackend->setRouterDeps(walletManager, multisigManager);


    QObject::connect(accountManager, &AccountManager::isAuthenticatedChanged,
                 torBackend, [accountManager,torBackend]() {
                      if (accountManager->isAuthenticated()) {
                        torBackend->startIfAutoconnect();
                        torBackend->ensureDefaultService();
                      }
                  });

    QObject::connect(accountManager, &AccountManager::logoutOccurred,
                     torBackend, &TorBackend::reset);

    QObject::connect(accountManager, &AccountManager::logoutOccurred,
                     walletManager,  &MultiWalletController::reset);

    QObject::connect(accountManager, &AccountManager::logoutOccurred,
                     multisigManager, &MultisigManager::reset);

    QObject::connect(accountManager, &AccountManager::logoutOccurred,
                     transferManager, &TransferManager::reset);


    QObject::connect(accountManager, &AccountManager::settingsChanged,
                     themeManager, [accountManager, themeManager]() {
                         themeManager->setDarkMode(accountManager->darkModePref());
                     });


    QObject::connect(themeManager, &ThemeManager::darkModeChanged,
                     accountManager, [accountManager, themeManager]() {
                         if (accountManager->isAuthenticated())
                             accountManager->setDarkModePref(themeManager->darkMode());
                     });


    QVariantMap core;
    core["app"]               = QVariant::fromValue(app);
    core["account_manager"]   = QVariant::fromValue(accountManager);
    core["tor_server"]        = QVariant::fromValue(torBackend);
    core["wallet_manager"]    = QVariant::fromValue(walletManager);
    core["multisig_manager"]  = QVariant::fromValue(multisigManager);
    core["transfer_manager"]  = QVariant::fromValue(transferManager);
    core["theme_manager"] = QVariant::fromValue(themeManager);
    return core;
}
