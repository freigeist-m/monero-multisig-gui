import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import Qt5Compat.GraphicalEffects
import "components"

Page {
    id: notifierStatusPage
    title: qsTr("Notifier Status")

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    required property string notifierRef
    readonly property bool notifierActive: notifierRef !== ""
    property string selectedUserOnion: ""
    readonly property QtObject notifierObj: notifierActive ? multisigManager.notifierFor(selectedUserOnion ,notifierRef) : null

    property bool notifierCompleted: false
    property int completedPeers: 0
    property int totalPeers: 0

    ListModel { id: peerStatusModel }

    Timer {
        id: refreshTimer
        interval: 5000 // 5 seconds
        running: notifierActive && !notifierCompleted
        repeat: true
        onTriggered: refreshPeerStatus()
    }

    function refreshPeerStatus() {
        peerStatusModel.clear();
        if (!notifierObj) return;

        const peers = notifierObj.getPeerStatus();
        completedPeers = notifierObj.getCompletedCount();
        totalPeers = notifierObj.getTotalCount();

        for (let i = 0; i < peers.length; ++i) {
            const p = peers[i];
            peerStatusModel.append({
                onion: String(p.onion || ""),
                notified: !!p.notified,
                trials: parseInt(p.trials || 0),
                lastAttempt: String(p.lastAttempt || ""),
                error: String(p.error || "")
            });
        }
    }

    function getStatusText(stage) {
        switch (stage) {
            case "INIT": return qsTr("Initializing...");
            case "POSTING": return qsTr("Notifying peers...");
            case "COMPLETE": return qsTr("Notification complete");
            case "ERROR": return qsTr("Error occurred");
            default: return stage;
        }
    }

    function getStatusColor(stage) {
        switch (stage) {
            case "COMPLETE": return themeManager.successColor;
            case "ERROR": return themeManager.errorColor;
            case "POSTING": return themeManager.warningColor;
            default: return themeManager.textSecondaryColor;
        }
    }

    function getProgressPercentage() {
        if (totalPeers === 0) return 0;
        return Math.round((completedPeers / totalPeers) * 100);
    }

    Connections {
        target: notifierObj
        enabled: notifierActive && notifierObj
        ignoreUnknownSignals: true

        function onStageChanged(s) {
            if (s === 'COMPLETE') {
                notifierCompleted = true;
                refreshTimer.stop();
            }
            refreshPeerStatus();
        }

        function onPeerStatusChanged() {
            refreshPeerStatus();
        }

        function onFinished(result) {
            notifierCompleted = true;
            refreshTimer.stop();
            refreshPeerStatus();
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 8
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 8

            AppBackButton {
                backText: qsTr("Back to Multisig")
                onClicked: leftPanel.buttonClicked("MultisigLandingPage")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    text: qsTr("Notifier Session Status")
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: themeManager.textColor
                    Layout.fillWidth: true
                }

                AppButton {
                    text: notifierCompleted ? qsTr("Start New Notifier") : qsTr("New Notifier")
                    iconSource: "/resources/icons/bell.svg"
                    visible: notifierCompleted
                    onClicked: leftPanel.buttonClicked("NotifierSetup")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: notifierActive

                Text {
                    text: qsTr("Session Information")
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
                    implicitHeight: sessionInfoLayout.implicitHeight + 16
                    color: themeManager.backgroundColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    ColumnLayout {
                        id: sessionInfoLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: qsTr("Reference:")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Text {
                                text: notifierRef
                                color: themeManager.textColor
                                font.weight: Font.Medium
                                font.family: "Monospace"
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                            }

                            AppCopyButton {
                                textToCopy: notifierRef
                                size: 14
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: qsTr("Stage:")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Text {
                                text: notifierObj ? notifierObj.stage : "Unknown"
                                color: notifierObj ? getStatusColor(notifierObj.stage) : themeManager.textSecondaryColor
                                font.weight: Font.Medium
                                Layout.fillWidth: true
                            }

                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: notifierObj && notifierObj.myOnion

                            Text {
                                text: qsTr("Your Identity:")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Text {
                                text: notifierObj ? notifierObj.myOnion : ""
                                color: themeManager.textColor
                                font.family: "Monospace"
                                font.pixelSize: 11
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: totalPeers > 0

                            Text {
                                text: qsTr("Progress:")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Text {
                                text: qsTr("%1 of %2 peers notified (%3%)")
                                    .arg(completedPeers)
                                    .arg(totalPeers)
                                    .arg(getProgressPercentage())
                                color: themeManager.textColor
                                font.weight: Font.Medium
                                Layout.fillWidth: true
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: themeManager.borderColor
                            opacity: 0.5
                            visible: !notifierCompleted
                        }

                        Text {
                            visible: !notifierCompleted
                            text: qsTr("Notification in progress... This may take several minutes.")
                            color: themeManager.textSecondaryColor
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            visible: notifierCompleted
                            text: qsTr("Notification completed successfully. Peers have been informed about the proposed multisig wallet.")
                            color: themeManager.successColor
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: notifierActive && peerStatusModel.count > 0

                Text {
                    text: qsTr("Peer Notification Status (%1)").arg(peerStatusModel.count)
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: themeManager.borderColor
                }

                ScrollView {
                    Layout.fillWidth: true

                    clip: true

                    ListView {
                        model: peerStatusModel
                        spacing: 6

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 44
                            color: themeManager.backgroundColor
                            border.color: themeManager.borderColor
                            border.width: 1
                            radius: 2

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                AppIcon {
                                    source: "/resources/icons/shield-network.svg"
                                    width: 14
                                    height: 14
                                    color: model.notified ? themeManager.successColor : themeManager.textSecondaryColor
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text: model.onion
                                        font.family: "Monospace"
                                        font.pixelSize: 11
                                        color: themeManager.textColor
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: {
                                            if (model.notified) {
                                                return qsTr("Notified successfully");
                                            } else if (model.trials > 0) {
                                                const lastAttempt = model.lastAttempt ? qsTr(" (last: %1)").arg(model.lastAttempt) : "";
                                                return qsTr("Attempt %1%2").arg(model.trials).arg(lastAttempt);
                                            } else {
                                                return qsTr("Pending notification");
                                            }
                                        }
                                        font.pixelSize: 10
                                        color: themeManager.textSecondaryColor
                                        Layout.fillWidth: true
                                    }
                                }

                                AppStatusIndicator {
                                    status: model.notified ? "success" : (model.trials > 0 ? "warning" : "offline")
                                    dotSize: 5
                                }

                                Text {
                                    text: model.notified ? qsTr("OK") : qsTr("Trying...")
                                    color: model.notified ? themeManager.successColor : themeManager.textSecondaryColor
                                    font.pixelSize: 10
                                    font.weight: Font.Medium
                                    Layout.preferredWidth: 60
                                    horizontalAlignment: Text.AlignCenter
                                }
                            }
                        }
                    }
                }


                Text {
                    visible: peerStatusModel.count === 0 && notifierActive
                    text: qsTr("No peer status information available yet")
                    color: themeManager.textSecondaryColor
                    font.pixelSize: 12
                    Layout.alignment: Qt.AlignCenter
                    Layout.topMargin: 12
                    Layout.bottomMargin: 12
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: notifierActive && !notifierCompleted && totalPeers > 0

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: progressLayout.implicitHeight + 16
                    color: themeManager.surfaceColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    ColumnLayout {
                        id: progressLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            AppIcon {
                                source: "/resources/icons/clock.svg"
                                width: 16
                                height: 16
                                color: themeManager.textSecondaryColor
                            }

                            Text {
                                text: qsTr("Notification Progress")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textColor
                                Layout.fillWidth: true
                            }

                            Text {
                                text: qsTr("%1%").arg(getProgressPercentage())
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textColor
                            }
                        }

                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: getProgressPercentage()
                            height: 6
                        }

                        Text {
                            text: qsTr("The notifier will continue attempting to reach all peers. Updates every 5 seconds.")
                            color: themeManager.textSecondaryColor
                            font.pixelSize: 11
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8

                Item { Layout.fillWidth: true }

                AppButton {
                    text: notifierCompleted ? qsTr("Close") : qsTr("Stop Notifier")
                    variant: notifierCompleted ? "secondary" : "error"
                    implicitHeight: 36
                    visible: notifierActive
                    onClicked: {
                        if (notifierRef) {
                            multisigManager.stopNotifier(selectedUserOnion, notifierRef);
                        }
                        leftPanel.buttonClicked("MultisigLandingPage");
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignCenter
                spacing: 12
                visible: !notifierActive

                AppIcon {
                    source: "/resources/icons/bell-off.svg"
                    width: 32
                    height: 32
                    color: themeManager.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: qsTr("No active notifier session")
                    font.pixelSize: 16
                    color: themeManager.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: qsTr("Start a new notifier to invite peers to create multisig wallets")
                    font.pixelSize: 12
                    color: themeManager.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                }

                AppButton {
                    text: qsTr("Start New Notifier")
                    iconSource: "/resources/icons/bell.svg"
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: leftPanel.buttonClicked("NotifierSetup")
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 12
            }
        }
    }

    Component.onCompleted: {
        if (notifierActive) {
            refreshPeerStatus();
        }
    }

    Component.onDestruction: {
        refreshTimer.stop();
    }
}
