import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import "pages/components"
import MoneroMultisigGui 1.0

Rectangle {
    id: leftPanel
    width: 200
    height: parent.height

    color: themeManager.surfaceColor

    signal buttonClicked(string pageName)
    property bool   torRunning:        torServer.online
    property bool   torInitializing:   torServer.initializing
    property bool   torInstalling:      torServer.installing

    property string currentPageUrl: ""

    function isCurrent(names) {
        if (!currentPageUrl || !names) return false
        var cur = String(currentPageUrl)
        for (var i = 0; i < names.length; i++) {
            var n = names[i]
            if (n.endsWith(".qml")) n = n.slice(0, -4)
            if (cur.endsWith("/" + n + ".qml")) return true
        }
        return false
    }



    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

            AppNavButton {
                text: "Account"
                // iconSource: "/resources/icons/user-circle.svg"
                active: leftPanel.isCurrent(["AccountPage",  "AccountDataPage"])
                Layout.fillWidth: true
                onClicked: {
                    walletsButton.collapse()
                    leftPanel.buttonClicked("AccountPage")
                }
            }


            AppNavButtonDot {
                text: "Onion Identities"
                dot_status : torRunning ? "online" :
                                          (torInstalling ? "pending" :
                                           (torInitializing ? "pending" : "offline"))
                dot_visible : !torRunning
                // iconSource: "/resources/icons/shield-network.svg"
                Layout.fillWidth: true
                active: leftPanel.isCurrent(["TorPage"])
                onClicked: {
                    walletsButton.collapse()
                    leftPanel.buttonClicked("TorPage")
                }
            }


            ExpandableNavButton {
                id: walletsButton
                text: "Wallets"
                // iconSource: "/resources/icons/wallet.svg"
                Layout.fillWidth: true
                active: leftPanel.isCurrent(["Wallets", "NewTransferSetup", "SimpleTransferSetup", "AddressQrPage", "WalletDetails", "WalletTransfersPage"])
                onClicked: {
                    walletsButton.expanded = true
                    leftPanel.buttonClicked("Wallets")
                }


                subButtons: [
                    AppNavButton {
                        text: "New Wallet"
                        // iconSource: "/resources/icons/add-circle.svg"
                        width: walletsButton.width
                        active: leftPanel.isCurrent(["MultisigLandingPage", "NewMultisig", "NotifierSetup", "NotifierStatus"])
                        textAlignment: "indent"
                        onClicked: leftPanel.buttonClicked("MultisigLandingPage")
                    },

                    AppNavButton {
                        text: "Restore"
                        // iconSource: "/resources/icons/refresh-circle.svg"
                        width: walletsButton.width
                        active: leftPanel.isCurrent(["AddWalletPage"])
                        textAlignment: "indent"
                        onClicked: leftPanel.buttonClicked("AddWalletPage")
                    },

                    AppNavButton {
                        text: "Multisig Import"
                        // iconSource: "/resources/icons/download-square.svg"
                        width: walletsButton.width
                        active: leftPanel.isCurrent(["MultisigImportPage"])
                        textAlignment: "indent"
                        onClicked: leftPanel.buttonClicked("MultisigImportPage")

                    }

                ]
            }


            AppNavButtonDot {
                text: "Transfers"
                dot_status : "primary"
                dot_visible : transferManager.pendingIncomingTransfers.length > 0
                // iconSource: "/resources/icons/shield-network.svg"
                Layout.fillWidth: true
                active: leftPanel.isCurrent(["SessionsOverview" , "OutgoingTransfer", "IncomingTransfer", "SavedTransfer", "TransferTracker", "SimpleTransferApproval"])
                onClicked: {
                    walletsButton.collapse()
                    leftPanel.buttonClicked("SessionsOverview")
                }
            }

            AppNavButton {
                text: "Address Book"
                // iconSource: "/resources/icons/book-bookmark.svg"
                Layout.fillWidth: true
                active: leftPanel.isCurrent(["UnifiedAddressBook"])
                onClicked: {
                    walletsButton.collapse()
                    leftPanel.buttonClicked("UnifiedAddressBook")
                }
            }



            Item {
                Layout.fillHeight: true
                Layout.fillWidth: true
            }

            AppNavButton  {
                text: themeManager.darkMode ? "Light Mode" : "Dark Mode"
                iconSource: themeManager.darkMode ? "/resources/icons/sun.svg" : "/resources/icons/moon.svg"
                variant: "secondary"
                textAlignment: "center"
                Layout.fillWidth: true
                Layout.margins: 4
                onClicked: themeManager.toggleTheme()
            }


            AppNavButton {
                text: LockController.locked ? "Unlock" : "Lock"
                iconSource: LockController.locked ? "/resources/icons/lock-keyhole-unlocked.svg" : "/resources/icons/lock-keyhole.svg"
                variant: "secondary"
                textAlignment: "center"
                buttonHeight: 44
                Layout.fillWidth: true
                Layout.margins: 8
                enabled: !LockController.locked
                onClicked: {
                    LockController.locked = true
                }
            }
        }

    Rectangle {
        id: borderLeft
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 1
        color: themeManager.borderColor
    }

}
