import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import Qt.labs.folderlistmodel 2.1
import Qt5Compat.GraphicalEffects
import "components"

Page {
    id: notifierSetupPage
    title: qsTr("Notifier Setup")

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    property string notifierRef
    readonly property bool notifierActive: notifierRef !== ""

    readonly property QtObject notifierObj: notifierActive ? multisigManager.notifierFor(selectedUserOnion, notifierRef) : null

    property bool notifierCompleted: false
    property string peerValidationError: ""
    property string referenceError: ""
    property string userOnionError: ""
    property int selectedPeerIndex: -1
    property string selectedUserOnion: ""


    ListModel { id: peerInputModel }
    ListModel { id: addressBookModel }
    ListModel { id: userOnionModel }


    function normalizeOnion(o) {
        var s = (o || "").trim().toLowerCase();
        if (s !== "" && !s.endsWith(".onion")) s += ".onion";
        return s;
    }

    function peersArrayCurrent() {
        const peers = [];
        for (let i = 0; i < peerInputModel.count; i++) {
            const row = peerInputModel.get(i);
            const o = normalizeOnion(row.onion || "");
            if (o) peers.push(o);
        }
        return peers;
    }

    function userOwnedOnionSet() {
        const s = new Set();
        for (let i = 0; i < userOnionModel.count; ++i) {
            const it = userOnionModel.get(i);
            const o = normalizeOnion(it.onion || "");
            if (o) s.add(o);
        }
        return s;
    }

    function referenceInUseForOnion(onion, ref) {
        if (!onion || !ref || !multisigManager) return false;

        try {

            const n = multisigManager.notifierFor(onion, ref);
            if (n) return true;

        } catch (e) { /* treat as not found */ }


        try {

            const wallet_name = walletManager.walletNameForRef( ref, onion);
            if (wallet_name) return true;

        } catch (e2) { /* ignore and assume not found */ }

        return false;
    }

    function chosenMyOnionFromPeers() {
        const owned = userOwnedOnionSet();
        const peers = peersArrayCurrent();
        const ownedInPeers = peers.filter(o => owned.has(o));
        if (ownedInPeers.length === 1) return ownedInPeers[0];
        return "";
    }

    function validateReference() {
        const r = refField.text.trim();
        if (r.length === 0) {
            referenceError = qsTr("Reference code cannot be empty");
            return false;
        }

        const myOnion = chosenMyOnionFromPeers();
        if (myOnion !== "") {
            if (walletManager.refExistsForOnion(r, myOnion)) {
                referenceError = qsTr("Reference is already used on %1").arg(myOnion);
                return false;
            }
            try {
                const n = multisigManager.notifierFor(myOnion, r);
                if (n) {
                    referenceError = qsTr("Reference is already used by a notifier on %1").arg(myOnion);
                    return false;
                }
            } catch (e) { /* ignore */ }
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
        const maxThreshold = peerInputModel.count;
        if (threshold > maxThreshold) {
            return qsTr("Threshold cannot be larger than %1 (number of peers)").arg(maxThreshold);
        }
        return "";
    }

    function isValidOnion(a) {
        return accountManager.isOnionAddress(normalizeOnion(a));
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

    function getUserOnionLabel(onion) {
        for (let i = 0; i < userOnionModel.count; ++i) {
            const item = userOnionModel.get(i);
            if (item.onion === onion) {
                return item.label;
            }
        }
        return onion;
    }

    Connections {
        target: accountManager
        function onAddressBookChanged() { loadAddressBook(); }
        function onTorIdentitiesChanged() { loadUserOnions(); }
    }

    Connections {
        target: notifierObj
        enabled: notifierActive && notifierObj
        ignoreUnknownSignals: true
        function onStageChanged(s) {
            stageLabel.text = s;
            if (s === 'COMPLETE') {
                notifierCompleted = true;
            }
        }
        function onFinished(result) {
            notifierCompleted = true;
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
                visible: !notifierActive

                Text {
                    text: "Setup New Notifier Session"
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
                            text: "Configure notifier parameters to invite peers to create a multisig wallet"
                            color: themeManager.textSecondaryColor
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignHCenter
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        AppInput {
                            id: refField
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            placeholderText: "Enter unique reference code for the proposed wallet"
                            iconSource: "/resources/icons/hashtag.svg"
                            onTextChanged: validateReference()
                            errorText: referenceError
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: qsTr("Your Identity (not used in the wallet)")
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
                                    border.color: own_id.hovered ? themeManager.textSecondaryColor : themeManager.borderColor
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
                                            text: selectedUserOnion ? getUserOnionLabel(selectedUserOnion) + " (" + selectedUserOnion + ")" : qsTr("Select one onion identity to notify other users")
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
                            text: "Peers to Notify"
                            color: themeManager.textColor
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: "These peers will receive notifications about the proposed multisig wallet. You can include a maximum one of your own onion addresses."
                            color: themeManager.textSecondaryColor
                            font.pixelSize: 11
                            Layout.alignment: Qt.AlignHCenter
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Repeater {
                            model: peerInputModel

                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                AppInput {
                                    id: peerField
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Peer onion address to notify")
                                    text: onion
                                    iconSource: "/resources/icons/shield-network.svg"
                                    onTextChanged: {
                                        peerInputModel.setProperty(index, "onion", text.trim())
                                        validateReference();
                                    }
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
                                    iconColor: peerInputModel.count > 2 ? themeManager.errorColor : themeManager.textSecondaryColor
                                    size: 18
                                    enabled: peerInputModel.count > 2
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
                            onClicked: peerInputModel.append({ onion: "" })
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
                        text: qsTr("Start notifier")
                        iconSource: "/resources/icons/bell.svg"
                        enabled: peerInputModel.count > 0
                        implicitHeight: 36
                        onClicked: {
                            let validationErrors = [];

                            const refVal = refField.text.trim();

                            if (!validateReference()) validationErrors.push(referenceError);
                            if (!validateUserOnion()) validationErrors.push(userOnionError);

                            const peers = [];
                            const peerOnions = new Set();
                            const myOnionLc = (selectedUserOnion || "").toLowerCase();

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

                                if (o === myOnionLc) {
                                    validationErrors.push(qsTr("Peer address cannot be the same as your identity"));
                                    break;
                                }

                                peerOnions.add(o);
                                peers.push(o);
                            }

                            const owned = userOwnedOnionSet();
                            let ownedCountInPeers = 0;
                            let ownedInPeersOnion = "";
                            for (let i = 0; i < peers.length; ++i) {
                                if (owned.has(peers[i])) {
                                    ownedCountInPeers++;
                                    if (ownedCountInPeers === 1) ownedInPeersOnion = peers[i];
                                    if (ownedCountInPeers > 1) break;
                                }
                            }

                            if (ownedCountInPeers > 1) {
                                validationErrors.push(qsTr("At most one of your own identities can appear in the peers list."));
                            } else if (ownedCountInPeers === 1) {

                                const refVal = refField.text.trim();
                                if (walletManager.refExistsForOnion(refVal, ownedInPeersOnion)) {
                                    validationErrors.push(qsTr("Reference ‘%1’ is already used on %2.")
                                                          .arg(refVal).arg(getUserOnionLabel(ownedInPeersOnion)));
                                } else {
                                    try {
                                        const n = multisigManager.notifierFor(ownedInPeersOnion, refVal);
                                        if (n) {
                                            validationErrors.push(qsTr("Reference ‘%1’ is already used by a notifier on %2.")
                                                                  .arg(refVal).arg(getUserOnionLabel(ownedInPeersOnion)));
                                        }
                                    } catch (e) { /* ignore */ }
                                }
                            }


                            const thresholdError = validateThreshold();
                            if (thresholdError) validationErrors.push(thresholdError);

                            if (validationErrors.length > 0) {
                                errorAlert.text = validationErrors[0];
                                errorAlert.visible = true;

                                return;
                            }

                            const allPeers = peers;
                            notifierRef = multisigManager.startStandaloneNotifier(
                                        refVal,
                                        parseInt(thresholdField.text) || 2,
                                        peers.length,
                                        allPeers,
                                        peers,
                                        selectedUserOnion
                                        );

                            if (!notifierRef) {
                                const msg = qsTr("Failed to start notifier session");
                                errorAlert.text = msg;
                                errorAlert.visible = true;
                                bottomErrorBar.show(msg);
                                return;
                            }

                            var pageComponent = Qt.resolvedUrl("NotifierStatus.qml");
                            middlePanel.currentPageUrl = pageComponent;
                            middlePanel.stackView.replace(pageComponent, {
                                                              selectedUserOnion: selectedUserOnion,
                                                              notifierRef: refVal
                                                          });
                        }
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
        descriptionText: qsTr("Choose which onion identity to use in the notifier (not part of wallet peers)")
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
        }
    }

    Component.onCompleted: {
        if (peerInputModel.count === 0) {
            peerInputModel.append({ onion: "" })
            peerInputModel.append({ onion: "" })
        }
        loadAddressBook();
        loadUserOnions();
    }
}
