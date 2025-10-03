import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import Qt.labs.folderlistmodel 2.1
import Qt5Compat.GraphicalEffects
import "components"

Page {
    id: multisigPage
    title: qsTr("Multisig Setup")

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    required property string sessionRef
    readonly property bool sessionActive: sessionRef !== ""

    readonly property QtObject sessionObj: sessionActive ? multisigManager.sessionFor(selectedUserOnion ,sessionRef) : null

    readonly property string walletNameFromSession: sessionObj ? sessionObj.walletName : ""
    readonly property int mFromSession: sessionObj ? sessionObj.m : 2
    readonly property int nFromSession: sessionObj ? sessionObj.n : 2

    property bool sessionCompleted: false
    property string peerValidationError: ""
    property string referenceError: ""
    property string walletNameError: ""
    property string userOnionError: ""
    property int selectedPeerIndex: -1
    property string selectedUserOnion: ""

    property var notifySelection: ({})
    property bool notifierStarted: false
    property string notifierMessage: ""

    ListModel { id: peerInputModel }
    ListModel { id: peerModel }
    ListModel { id: addressBookModel }
    ListModel { id: userOnionModel }

    function normalizeOnion(o) {
        var s = (o || "").trim().toLowerCase();
        if (s !== "" && !s.endsWith(".onion")) s += ".onion";
        return s;
    }

    function validateReference() {
        const r = refField.text.trim();
        if (r.length === 0) {
            referenceError = qsTr("Reference code cannot be empty");
            return false;
        }

        if (selectedUserOnion === "") {
            referenceError = qsTr("Select your onion identity first");
            return false;
        }

        const on = normalizeOnion(selectedUserOnion);
        if (walletManager.refExistsForOnion(r, on)) {
            referenceError = qsTr("Reference is already used on %1").arg(on);
            return false;
        }

        referenceError = "";
        return true;
    }

    function validateUserOnion() {
        if (selectedUserOnion === "") {
            userOnionError = qsTr("You must select an onion identity");
            return false;
        }
        userOnionError = "";
        return true;
    }

    function validateThreshold() {
        const threshold = parseInt(thresholdField.text) || 2;
        const maxThreshold = peerInputModel.count + 1;
        if (threshold > maxThreshold) {
            return qsTr("Threshold cannot be larger than %1 (peers + you)").arg(maxThreshold);
        }
        return "";
    }

    function walletExists(name) {
        return walletManager.walletExists(name);
    }

    function validateWalletName() {
        const n = walletNameField.text.trim();
        if (n === "") {
            walletNameError = qsTr("Wallet name required");
            return false;
        }
        if (walletExists(n)) {
            walletNameError = qsTr("Wallet already exists");
            return false;
        }
        walletNameError = "";
        return true;
    }

    function isValidOnion(a) {
        return /^[a-z0-9]{56}\.onion$/.test(a.toLowerCase());
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
            const identity = identities[i];
            userOnionModel.append({
                label: String(identity.label),
                onion: String(identity.onion),
                online: !!identity.online
            });
        }
    }

    function refreshPeerModel() {
        peerModel.clear();
        if (!sessionObj) return;

        const peers = sessionObj.peerList;
        for (let i = 0; i < peers.length; ++i) {
            const p = peers[i];
            const key = String(p.onion).trim().toLowerCase();
            if (notifySelection[key] === undefined) notifySelection[key] = true;
            peerModel.append({
                onion: key,
                online: !!p.online,
                pstage: String(p.pstage),
                notify: !!notifySelection[key]
            });
        }
    }

    function selectedNotifyPeers() {
        const out = [];
        for (let i = 0; i < peerModel.count; ++i) {
            const row = peerModel.get(i);
            if (row.notify) out.push(row.onion);
        }
        return out;
    }

    function getUserOnionLabel(onion) {
        for (let i = 0; i < userOnionModel.count; ++i) {
            const item = userOnionModel.get(i);
            if (item.onion === onion) {
                return item.label;
            }
        }
        return onion;
    }

    // Connections
    Connections {
        target: accountManager
        function onAddressBookChanged() { loadAddressBook(); }
        function onTorIdentitiesChanged() { loadUserOnions(); }
    }

    Connections {
        target: sessionObj
        enabled: sessionActive && sessionObj
        ignoreUnknownSignals: true
        function onStageChanged(s) { stageLabel.text = s

        if (s === 'COMPLETE'){
            redirectTimer.start()
        }

        }
        function onPeerStatusChanged() { refreshPeerModel() }
        function onFinished(result) {
            sessionCompleted = true

        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 8
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            id: mainColumn
            width: parent.width
            spacing: 8

            AppBackButton {
                backText: qsTr("Back to Multisig")
                onClicked: leftPanel.buttonClicked("MultisigLandingPage")
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: !sessionActive

                Text {
                    text: "Setup New Multisig Session"
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
                    implicitHeight: setupLayout.implicitHeight + 16
                    color: themeManager.backgroundColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    ColumnLayout {
                        id: setupLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 8

                        Text {
                            text: "Configure your new multisig wallet parameters"
                            color: themeManager.textSecondaryColor
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignHCenter
                        }

                        AppInput {
                            id: refField
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            placeholderText: "Enter unique reference code"
                            iconSource: "/resources/icons/hashtag.svg"
                            onTextChanged: validateReference()
                            errorText: referenceError
                        }

                        AppInput {
                            id: walletNameField
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            placeholderText: "Enter wallet name"
                            iconSource: "/resources/icons/wallet.svg"
                            onTextChanged: validateWalletName()
                            errorText: walletNameError
                        }

                        AppInput {
                            id: walletPasswordField
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            placeholderText: "Enter wallet password"
                            iconSource: "/resources/icons/lock-password.svg"
                            echoMode: TextInput.Password
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
                                    border.color: own_id.hovered ?  themeManager.textSecondaryColor : themeManager.borderColor
                                    border.width: 1
                                    radius: 2

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 8

                                        AppIcon {
                                            source: "/resources/icons/shield-network.svg"
                                            width: 16
                                            height: 16
                                            color: themeManager.textSecondaryColor
                                        }

                                        TextField {
                                            id: own_id
                                            text: selectedUserOnion ? getUserOnionLabel(selectedUserOnion) + " (" + selectedUserOnion + ")" : qsTr("Select your onion identity")
                                            color: selectedUserOnion ? themeManager.textColor : themeManager.textSecondaryColor
                                            font.pixelSize: 12
                                            Layout.fillWidth: true
                                            readOnly: true

                                            background: Rectangle {
                                                anchors.fill: parent
                                                color: themeManager.backgroundColor
                                                border.width: 0
                                            }
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            loadUserOnions();
                                            userOnionDialog.open();
                                        }
                                    }
                                }

                                AppIconButton {
                                    iconSource: "/resources/icons/book-bookmark.svg"
                                    size: 18
                                    onClicked: {
                                        loadUserOnions();
                                        userOnionDialog.open();
                                    }
                                }
                            }

                            Text {
                                visible: userOnionError !== ""
                                text: userOnionError
                                color: themeManager.errorColor
                                font.pixelSize: 11
                                Layout.fillWidth: true
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 12

                            Text {
                                text: qsTr("Threshold")
                                color: themeManager.textColor
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }

                            RowLayout {
                                spacing: 6

                                AppIconButton {
                                    iconSource: "/resources/icons/minus-circle.svg"
                                    iconColor: themeManager.textColor
                                    size: 16
                                    enabled: parseInt(thresholdField.text) > 2
                                    onClicked: {
                                        const current = parseInt(thresholdField.text) || 2
                                        if (current > 2) {
                                            thresholdField.text = (current - 1).toString()
                                        }
                                    }
                                }

                                AppInput {
                                    id: thresholdField
                                    Layout.preferredWidth: 50
                                    text: "2"
                                    horizontalAlignment: Text.AlignHCenter
                                    validator: IntValidator { bottom: 2; top: 10 }
                                    onTextChanged: {
                                        const val = parseInt(text) || 2
                                        if (val < 2) text = "2"
                                        else if (val > 10) text = "10"
                                    }
                                }

                                AppIconButton {
                                    iconSource: "/resources/icons/add-circle.svg"
                                    iconColor: themeManager.textColor
                                    size: 16
                                    enabled: parseInt(thresholdField.text) < 10
                                    onClicked: {
                                        const current = parseInt(thresholdField.text) || 2
                                        if (current < 10) {
                                            thresholdField.text = (current + 1).toString()
                                        }
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: themeManager.borderColor
                        }

                        Text {
                            text: "Peer Addresses"
                            color: themeManager.textColor
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Repeater {
                            model: peerInputModel

                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                AppInput {
                                    id: peerField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Peer onion address")
                                    text: onion
                                    iconSource: "/resources/icons/shield-network.svg"
                                    onTextChanged: peerInputModel.setProperty(index, "onion", text.trim())
                                }

                                AppIconButton {
                                    iconSource: "/resources/icons/book-bookmark.svg"
                                    size: 18
                                    onClicked: {
                                        selectedPeerIndex = index;
                                        loadAddressBook();
                                        addressDialog.open();
                                    }
                                }

                                AppIconButton {
                                    iconSource: "/resources/icons/close-circle.svg"
                                    iconColor:   peerInputModel.count > 1? themeManager.errorColor :  themeManager.textSecondaryColor
                                    size: 18
                                    enabled: peerInputModel.count > 1
                                    onClicked: peerInputModel.remove(index)
                                }
                            }
                        }

                        AppButton {
                            text: qsTr("Add another peer")
                            iconSource: "/resources/icons/add-circle.svg"
                            variant: "secondary"
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            onClicked: peerInputModel.append({ onion: "", notify: true })
                        }

                        AppAlert {
                            id: errorAlert
                            Layout.fillWidth: true
                            visible: false
                            variant: "error"
                            closable: true
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8

                    Item { Layout.fillWidth: true }

                    AppButton {
                        text: qsTr("Start multisig setup")
                        iconSource: "/resources/icons/arrow-right.svg"
                        enabled: peerInputModel.count > 0
                        implicitHeight: 36
                        onClicked: {

                            let validationErrors = [];

                            if (!validateUserOnion()) {
                                validationErrors.push(userOnionError);
                            }


                            if (!validateReference()) {
                                validationErrors.push(referenceError);
                            }


                            if (!validateWalletName()) {
                                validationErrors.push(walletNameError);
                            }


                            if (walletPasswordField.text.trim() === "") {
                                validationErrors.push(qsTr("Wallet password is required"));
                            }


                            const peers = [];
                            const peerOnions = new Set();

                            for (let i = 0; i < peerInputModel.count; i++) {
                                const row = peerInputModel.get(i);
                                const o = (row.onion || "").trim().toLowerCase();

                                if (o === "") {
                                    validationErrors.push(qsTr("All peer fields must be filled"));
                                    break;
                                }

                                if (!isValidOnion(o)) {
                                    validationErrors.push(qsTr("Invalid onion address: %1").arg(o));
                                    break;
                                }

                                if (peerOnions.has(o)) {
                                    validationErrors.push(qsTr("Duplicate peer address: %1").arg(o));
                                    break;
                                }

                                if (o === selectedUserOnion.toLowerCase()) {
                                    validationErrors.push(qsTr("Peer address cannot be the same as your identity"));
                                    break;
                                }

                                peerOnions.add(o);
                                peers.push(o);
                            }


                            const thresholdError = validateThreshold();
                            if (thresholdError) {
                                validationErrors.push(thresholdError);
                            }


                            if (validationErrors.length > 0) {
                                errorAlert.text = validationErrors[0];
                                errorAlert.visible = true;
                                return;
                            }


                            const notifyPeers = [];
                            for (let i = 0; i < peers.length; i++) {
                                const row = peerInputModel.get(i);
                                if (row.notify === true) notifyPeers.push(peers[i]);
                            }

                            sessionRef = multisigManager.startMultisig(
                                refField.text.trim(),
                                parseInt(thresholdField.text) || 2,
                                peers.length + 1,
                                peers,
                                walletNameField.text.trim(),
                                walletPasswordField.text,
                                selectedUserOnion,
                                "user"
                            );

                            notifierStarted = false;
                            notifierMessage = "";

                            notifySelection = ({});
                            for (let i = 0; i < peers.length; ++i) {
                                const o = peers[i];
                                notifySelection[o] = (notifyPeers.indexOf(o) !== -1);
                            }

                            if (notifyPeers.length > 0) {
                                const res = multisigManager.startMultisigNotifier(sessionRef, notifyPeers,selectedUserOnion);
                                notifierStarted = (res && res.length > 0);
                                notifierMessage = notifierStarted
                                    ? qsTr("Notifier started in background.")
                                    : qsTr("Could not start notifier.");
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: sessionActive

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
                            spacing: 8

                            Text {
                                text: qsTr("Reference:")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Text {
                                text: sessionRef
                                color: themeManager.textColor
                                font.weight: Font.Medium
                                font.family: "Monospace"
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                            }

                            AppCopyButton {
                                textToCopy: sessionRef
                                size: 14
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: qsTr("Wallet:")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Text {
                                text: walletNameFromSession
                                color: themeManager.textColor
                                font.weight: Font.Medium
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: qsTr("Configuration:")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                            }

                            Text {
                                text: qsTr("%1 of %2 signatures required").arg(mFromSession).arg(nFromSession)
                                color: themeManager.textColor
                                font.weight: Font.Medium
                                Layout.fillWidth: true
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
                                id: stageLabel
                                text: sessionObj ? sessionObj.stage : "INIT"
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
                        }

                        Text {
                            text: qsTr("Signing Peers (%1)").arg(peerModel.count)
                            color: themeManager.textColor
                            font.pixelSize: 14
                            font.weight: Font.Medium
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Repeater {
                                model: peerModel
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
                                            source: "/resources/icons/shield-network.svg"
                                            width: 12
                                            height: 12
                                            color: model.online ? themeManager.successColor : themeManager.textSecondaryColor
                                        }

                                        Text {
                                            text: model.onion
                                            font.family: "Monospace"
                                            font.pixelSize: 10
                                            color: themeManager.textColor
                                            elide: Text.ElideMiddle
                                            Layout.fillWidth: true
                                        }

                                        AppStatusIndicator {
                                            status: model.online ? "online" : "offline"
                                            dotSize: 5
                                        }

                                        Text {
                                            text: model.pstage
                                            color: themeManager.textSecondaryColor
                                            font.pixelSize: 10
                                            Layout.preferredWidth: 50
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: peerModel.count === 0
                                text: qsTr("No peers connected yet")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignCenter
                                Layout.topMargin: 12
                                Layout.bottomMargin: 12
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8
                visible: sessionActive && !sessionCompleted && sessionObj !== null && sessionObj.stage !== 'KEX' && sessionObj.stage !== 'PENDING' && sessionObj.stage !== 'ACK' && sessionObj.stage !== 'COMPLETE'

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Stop Session")
                    iconSource: "/resources/icons/stop-circle.svg"
                    variant: "error"
                    implicitHeight: 36
                    onClicked: {
                        if (sessionRef) {
                            multisigManager.stopMultisig(selectedUserOnion , sessionRef);
                            multisigManager.stopNotifier(selectedUserOnion, sessionRef);
                        }

                        sessionRef = ""
                        leftPanel.buttonClicked("MultisigLandingPage")
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 12
            }
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

    Timer {
        id: redirectTimer
        interval: 3000 // 3 seconds
        onTriggered: {
            const pageComponent = "WalletDetails.qml"
            middlePanel.currentPageUrl = pageComponent
            middlePanel.stackView.replace(pageComponent, { walletName: walletNameFromSession })
        }
    }

    Component.onCompleted: {

        if (peerInputModel.count === 0) {
            peerInputModel.append({ onion: "", notify: true })
        }
        if (sessionActive) {
            refreshPeerModel()
        }
        loadAddressBook();
        loadUserOnions();
    }
}
