import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: "Multisig Import Session"

    property bool sessionRunning: transferManager ? transferManager.isMultisigImportSessionRunning() : false
    property int activeWalletCount: walletManager ? walletManager.walletConnectedCount : 0

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    Timer {
        id: refreshTimer
        interval: 2000
        running: root.visible
        repeat: true
        onTriggered: {
            root.sessionRunning = transferManager ? transferManager.isMultisigImportSessionRunning() : false
            root.activeWalletCount = walletManager ? walletManager.walletConnectedCount : 0
            importActivityModel.refresh()
            peerActivityModel.refresh()
        }
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
                text: qsTr("Multisig Import Session")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: themeManager.textColor
                Layout.alignment: Qt.AlignLeft
                Layout.bottomMargin: 4
            }


            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: qsTr("Session Status")
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: themeManager.borderColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: statusLayout.implicitHeight + 16
                    color: themeManager.backgroundColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    ColumnLayout {
                        id: statusLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 16

                            RowLayout {
                                spacing: 8

                                Text {
                                    text: qsTr("Session Running:")
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    color: themeManager.textColor
                                }

                                AppStatusIndicator {
                                    status: root.sessionRunning ? "online" : "offline"
                                    text: root.sessionRunning ? qsTr("Yes") : qsTr("No")
                                    dotSize: 6
                                }
                            }

                            Item { Layout.fillWidth: true }

                            RowLayout {
                                spacing: 8

                                Text {
                                    text: qsTr("Active Wallets:")
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    color: themeManager.textColor
                                }

                                Text {
                                    text: root.activeWalletCount.toString()
                                    font.pixelSize: 12
                                    color: root.activeWalletCount > 0 ? themeManager.warningColor : themeManager.textSecondaryColor
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            AppButton {
                                text: qsTr("Start Session")
                                enabled: !root.sessionRunning && transferManager
                                variant: "primary"
                                onClicked: {
                                    if (transferManager) {
                                        transferManager.startMultisigImportSession()
                                    }
                                }
                            }

                            AppButton {
                                text: qsTr("Stop Session")
                                enabled: root.sessionRunning && transferManager
                                variant: "secondary"
                                onClicked: {
                                    if (transferManager) {
                                        transferManager.stopMultisigImportSession()
                                    }
                                }
                            }

                            AppButton {
                                text: qsTr("Clear Activity")
                                enabled: transferManager
                                variant: "secondary"
                                onClicked: {
                                    if (transferManager) {
                                        transferManager.clearMultisigImportActivity()
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4

                Text {
                    text: qsTr("Import Activity")
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: themeManager.borderColor
                }

                ListView {
                    id: importActivityList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 200
                    clip: true
                    spacing: 6

                    model: ListModel {
                        id: importActivityModel

                        function refresh() {
                            clear()
                            if (!transferManager) return

                            try {
                                var activityJson = transferManager.getMultisigImportActivity()
                                var activities = JSON.parse(activityJson)

                                for (var i = 0; i < activities.length; i++) {
                                    var activity = activities[i]
                                    append({
                                        walletName: activity.wallet_name || "Unknown",
                                        status: activity.status || "unknown",
                                        lastActivityTime: activity.last_activity_time || 0,
                                        totalImports: activity.total_imports || 0,
                                        currentImportPeers: activity.current_import_peers || [],
                                        lastImportPeers: activity.last_import_peers || [],
                                        lastImportTime: activity.last_import_time || 0
                                    })
                                }
                            } catch (e) {
                                console.log("Error parsing import activity:", e)
                            }
                        }

                        Component.onCompleted: refresh()
                    }

                    delegate: Rectangle {
                        width: importActivityList.width
                        height: 56
                        color: themeManager.backgroundColor
                        border.color: themeManager.borderColor
                        border.width: 1
                        radius: 2

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        text: qsTr("Wallet: %1").arg(model.walletName)
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                        color: themeManager.textColor
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }

                                    AppStatusIndicator {
                                        status: {
                                            switch (model.status) {
                                                case "importing": return "pending"
                                                case "completed": return "success"
                                                case "receiving_peer_info": return "online"
                                                default: return "offline"
                                            }
                                        }
                                        text: model.status
                                        dotSize: 5
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    Text {
                                        text: qsTr("Total Imports: %1").arg(model.totalImports)
                                        font.pixelSize: 10
                                        color: themeManager.textSecondaryColor
                                    }

                                    Text {
                                        text: qsTr("Last Activity: %1").arg(
                                            model.lastActivityTime > 0 ?
                                                new Date(model.lastActivityTime * 1000).toLocaleString() :
                                                qsTr("Never")
                                        )
                                        font.pixelSize: 10
                                        color: themeManager.textSecondaryColor
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                }

                                Text {
                                    text: qsTr("Current Peers: %1").arg(
                                        model.currentImportPeers.length > 0 ?
                                            model.currentImportPeers.join(", ") :
                                            qsTr("None")
                                    )
                                    font.pixelSize: 10
                                    color: themeManager.textSecondaryColor
                                    visible: model.currentImportPeers.length > 0
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("No import activity")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                        visible: importActivityList.count === 0
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4

                Text {
                    text: qsTr("Peer Activity")
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: themeManager.borderColor
                }

                ListView {
                    id: peerActivityList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 200
                    clip: true
                    spacing: 4

                    model: ListModel {
                        id: peerActivityModel

                        function refresh() {
                            clear()
                            if (!transferManager) return

                            try {
                                var activityJson = transferManager.getMultisigImportPeerActivity()
                                var activities = JSON.parse(activityJson)

                                for (var i = 0; i < activities.length; i++) {
                                    var activity = activities[i]
                                    append({
                                        timestamp: activity.timestamp || 0,
                                        walletName: activity.wallet_name || "Unknown",
                                        peerOnion: activity.peer_onion || "Unknown",
                                        action: activity.action || "unknown"
                                    })
                                }
                            } catch (e) {
                                console.log("Error parsing peer activity:", e)
                            }
                        }

                        Component.onCompleted: refresh()
                    }

                    delegate: Rectangle {
                        width: peerActivityList.width
                        height: 36
                        color: themeManager.backgroundColor
                        border.color: themeManager.borderColor
                        border.width: 1
                        radius: 2

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 8


                            Text {
                                text: qsTr("Wallet: %1").arg(model.walletName)
                                font.pixelSize: 10
                                color: themeManager.textColor
                                Layout.preferredWidth: 100
                                elide: Text.ElideRight
                            }


                            Text {
                                text: qsTr("Peer: %1").arg(model.peerOnion)
                                font.pixelSize: 10
                                font.family: "monospace"
                                color: themeManager.textSecondaryColor
                                Layout.fillWidth: true
                                // elide: Text.ElideRight
                            }

                            Text {
                                text: new Date(model.timestamp * 1000).toLocaleString()
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 10
                                Layout.fillWidth: true
                                // elide: Text.ElideRight
                            }





                            Text {
                                text: model.action
                                color: model.action === "info_received" ? themeManager.successColor : themeManager.textSecondaryColor
                                font.pixelSize: 9
                                Layout.preferredWidth: 60
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("No peer activity")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                        visible: peerActivityList.count === 0
                    }
                }
            }


            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 12
            }
        }
    }

    Connections {
        target: transferManager

        function onMultisigImportSessionStarted() {
            root.sessionRunning = true
        }

        function onMultisigImportSessionStopped() {
            root.sessionRunning = false
            root.activeWalletCount = 0
        }

        function onMultisigImportWalletCompleted(walletName) {
            importActivityModel.refresh()
        }

        function onMultisigImportPeerInfoReceived(walletName, peerOnion) {
            peerActivityModel.refresh()
        }
    }
}
