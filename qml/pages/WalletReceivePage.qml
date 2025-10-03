import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    property string walletName: ""
    readonly property var wallet: WalletManager.walletInstance(walletName)

    title: qsTr("Receive · %1").arg(walletName || qsTr("Wallet"))

    background: Rectangle { color: themeManager.backgroundColor }

    // ---- State
    ListModel { id: accountsModel }
    property var subMap: ({})
    property bool  loading: false
    property string errorMessage: ""
    property string debugInfo: ""

    function loadAccounts() {
        if (!walletName || loading) return
        loading = true
        errorMessage = ""
        debugInfo = qsTr("Loading accounts…")
        console.log("[DEBUG] Calling WalletManager.getAccounts for:", walletName)
        WalletManager.getAccounts(walletName)
    }

    function findRowByAccountIndex(major) {
        for (let i = 0; i < accountsModel.count; i++) {
            const a = accountsModel.get(i)
            if (a.account_index === major) return i
        }
        return -1
    }

    Component.onCompleted: loadAccounts()
    onWalletNameChanged: loadAccounts()

    // ---- Header
    header: Rectangle {
        height: 48
        color: themeManager.backgroundColor
        border.color: themeManager.borderColor
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            AppBackButton {
                onClicked: leftPanel.buttonClicked("Wallets")
            }

            Text {
                text: root.title
                color: themeManager.textColor
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            AppButton {
                text: qsTr("Refresh")
                enabled: !!wallet && !loading
                variant : "secondary"
                implicitHeight: 24
                onClicked: loadAccounts()
            }
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

            Rectangle {
                Layout.fillWidth: true
                height: 60
                visible: loading && accountsModel.count === 0
                color: themeManager.backgroundColor
                border.color: themeManager.borderColor
                border.width: 1
                radius: 2

                RowLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    BusyIndicator { running: true; implicitWidth: 20; implicitHeight: 20 }
                    Text { text: qsTr("Loading accounts…"); color: themeManager.textSecondaryColor; font.pixelSize: 12 }
                }
            }

            AppAlert {
                Layout.fillWidth: true
                text: errorMessage
                variant: "error"
                visible: errorMessage !== ""
                closable: true
                onClosed: errorMessage = ""
            }

            Rectangle {
                Layout.fillWidth: true
                height: 80
                visible: !loading && accountsModel.count === 0 && errorMessage === ""
                color: themeManager.backgroundColor
                border.color: themeManager.borderColor
                border.width: 1
                radius: 2
                Text { anchors.centerIn: parent; text: qsTr("No accounts found."); color: themeManager.textSecondaryColor; font.pixelSize: 12 }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Repeater {
                    model: accountsModel
                    delegate: AccountCard {
                        Layout.fillWidth: true
                        width: parent ? parent.width : undefined
                    }
                }
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 12 }
        }
    }


    component AccountCard: Rectangle {

        implicitHeight: contentColumn.implicitHeight + 16
        color: themeManager.backgroundColor
        border.width: 1
        border.color: themeManager.borderColor
        radius: 2

        ColumnLayout {
            id: contentColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 8


            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: qsTr("Account")
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: themeManager.textColor
                }

                Text {
                    text: (label && label.length) ? label : qsTr("(no label)")
                    color: themeManager.textSecondaryColor
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: 12
                    Text {
                        text: qsTr("Balance: %1").arg(formatXmr(balance || 0))
                        font.family: "Monospace"
                        font.pixelSize: 10
                        color: themeManager.textColor
                    }
                    Text {
                        text: qsTr("Unlocked: %1").arg(formatXmr(unlocked || 0))
                        font.family: "Monospace"
                        font.pixelSize: 10
                        color: themeManager.textSecondaryColor
                    }
                }

            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: qsTr("Base Address (Account %1, Index 0)").arg(account_index || 0)
                    color: themeManager.textSecondaryColor
                    font.pixelSize: 10
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 28
                    color: themeManager.backgroundColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 6

                        TextInput {
                            id: baseAddrInput
                            Layout.fillWidth: true
                            text: base_address || ""
                            readOnly: true
                            selectByMouse: true
                            font.family: "Monospace"
                            font.pixelSize: 10
                            color: themeManager.textColor
                            clip: true
                            verticalAlignment: Text.AlignVCenter
                        }

                        AppCopyButton { textToCopy: baseAddrInput.text; size: 12 }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: expanded === true

                Rectangle { Layout.fillWidth: true; height: 1; color: themeManager.borderColor }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("Subaddresses (%1)").arg((root.subMap[account_index] || []).length)
                        color: themeManager.textColor
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        Layout.fillWidth: true
                    }

                    AppInput {
                        id: newSubaddrLabel
                        placeholderText: qsTr("Optional label")
                        implicitWidth: 120
                        font.pixelSize: 10
                    }

                    AppButton {
                        text: qsTr("New subaddress")
                        variant: "secondary"
                        implicitHeight: 28
                        implicitWidth: 120
                        enabled: !!wallet && !loading
                        onClicked: {
                            console.log("[DEBUG] Creating subaddress for account:", account_index, "label:", newSubaddrLabel.text.trim())
                            debugInfo = qsTr("Creating subaddress…")
                            WalletManager.createSubaddress(walletName, account_index, newSubaddrLabel.text.trim())
                            newSubaddrLabel.text = ""
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true

                    Repeater {
                        id: subRep
                        model: (root.subMap[account_index] || []).length

                        delegate: SubaddressItem {
                            Layout.fillWidth: true

                            // pull current item
                            property var subaddrData: {
                                const arr = root.subMap[account_index] || []
                                const it = arr[index] || {}
                                return it
                            }
                        }
                    }
                }

                Text {
                    visible: (root.subMap[account_index] || []).length === 0
                    text: qsTr("No subaddresses created yet. Click 'Add' to create one.")
                    color: themeManager.textSecondaryColor
                    font.pixelSize: 10
                    Layout.alignment: Qt.AlignCenter
                    Layout.topMargin: 8
                    Layout.bottomMargin: 8
                }
            }
        }

        function toggleExpanded() {
            const willExpand = !(expanded === true)
            accountsModel.setProperty(index, "expanded", willExpand)
            if (willExpand) {
                console.log("[DEBUG] Loading subaddresses for account:", account_index)
                debugInfo = qsTr("Loading subaddresses…")
                WalletManager.getSubaddresses(walletName, account_index)
            }
        }

        function formatXmr(atomic) { return ((atomic || 0) / 1e12).toFixed(12) + " XMR" }
    }

    component SubaddressItem: Rectangle {
        property int subaddrIndex: index
        property var subaddrData: {
            const arr = root.subMap[account_index] || []
            return arr[subaddrIndex] || { account_index: 0, address_index: 0, address: "", label: "", balance: 0, unlocked: 0 }
        }
        property bool editing: false
        property string editText: String(subaddrData.label || "")

        implicitHeight: subItemColumn.implicitHeight + 12
        color: themeManager.backgroundColor
        border.color: themeManager.borderColor
        border.width: 0
        radius: 2

        ColumnLayout {
            id: subItemColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 6
            Rectangle { Layout.fillWidth: true; height: 1; color: themeManager.borderColor }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: {
                        const a = subaddrData.account_index ?? 0
                        const i = subaddrData.address_index ?? 0
                        return qsTr("%1/%2").arg(a).arg(i)
                    }
                    color: themeManager.textSecondaryColor
                    font.pixelSize: 10
                    font.weight: Font.Medium
                    Layout.alignment: Qt.AlignVCenter
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        visible: !editing
                        text: subaddrData.label && String(subaddrData.label).length > 0 ? String(subaddrData.label) : qsTr("(no label)")
                        color: themeManager.textColor
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    AppInput {
                        id: labelEditor
                        visible: editing
                        text: editText
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        onTextChanged: editText = text
                        onAccepted: saveLabelEdit()
                    }
                }

                RowLayout {
                    spacing: 8
                    Text {
                        text: qsTr("Bal: %1").arg(formatXmr(subaddrData.balance || 0))
                        font.family: "Monospace"
                        font.pixelSize: 9
                        color: themeManager.textSecondaryColor
                    }
                    Text {
                        text: qsTr("Unlocked: %1").arg(formatXmr(subaddrData.unlocked || 0))
                        font.family: "Monospace"
                        font.pixelSize: 9
                        color: themeManager.textSecondaryColor
                    }
                }

                AppButton {
                    text: editing ? qsTr("Save") : qsTr("Edit")
                    variant: "secondary"
                    implicitHeight: 24
                    implicitWidth: 40
                    onClicked: {
                        if (editing) {
                            saveLabelEdit()
                        } else {
                            editing = true
                            editText = String(subaddrData.label || "")
                            labelEditor.forceActiveFocus()
                            labelEditor.selectAll()
                        }
                    }
                }


                AppButton {
                    text: qsTr("QR")
                    variant: "secondary"
                    implicitHeight: 24
                    implicitWidth: 36
                    onClicked: {
                        qrPopup.openWith(String(subaddrData.address || ""),
                                         subaddrData.account_index, subaddrData.address_index,
                                         String(subaddrData.label || ""))
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 28
                color: themeManager.backgroundColor
                border.color: themeManager.borderColor
                border.width: 0
                radius: 2

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 4

                    TextInput {
                        id: subAddrInput
                        Layout.fillWidth: true
                        text: String(subaddrData.address || "")
                        readOnly: true
                        selectByMouse: true
                        font.family: "Monospace"
                        font.pixelSize: 9
                        color: themeManager.textColor
                        clip: true
                        verticalAlignment: Text.AlignVCenter
                    }

                    AppCopyButton { textToCopy: subAddrInput.text; size: 10 }
                }
            }
        }

        function saveLabelEdit() {
            if (subaddrData.account_index !== undefined && subaddrData.address_index !== undefined) {
                console.log("[DEBUG] Saving label:", subaddrData.account_index, subaddrData.address_index, editText.trim())
                WalletManager.labelSubaddress(walletName, subaddrData.account_index, subaddrData.address_index, editText.trim())
            }
            editing = false
        }

        function formatXmr(atomic) { return ((atomic || 0) / 1e12).toFixed(6) + " XMR" }
    }


    Popup {
        id: qrPopup
        property string addr: ""
        property int maj: 0
        property int min: 0
        property string label: ""
        function openWith(a, A, I, L) { addr=a; maj=A; min=I; label=L; open(); }

        modal: true
        focus: true
        anchors.centerIn: parent
        width: Math.min(380, parent.width - 20)
        height: Math.min(560, parent.height - 20)
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
                text: qsTr("Receive QR · %1/%2").arg(qrPopup.maj).arg(qrPopup.min)
                color: themeManager.textColor
                font.pixelSize: 14
                font.bold: true
                Layout.fillWidth: true
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(320, qrPopup.width - 40)
                Layout.alignment: Qt.AlignHCenter

                QRCode {
                    id: qrCodeItem
                    anchors.centerIn: parent
                    value: qrPopup.addr
                    size: Math.min(320, parent.width)
                    margin: 4
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: themeManager.backgroundColor
                border.color: themeManager.borderColor
                border.width: 1
                radius: 2

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 6

                    TextArea {
                        id: addrBox
                        Layout.fillWidth: true
                        readOnly: true
                        text: qrPopup.addr
                        wrapMode: TextEdit.Wrap
                        font.family: "Monospace"
                        font.pixelSize: 10
                        color: themeManager.textColor
                        selectByMouse: true

                        background: Rectangle {
                            color: "transparent"
                        }
                    }

                    AppCopyButton {
                        textToCopy: addrBox.text
                        size: 12
                        Layout.alignment: Qt.AlignVCenter
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
                    onClicked: qrPopup.close()
                }
            }
        }
    }

    Connections {
        target: WalletManager
        enabled: walletName !== ""

        function onAccountsReady(name, accounts) {
            if (name !== walletName) return

            console.log("[DEBUG] onAccountsReady called with", accounts.length, "accounts")
            debugInfo = "Received " + accounts.length + " accounts"
            loading = false

            accountsModel.clear()
            for (let i = 0; i < accounts.length; ++i) {
                const a = accounts[i]
                accountsModel.append({
                    account_index: a.account_index,
                    label: a.label || "",
                    balance: a.balance || 0,
                    unlocked: a.unlocked || 0,
                    base_address: a.base_address || "",
                    expanded: false
                })
            }

            if (accountsModel.count > 0) {
                accountsModel.setProperty(0, "expanded", true)
                const acc = accountsModel.get(0)
                console.log("[DEBUG] Auto-expanding first account:", acc.account_index)
                WalletManager.getSubaddresses(walletName, acc.account_index)
            }
        }

        function onSubaddressesReady(name, accountIndex, items) {
            if (name !== walletName) return

            console.log("[DEBUG] onSubaddressesReady:", name, accountIndex, "items:", items.length)
            debugInfo = "Received " + items.length + " subaddresses"

            const m = Object.assign({}, root.subMap)
            m[accountIndex] = items
            root.subMap = m

            const row = findRowByAccountIndex(accountIndex)
            console.log("[DEBUG] Row =", row, "sub count =", (root.subMap[accountIndex] || []).length)
        }

        function onSubaddressCreated(name, accountIndex) {
            if (name !== walletName) return
            debugInfo = qsTr("Subaddress created, refreshing…")
            WalletManager.getSubaddresses(walletName, accountIndex)
        }

        function onSubaddressLabeled(name, accountIndex) {
            if (name !== walletName) return
            debugInfo = qsTr("Subaddress labeled, refreshing…")
            WalletManager.getSubaddresses(walletName, accountIndex)
        }

        function onRpcError(name, message) {
            if (name !== walletName) return
            loading = false
            debugInfo = ""
            errorMessage = message || qsTr("Unknown wallet error occurred")
        }
    }
}
