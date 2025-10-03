import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import Qt.labs.folderlistmodel 2.1
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: qsTr("Add Existing Wallet")

    property bool isMultisig: true
    property bool isRecoveryMode: false
    property string selectedWalletFile: ""

    property string peerValidationError: ""
    property string referenceError: ""
    property string walletNameError: ""
    property string addWalletError: ""
    property int selectedPeerIndex: -1
    property string selectedUserOnion: ""
    ListModel { id: userOnionModel }

    background: Rectangle {
        color: themeManager.backgroundColor
    }


    ListModel { id: peerInputModel }
    ListModel { id: addressBookModel }

    function normalizeOnion(o) {
        var s = (o || "").trim().toLowerCase();
        if (s !== "" && !s.endsWith(".onion")) s += ".onion";
        return s;
    }

    function loadAddressBook() {
        addressBookModel.clear();
        if (!accountManager || !accountManager.is_authenticated) return;
        const raw = accountManager.getAddressBook();
        for (let i = 0; i < raw.length; ++i) {
            const it = raw[i];
            addressBookModel.append({ label: String(it.label), onion: String(it.onion) });
        }
    }

    function loadUserOnions() {
        userOnionModel.clear();
        if (!accountManager || !accountManager.is_authenticated) return;
        const identities = accountManager.getTorIdentities();
        for (let i = 0; i < identities.length; ++i) {
            const it = identities[i];
            userOnionModel.append({
                                      label: String(it.label),
                                      onion: String(it.onion),
                                      online: !!it.online
                                  });
        }
    }
    function validateUserOnion() {
        if (!isMultisig) return true;
        if (!selectedUserOnion) return false;
        return /^[a-z0-9]{56}\.onion$/.test(selectedUserOnion.toLowerCase());
    }


    Connections {
        target: accountManager
        function onAddressBookChanged() { loadAddressBook(); }
    }

    function isValidOnion(addr) {
        return /^[a-z0-9]{56}\.onion$/.test(addr.toLowerCase());
    }

    function randomString(len) {
        const alphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
        let s = "";
        for (let i = 0; i < len; ++i)
            s += alphabet[(Math.random() * alphabet.length) | 0];
        return s;
    }

    function validatePeers(requireFilled) {
        const strict = !!requireFilled;
        peerValidationError = "";
        var seen = {};
        const myOn = (selectedUserOnion || "").trim().toLowerCase();

        for (let i = 0; i < peerInputModel.count; ++i) {
            const raw = (peerInputModel.get(i).onion || "").toString().trim().toLowerCase();
            if (!raw.length) {
                if (strict) peerValidationError = qsTr("All rows must contain an onion address or be removed");
                continue;
            }
            if (!isValidOnion(raw)) { peerValidationError = qsTr("Invalid onion address: %1").arg(raw); return; }
            if (myOn && raw === myOn) { peerValidationError = qsTr("Peer list must not include your own onion identity"); return; }
            if (seen[raw]) { peerValidationError = qsTr("Duplicate onion address: %1").arg(raw); return; }
            seen[raw] = true;
        }
        if (strict && isMultisig && Object.keys(seen).length === 0)
            peerValidationError = qsTr("At least one peer address is required");
    }

    function validateReference() {
        if (!isMultisig) { referenceError = ""; return true; }

        const r = refField.text.trim();
        if (!r.length) { referenceError = qsTr("Reference code cannot be empty"); return false; }

        if (!validateUserOnion()) {
            referenceError = qsTr("Select your onion identity first");
            return false;
        }

        const myOn = normalizeOnion(selectedUserOnion);

        if (walletManager.refExistsForOnion && walletManager.refExistsForOnion(r, myOn)) {
            referenceError = qsTr("Reference is already used on %1").arg(myOn);
            return false;
        }

        try {
            if (multisigManager.notifierFor && multisigManager.notifierFor(myOn, r)) {
                referenceError = qsTr("Reference is already used by a notifier on %1").arg(myOn);
                return false;
            }
        } catch (e) { /* ignore */ }

        referenceError = "";
        return true;
    }

    function walletExists(name) {
        return walletManager.nameExists ? walletManager.nameExists(name) : walletManager.walletExists(name);
    }

    function validateWalletName() {
        if (!isRecoveryMode) { walletNameError = ""; return; }
        const n = walletNameField.text.trim();
        if (!n.length) { walletNameError = qsTr("Wallet name cannot be empty"); return; }
        if (walletExists(n)) { walletNameError = qsTr("Wallet %1 already exists").arg(n); return; }
        walletNameError = "";
    }

    function formReady() {
        const seedOk = isRecoveryMode ? (seedArea.text.trim().length && restoreHeightField.text.trim().length) : true;
        const fileOk = !isRecoveryMode ? (selectedWalletFile.length > 0) : true;
        return peerValidationError === "" && referenceError === "" && walletNameError === "" && seedOk && fileOk;
    }

    function importWalletFrontend() {
        addWalletError = "";
        validatePeers(isMultisig);
        validateReference();
        validateWalletName();
        if (!formReady()) return;

        // Collect peers
        var peers = [];
        for (var i = 0; i < peerInputModel.count; ++i) {
            const p = (peerInputModel.get(i).onion || "").toString().trim().toLowerCase();
            if (p.length) peers.push(p);
        }

        if (isMultisig && !validateUserOnion()) {
            addWalletError = qsTr("Please select your onion identity");
            return;
        }

        if (isMultisig && selectedUserOnion) {
            const myOn = selectedUserOnion.trim().toLowerCase();
            if (peers.indexOf(myOn) !== -1) {
                addWalletError = qsTr("Peer list must not include your own onion identity");
                return;
            }
        }

        var wallet_name, seed = "", password = "", restore_height = 0, sourcePath = "", reference = "";

        if (isRecoveryMode) {
            wallet_name = walletNameField.text.trim();
            seed = seedArea.text.trim();
            password = walletPasswordFieldRec.text;
            restore_height = parseInt(restoreHeightField.text);
        } else {
            sourcePath = selectedWalletFile;
            wallet_name = walletNameField.text;
            password = walletPasswordField.text;
        }

        if (isMultisig) {
            reference = refField.text.trim();
        } else {
            reference = randomString(20);
        }

        const ok = walletManager.importWallet(
                     !isRecoveryMode,
                     wallet_name,
                     sourcePath,
                     password,
                     seed,
                     restore_height,
                     isMultisig,
                     reference,
                     peers,
                     selectedUserOnion,
                     "user"
                     );

        addWalletError = ok ? leftPanel.buttonClicked("Wallets")
                            : "Failed to import wallet – please check inputs";
    }


    FileDialog {
        id: walletFileDialog
        title: qsTr("Select Wallet File")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Wallet files (*.keys *.txt)"), qsTr("All files (*)")]
        currentFolder: walletFolder

        onAccepted: {
            selectedWalletFile = selectedFile.toString();
            let fileName = selectedFile.toString().substring(selectedFile.toString().lastIndexOf("/")+1);
            walletFileLabel.text = fileName;
            walletNameField.text = fileName.substring(0, fileName.lastIndexOf("."));
        }
    }


    AppAddressBookDialog {
        id: addressDialog
        titleText: qsTr("Select Peer Address")
        descriptionText: qsTr("Choose a peer address from your address book")
        model: addressBookModel
        addressBookType: "peer"
        primaryField: "label"
        secondaryField: "onion"
        emptyStateText: qsTr("No peer addresses in your address book yet.")

        onItemSelected: function(item, index) {
            if (selectedPeerIndex >= 0 && selectedPeerIndex < peerInputModel.count) {
                peerInputModel.setProperty(selectedPeerIndex, "onion", item.onion)
            }
        }

        onQuickAddRequested: function() {
            const tabMap = { "peer": 0, "trusted": 1, "xmr": 2, "daemon": 3 };
            const tabIndex = tabMap[addressDialog.addressBookType] || 0;

            var pageComponent = Qt.resolvedUrl("UnifiedAddressBook.qml");
            middlePanel.currentPageUrl = pageComponent;
            middlePanel.stackView.replace(pageComponent, { currentTab: tabIndex });
        }
    }


    AppAddressBookDialog {
        id: userOnionDialog
        titleText: qsTr("Select Your Onion Identity")
        descriptionText: qsTr("Choose which onion identity to use")
        model: userOnionModel
        addressBookType: "peer"
        primaryField: "label"
        secondaryField: "onion"
        showStatusIndicator: true
        statusField: "online"
        emptyStateText: qsTr("No onion identities available. Please create one first.")
        showQuickAddButton: false

        onItemSelected: function(item, index) {
            selectedUserOnion = item.onion
            validateUserOnion()
            validateReference()
        }
    }


    readonly property url walletFolder: {
        if (!accountManager || !accountManager.current_account)
            return Qt.resolvedUrl("../../");
        return Qt.resolvedUrl("../../wallets/" + accountManager.current_account);
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    text: qsTr("Add Existing Wallet")
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
                    text: "Wallet Type"
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
                    spacing: 0

                    AppNavButton {
                        text: qsTr("Multisig Wallet")
                        variant: isMultisig ? "primary" : "navigation"
                        Layout.fillWidth: true
                        onClicked: isMultisig = true
                    }

                    AppNavButton {
                        text: qsTr("Standard Wallet")
                        variant: !isMultisig ? "primary" : "navigation"
                        Layout.fillWidth: true
                        onClicked: isMultisig = false
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4
                enabled: isMultisig
                visible: isMultisig

                Text {
                    text: "Multisig Configuration"
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

                    AppInput {
                        id: refField
                        placeholderText: qsTr("Shared reference identifier")
                        Layout.fillWidth: true
                        errorText: referenceError
                        onTextChanged: validateReference()
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: qsTr("Your Identity")
                            color: themeManager.textColor
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
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
                                        source: "/resources/icons/shield-network.svg"
                                        width: 16; height: 16
                                        color: themeManager.textSecondaryColor
                                    }

                                    TextField {
                                        id: ownedIdField
                                        text: selectedUserOnion ? (selectedUserOnion) : qsTr("Select your onion identity")
                                        color: selectedUserOnion ? themeManager.textColor : themeManager.textSecondaryColor
                                        font.pixelSize: 12
                                        readOnly: true
                                        Layout.fillWidth: true
                                        background: Rectangle { color: themeManager.backgroundColor; border.width: 0 }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: { loadUserOnions(); userOnionDialog.open(); }
                                }
                            }

                            AppIconButton {
                                iconSource: "/resources/icons/book-bookmark.svg"
                                size: 18
                                onClicked: { loadUserOnions(); userOnionDialog.open(); }
                            }
                        }
                    }


                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: qsTr("Peer Addresses (exclude your identity)")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textColor
                                Layout.fillWidth: true
                            }

                            AppButton {
                                text: qsTr("Add Peer")
                                variant: "secondary"
                                onClicked: peerInputModel.append({onion: ""})
                            }
                        }


                        Repeater {
                            model: peerInputModel
                            delegate: RowLayout {
                                spacing: 6
                                Layout.fillWidth: true

                                AppInput {
                                    placeholderText: qsTr("peer.onion")
                                    text: model.onion
                                    Layout.fillWidth: true
                                    font.family: "Monospace"
                                    font.pixelSize: 10
                                    onTextChanged: {
                                        peerInputModel.setProperty(index, "onion", text);
                                        validatePeers();
                                    }
                                }

                                AppIconButton {
                                    iconSource: "/resources/icons/book-bookmark.svg"
                                    size: 14
                                    onClicked: {
                                        selectedPeerIndex = index;
                                        loadAddressBook();
                                        addressDialog.open();
                                    }
                                }

                                AppIconButton {
                                    iconSource: "/resources/icons/close-circle.svg"
                                    size: 14
                                    iconColor: peerInputModel.count > 1? themeManager.errorColor : themeManager.textSecondaryColor
                                    enabled: peerInputModel.count > 1
                                    onClicked: peerInputModel.remove(index)
                                }
                            }
                        }

                        AppAlert {
                            text: peerValidationError
                            variant: "error"
                            visible: peerValidationError !== ""
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Text {
                    text: "Wallet Source"
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
                    spacing: 0

                    AppNavButton {
                        text: qsTr("Load from File")
                        variant: !isRecoveryMode ? "primary" : "navigation"
                        Layout.fillWidth: true
                        onClicked: isRecoveryMode = false
                    }

                    AppNavButton {
                        text: qsTr("Recover from Seed")
                        variant: isRecoveryMode ? "primary" : "navigation"
                        Layout.fillWidth: true
                        onClicked: isRecoveryMode = true
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Text {
                    text: isRecoveryMode ? "Seed Recovery" : "Wallet File"
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

                    ColumnLayout {
                        visible: !isRecoveryMode
                        spacing: 8
                        Layout.fillWidth: true

                        RowLayout {
                            spacing: 8
                            Layout.fillWidth: true

                            AppInput {
                                id: walletFileLabel
                                placeholderText: qsTr("No file selected")
                                readOnly: true
                                Layout.fillWidth: true
                            }

                            AppButton {
                                text: qsTr("Browse")
                                variant: "secondary"
                                Layout.alignment: Qt.AlignBottom
                                onClicked: walletFileDialog.open()
                            }
                        }

                        AppInput {
                            id: walletPasswordField
                            placeholderText: qsTr("Enter wallet password")
                            echoMode: TextInput.Password
                            Layout.fillWidth: true
                        }
                    }

                    ColumnLayout {
                        visible: isRecoveryMode
                        spacing: 8
                        Layout.fillWidth: true

                        AppInput {
                            id: walletNameField
                            placeholderText: qsTr("Enter wallet name")
                            Layout.fillWidth: true
                            errorText: walletNameError
                            onTextChanged: validateWalletName()
                        }

                        AppInput {
                            id: walletPasswordFieldRec
                            placeholderText: qsTr("Enter password to encrypt wallet (optional)")
                            echoMode: TextInput.Password
                            Layout.fillWidth: true
                        }

                        ColumnLayout {
                            spacing: 6
                            Layout.fillWidth: true

                            Text {
                                text: qsTr("Seed Phrase")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textColor
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 100
                                color: themeManager.backgroundColor
                                border.color: themeManager.borderColor
                                border.width: 1
                                radius: 2

                                ScrollView {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    clip: true

                                    TextArea {
                                        id: seedArea
                                        placeholderText: qsTr("Enter seed phrase (simple wallets) or seed string (multisig wallets)")
                                        wrapMode: TextEdit.Wrap
                                        font.pixelSize: 12
                                        color: themeManager.textColor
                                        selectByMouse: true
                                        selectByKeyboard: true

                                        background: Rectangle {
                                            color: "transparent"
                                        }
                                    }
                                }
                            }
                        }

                        AppInput {
                            id: restoreHeightField
                            placeholderText: qsTr("Restore height - number")
                            Layout.fillWidth: true
                            validator: IntValidator { bottom: 0 }
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: 8
                Layout.fillWidth: true
                Layout.topMargin: 8

                AppButton {
                    text: qsTr("Import Wallet")
                    enabled: formReady()
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 160
                    onClicked: importWalletFrontend()
                }

                AppAlert {
                    text: addWalletError
                    variant: addWalletError.startsWith("Failed") ? "error" : "success"
                    visible: addWalletError !== ""
                    Layout.fillWidth: true
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 12
            }
        }
    }

    Component.onCompleted: {
        peerInputModel.append({onion: ""});
        loadAddressBook();
        loadUserOnions();
    }
}
