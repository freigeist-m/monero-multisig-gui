import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: qsTr("Wallets")

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8


            Text {
                text: qsTr("Wallets")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: themeManager.textColor
                Layout.alignment: Qt.AlignLeft
                Layout.bottomMargin: 4
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8


                Text {
                    text: qsTr("View Archived Wallets")
                    font.pixelSize: 12
                    color: themeManager.textSecondaryColor

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const page = "ArchivedWalletsPage.qml"
                            middlePanel.currentPageUrl = page
                            middlePanel.stackView.replace(page)
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12


                Text {
                    text: qsTr("Available wallets: %1").arg(WalletManager.walletCount)
                    font.pixelSize: 12
                    color: themeManager.textSecondaryColor
                    Layout.fillWidth: true
                }

                Item {Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Stop All Wallets")
                    variant: "warning"
                    enabled: WalletManager.walletConnectedCount > 0
                    onClicked: WalletManager.stopAllWallets()
                }
            }


            ListView {
                id: walletView
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(200, contentHeight)
                Layout.maximumHeight: 600
                model: WalletManager.walletNames.filter(function(name) {
                    var meta = WalletManager.getWalletMeta(name);
                    return !meta.archived;
                })
                clip: true
                spacing: 8
                delegate: WalletDelegate {}


                Text {
                    anchors.centerIn: parent
                    text: qsTr("No wallets available")
                    color: themeManager.textSecondaryColor
                    font.pixelSize: 12
                    visible: walletView.count === 0
                }
            }


            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 12
            }
        }
    }


    component WalletDelegate: Rectangle {
        required property var modelData
        property string name: String(modelData)
        property var    wallet: null
        property var    walletMeta: null

        width: walletView.width
        implicitHeight: contentCol.implicitHeight + 16
        height: implicitHeight
        color: themeManager.backgroundColor
        border.color: themeManager.borderColor
        border.width: 1
        radius: 2

        readonly property bool isConnected: wallet !== null
        readonly property bool isBusy:     wallet ? wallet.busy : false

        property var    address_text: walletMeta?  walletMeta.address : "pending"

        Component.onCompleted: {
            wallet = WalletManager.walletInstance(name)
            walletMeta = WalletManager.getWalletMeta(name)
        }

        Connections {
            target: WalletManager
            function onEpochChanged() {
                wallet = WalletManager.walletInstance(name)
                walletMeta = WalletManager.getWalletMeta(name)
            }
        }

        ColumnLayout {
            id: contentCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            spacing: 8


            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: qsTr("%1").arg(name)
                    font.bold: true
                    font.pixelSize: 14
                    color: themeManager.textColor

                }

                Text {
                    text: qsTr("# %1").arg(walletMeta.reference)
                    font.bold: false
                    font.pixelSize: 12
                    color: themeManager.textSecondaryColor

                    visible: walletMeta.multisig
                }

                Text {
                    text: qsTr("%1 / %2").arg(walletMeta.threshold).arg(walletMeta.total)
                    font.bold: false
                    font.pixelSize: 12
                    color: themeManager.textSecondaryColor

                    visible: walletMeta.multisig
                }

                Item {Layout.fillWidth: true}

                AppStatusIndicator {
                    status: isConnected ? "online" : "offline"
                    text: isConnected ? qsTr("Connected") : qsTr("Disconnected")
                    dotSize: 6
                    Layout.alignment: Qt.AlignVCenter
                }
            }


            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                visible: isConnected

                Text {
                    text: qsTr("Busy: %1").arg(isBusy ? qsTr("Yes") : qsTr("No"))
                    font.pixelSize: 10
                    color: themeManager.textSecondaryColor
                }

                Text {
                    text: qsTr("Sync: %1").arg(wallet ? (wallet.activeSyncTimer ? qsTr("On") : qsTr("Off")) : qsTr("N/A"))
                    font.pixelSize: 10
                    color: themeManager.textSecondaryColor
                }

                Text {
                    text: qsTr("Ops: %1").arg(wallet && wallet.pendingOps ? wallet.pendingOps.length : 0)
                    font.pixelSize: 10
                    color: themeManager.textSecondaryColor
                }

                Text {
                    id: syncProgress
                    text: wallet? qsTr("Sync: %1 / %2").arg(Math.floor(wallet.wallet_height)).arg(Math.floor(wallet.daemon_height)) : qsTr("Sync: 0 / 0")
                    font.pixelSize: 10
                    color: themeManager.textSecondaryColor
                }

                Text {
                    text: qsTr("Import needed: %1").arg(wallet && wallet.has_multisig_partial_key_images ? qsTr("Yes") :  qsTr("No")) // Shortened
                    font.pixelSize: 10
                    visible : WalletManager.getWalletMeta(name).multisig && wallet
                    color: themeManager.textSecondaryColor
                }

                Item { Layout.fillWidth: true }
            }

            ColumnLayout {
                visible: isConnected
                spacing: 6
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: qsTr("Address:")
                        font.pixelSize: 10
                        font.weight: Font.Medium
                        color: themeManager.textSecondaryColor
                        Layout.preferredWidth: 50
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 20
                        color: themeManager.backgroundColor
                        border.color: themeManager.borderColor
                        border.width: 1
                        radius: 2

                        TextInput {
                            id: addressText
                            anchors.fill: parent
                            anchors.margins: 4
                            text: address_text
                            font.family: "Monospace"
                            font.pixelSize: 9
                            color: themeManager.textColor
                            readOnly: true
                            selectByMouse: true
                            clip: true
                            verticalAlignment: TextInput.AlignVCenter
                        }
                    }

                    AppCopyButton {
                        textToCopy: addressText.text
                        visible: addressText.text !== "" && addressText.text !== qsTr("(not available)")
                        size: 14
                        Layout.alignment: Qt.AlignVCenter
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    Text {
                        text: qsTr("Balance: %1 XMR").arg(((wallet && wallet.balance ? wallet.balance : 0) / 1e12).toFixed(12))
                        font.pixelSize: 10
                        color: themeManager.textColor
                        font.family: "Monospace"
                    }

                    Text {
                        text: qsTr("Unlocked: %1 XMR").arg(((wallet && wallet.unlockedBalance ? wallet.unlockedBalance : 0) / 1e12).toFixed(12))
                        font.pixelSize: 10
                        color: themeManager.textColor
                        font.family: "Monospace"
                    }

                    Item { Layout.fillWidth: true }
                }
            }


            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    AppButton {
                        text: isConnected ? qsTr("Disconnect") : qsTr("Connect")
                        variant: isConnected ? "secondary" : "primary"
                        onClicked: isConnected
                                   ? WalletManager.disconnectWallet(name)
                                   : WalletManager.connectWallet(name)
                    }

                    AppButton {
                        text: qsTr("Start Sync")
                        variant: "secondary"
                        enabled: isConnected && wallet && !wallet.activeSyncTimer
                        onClicked: wallet.startSync(30)
                    }


                    AppButton {
                        text: qsTr("Transfers")
                        variant: "secondary"
                        enabled: isConnected
                        onClicked: {
                            const page = "WalletTransfersPage.qml"
                            middlePanel.currentPageUrl = page
                            middlePanel.stackView.replace(page, { walletName: name })
                        }
                    }

                    AppButton {
                        text: qsTr("Receive")
                        variant: "secondary"
                        enabled: isConnected
                        onClicked: {
                            const page = "WalletReceivePage.qml"
                            middlePanel.currentPageUrl = page
                            middlePanel.stackView.replace(page, { walletName: name })
                        }
                    }


                    AppIconButton {
                        iconSource: "/resources/icons/refresh.svg"
                        enabled: isConnected && !isBusy
                        size: 16 // Compact icon button
                        onClicked: WalletManager.refreshWallet(name)
                    }



                    AppIconButton {
                        iconSource: "/resources/icons/settings.svg"
                        size: 16
                        onClicked: {
                            const pageComponent = "WalletDetails.qml"
                            middlePanel.currentPageUrl = pageComponent
                            middlePanel.stackView.replace(pageComponent, { walletName: name })
                        }
                    }

                    AppIconButton {
                        iconSource: "/resources/icons/archive.svg"
                        size: 16
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Archive Wallet")
                        ToolTip.delay: 500
                        onClicked: {
                            if (WalletManager.setWalletArchived(name, true)) {
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }


                    AppButton {
                        text: qsTr("New Transfer")
                        variant: "primary"
                        enabled: WalletManager.getWalletMeta(name).multisig ? isConnected : isConnected

                        ToolTip.visible: hovered && WalletManager.getWalletMeta(name).multisig && torServer.online === false
                        ToolTip.text: qsTr("Connect to Tor")
                        ToolTip.delay: 500

                        onClicked: {
                            const meta = WalletManager.getWalletMeta(name) || {}
                            const isMultisig = !!meta.multisig

                            const pageComponent = isMultisig
                                ? "NewTransferSetup.qml"
                                : "SimpleTransferSetup.qml"

                            middlePanel.currentPageUrl = pageComponent
                            middlePanel.stackView.replace(pageComponent, { walletName: name })
                        }
                    }

                }
            }

            AppAlert {
                id: walletAlert
                Layout.fillWidth: true
                visible: text !== ""
                closable: true
                onClosed: text = ""
            }

            Timer {
                id: alertTimer
                interval: 3000
                onTriggered: {
                    if (walletAlert.variant === "success") {
                        walletAlert.text = ""
                    }
                }
            }
        }

        Connections {
            target: wallet
            enabled: wallet !== null
            function onWalletOpened()    {
                walletAlert.text = qsTr("Wallet opened successfully")
                walletAlert.variant = "success"
                alertTimer.restart()
            }
            function onWalletCreated()   {
                walletAlert.text = qsTr("Wallet created successfully")
                walletAlert.variant = "success"
                alertTimer.restart()
            }
            function onErrorOccurred(m)  {
                walletAlert.text = qsTr("Error: %1").arg(m)
                walletAlert.variant = "error"
                // No auto-hide for errors
            }
            function onSyncProgress(c,t) {
                syncProgress.text = qsTr("Sync: %1 / %2").arg(Math.floor(c)).arg(Math.floor(t))
            }
            function onWalletSaved()     {
                walletAlert.text = qsTr("Wallet saved successfully")
                walletAlert.variant = "success"
                alertTimer.restart()
            }
        }
    }
}
