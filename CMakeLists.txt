cmake_minimum_required(VERSION 3.16)
project(monero-multisig-gui VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------------------
# Add Monero core first (static build like the official GUI does)
# -------------------------------------------------------------------
set(BUILD_GUI_DEPS ON)          # pulls in easylogging etc.
add_subdirectory(monero)

find_package(Qt6 REQUIRED COMPONENTS Quick Network HttpServer QuickDialogs2 Concurrent)
qt_standard_project_setup(REQUIRES 6.5)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)

qt_add_executable(appmonero-multisig-gui
    src/cpp/main.cpp
    src/h/accountmanager.h
    src/cpp/accountmanager.cpp
    src/h/bootstrap.h
    src/cpp/bootstrap.cpp
    src/h/wallet.h
    src/cpp/wallet.cpp
    src/cpp/incomingtransfer.cpp
    src/h/incomingtransfer.h
)

target_compile_definitions(appmonero-multisig-gui PRIVATE APP_VERSION="${PROJECT_VERSION}")


if(WIN32)
    # Create ICO file during build if it doesn't exist

    add_custom_target(create_ico DEPENDS ${CMAKE_SOURCE_DIR}/resources/icons/app.ico)
    add_dependencies(appmonero-multisig-gui create_ico)


    # Add resource file to embed icon in executable
    target_sources(appmonero-multisig-gui PRIVATE app.rc)
endif()

if(UNIX AND NOT APPLE)
    # Install everything to a staging directory for AppImage creation
    set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/AppDir/usr)

    # Install the executable
    install(TARGETS appmonero-multisig-gui
        DESTINATION bin
    )

    # Install desktop entry
    install(FILES ${CMAKE_SOURCE_DIR}/monero-multisig.desktop
        DESTINATION share/applications
    )

    # Install icon at multiple sizes for better compatibility
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/monero_rotated_blue.svg
        DESTINATION share/icons/hicolor/scalable/apps
        RENAME monero-multisig.svg
    )

    # Create AppRun script for AppImage
    file(WRITE ${CMAKE_BINARY_DIR}/AppRun "#!/bin/bash\n")
    file(APPEND ${CMAKE_BINARY_DIR}/AppRun "SELF=$(readlink -f \"$0\")\n")
    file(APPEND ${CMAKE_BINARY_DIR}/AppRun "HERE=\${SELF%/*}\n")
    file(APPEND ${CMAKE_BINARY_DIR}/AppRun "export PATH=\"\${HERE}/usr/bin/:\${PATH}\"\n")
    file(APPEND ${CMAKE_BINARY_DIR}/AppRun "export LD_LIBRARY_PATH=\"\${HERE}/usr/lib/:\${LD_LIBRARY_PATH}\"\n")
    file(APPEND ${CMAKE_BINARY_DIR}/AppRun "cd \"\${HERE}/usr/bin\"\n")
    file(APPEND ${CMAKE_BINARY_DIR}/AppRun "exec ./appmonero-multisig-gui \"$@\"\n")

    install(PROGRAMS ${CMAKE_BINARY_DIR}/AppRun
        DESTINATION ../
    )

    # Copy desktop entry and icon to AppDir root (required by AppImage)
    install(FILES ${CMAKE_SOURCE_DIR}/monero-multisig.desktop
        DESTINATION ../
    )
    install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/monero_rotated_blue.svg
        DESTINATION ../
        RENAME monero-multisig.svg
    )
endif()




qt_add_qml_module(appmonero-multisig-gui
    URI MoneroMultisigGui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/LeftPanel.qml
        qml/MiddlePanel.qml
        qml/pages/components/LoginScreen.qml
        qml/pages/AccountPage.qml
        qml/LockController.qml
        SOURCES src/h/cryptoutils.h
        SOURCES src/cpp/cryptoutils.cpp
        SOURCES src/h/torbackend.h
        SOURCES src/cpp/torbackend.cpp
        QML_FILES qml/pages/TorPage.qml
        SOURCES src/h/routerhandler.h
        SOURCES src/cpp/routerhandler.cpp
        QML_FILES qml/pages/Wallets.qml
        SOURCES src/h/multiwalletcontroller.h
        SOURCES src/cpp/multiwallercontroller.cpp
        SOURCES src/cpp/incomingtransfer.cpp
        SOURCES src/h/incomingtransfer.h
        SOURCES src/h/multisigsession.h
        SOURCES src/cpp/multisigsession.cpp
        SOURCES src/h/multisigmanager.h
        SOURCES src/cpp/multisigmanager.cpp
        SOURCES src/h/httptask.h
        SOURCES src/cpp/httptask.cpp
        SOURCES src/cpp/multisigapirouter.cpp
        SOURCES src/h/multisigapirouter.h
        SOURCES src/h/cryptoutils_extras.h
        SOURCES src/cpp/cryptoutils_extras.cpp
        QML_FILES qml/pages/MultisigLandingPage.qml
        QML_FILES qml/pages/NewMultisig.qml
        QML_FILES qml/pages/WalletDetails.qml
        QML_FILES qml/pages/AddWalletPage.qml
        SOURCES src/h/multisignotifier.h
        SOURCES src/cpp/multisignotifier.cpp
        SOURCES src/h/transferinitiator.h
        SOURCES src/cpp/transferinitiator.cpp
        SOURCES src/cpp/transfermanager.cpp
        SOURCES src/h/transfermanager.h
        SOURCES src/h/transfertracker.h
        SOURCES src/cpp/transfertracker.cpp
        QML_FILES qml/pages/OutgoingTransfer.qml
        QML_FILES qml/pages/IncomingTransfer.qml
        QML_FILES qml/pages/NewTransferSetup.qml
        QML_FILES qml/pages/SessionsOverview.qml
        QML_FILES qml/pages/SavedTransfer.qml
        QML_FILES qml/pages/TransferTracker.qml
        SOURCES src/h/multisigimportsession.h
        SOURCES src/cpp/multisigimportsession.cpp
        QML_FILES qml/pages/MultisigImportPage.qml
        QML_FILES qml/pages/SimpleTransferApproval.qml
        QML_FILES qml/pages/SimpleTransferSetup.qml
        SOURCES src/h/simpletransfer.h
        SOURCES src/cpp/simpletransfer.cpp
        QML_FILES qml/pages/WalletTransfersPage.qml
        SOURCES src/h/thememanager.h
        SOURCES src/cpp/thememanager.cpp
        QML_FILES qml/pages/components/AppButton.qml
        QML_FILES qml/pages/components/ColorOverlay.qml
        QML_FILES qml/pages/components/AppInput.qml
        QML_FILES qml/pages/components/AppIconButton.qml
        QML_FILES qml/pages/components/AppCard.qml
        QML_FILES qml/pages/components/AppAlert.qml
        QML_FILES qml/pages/components/AppBackButton.qml
        QML_FILES qml/pages/components/AppStatusIndicator.qml
        QML_FILES qml/pages/components/AppSwitch.qml
        QML_FILES qml/pages/components/AppCopyButton.qml
        QML_FILES qml/pages/components/AppIcon.qml
        QML_FILES qml/pages/components/AppNavButton.qml
        QML_FILES qml/pages/components/ExpandableNavButton.qml
        QML_FILES qml/pages/UnifiedAddressBook.qml
        QML_FILES qml/pages/components/PeerAddressBookTab.qml
        QML_FILES qml/pages/components/TrustedPeersTab.qml
        QML_FILES qml/pages/components/XMRAddressBookTab.qml
        QML_FILES qml/pages/AccountDataPage.qml
        QML_FILES qml/pages/components/AppCheckBox.qml
        QML_FILES qml/pages/components/AppSection.qml
        QML_FILES qml/pages/components/AppInputDialog.qml
        QML_FILES qml/pages/components/AppFormDialog.qml
        QML_FILES qml/pages/components/AppListDialog.qml
        QML_FILES qml/pages/components/AppConfirmDialog.qml
        QML_FILES qml/pages/WalletReceivePage.qml
        QML_FILES qml/pages/AddressQrPage.qml
        QML_FILES qml/pages/components/QRCode.qml
        QML_FILES qml/js/qrcode.js
        QML_FILES qml/pages/components/AppDotIndicator.qml
        QML_FILES qml/pages/components/AppNavButtonDot.qml
        SOURCES src/h/torinstaller.h
        SOURCES src/cpp/torinstaller.cpp
        SOURCES src/h/torinstallworker.h
        QML_FILES qml/pages/NotifierSetup.qml
        QML_FILES qml/pages/NotifierStatus.qml
        QML_FILES qml/pages/components/DaemonAddressBookTab.qml
        QML_FILES qml/pages/components/AppAddressBookDialog.qml
        SOURCES src/h/restore_height.h
        SOURCES src/h/win_compat.h
        QML_FILES qml/pages/ArchivedWalletsPage.qml
        QML_FILES qml/pages/NewStandardWallet.qml
)

file(GLOB_RECURSE ICON_FILES "resources/icons/*.svg")
qt_add_resources(appmonero-multisig-gui "app_icons"
    PREFIX "/resources"
    BASE "resources"
    FILES ${ICON_FILES}
)


# set_source_files_properties(LockController.qml
#     PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

target_include_directories(appmonero-multisig-gui
    PRIVATE
        src/h
        ${CMAKE_SOURCE_DIR}/monero/src
        ${CMAKE_SOURCE_DIR}/monero/external/easylogging++
        ${CMAKE_SOURCE_DIR}/monero/external
        ${CMAKE_SOURCE_DIR}/monero/contrib/epee/include

    )


target_link_libraries(appmonero-multisig-gui
    PRIVATE
        wallet                     # tools::wallet2 (built by monero)
        cryptonote_core
        cryptonote_basic
        common
        epee
        ringct
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Network
        Qt6::QuickDialogs2
        Qt6::Concurrent
        ${SODIUM_LIBRARIES}

)
target_include_directories(appmonero-multisig-gui PRIVATE ${SODIUM_INCLUDE_DIRS})

set_target_properties(appmonero-multisig-gui PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# Copy QML files to build directory
file(GLOB_RECURSE QML_FILES "qml/*.qml")
foreach(qml_file ${QML_FILES})
    file(RELATIVE_PATH rel_path ${CMAKE_SOURCE_DIR} ${qml_file})
    configure_file(${qml_file} ${CMAKE_BINARY_DIR}/${rel_path} COPYONLY)
endforeach()

file(GLOB_RECURSE QML_JS_FILES "qml/*.js")
foreach(js_file ${QML_JS_FILES})
    file(RELATIVE_PATH rel_path ${CMAKE_SOURCE_DIR} ${js_file})
    configure_file(${js_file} ${CMAKE_BINARY_DIR}/${rel_path} COPYONLY)
endforeach()

include(GNUInstallDirs)
install(TARGETS appmonero-multisig-gui
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
