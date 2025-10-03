import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: qsTr("Simple Transfer")

    property string transferRef: ""
    property var    sessionObj: null
    property var    snapshot: ({})
    property string stageName: ""
    property string statusText: ""
    property bool   navigated: false
    property real   totalAmountXmr: 0.0

    ListModel { id: recipientsModel }

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    function parseSnapshot(j) {
        try {
            let o = JSON.parse(j || "{}")
            snapshot = o
            stageName = o.stage || ""

            // Calculate fee in XMR
            let feeXmr = 0
            if (o.fee !== undefined && o.fee !== null) {
                feeXmr = Number(o.fee)
            }
            o.feeXmr = feeXmr / 1e12
            snapshot = o

            recipientsModel.clear()
            let totalA = 0
            const arr = (o.destinations || [])
            for (let i=0;i<arr.length;i++) {
                const r = arr[i]
                const amtA = Number(r.amount || 0)
                totalA += amtA
                recipientsModel.append({
                                           address: r.address,
                                           amountAtomic: amtA,
                                           amountXmr: (amtA/1e12).toFixed(12)
                                       })
            }
            totalAmountXmr = (totalA + feeXmr) /1e12
        } catch(e) {}
    }

    function refreshOnce() {
        if (sessionObj && sessionObj.getTransferDetailsJson) {
            parseSnapshot(sessionObj.getTransferDetailsJson())
        } else {
            const s = TransferManager.getSavedTransferDetails(transferRef)
            if (s && s.length) parseSnapshot(s)
        }
    }

    function getStageIcon() {
        switch(stageName) {
        case "APPROVING": return "/resources/icons/clock-circle.svg"
        case "COMPLETE": return "/resources/icons/check-circle.svg"
        case "ERROR": return "/resources/icons/danger-circle.svg"
        default: return "/resources/icons/refresh-circle.svg"
        }
    }

    function getStageColor() {
        switch(stageName) {
        case "COMPLETE": return themeManager.successColor
        case "ERROR": return themeManager.errorColor
        case "APPROVING": return themeManager.warningColor
        default: return themeManager.primaryColor
        }
    }

    Timer {
        id: refreshTimer
        interval: 1200
        repeat: true
        running: true
        onTriggered: refreshOnce()
    }

    Connections {
        target: sessionObj
        enabled: sessionObj !== null
        ignoreUnknownSignals: true

        function onStageChanged(s) {
            stageName = s
            refreshOnce()
        }
        function onStatusChanged(t) {
            statusText = t
        }
        function onFinished(tref, result) {
            if (navigated) return
            navigated = true
            refreshTimer.running = false
            leftPanel.buttonClicked("SessionsOverview")
        }
        function onSubmittedSuccessfully(tref) {
            if (navigated) return
            navigated = true
            refreshTimer.running = false
            const page = Qt.resolvedUrl("SavedTransfer.qml")
            middlePanel.currentPageUrl = page
            middlePanel.stackView.replace(page, { transferRef: tref || transferRef })
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

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                AppBackButton {
                    backText : qsTr("Transfers")
                    onClicked: {
                        leftPanel.buttonClicked("SessionsOverview")
                    }
                }

                Text {
                    text: qsTr("Simple Transfer")
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: themeManager.textColor
                    Layout.fillWidth: true
                }

            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "Transfer Reference"
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
                    implicitHeight: refLayout.implicitHeight + 16
                    color: themeManager.backgroundColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    RowLayout {
                        id: refLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: transferRef
                            font.family: "Monospace"
                            font.pixelSize: 10
                            color: themeManager.textColor
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                        }

                        AppCopyButton {
                            textToCopy: transferRef
                            size: 14
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Text {
                    text: "Status"
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
                            spacing: 8

                            AppIcon {
                                source: getStageIcon()
                                width: 16
                                height: 16
                                color: getStageColor()
                            }

                            Text {
                                text: qsTr("Stage:")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textColor
                                Layout.preferredWidth: 60
                            }

                            Text {
                                text: stageName || "—"
                                font.pixelSize: 12
                                color: getStageColor()
                                font.weight: Font.Medium
                                Layout.fillWidth: true
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: themeManager.borderColor
                            opacity: 0.5
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 6
                            Layout.fillWidth: true

                            Text {
                                text: qsTr("Status:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                            Text {
                                text: statusText || "—"
                                font.pixelSize: 12
                                color: themeManager.textColor
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }

                            Text {
                                text: qsTr("Fee:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                            Text {
                                text: (snapshot.feeXmr !== 0 && snapshot.feeXmr !== undefined) ? snapshot.feeXmr.toFixed(12) + " XMR" : "—"
                                font.pixelSize: 12
                                color: themeManager.textColor
                                font.family: "Monospace"
                            }

                            Text {
                                text: qsTr("Payment ID:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                            Text {
                                text: snapshot.payment_id || "—"
                                font.pixelSize: 12
                                color: themeManager.textColor
                                font.family: "Monospace"
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }

                            Text {
                                text: qsTr("Tx ID:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                            Text {
                                text: snapshot.tx_id || "pending"
                                font.pixelSize: 12
                                color: snapshot.tx_id ? themeManager.successColor : themeManager.textSecondaryColor
                                font.family: "Monospace"
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Text {
                    text: qsTr("Recipients (%1)").arg(recipientsModel.count)
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: themeManager.borderColor
                }



                ColumnLayout {
                    id: recipientsLayout
                    Layout.fillWidth: true

                    spacing: 6

                    Repeater {
                        model: recipientsModel
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            color: themeManager.backgroundColor
                            border.color: themeManager.borderColor
                            border.width: 1
                            radius: 2

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                AppIcon {
                                    source: "/resources/icons/arrow-right-up.svg"
                                    width: 12
                                    height: 12
                                    color: themeManager.primaryColor
                                }

                                Text {
                                    text: address
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    font.family: "Monospace"
                                    font.pixelSize: 10
                                    color: themeManager.textColor
                                    wrapMode: Text.NoWrap
                                }

                                Text {
                                    text: amountXmr + " XMR"
                                    Layout.alignment: Qt.AlignRight
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    font.family: "Monospace"
                                    color: themeManager.successColor
                                }

                                AppCopyButton {
                                    textToCopy: address + ":" + amountXmr
                                    size: 12
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 32
                        color: themeManager.backgroundColor
                        border.color: themeManager.borderColor
                        border.width: 1
                        radius: 2
                        visible: recipientsModel.count > 0

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            Text {
                                text: qsTr("Total Amount (inc fee)")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textColor
                                Layout.fillWidth: true
                            }

                            Text {
                                text: totalAmountXmr.toFixed(12) + " XMR"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                                font.family: "Monospace"
                                color: themeManager.successColor
                            }
                        }
                    }

                    Text {
                        visible: recipientsModel.count === 0
                        text: qsTr("No recipients")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignCenter
                        Layout.topMargin: 12
                        Layout.bottomMargin: 12
                    }
                }

            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Approve & Broadcast")
                    iconSource: "/resources/icons/check-circle.svg"
                    visible: stageName === "APPROVING"
                    enabled: WalletManager.walletInstance(snapshot.wallet_name)
                    implicitHeight: 32
                    onClicked: TransferManager.proceedSimpleAfterApproval(transferRef)
                    ToolTip.visible: (hovered && !WalletManager.walletInstance(snapshot.wallet_name))
                    ToolTip.text: qsTr("Connect Wallet")
                    ToolTip.delay: 500
                }

                AppButton {
                    text: qsTr("Abort Transfer")
                    iconSource: "/resources/icons/stop-circle.svg"
                    variant: "error"
                    visible: stageName !== "COMPLETE" && stageName !== "ERROR"
                    implicitHeight: 32
                    onClicked: TransferManager.abortSimpleTransfer(transferRef)
                }
            }
        }
    }

    Component.onCompleted: {
        sessionObj = TransferManager.getSimpleSession(transferRef)
        refreshOnce()
    }
}
