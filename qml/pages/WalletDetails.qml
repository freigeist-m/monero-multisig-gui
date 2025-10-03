import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: page
    title: qsTr("Wallet Details")

    required property string walletName

    property var meta: ({})
    property var peersModel: ListModel {}
    property bool editingPeers: false
    property var userOnionModel: ListModel {}
    property string selectedOwnedOnion: ""

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    function isOnion(s) {
        return /^[a-z0-9]{56}\.onion$/.test(String(s || "").trim().toLowerCase())
    }

    function isWalletConnected() {
        return WalletManager.connectedWalletNames().indexOf(walletName) !== -1
    }

    function walletNameExists(name) {
        return WalletManager.walletExists(name)
    }

    function refresh() {
        meta = WalletManager.getWalletMeta(walletName)
        if (!meta) meta = {}

        peersModel.clear()
        const p = meta.peers || []
        if (p.length === 0) peersModel.append({ onion: "" })
        else p.forEach(o => peersModel.append({ onion: String(o) }))

        onlineSwitch.checked = !!meta.online

        addressField.text = meta.address || ""
        refField.text = meta.reference || ""
        pwdField.text = meta.password || ""
        myOnionField.text = meta.my_onion || ""

        editingPeers = false
    }

    function loadOwnedOnions() {
        userOnionModel.clear();
        if (!accountManager || !accountManager.is_authenticated) return;
        const list = accountManager.getTorIdentities();
        for (let i = 0; i < list.length; ++i) {
            const it = list[i];
            userOnionModel.append({
                                      label: String(it.label),
                                      onion: String(it.onion).toLowerCase(),
                                      online: !!it.online
                                  });
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

            // Header
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                AppBackButton {
                    onClicked: leftPanel.buttonClicked("Wallets")
                }



                Text {
                    text: qsTr("Wallet Details")
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: themeManager.textColor
                    Layout.fillWidth: true
                }

                AppButton {
                    text: qsTr("Rename")
                    variant: "secondary"
                    enabled: !isWalletConnected()
                    onClicked: renameDialog.open()

                    ToolTip.visible: hovered && !enabled
                    ToolTip.text: qsTr("Disconnect wallet to rename")
                }
            }

            AppAlert {
                id: generalAlert
                Layout.fillWidth: true
                visible: false
                variant: "warning"
                closable: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: qsTr("Wallet Information")
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: themeManager.borderColor
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 8

                    Text {
                        text: qsTr("Wallet Name:")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                        Layout.preferredWidth: 120
                    }
                    Text {
                        text: page.walletName
                        color: themeManager.textColor
                        font.weight: Font.Medium
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTr("Wallet Type:")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                        Layout.preferredWidth: 120
                    }

                    Text {
                        text: meta.multisig ? qsTr("Multisig") : qsTr("Standard")
                        color: themeManager.textColor
                        font.weight: Font.Medium
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }


                    Text {
                        text: qsTr("Signature Threshold:")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                        Layout.preferredWidth: 120
                        visible: meta.multisig
                    }

                    Text {
                        text: qsTr("%1 / %2").arg(meta.threshold).arg(meta.total)
                        color: themeManager.textColor
                        font.weight: Font.Medium
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        visible: meta.multisig
                    }

                    Text {
                        text: qsTr("Restore Height:")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                        Layout.preferredWidth: 120
                        visible: meta.restore_height !== undefined && meta.restore_height > 0
                    }

                    RowLayout{
                        spacing: 6
                        visible: meta.restore_height !== undefined && meta.restore_height > 0

                        Text {
                            id: restoreHeight
                            text: meta.restore_height || ""
                            color: themeManager.textColor
                            font.family: "Monospace"
                            font.pixelSize: 11
                        }

                        AppCopyButton {
                            textToCopy: restoreHeight.text
                            size: 16
                        }

                        Item { Layout.fillWidth: true}

                    }

                    Text {
                        text: qsTr("Created by:")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                        Layout.preferredWidth: 120
                    }

                    Text {
                        text: meta.creator || "user"
                        color: themeManager.textColor
                        font.family: "Monospace"
                        font.pixelSize: 11
                        Layout.fillWidth: true
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4
                visible: !!meta.multisig

                Text {
                    text: qsTr("Reference Code")
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: themeManager.borderColor
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 6

                    AppInput {
                        id: refField
                        Layout.fillWidth: true
                        readOnly: true
                        placeholderText: qsTr("Reference code")
                        font.family: "Monospace"
                        font.pixelSize: 11
                    }

                    AppButton {
                        text: qsTr("Edit")
                        variant: "secondary"
                        onClicked: editReferenceDialog.open()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Text {
                    text: qsTr("Main Address")
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: themeManager.borderColor
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 6

                    AppInput {
                        id: addressField
                        Layout.fillWidth: true
                        readOnly: true
                        placeholderText: qsTr("Wallet address")
                        font.family: "Monospace"
                        font.pixelSize: 10
                    }

                    AppCopyButton {
                        textToCopy: addressField.text
                        size: 16
                    }
                }
            }


            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4
                visible: !!meta.multisig

                Text {
                    text: qsTr("Multisig Settings")
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: themeManager.borderColor
                }

                AppSwitch {
                    id: onlineSwitch
                    label: qsTr("Share info with peers (online)")

                    Layout.topMargin: 8
                    onToggled: {
                        const desired = onlineSwitch.checked
                        const ok = WalletManager.setWalletOnlineStatus(page.walletName, desired)
                        if (!ok) {

                            onlineSwitch.checked = !desired
                            console.warn("Failed to set online status")
                        } else {
                            meta.online = desired
                        }
                    }
                }
            }


            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4
                visible: !!meta.multisig

                Text {
                    text: qsTr("My onion")
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: themeManager.borderColor
                }


                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    AppInput {
                        id: myOnionField
                        Layout.fillWidth: true
                        text: meta.my_onion
                        readOnly: true
                        placeholderText: qsTr("Your onion identity")
                        font.family: "Monospace"
                        font.pixelSize: 10
                    }

                    AppButton {
                        text: qsTr("Change")
                        variant: "secondary"
                        onClicked: {
                            loadOwnedOnions();
                            selectedOwnedOnion = meta.my_onion || "";
                            ownedOnionDialog.open();
                        }
                    }

                    AppCopyButton {
                        textToCopy: myOnionField.text
                        size: 16
                    }
                }

            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4
                visible: !!meta.multisig

                Text {
                    text: qsTr("Peers (onion addresses including mine)")
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
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 8

                    Repeater {
                        model: peersModel
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            AppInput {
                                Layout.fillWidth: true
                                text: onion
                                readOnly: !editingPeers
                                placeholderText: qsTr("Peer onion address")
                                font.family: "Monospace"
                                font.pixelSize: 10
                                onTextChanged: {
                                    if (editingPeers) {
                                        peersModel.setProperty(index, "onion", text.trim())
                                    }
                                }
                            }

                            AppIconButton {
                                iconSource: "/resources/icons/close-circle.svg"
                                iconColor: themeManager.errorColor
                                size: 16
                                visible: editingPeers
                                enabled: peersModel.count > 1
                                onClicked: peersModel.remove(index)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        AppButton {
                            text: qsTr("Add Peer")
                            variant: "secondary"
                            visible: editingPeers
                            onClicked: peersModel.append({ onion: "" })
                        }

                        Item { Layout.fillWidth: true }

                        AppButton {
                            text: qsTr("Edit Peers")
                            variant: "secondary"
                            visible: !editingPeers
                            onClicked: editingPeers = true
                        }

                        AppButton {
                            text: qsTr("Save Changes")
                            visible: editingPeers
                            onClicked: {
                                const out = []
                                for (let i = 0; i < peersModel.count; ++i) {
                                    const v = peersModel.get(i).onion.trim()
                                    if (v.length === 0) continue
                                    if (!isOnion(v)) {
                                        peerErr.text = qsTr("Invalid onion: %1").arg(v)
                                        return
                                    }
                                    out.push(v)
                                }
                                if (WalletManager.updateWalletPeers(walletName, out)) {
                                    peerErr.text = ""
                                    editingPeers = false
                                    refresh()
                                } else {
                                    peerErr.text = qsTr("Failed to save peers")
                                }
                            }
                        }

                        AppButton {
                            text: qsTr("Cancel")
                            variant: "secondary"
                            visible: editingPeers
                            onClicked: {
                                editingPeers = false
                                refresh() // Reset peers to original state
                            }
                        }
                    }

                    AppAlert {
                        id: peerErr
                        variant: "error"
                        visible: text !== ""
                        Layout.fillWidth: true
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Text {
                    text: qsTr("Password & Seed")
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
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 8

                    RowLayout {
                        spacing: 8

                        AppInput {
                            id: pwdField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Wallet password")
                            echoMode: showPwd.checked ? TextInput.Normal : TextInput.Password
                        }

                        AppSwitch {
                            id: showPwd
                            label: qsTr("Show")
                            Layout.alignment: Qt.AlignVCenter
                        }

                        AppButton {
                            text: qsTr("Change")
                            variant: "secondary"
                            enabled: isWalletConnected()
                            onClicked: changePwdDialog.open()
                            ToolTip.visible: hovered && !enabled
                            ToolTip.text: qsTr("Connect wallet to change password")
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 70
                        color: themeManager.backgroundColor
                        border.color: themeManager.borderColor
                        border.width: 1
                        radius: 2

                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true

                            TextArea {
                                id: seedArea
                                readOnly: true
                                wrapMode: Text.Wrap
                                selectByMouse: true
                                text: meta.seed || ""
                                visible: showSeed.checked
                                color: themeManager.textColor
                                font.family: "Monospace"
                                font.pixelSize: 10
                                background: Item {}
                            }

                            TextArea {
                                id: seedMask
                                readOnly: true
                                wrapMode: Text.Wrap
                                text: (meta.seed || "").replace(/./g, "•")
                                visible: !showSeed.checked
                                color: themeManager.textColor
                                font.family: "Monospace"
                                font.pixelSize: 10
                                background: Item {}
                            }
                        }
                    }

                    AppSwitch {
                        id: showSeed
                        label: qsTr("Show seed")
                        Layout.alignment: Qt.AlignLeft
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Text {
                    text: qsTr("Danger Zone")
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
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 8

                    Text {
                        text: qsTr("Permanently delete this wallet from your account. This action cannot be undone.")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    AppButton {
                        text: qsTr("Delete Wallet")
                        variant: "error"
                        Layout.alignment: Qt.AlignLeft
                        onClicked: deleteWalletDialog.open()
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 12
            }
        }
    }


    AppFormDialog {
        id: changePwdDialog
        titleText: qsTr("Change Wallet Password")
        confirmButtonText: qsTr("Change Password")
        confirmEnabled: newPwd1.text.length > 0 && newPwd1.text === newPwd2.text
        errorText: pwdErr.visible ? pwdErr.text : ""

        content: [
            AppInput {
                id: newPwd1
                Layout.fillWidth: true
                placeholderText: qsTr("New password")
                iconSource: "/resources/icons/key.svg"
                echoMode: TextInput.Password
            },

            AppInput {
                id: newPwd2
                Layout.fillWidth: true
                placeholderText: qsTr("Repeat new password")
                iconSource: "/resources/icons/key.svg"
                echoMode: TextInput.Password
                errorText: newPwd1.text !== newPwd2.text && newPwd2.text.length > 0 ? qsTr("Passwords do not match") : ""
            }
        ]

        property bool pwdErr: false
        property string pwdErrText: ""

        onAccepted: {

            if (!isWalletConnected()) {
                errorText = qsTr("Wallet disconnected")
                return}

            if (!WalletManager.updateWalletPassword(walletName, newPwd1.text)) {
                errorText = qsTr("Could not start password change")
                changePwdDialog.open()
                return
            }

        }

        onOpened: {
            newPwd1.text = ""
            newPwd2.text = ""
            errorText = ""
            newPwd1.forceActiveFocus()
        }
    }


    AppInputDialog {
        id: renameDialog
        titleText: qsTr("Rename Wallet")
        placeholderText: qsTr("New wallet name")
        inputText: page.walletName
        iconSource: "/resources/icons/wallet.svg"
        confirmButtonText: qsTr("Rename")

        onAccepted: function(newName) {
            const nn = newName.trim()
            if (nn === "") {
                renameDialog.errorText = qsTr("Wallet name cannot be empty")
                renameDialog.open()
                return
            }
            if (nn === page.walletName) {
                renameDialog.errorText = ""
                return
            }
            if (walletNameExists(nn)) {
                renameDialog.errorText = qsTr("Wallet name already exists")
                renameDialog.open()
                return
            }

            if (WalletManager.renameWallet(page.walletName, nn)) {
                page.walletName = nn
                renameDialog.errorText = ""
                refresh()
            } else {
                renameDialog.errorText = qsTr("Failed to rename wallet")
                renameDialog.open()
            }
        }

        onOpened: {
            inputText = page.walletName
            errorText = ""
        }
    }

    AppInputDialog {
        id: editReferenceDialog
        titleText: qsTr("Edit Reference Code")
        placeholderText: qsTr("Reference code")
        inputText: meta.reference || ""
        iconSource: "/resources/icons/hashtag.svg"
        confirmButtonText: qsTr("Update")

        onAccepted: function(newRef) {
            const ref = newRef.trim()
            if (ref === "") {
                generalAlert.text = qsTr("Reference code cannot be empty")
                generalAlert.visible =  true
                editReferenceDialog.open()
                return
            }
            if (ref === meta.reference) return

            const my = String(meta.my_onion || "").trim().toLowerCase()
            const existing = walletManager.walletNameForRef(ref, my) || multisigManager.sessionFor(my, ref) || multisigManager.hasNotifier(my, ref)
            if (existing && existing !== page.walletName) {
                generalAlert.text  = qsTr("Reference already used by another wallet for this identity")
                generalAlert.visible =  true
                editReferenceDialog.open()
                return
            }

            if (WalletManager.updateWalletReference(walletName, ref)) {
                editReferenceDialog.errorText = ""
                refresh()
            } else {
                generalAlert.text = qsTr("Failed to update reference code")
                generalAlert.visible =  true
                editReferenceDialog.open()
            }
        }

        onOpened: {
            inputText = meta.reference || ""
            errorText = ""
        }
    }

    AppFormDialog {
        id: deleteWalletDialog
        titleText: qsTr("Delete Wallet")
        confirmButtonText: qsTr("Delete Wallet")
        confirmEnabled: confirmPwd.text.length > 0

        content: [
            Text {
                text: qsTr("This will permanently delete the wallet:")
                color: themeManager.textColor
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            RowLayout {
                Item{Layout.fillWidth: true}
                Text {
                    text: qsTr("%1").arg(page.walletName)
                    color: themeManager.textColor
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    wrapMode: Text.WordWrap

                }
                Item{Layout.fillWidth: true}
            },

            Text {
                text: qsTr("This action cannot be undone.")
                color: themeManager.textColor
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true

            },

            Text {
                text: qsTr("Enter your account password to confirm:")
                color: themeManager.textSecondaryColor
                font.pixelSize: 12
                Layout.fillWidth: true
                Layout.topMargin: 8
            },

            AppInput {
                id: confirmPwd
                Layout.fillWidth: true
                placeholderText: qsTr("Account password")
                iconSource: "/resources/icons/lock-password.svg"
                echoMode: TextInput.Password
                onAccepted: {
                    if (confirmPwd.text.length > 0) {
                        deleteWalletDialog.accepted()
                    }
                }
            }
        ]

        onAccepted: {
            if (!accountManager.verifyPassword(confirmPwd.text)) {
                errorText = qsTr("Incorrect password")
                deleteWalletDialog.open()
                return
            }
            if (WalletManager.removeWallet(walletName)) {
                leftPanel.buttonClicked("Wallets")
            } else {
                errorText = qsTr("Failed to remove wallet")
                deleteWalletDialog.open()
            }
        }

        onOpened: {
            confirmPwd.text = ""
            errorText = ""
            confirmPwd.forceActiveFocus()
        }
    }

    AppAddressBookDialog {
        id: ownedOnionDialog
        titleText: qsTr("Select Your Onion Identity")
        descriptionText: qsTr("Choose one of your own identities (you control the key)")
        model: userOnionModel
        addressBookType: "peer"
        primaryField: "label"
        secondaryField: "onion"
        showStatusIndicator: true
        statusField: "online"
        emptyStateText: qsTr("No identities found. Create one in Settings.")
        confirmButtonText: qsTr("Select")
        showQuickAddButton: false

        onItemSelected: function(item, index) {
            const sel = String(item.onion || "").toLowerCase()
            if (sel === String(meta.my_onion || "").toLowerCase()) return
            if (!isOnion(sel)) return

            const curRef = String(meta.reference || "").trim()
            if (curRef.length) {
                const collision = walletManager.walletNameForRef(curRef, sel) || multisigManager.sessionFor(sel, curRef) || multisigManager.hasNotifier(curRef, sel)
                if (collision && collision !== page.walletName) {
                    generalAlert.text = qsTr("That identity already uses this reference")
                    generalAlert.visible =  true
                    ownedOnionDialog.open()
                    return
                }
            }

            if (!WalletManager.updateWalletMyOnion(page.walletName, sel)) {
                generalAlert.text = qsTr("Failed to change identity")
                generalAlert.visible =  true
                ownedOnionDialog.open()
                return
            }
            refresh()
        }
    }


    Connections {
        target: WalletManager

        function onPasswordReady(success, name, newPassword) {
            if (name !== page.walletName) return
            if (success) {
                pwdField.text = newPassword
                changePwdDialog.errorText = ""
                refresh()
            } else {
                changePwdDialog.errorText = qsTr("Failed to change password")
                changePwdDialog.open()
            }
        }

        function onWalletsChanged() { refresh() }
    }

    Connections {
        target: accountManager
        function onTorIdentitiesChanged() { loadOwnedOnions(); }
    }

    Component.onCompleted:{
        loadOwnedOnions();

        refresh()}
}
