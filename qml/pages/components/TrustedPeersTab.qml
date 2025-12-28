import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Item {
    id: root

    ListModel { id: peersModel }
    ListModel { id: addressBookModel }
    property int maxN: 10
    property int editingIndex: -1
    property bool useAllIdentities: true
    property bool formHasErrors: !isValidOnion(onionField.text.trim()) ||
                                 labelField.text.trim() === "" ||
                                 parseInt(maxNField.text) < 2 ||
                                 parseInt(minThresholdField.text) < 2 ||
                                 parseInt(minThresholdField.text) > parseInt(maxNField.text) ||
                                 isDuplicateOnion(onionField.text) ||
                                 parseInt(maxNField.text) > maxN ||
                                 (!useAllIdentities && selectedAllowedOnions().length === 0)

    property var myIdentities: []
    property var allowedSelection: ({})

    function loadMyIdentities() {
        myIdentities = [];
        if (!accountManager || !accountManager.is_authenticated) return;
        const xs = accountManager.getTorIdentities();
        for (let i = 0; i < xs.length; i++) {
            myIdentities.push({
                                  onion: String(xs[i].onion || "").toLowerCase(),
                                  label: String(xs[i].label || ""),
                                  online: Boolean(xs[i].online)
                              });
        }
        if (useAllIdentities) {
            selectAllIdentities();
        }
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

    function selectAllIdentities() {
        const map = {};
        myIdentities.forEach(id => { map[id.onion] = true; });
        allowedSelection = map;
    }
    function setAllowedSelection(list) {
        const map = {};

        // Create a set of valid onion addresses from myIdentities
        const validOnions = new Set();
        myIdentities.forEach(id => validOnions.add(id.onion.toLowerCase()));

        // Handle both JS arrays and QQmlListModel
        if (list && typeof list.count !== 'undefined') {
            // It's a QQmlListModel
            for (let i = 0; i < list.count; i++) {
                const o = String(list.get(i)).toLowerCase();
                // Only add if it's a valid identity we own
                if (validOnions.has(o)) {
                    map[o] = true;
                }
            }
        } else if (Array.isArray(list)) {
            // It's a JS array
            list.forEach(o => {
                const onion = String(o).toLowerCase();
                // Only add if it's a valid identity we own
                if (validOnions.has(onion)) {
                    map[onion] = true;
                }
            });
        }

        allowedSelection = map;

        const selectedCount = Object.keys(allowedSelection).length;
        const totalCount = myIdentities.length;
        useAllIdentities = (selectedCount === totalCount && totalCount > 0);
    }

    function selectedAllowedOnions() {
        if (useAllIdentities) {
            return myIdentities.map(id => id.onion);
        }
        const out = [];
        for (let i = 0; i < myIdentities.length; i++) {
            const on = myIdentities[i].onion;
            if (allowedSelection[on]) out.push(on);
        }
        return out;
    }

    function normalizeOnion(o) {
        var s = (o || "").trim().toLowerCase();
        if (s !== "" && !s.endsWith(".onion")) s += ".onion";
        return s;
    }

    function isValidOnion(a) {
        return accountManager.isOnionAddress(normalizeOnion(a));
    }

    function isDuplicateOnion(addr) {
        if (editingIndex >= 0) return false;

        const normalizedAddr = addr.trim().toLowerCase();
        for (let i = 0; i < peersModel.count; i++) {
            if (peersModel.get(i).onion === normalizedAddr) {
                return true;
            }
        }
        return false;
    }

    function loadPeers() {

        peersModel.clear();
        if (!accountManager || !accountManager.is_authenticated) {

            return;
        }

        try {
            const jsonStr = accountManager.getTrustedPeers();
            const data = JSON.parse(jsonStr);

            for (const onion in data) {
                const peer = data[onion];
                peersModel.append({
                                      onion: String(onion),
                                      label: String(peer.label || ""),
                                      maxN: parseInt(peer.max_n) || 1,
                                      minThreshold: parseInt(peer.min_threshold) || 1,
                                      active: Boolean(peer.active !== undefined ? peer.active : true),
                                      allowedIdentities: Array.isArray(peer.allowed_identities) ? peer.allowed_identities.map(x => String(x).toLowerCase()) : [],
                                      max_number_wallets: parseInt(peer.max_number_wallets),
                                      current_number_wallets: parseInt(peer.current_number_wallets)
                                  });
            }

        } catch (e) {
            console.log("TrustedPeersTab: Error parsing trusted peers:", e);
        }
    }

    function addOrUpdatePeer() {
        const onion = onionField.text.trim().toLowerCase();
        const label = labelField.text.trim();
        const maxN = parseInt(maxNField.text) || 1;
        const minThreshold = parseInt(minThresholdField.text) || 1;
        const active = activeCheck.checked;
        const allowed = selectedAllowedOnions();
        const max_number_wallets =  parseInt(maxNWallets.text) || 10;


        if (formHasErrors) return;

        let success = false;
        try {
            if (editingIndex >= 0) {
                success = accountManager.updateTrustedPeer(onion, label, maxN, minThreshold, allowed, max_number_wallets );
                if (success) {
                    accountManager.setTrustedPeerActive(onion, active);
                    const item = peersModel.get(editingIndex);
                    item.label = label;
                    item.maxN = maxN;
                    item.minThreshold = minThreshold;
                    item.active = active;
                    item.allowedIdentities = allowed;
                    item.max_number_wallets =  max_number_wallets ;

                }
            } else {
                success = accountManager.addTrustedPeer(onion, label, maxN, minThreshold, active, allowed, max_number_wallets );
                if (success) {
                    peersModel.append({
                                          onion: onion,
                                          label: label,
                                          maxN: maxN,
                                          minThreshold: minThreshold,
                                          active: active,
                                          allowedIdentities: allowed,
                                          max_number_wallets : max_number_wallets,
                                          current_number_wallets : 0
                                      });
                }
            }

            if (success) clearForm();
        } catch (e) {
            console.log("TrustedPeersTab: exception during add/update:", e);
        }
    }

    function editPeer(index) {
        const item = peersModel.get(index);
        onionField.text = item.onion;
        labelField.text = item.label;
        maxNField.text = item.maxN.toString();
        minThresholdField.text = item.minThreshold.toString();
        activeCheck.checked = item.active;
        maxNWallets.text = item.max_number_wallets.toString();
        editingIndex = index;

        // Fetch allowed identities from backend (same as View Identities button)
        try {
            const jsonStr = accountManager.getTrustedPeers();
            const peersData = JSON.parse(jsonStr);
            const peerData = peersData[item.onion];

            if (peerData && peerData.allowed_identities) {
                const allowedOnions = peerData.allowed_identities;
                setAllowedSelection(allowedOnions);
            } else {
                setAllowedSelection([]);
            }
        } catch (e) {
            console.log("Error loading peer data for edit:", e);
            setAllowedSelection([]);
        }
    }

    function removePeer(index) {
        const item = peersModel.get(index);
        if (accountManager.removeTrustedPeer(item.onion)) {
            peersModel.remove(index);
        }
    }

    function togglePeerActive(index) {
        const item = peersModel.get(index);
        const newActive = !item.active;
        if (accountManager.setTrustedPeerActive(item.onion, newActive)) {
            item.active = newActive;
        }
    }

    function clearForm() {
        onionField.text = "";
        labelField.text = "";
        maxNField.text = "2";
        minThresholdField.text = "2";
        activeCheck.checked = true;
        editingIndex = -1;
        useAllIdentities = true;
        selectAllIdentities();
        maxNWallets.text = "10";
    }

    Dialog {
        id: identitySelectionDialog
        modal: true
        title: qsTr("Select Allowed Identities")
        anchors.centerIn: parent
        width: Math.min(500, parent.width * 0.9)
        height: 400

        background: Rectangle {
            color: themeManager.surfaceColor
            border.color: themeManager.borderColor
            border.width: 1
            radius: 2
        }

        onOpened: {
            identityListView.model = null;
            identityListView.model = myIdentities;
        }

        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: qsTr("Choose which of your onion identities this peer can use to create new multisig wallets automatically:")
                color: themeManager.textSecondaryColor
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    id: identityListView
                    model: myIdentities
                    spacing: 6

                    delegate: Rectangle {
                        width: ListView.view ? ListView.view.width : 400
                        height: 44
                        color: themeManager.surfaceColor
                        border.color: !!allowedSelection[modelData.onion] ? themeManager.primaryColor : themeManager.borderColor
                        border.width: !!allowedSelection[modelData.onion] ? 2 : 1
                        radius: 2

                        Component.onCompleted: {
                            // Delegate created
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            AppIcon {
                                source: "/resources/icons/shield-network.svg"
                                width: 16
                                height: 16
                                color: modelData.online ? themeManager.successColor : themeManager.textSecondaryColor
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: modelData.label || qsTr("Unlabeled Identity")
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    color: themeManager.textColor
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: modelData.onion
                                    font.family: "Monospace"
                                    font.pixelSize: 10
                                    color: themeManager.textSecondaryColor
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }

                            AppStatusIndicator {
                                status: modelData.online ? "online" : "offline"
                                dotSize: 5
                            }

                            AppCheckBox {
                                checked: !!allowedSelection[modelData.onion]

                                onToggled: {
                                    const m = Object.assign({}, allowedSelection);
                                    if (checked) {
                                        m[modelData.onion] = true;
                                    } else {
                                        delete m[modelData.onion];
                                    }
                                    allowedSelection = m;
                                }
                            }
                        }
                    }
                }
            }

            Text {
                visible: myIdentities.length === 0 && identityListView.count === 0
                text: qsTr("No onion identities available. Please create one first.")
                color: themeManager.textSecondaryColor
                font.pixelSize: 12
                Layout.alignment: Qt.AlignCenter
                Layout.topMargin: 12
                Layout.bottomMargin: 12
            }

            RowLayout {
                Layout.fillWidth: true

                AppButton {
                    text: qsTr("Select All")
                    variant: "secondary"
                    onClicked: selectAllIdentities()
                }

                AppButton {
                    text: qsTr("Select None")
                    variant: "secondary"
                    onClicked: {
                        allowedSelection = {};
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: qsTr("Selected: %1/%2").arg(Object.keys(allowedSelection).length).arg(myIdentities.length)
                    font.pixelSize: 11
                    color: themeManager.textSecondaryColor
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                AppButton {
                    text: qsTr("Cancel")
                    variant: "secondary"
                    onClicked: identitySelectionDialog.close()
                }

                AppButton {
                    text: qsTr("OK")
                    enabled: Object.keys(allowedSelection).length > 0
                    onClicked: identitySelectionDialog.close()
                }
            }
        }
    }

    Dialog {
        id: viewIdentitiesDialog
        property var peerIdentities: []
        property string peerLabel: ""

        title: qsTr("Allowed Identities - %1").arg(peerLabel)
        modal: true
        anchors.centerIn: parent
        width: Math.min(420, parent ? parent.width * 0.9 : 420)
        height: 320

        background: Rectangle {
            color: themeManager.surfaceColor
            border.color: themeManager.borderColor
            border.width: 1
            radius: 2
        }

        contentItem: ColumnLayout {
            spacing: 8

            Text {
                text: qsTr("Allowed Identities")
                font.pixelSize: 14
                font.weight: Font.Medium
                color: themeManager.textColor
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: qsTr("This peer can create new multisig wallets using these %1 identities:").arg(viewIdentitiesDialog.peerIdentities.length)
                color: themeManager.textSecondaryColor
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ListView {
                    model: viewIdentitiesDialog.peerIdentities
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
                                width: 16
                                height: 16
                                color: themeManager.textSecondaryColor
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: modelData.label || qsTr("Unlabeled Identity")
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    color: themeManager.textColor
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: modelData.onion
                                    font.family: "Monospace"
                                    font.pixelSize: 10
                                    color: themeManager.textSecondaryColor
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }

            Text {
                visible: viewIdentitiesDialog.peerIdentities.length === 0
                text: qsTr("No identities configured for this peer")
                color: themeManager.textSecondaryColor
                font.pixelSize: 12
                Layout.alignment: Qt.AlignCenter
                Layout.topMargin: 12
                Layout.bottomMargin: 12
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Close")
                    variant: "secondary"
                    onClicked: viewIdentitiesDialog.close()
                }
            }
        }
    }


    AppAddressBookDialog {
        id: addressBookDialog
        titleText: qsTr("Select Peer Address")
        descriptionText: qsTr("Choose a peer address from your address book")
        model: addressBookModel
        addressBookType: "peer"
        primaryField: "label"
        secondaryField: "onion"
        emptyStateText: qsTr("No peer addresses in your address book yet.")

        onItemSelected: function(item, index) {
            onionField.text = item.onion;
            labelField.text = item.label;
        }

        onQuickAddRequested: function() {
            const tabMap = { "peer": 0, "trusted": 1, "xmr": 2, "daemon": 3 };
            const tabIndex = tabMap[addressBookDialog.addressBookType] || 0;

            var pageComponent = Qt.resolvedUrl("UnifiedAddressBook.qml");
            middlePanel.currentPageUrl = pageComponent;
            middlePanel.stackView.replace(pageComponent, { currentTab: tabIndex });
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                text: editingIndex >= 0 ? qsTr("Edit Trusted Peer") : qsTr("Add New Trusted Peer")
                font.pixelSize: 14
                font.weight: Font.Medium
                color: editingIndex >= 0 ? themeManager.warningColor : themeManager.textColor
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: editingIndex >= 0 ? themeManager.warningColor : themeManager.borderColor
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 8

                // First row: Onion + Label
                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true

                    AppInput {
                        id: onionField
                        placeholderText: qsTr("56‑char v3 onion + .onion")
                        Layout.fillWidth: true
                        enabled: editingIndex < 0
                        font.family: "Monospace"
                        font.pixelSize: 10
                        errorText: {
                            const addr = text.trim();
                            if (addr !== "" && !isValidOnion(addr)) {
                                return qsTr("Invalid onion address format");
                            }
                            if (addr !== "" && isDuplicateOnion(addr)) {
                                return qsTr("Address already exists");
                            }
                            return "";
                        }
                    }

                    AppIconButton {
                        iconSource: "/resources/icons/book-bookmark.svg"
                        size: 18
                        enabled: editingIndex < 0
                        onClicked: {
                            loadAddressBook();
                            addressBookDialog.open();
                        }
                    }

                    AppInput {
                        id: labelField
                        placeholderText: qsTr("Peer label")
                        Layout.preferredWidth: 160
                        errorText: text.trim() === "" && text.length > 0 ? qsTr("Label cannot be empty") : ""
                    }
                }

                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true

                    Text {
                        text: qsTr("Max participants:")
                        font.pixelSize: 12
                        color: themeManager.textColor
                        Layout.alignment: Qt.AlignVCenter
                    }

                    AppInput {
                        id: maxNField
                        text: "3"
                        placeholderText: qsTr("2-10")
                        Layout.preferredWidth: 36
                        errorText: {
                            const val = parseInt(text) || 0;
                            const minVal = parseInt(minThresholdField.text) || 0;
                            if (val < 2) return qsTr("Must be at least 2");
                            if (minVal > 0 && val < minVal) return qsTr("Must be >= min threshold");
                            if (val > maxN) return qsTr("Maximum is 10");
                            return "";
                        }
                    }

                    Text {
                        text: qsTr("Min threshold:")
                        font.pixelSize: 12
                        color: themeManager.textColor
                        Layout.alignment: Qt.AlignVCenter
                    }

                    AppInput {
                        id: minThresholdField
                        text: "2"
                        placeholderText: qsTr("2-max")
                        Layout.preferredWidth: 36
                        validator: IntValidator { bottom: 2; top: 10 }
                        errorText: {
                            const val = parseInt(text) || 0;
                            const maxVal = parseInt(maxNField.text) || 0;
                            if (val < 2) return qsTr("Must be at least 2");
                            if (maxVal >= 2 && val > maxVal) return qsTr("Cannot exceed max participants");
                            return "";
                        }
                    }

                    Text {
                        text: qsTr("Max number of wallets it can create:")
                        font.pixelSize: 12
                        color: themeManager.textColor
                        Layout.alignment: Qt.AlignVCenter
                    }

                    AppInput {
                        id: maxNWallets
                        text: "10"
                        placeholderText: qsTr("2-max")
                        Layout.preferredWidth: 36
                        validator: IntValidator { bottom: 1; top: 1000 }
                    }


                    AppSwitch {
                        id: activeCheck
                        label: qsTr("Active")
                        checked: true
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Item { Layout.fillWidth: true }

                    AppButton {
                        text: editingIndex >= 0 ? qsTr("Update") : qsTr("Add")
                        enabled: !formHasErrors
                        Layout.alignment: Qt.AlignBottom
                        onClicked: addOrUpdatePeer()
                        variant: editingIndex >= 0 ?  "warning" : "primary"

                    }

                    AppButton {
                        text: qsTr("Cancel")
                        variant: "secondary"
                        visible: editingIndex >= 0
                        Layout.alignment: Qt.AlignBottom
                        onClicked: clearForm()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("Allowed Onion Identities")
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        color: themeManager.textColor
                    }

                    RowLayout {
                        spacing: 12
                        Layout.fillWidth: true

                        AppSwitch {
                            id: allIdentitiesToggle
                            label: qsTr("All onion identities")
                            checked: useAllIdentities
                            onToggled: {
                                useAllIdentities = checked;
                                if (checked) {
                                    selectAllIdentities();
                                }
                            }
                        }

                        AppButton {
                            text: qsTr("Select Specific (%1)").arg(Object.keys(allowedSelection).length)
                            variant: "secondary"
                            enabled: !useAllIdentities && myIdentities.length > 0
                            visible: true

                            onClicked: identitySelectionDialog.open()
                        }





                        Item { Layout.fillWidth: true }

                        Text {
                            text: useAllIdentities
                                  ? qsTr("Using all %1 identities").arg(myIdentities.length)
                                  : qsTr("Using %1 of %2 identities").arg(Object.keys(allowedSelection).length).arg(myIdentities.length)
                            font.pixelSize: 11
                            color: themeManager.textSecondaryColor
                        }
                    }
                }

                AppAlert {
                    text: {
                        if (onionField.errorText !== "") return onionField.errorText;
                        if (labelField.errorText !== "") return labelField.errorText;
                        if (maxNField.errorText !== "") return maxNField.errorText;
                        if (minThresholdField.errorText !== "") return minThresholdField.errorText;
                        if (!useAllIdentities && selectedAllowedOnions().length === 0) return qsTr("Must select at least one identity");
                        return "";
                    }
                    variant: "error"
                    visible: text !== ""
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                visible:  editingIndex >= 0
                color: editingIndex >= 0 ? themeManager.warningColor : themeManager.borderColor
            }

        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            spacing: 4

            Text {
                text: qsTr("Current Trusted Peers (%1)").arg(peersModel.count)
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
                Layout.fillHeight: true
                Layout.topMargin: 8
                clip: true

                ListView {
                    id: listView
                    model: peersModel
                    spacing: 6

                    delegate: Rectangle {
                        width: listView.width
                        height: delegateLayout.height + 12
                        color: model.active ? themeManager.backgroundColor : themeManager.backgroundColor
                        border.color: model.active ? themeManager.borderColor : themeManager.textSecondaryColor
                        border.width: 1
                        radius: 2

                        ColumnLayout {
                            id: delegateLayout
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 8
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: model.label
                                    font.bold: true
                                    font.pixelSize: 12
                                    color: themeManager.textColor
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                AppStatusIndicator {
                                    status: model.active ? "online" : "offline"
                                    text: model.active ? qsTr("Active") : qsTr("Inactive")
                                    dotSize: 6
                                }
                            }

                            TextInput {
                                id: addressText
                                text: model.onion
                                font.family: "Monospace"
                                font.pixelSize: 10
                                color: themeManager.textSecondaryColor
                                Layout.fillWidth: true
                                readOnly: true
                                selectByMouse: true
                            }

                            RowLayout {
                                Text {
                                    text: qsTr("Max n: %1 | Min threshold: %2 | Identities: %3  | Wallets created %4/%5").arg(model.maxN).arg(model.minThreshold).arg(model.allowedIdentities.count).arg(model.current_number_wallets).arg(model.max_number_wallets)
                                    font.pixelSize: 10
                                    color: themeManager.textSecondaryColor
                                }

                                Item { Layout.fillWidth: true }
                            }

                            RowLayout {
                                Item { Layout.fillWidth: true }

                                AppButton {
                                    text: qsTr("Reset Wallets Created #")
                                    variant: "secondary"
                                    onClicked: {
                                        if (accountManager.resetTrustedPeerWalletCount(model.onion)) {

                                            model.current_number_wallets = 0

                                        }
                                    }
                                }


                                AppButton {
                                    text: qsTr("View Identities")
                                    variant: "secondary"
                                    onClicked: {
                                        var identities = [];

                                        try {
                                            const jsonStr = accountManager.getTrustedPeers();
                                            const peersData = JSON.parse(jsonStr);
                                            const peerData = peersData[model.onion];

                                            if (peerData && peerData.allowed_identities) {
                                                const allowedOnions = peerData.allowed_identities;


                                                for (var i = 0; i < allowedOnions.length; i++) {
                                                    var allowedOnion = allowedOnions[i];


                                                    for (var j = 0; j < myIdentities.length; j++) {
                                                        if (myIdentities[j].onion === allowedOnion) {
                                                            identities.push({
                                                                                onion: myIdentities[j].onion,
                                                                                label: myIdentities[j].label,
                                                                                online: myIdentities[j].online
                                                                            });
                                                            break;
                                                        }
                                                    }
                                                }
                                            }
                                        } catch (e) {
                                            console.log("Error parsing peer data:", e);
                                        }

                                        viewIdentitiesDialog.peerIdentities = identities;
                                        viewIdentitiesDialog.peerLabel = model.label;
                                        viewIdentitiesDialog.open();
                                    }


                                    Item {
                                        width: 12
                                        height: 12
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter

                                        Canvas {
                                            anchors.fill: parent
                                            onPaint: {
                                                var ctx = getContext("2d");
                                                ctx.clearRect(0, 0, width, height);
                                                ctx.strokeStyle = themeManager.textSecondaryColor;
                                                ctx.lineWidth = 1;


                                                ctx.beginPath();
                                                ctx.ellipse(6, 6, 5, 3, 0, 0, 2 * Math.PI);
                                                ctx.stroke();


                                                ctx.beginPath();
                                                ctx.arc(6, 6, 1, 0, 2 * Math.PI);
                                                ctx.fill();
                                            }
                                        }
                                    }
                                }

                                AppButton {
                                    text: qsTr("Copy")
                                    variant: "secondary"
                                    onClicked: {
                                        addressText.selectAll()
                                        addressText.copy()
                                        addressText.deselect()
                                    }
                                }

                                AppButton {
                                    text: model.active ? qsTr("Deactivate") : qsTr("Activate")
                                    variant: "secondary"
                                    onClicked: togglePeerActive(index)
                                }

                                AppButton {
                                    text: qsTr("Edit")
                                    variant: "secondary"
                                    onClicked: editPeer(index)
                                }

                                AppButton {
                                    text: qsTr("Delete")
                                    variant: "error"
                                    onClicked: removePeer(index)
                                }
                            }
                        }
                    }
                }
            }

            Text {
                text: qsTr("No trusted peers configured")
                font.pixelSize: 12
                color: themeManager.textSecondaryColor
                Layout.alignment: Qt.AlignCenter
                Layout.topMargin: 20
                visible: peersModel.count === 0
            }
        }
    }

    Component.onCompleted: {
        loadMyIdentities();
        loadPeers();
        loadAddressBook();
    }
}
