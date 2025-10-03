import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: qsTr("Transfers")

    property string walletName: ""
    property var wallet: null
    property var rawTransfers: []

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    function atomicToXMR(a) { return (Number(a) / 1e12).toFixed(12) }
    function fmtDate(ts) { if (!ts) return ""; return new Date(ts * 1000).toLocaleString() }

    function processTransfers() {
        if (!rawTransfers) return

        const want = dirFilter.currentText
        function keep(m) {
            if (want === "Incoming") return m.direction === "in"
            if (want === "Outgoing") return m.direction === "out"
            if (want === "Pending") return m.height === 0 || m.confirmations === 0
            return true
        }

        transferModel.clear()
        for (let i = 0; i < rawTransfers.length; ++i) {
            const m = rawTransfers[i]
            if (!keep(m)) continue

            const ts = Number(m.timestamp || 0)
            const date = ts ? new Date(ts * 1000) : null
            const section = date ?
                (date.getFullYear() + "-" +
                 (date.getMonth() + 1).toString().padStart(2, '0')) :
                (m.height === 0 ? qsTr("Pending") : qsTr("Unknown date"))

            transferModel.append({
                txid: m.txid || "",
                amount: m.amount || 0,
                blockHeight: m.height || 0,
                direction: m.direction || "",
                confirmations: m.confirmations || 0,
                unlocked: !!m.unlocked,
                subaddr_major: m.subaddr_major || 0,
                subaddr_minor: m.subaddr_minor || 0,
                address: m.address || "",
                timestamp: ts,
                section: section
            })
        }
    }

    Component.onCompleted: {
        wallet = WalletManager.walletInstance(walletName)
        if (wallet) wallet.getTransfers()
    }

    Connections {
        target: WalletManager
        function onEpochChanged() {
            wallet = WalletManager.walletInstance(walletName)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            AppBackButton {
                onClicked: leftPanel.buttonClicked("Wallets")
            }

            Text {
                text: qsTr("Transfers – %1").arg(walletName)
                font.pixelSize: 20
                font.weight: Font.Bold
                color: themeManager.textColor
                Layout.fillWidth: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: qsTr("Filter:")
                font.pixelSize: 12
                color: themeManager.textSecondaryColor
            }

            ComboBox {
                id: dirFilter
                Layout.preferredWidth: 120
                implicitHeight: 28
                model: ["All", "Incoming", "Outgoing", "Pending"]

                background: Rectangle {
                    color: themeManager.surfaceColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2
                }

                contentItem: Text {
                    text: dirFilter.displayText
                    font.pixelSize: 12
                    color: themeManager.textColor
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                    rightPadding: 24
                }

                onCurrentTextChanged: {
                    processTransfers()
                }
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("Refresh")
                iconSource: "/resources/icons/refresh.svg"
                variant: "secondary"
                implicitHeight: 28
                enabled: wallet && !wallet.busy
                onClicked: if (wallet) wallet.getTransfers()
            }
        }

        ListModel { id: transferModel }


        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: transferModel
            spacing: 0
            delegate: transferDelegate
            section.property: "section"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                width: list.width
                color: themeManager.surfaceColor
                height: 24

                Text {
                    text: section
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }
            }

            Text {
                anchors.centerIn: parent
                text: qsTr("No transfers found")
                color: themeManager.textSecondaryColor
                font.pixelSize: 12
                visible: list.count === 0 && wallet && !wallet.busy
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                id: status
                text: wallet ? (wallet.busy ? qsTr("Loading…") : qsTr("%1 transfers").arg(transferModel.count)) : qsTr("Wallet not connected")
                color: wallet && wallet.busy ? themeManager.textSecondaryColor : themeManager.textColor
                font.pixelSize: 10
            }

            Item { Layout.fillWidth: true }
        }
    }

    Component {
        id: transferDelegate
        Rectangle {
            required property string txid
            required property string direction
            required property var amount
            required property int confirmations
            required property int blockHeight
            required property bool unlocked
            required property int subaddr_major
            required property int subaddr_minor
            required property string address
            required property int timestamp
            required property string section

            width: list.width
            height: 52
            color: themeManager.backgroundColor
            border.color: themeManager.borderColor
            border.width: 0

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Rectangle {
                    width: 24
                    height: 24
                    radius: 2
                    color: direction === "in" ? Qt.rgba(0.2, 0.8, 0.3, 0.15) : Qt.rgba(1.0, 0.6, 0.0, 0.15)
                    border.color: direction === "in" ? themeManager.successColor : "#f97316"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: direction === "in" ? "↓" : "↑"
                        color: direction === "in" ? themeManager.successColor : "#f97316"
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: (direction === "in" ? "+" : "-") + atomicToXMR(amount) + " XMR"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: direction === "in" ? themeManager.successColor : themeManager.errorColor
                    }

                    Text {
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 10
                        text: qsTr("%1 conf • %2").arg(confirmations).arg(timestamp ? fmtDate(timestamp) : (blockHeight === 0 ? qsTr("Pending") : qsTr("Block #%1").arg(blockHeight)))
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                Rectangle {
                    visible: !unlocked
                    radius: 2
                    height: 18
                    width: statusText.width + 12
                    color: blockHeight === 0 ? Qt.rgba(1.0, 0.6, 0.0, 0.15) : Qt.rgba(0.8, 0.3, 0.3, 0.15)
                    border.color: blockHeight === 0 ? "#f97316" : themeManager.errorColor
                    border.width: 1

                    Text {
                        id: statusText
                        anchors.centerIn: parent
                        text: blockHeight === 0 ? qsTr("Pending") : qsTr("Locked")
                        color: blockHeight === 0 ? "#f97316" : themeManager.errorColor
                        font.pixelSize: 9
                        font.weight: Font.Medium
                    }
                }

                AppButton {
                    text: qsTr("Details")
                    variant: "secondary"
                    implicitHeight: 24
                    onClicked: detailsPopup.openWith(parent.parent)
                }
            }
        }
    }

    Popup {
        id: detailsPopup
        modal: true
        focus: true
        anchors.centerIn: parent
        width: Math.min(500, parent.width - 20)
        height: Math.min(400, parent.height - 20)
        padding: 0

        background: Rectangle {
            color: themeManager.surfaceColor
            border.color: themeManager.borderColor
            border.width: 1
            radius: 2
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Text {
                text: qsTr("Transfer Details")
                font.bold: true
                font.pixelSize: 14
                color: themeManager.textColor
                Layout.fillWidth: true
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                GridLayout {
                    id: detailsGrid
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6
                    width: detailsPopup.width - 24

                    Text { text: qsTr("Direction:"); color: themeManager.textSecondaryColor; font.pixelSize: 12 }
                    Text { id: d_direction; text: ""; color: themeManager.textColor; font.pixelSize: 12; Layout.fillWidth: true }

                    Text { text: qsTr("Amount:"); color: themeManager.textSecondaryColor; font.pixelSize: 12 }
                    Text { id: d_amount; text: ""; color: themeManager.textColor; font.pixelSize: 12; font.weight: Font.Medium; Layout.fillWidth: true }

                    Text { text: qsTr("Tx ID:"); color: themeManager.textSecondaryColor; font.pixelSize: 12 }

                    RowLayout {
                        Layout.topMargin: 4
                        spacing: 4
                        Layout.fillWidth: true

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 50
                            Layout.maximumWidth: detailsGrid.width - 32
                            color: themeManager.backgroundColor
                            border.color: themeManager.borderColor
                            border.width: 1
                            radius: 2

                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 6
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                                TextArea {
                                    id: d_txid
                                    readOnly: true
                                    wrapMode: TextEdit.Wrap
                                    text: ""
                                    color: themeManager.textColor
                                    font.family: "Monospace"
                                    font.pixelSize: 10
                                    selectByMouse: true
                                    width: parent.width

                                    background: Rectangle {
                                        color: "transparent"
                                    }
                                }
                            }
                        }

                        AppCopyButton {
                            textToCopy: d_txid.text
                            size: 14
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            Layout.alignment: Qt.AlignVCenter

                        }
                    }

                    Text { text: qsTr("Confirmations:"); color: themeManager.textSecondaryColor; font.pixelSize: 12 }
                    Text { id: d_conf; text: ""; color: themeManager.textColor; font.pixelSize: 12; Layout.fillWidth: true }

                    Text { text: qsTr("Unlocked:"); color: themeManager.textSecondaryColor; font.pixelSize: 12 }
                    Text { id: d_unlocked; text: ""; color: themeManager.textColor; font.pixelSize: 12; Layout.fillWidth: true }

                    Text { text: qsTr("When:"); color: themeManager.textSecondaryColor; font.pixelSize: 12 }
                    Text { id: d_when; text: ""; color: themeManager.textColor; font.pixelSize: 12; Layout.fillWidth: true }

                    Text { text: qsTr("Subaddress index:"); color: themeManager.textSecondaryColor; font.pixelSize: 12 }
                    Text { id: d_subaddr; text: ""; color: themeManager.textColor; font.pixelSize: 12; font.family: "Monospace"; Layout.fillWidth: true }

                    Text { text: qsTr("Address:"); color: themeManager.textSecondaryColor; font.pixelSize: 12 }

                    RowLayout {
                        Layout.topMargin: 4
                        spacing: 4
                        Layout.fillWidth: true

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 50
                            Layout.maximumWidth: detailsGrid.width - 32
                            color: themeManager.backgroundColor
                            border.color: themeManager.borderColor
                            border.width: 1
                            radius: 2

                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 6
                                clip: true
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                                TextArea {
                                    id: d_addr
                                    readOnly: true
                                    wrapMode: TextEdit.Wrap
                                    text: ""
                                    color: themeManager.textColor
                                    font.family: "Monospace"
                                    font.pixelSize: 10
                                    selectByMouse: true
                                    width: parent.width

                                    background: Rectangle {
                                        color: "transparent"
                                    }
                                }
                            }
                        }

                        AppCopyButton {
                            textToCopy: d_addr.text
                            size: 14
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            Layout.alignment: Qt.AlignVCenter

                        }
                    }
                }
            }


            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Close")
                    variant: "secondary"
                    Layout.alignment: Qt.AlignRight
                    onClicked: detailsPopup.close()
                }
            }
        }

        function openWith(delegateItem) {
            if (!delegateItem) return

            d_direction.text = delegateItem.direction === "in" ? qsTr("Incoming") : qsTr("Outgoing")
            d_amount.text = ((delegateItem.direction === "in" ? "+" : "-") +
                            atomicToXMR(delegateItem.amount || 0) + " XMR")
            d_txid.text = delegateItem.txid || ""
            d_conf.text = String(delegateItem.confirmations || 0)
            d_unlocked.text = delegateItem.unlocked ? qsTr("Yes") :
                             (delegateItem.blockHeight === 0 ? qsTr("Pending") : qsTr("No"))
            d_when.text = delegateItem.timestamp ? fmtDate(delegateItem.timestamp) :
                         (delegateItem.blockHeight === 0 ? qsTr("Pending") :
                          qsTr("Block #%1").arg(delegateItem.blockHeight))
            d_subaddr.text = (delegateItem.subaddr_major || 0) + qsTr(" / ") + (delegateItem.subaddr_minor || 0)
            d_addr.text = (delegateItem.address || "")
            open()
        }
    }

    Connections {
        target: wallet
        enabled: wallet !== null
        function onTransfersReady(items) {
            if (!items) return

            rawTransfers = items
            processTransfers()
        }
    }
}
