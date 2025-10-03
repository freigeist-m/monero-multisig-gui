import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: qsTr("Transfer Sessions")

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    function getAllTransferRefs() {

        let allRefs = new Set()


        transferManager.allSavedTransfers.forEach(ref => allRefs.add(ref))
        transferManager.outgoingSessions.forEach(ref => allRefs.add(ref))
        transferManager.incomingSessions.forEach(ref => allRefs.add(ref))
        transferManager.activeTrackers.forEach(ref => allRefs.add(ref))

        if (transferManager.simpleSessions) {
            transferManager.simpleSessions.forEach(ref => allRefs.add(ref))
        }

        let refsArray = Array.from(allRefs)

        return refsArray.sort((a, b) => {
            const summaryA = transferManager.getTransferSummary(a)
            const summaryB = transferManager.getTransferSummary(b)

            const timeA = summaryA.created_at || (Date.now() / 1000)
            const timeB = summaryB.created_at || (Date.now() / 1000)

            return timeB - timeA
        })
    }

    function openTransferByType(ref, maybeType) {
        let t = (maybeType || "").toString().trim().toUpperCase()

        const pageComponent = Qt.resolvedUrl(
            t === "SIMPLE" ? "SimpleTransferApproval.qml" : "OutgoingTransfer.qml"
        )
        middlePanel.currentPageUrl = pageComponent
        middlePanel.stackView.replace(pageComponent, { transferRef: ref })
    }

    function formatDate(timestamp) {
        if (!timestamp) return ""
        return new Date(timestamp * 1000).toLocaleDateString()
    }

    function getStageIcon(stage) {
        switch(stage?.toUpperCase()) {
            case "COMPLETE": return "/resources/icons/check-circle.svg"
            case "ERROR": return "/resources/icons/danger-circle.svg"
            case "FAILED": return "/resources/icons/danger-triangle.svg"
            case "DECLINED": return "/resources/icons/forbidden-circle.svg"
            case "CHECKING_STATUS": return "/resources/icons/refresh-circle.svg"
            case "RECEIVED": return "/resources/icons/arrow-left-down.svg"
            case "APPROVING": return "/resources/icons/clock-circle.svg"
            case "ABORTED": return "/resources/icons/stop-circle.svg"
            default: return "/resources/icons/layers.svg"
        }
    }

    function getStageColor(stage) {
        switch(stage?.toUpperCase()) {
            case "COMPLETE": return themeManager.successColor
            case "ERROR":
            case "FAILED": return themeManager.errorColor
            case "DECLINED": return themeManager.errorColor
            case "CHECKING_STATUS": return themeManager.warningColor
            case "RECEIVED": return themeManager.primaryColor
            case "APPROVING": return themeManager.warningColor
            case "ABORTED": return themeManager.textSecondaryColor
            default: return themeManager.textSecondaryColor
        }
    }

    function isLiveSession(ref) {
        return transferManager.outgoingSessions.includes(ref) ||
               transferManager.incomingSessions.includes(ref) ||
               transferManager.activeTrackers.includes(ref) ||
               hasLiveSimple(ref)
    }

    function hasLiveOutgoing(ref) {
        return transferManager.outgoingSessions.includes(ref)
    }

    function hasLiveIncoming(ref) {
        return transferManager.incomingSessions.includes(ref)
    }

    function hasLiveTracker(ref) {
        return transferManager.activeTrackers.includes(ref)
    }

    function hasLiveSimple(ref) {
        return transferManager.getSimpleSession(ref) !== null
    }

    function getActionButtons(ref, summary) {
        const stage = summary.stage || ""
        const type = summary.type || ""
        const stageUpper = stage.toUpperCase()

        let buttons = []

        if (hasLiveOutgoing(ref)) {
            buttons.push({
                text: qsTr("Open Outgoing"),
                icon: "/resources/icons/arrow-right-up.svg",
                variant: "primary",
                action: function() { openTransferByType(ref, type) }
            })
        }

        if (hasLiveIncoming(ref)) {
            buttons.push({
                text: qsTr("Open Incoming"),
                icon: "/resources/icons/arrow-left-down.svg",
                variant: "primary",
                action: function() {
                    const url = Qt.resolvedUrl("IncomingTransfer.qml")
                    middlePanel.currentPageUrl = url
                    middlePanel.stackView.replace(url, { transferRef: ref })
                }
            })
        }

        if (hasLiveSimple(ref)) {
            buttons.push({
                text: qsTr("Open Simple"),
                icon: "/resources/icons/check-circle.svg",
                variant: "primary",
                action: function() {
                    const url = Qt.resolvedUrl("SimpleTransferApproval.qml")
                    middlePanel.currentPageUrl = url
                    middlePanel.stackView.replace(url, { transferRef: ref })
                }
            })
        }

        if (hasLiveTracker(ref)) {
            buttons.push({
                text: qsTr("Open Tracker"),
                icon: "/resources/icons/refresh-circle.svg",
                variant: "secondary",
                action: function() {
                    const url = Qt.resolvedUrl("TransferTracker.qml")
                    middlePanel.currentPageUrl = url
                    middlePanel.stackView.replace(url, { transferRef: ref })
                }
            })
        }

        if (buttons.length === 0) {
            if (stageUpper === "RECEIVED") {
                buttons.push({
                    text: qsTr("Start Incoming"),
                    icon: "/resources/icons/arrow-left-down.svg",
                    variant: "primary",
                    action: function() {
                        const url = Qt.resolvedUrl("IncomingTransfer.qml")
                        middlePanel.currentPageUrl = url
                        middlePanel.stackView.replace(url, { transferRef: ref })
                    }
                })
            } else if (stageUpper === "APPROVING" && type === "SIMPLE") {
                buttons.push({
                    text: qsTr("Start Simple"),
                    icon: "/resources/icons/check-circle.svg",
                    variant: "primary",
                    action: function() {
                        const url = Qt.resolvedUrl("SimpleTransferApproval.qml")
                        middlePanel.currentPageUrl = url
                        middlePanel.stackView.replace(url, { transferRef: ref })
                    }
                })
            } else if (stageUpper === "CHECKING_STATUS") {
                buttons.push({
                    text: qsTr("Start Tracker"),
                    icon: "/resources/icons/refresh-circle.svg",
                    variant: "primary",
                    action: function() {
                        if (transferManager.resumeTracker(ref)) {
                            const url = Qt.resolvedUrl("TransferTracker.qml")
                            middlePanel.currentPageUrl = url
                            middlePanel.stackView.replace(url, { transferRef: ref })
                        }
                    }
                })
            }

            buttons.push({
                text: qsTr("View Details"),
                icon: "/resources/icons/eye.svg",
                variant: "secondary",
                action: function() {
                    const url = Qt.resolvedUrl("SavedTransfer.qml")
                    middlePanel.currentPageUrl = url
                    middlePanel.stackView.replace(url, { transferRef: ref })
                }
            })
        }

        return buttons
    }

    Timer {
        id: refreshTimer
        interval: 5000
        repeat: true
        running: root.visible
        onTriggered: {

            getAllTransferRefs()
        }
    }


    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 8
            spacing: 12

            Text {
                text: qsTr("Transfer Sessions")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: themeManager.textColor
                Layout.fillWidth: true
            }

            Text {
                text: qsTr("(%1 transfers)").arg(getAllTransferRefs().length)
                font.pixelSize: 14
                color: themeManager.textSecondaryColor
            }

            AppButton {
                text: qsTr("Refresh")
                iconSource: "/resources/icons/refresh.svg"
                variant: "secondary"
                implicitHeight: 28
                onClicked: {
                    transferManager.restoreAllSaved()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 8
            spacing: 16

            RowLayout {
                spacing: 4
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: themeManager.primaryColor
                }
                Text {
                    text: qsTr("Live: %1").arg(
                        transferManager.outgoingSessions.length +
                        transferManager.incomingSessions.length +
                        transferManager.activeTrackers.length +
                        (transferManager.simpleSessions ? transferManager.simpleSessions.length : 0)
                    )
                    font.pixelSize: 12
                    color: themeManager.textSecondaryColor
                }
            }

            RowLayout {
                spacing: 4
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: themeManager.warningColor
                }
                Text {
                    text: qsTr("Pending: %1").arg(transferManager.pendingIncomingTransfers.length)
                    font.pixelSize: 12
                    color: themeManager.textSecondaryColor
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: themeManager.borderColor
            Layout.bottomMargin: 8
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: getAllTransferRefs()

                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: contentColumn.implicitHeight + 20

                            property string transferRef: modelData
                            property var summary: transferManager.getTransferSummary(transferRef)
                            property bool isLive: isLiveSession(transferRef)

                            color: themeManager.backgroundColor
                            border.color: isLive ? themeManager.primaryColor : themeManager.borderColor
                            border.width: isLive ? 2 : 1
                            radius: 2

                            ColumnLayout {
                                id: contentColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 10
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    AppIcon {
                                        source: getStageIcon(summary.stage)
                                        width: 16
                                        height: 16
                                        color: getStageColor(summary.stage)
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Text {
                                                text: summary.wallet_name || summary.wallet_ref || "Unknown Wallet"
                                                font.pixelSize: 14
                                                font.weight: Font.Medium
                                                color: themeManager.textColor
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                            }

                                            Rectangle {
                                                visible: isLive
                                                width: 6
                                                height: 6
                                                radius: 3
                                                color: themeManager.successColor
                                            }

                                            Text {
                                                visible: isLive
                                                text: "LIVE"
                                                font.pixelSize: 9
                                                font.weight: Font.Bold
                                                color: themeManager.successColor
                                            }
                                        }

                                        Text {
                                            text: transferRef
                                            font.pixelSize: 10
                                            font.family: "Monospace"
                                            color: themeManager.textSecondaryColor
                                            Layout.fillWidth: true
                                            elide: Text.ElideMiddle
                                        }
                                    }

                                    AppCopyButton {
                                        textToCopy: transferRef
                                        size: 12
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    AppStatusIndicator {
                                        status: {
                                            switch(summary.stage?.toUpperCase()) {
                                                case "COMPLETE": return "success"
                                                case "ERROR":
                                                case "FAILED":
                                                case "DECLINED": return "error"
                                                case "CHECKING_STATUS": return "pending"
                                                default: return "pending"
                                            }
                                        }
                                        text: summary.stage || "Unknown"
                                        dotSize: 5
                                    }

                                    Text {
                                        text: formatDate(summary.created_at)
                                        font.pixelSize: 9
                                        color: themeManager.textSecondaryColor
                                        visible: summary.created_at > 0
                                    }

                                    Text {
                                        text: (summary.type || "").toUpperCase()
                                        font.pixelSize: 8
                                        color: themeManager.textSecondaryColor
                                        font.weight: Font.Medium
                                    }

                                    Item { Layout.fillWidth: true }

                                    RowLayout {
                                        spacing: 6

                                        Repeater {
                                            model: getActionButtons(transferRef, summary)

                                            AppButton {
                                                text: modelData.text
                                                iconSource: modelData.icon
                                                variant: modelData.variant
                                                implicitHeight: 24
                                                onClicked: modelData.action()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignCenter
                    spacing: 12
                    visible: getAllTransferRefs().length === 0

                    AppIcon {
                        source: "/resources/icons/layers.svg"
                        width: 32
                        height: 32
                        color: themeManager.textSecondaryColor
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: qsTr("No transfers found")
                        font.pixelSize: 14
                        color: themeManager.textSecondaryColor
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: qsTr("Start a new transfer to see sessions here")
                        font.pixelSize: 12
                        color: themeManager.textSecondaryColor
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        transferManager.restoreAllSaved()
    }

    onVisibleChanged: {
        if (visible) {
            transferManager.restoreAllSaved()
        }
    }
}
