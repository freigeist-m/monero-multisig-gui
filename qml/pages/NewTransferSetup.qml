import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: qsTr("New Transfer – Setup")

    property string walletName: ""
    property var    walletMeta: ({})
    property var    walletObj: null


    ListModel { id: destModel }
    ListModel { id: peerModel }
    ListModel { id: xmrAddressBookModel }

    property string addrError: ""
    property string peerError: ""
    property string balanceError: ""
    property string feePickError: ""
    property int    feePriority: 0
    property bool   inspectBeforeSending: true
    property bool   deductFeeFromDests: false
    property var    feeDeductIdxs: []
    property int    selectedDestIndex: -1

    property bool   userHasEditedPeers: false

    readonly property real unlockedXmr:
        walletObj ? (Number(walletObj.unlockedBalance || 0) / 1e12)
                  : Number(walletMeta.unlocked || 0)

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    function normalizeOnion(s) {
        if (!s || typeof s !== "string") return ""
        let t = s.trim().toLowerCase()
        if (!t.endsWith(".onion")) t += ".onion"
        return t
    }

    function loadXMRAddressBook() {
        xmrAddressBookModel.clear();
        if (!accountManager || !accountManager.is_authenticated) return;
        const raw = accountManager.getXMRAddressBook();
        for (let i = 0; i < raw.length; ++i) {
            const it = raw[i];
            xmrAddressBookModel.append({
                label: String(it.label),
                xmr_address: String(it.xmr_address)
            });
        }
    }

    // function _validAddress(s) {
    //     if (typeof s !== "string") return false
    //     const cleaned = s.replace(/\s+/g,"")
    //     const base58 = "[123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz]"
    //     const re = new RegExp("^(?:4" + base58 + "{94}|8" + base58 + "{94}|4" + base58 + "{105})$")
    //     return re.test(cleaned)
    // }


    // Build a map onion -> label from address book + trusted peers
    function _peerLabelsByOnion() {
        let out = {};

        if (accountManager && accountManager.is_authenticated) {
            // Peer Address Book
            const ab = accountManager.getAddressBook() || [];
            for (let i = 0; i < ab.length; ++i) {
                const it = ab[i];
                const on = normalizeOnion(String(it.onion || ""));
                const lb = String(it.label || "");
                if (on && lb) out[on] = lb;
            }

            // Trusted Peers (JSON string with onion keys)
            try {
                const tp = JSON.parse(accountManager.getTrustedPeers() || "{}");
                for (const onRaw in tp) {
                    if (!tp.hasOwnProperty(onRaw)) continue;
                    const on = normalizeOnion(String(onRaw || ""));
                    const lb = String((tp[onRaw] && tp[onRaw].label) || "");
                    if (on && lb) out[on] = lb;
                }
            } catch (e) {
                console.log("Could not parse trusted peers JSON:", e);
            }
        }
        return out;
    }

    // Update existing rows in peerModel with labels (if found)
    function refreshPeerLabels() {
        const labels = _peerLabelsByOnion();
        for (let i = 0; i < peerModel.count; ++i) {
            const p = peerModel.get(i);
            const on = normalizeOnion(p.onion || "");
            const lb = labels[on] || "";
            if ((p.label || "") !== lb) {
                peerModel.setProperty(i, "label", lb);
            }
        }
    }


    function _validAddress(s) {
        if (typeof s !== "string") return false;

        const a = s.replace(/\s+/g, "");
        const B58 = "[123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz]";


        // normalize network from AccountManager (fallback to mainnet)
        const net = (accountManager.networkType || "mainnet");
        console.log(net)
        // prefixes per network
        let stdPrefix, subPrefix, intPrefix;
        switch (net) {
        case "testnet":
            stdPrefix = "[9A]";  // standard
            subPrefix = "[B]";   // subaddress
            intPrefix = "[9A]";  // integrated
            break;
        case "stagenet":
            stdPrefix = "[5]";
            subPrefix = "[7]";
            intPrefix = "[5]";
            break;
        default: // mainnet
            stdPrefix = "[4]";
            subPrefix = "[8]";
            intPrefix = "[4]";
            break;
        }

        // 95 chars for standard/subaddress, 106 for integrated
        const re = new RegExp(
            `^(?:${stdPrefix}${B58}{94}|${subPrefix}${B58}{94}|${intPrefix}${B58}{105})$`
        );

        return re.test(a);
    }


    function _sumDest() {
        var s = 0
        for (let i=0;i<destModel.count;i++) {
            const a = parseFloat(destModel.get(i).amount || "0")
            if (!isNaN(a)) s += a
        }
        return s
    }

    function ensureFeePickValidity() {
        feePickError = ""
        if (deductFeeFromDests && (!feeDeductIdxs || feeDeductIdxs.length === 0)) {
            feePickError = qsTr("Select at least one destination for fee deduction")
        }
        return feePickError === ""
    }

    function selectAllFeeIdxs() {
        const n = destModel.count
        const arr = []
        for (let i=0;i<n;i++) arr.push(i)
        feeDeductIdxs = arr
    }

    function addFeeIdxIfNeeded(idx) {
        if (!deductFeeFromDests) return
        let arr = (feeDeductIdxs || []).slice()
        if (arr.indexOf(idx) === -1) arr.push(idx)
        feeDeductIdxs = arr
    }

    function removeFeeIdxAndShift(removedIndex) {
        let arr = (feeDeductIdxs || []).slice()
        let out = []
        for (let i=0;i<arr.length;i++) {
            const v = arr[i]
            if (v === removedIndex) continue
            out.push(v > removedIndex ? (v-1) : v)
        }
        feeDeductIdxs = out
    }

    function validateInputs(strictMode) {
        addrError = ""
        peerError = ""
        balanceError = ""

        if (userHasEditedPeers || strictMode) {
            let selected = 0
            for (let i=0;i<peerModel.count;i++)
                if (peerModel.get(i).include) selected++
            const thr = Number(walletMeta.threshold || 0)
            if (selected < thr) {
                peerError = qsTr("Select at least %1 peers (threshold)").arg(thr)
            }
        }

        if (strictMode) {
            for (let i=0;i<destModel.count;i++) {
                const d = destModel.get(i)
                const addr = (d.address || "").trim()
                const amt  = (d.amount || "").trim()

                if (!addr || !amt) {
                    addrError = qsTr("All rows need address and amount")
                    break
                }
                if (!_validAddress(addr)) {
                    addrError = qsTr("Invalid address: %1").arg(addr); break
                }
                const v = parseFloat(amt)
                if (isNaN(v) || v<=0) {
                    addrError = qsTr("Invalid amount for %1").arg(addr); break
                }
            }

            const total = _sumDest()
            const unlocked = unlockedXmr
            if (total > unlocked) {
                balanceError = qsTr("Total (%1 XMR) exceeds unlocked (%2 XMR)")
                               .arg(total.toFixed(12))
                               .arg(unlocked.toFixed(12))
            }
        }

        ensureFeePickValidity()

        return !addrError && !peerError && !balanceError && !feePickError
    }

    function selectedPeers() {
        let out=[]
        for (let i=0;i<peerModel.count;i++) if (peerModel.get(i).include) out.push(peerModel.get(i).onion)
        return out
    }

    function destList() {
        let out=[]
        for (let i=0;i<destModel.count;i++) {
            const d=destModel.get(i)
            if ((d.address||"").trim().length && !isNaN(parseFloat(d.amount)))
                out.push({ address:d.address.trim(), amount: parseFloat(d.amount) })
        }
        return out
    }

    function ensureWalletObj() {
        const w = WalletManager.walletInstance(walletName)
        if (w && w !== walletObj) {
            walletObj = w
            if (walletObj.getBalance) walletObj.getBalance()
            if (walletObj.startSync && !walletObj.activeSyncTimer) walletObj.startSync(30)
            return true
        }
        return false
    }

    function startSession() {
        if (!validateInputs(true)) return

        var feeSplit = []
        if (deductFeeFromDests) {
            for (var i=0;i<feeDeductIdxs.length;i++) feeSplit.push(parseInt(feeDeductIdxs[i], 10))
        }

        const ref = TransferManager.startOutgoingTransfer(
            walletMeta.reference,
            destList(),
            selectedPeers(),
            selectedPeers(),
            Number(walletMeta.threshold || 0),
            feePriority,
            feeSplit,
            inspectBeforeSending,
            walletMeta.my_onion
        )
        if (!ref || ref.length===0) return

        var pageComponent = Qt.resolvedUrl("OutgoingTransfer.qml");
        middlePanel.currentPageUrl = pageComponent;
        middlePanel.stackView.replace(pageComponent, { transferRef: ref });
    }

    Connections {
        target: accountManager
        function onAddressXMRBookChanged() { loadXMRAddressBook(); }
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
                    onClicked: leftPanel.buttonClicked("Wallets")
                }

                Text {
                    text: qsTr("New Multisig Transfer")
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
                    text: "Wallet Information"
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
                    implicitHeight: walletInfoLayout.implicitHeight + 16
                    color: themeManager.backgroundColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    ColumnLayout {
                        id: walletInfoLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 6

                        RowLayout {
                            spacing: 16
                            Text {
                                text: qsTr("Wallet: %1").arg(walletName)
                                font.pixelSize: 12
                                color: themeManager.textColor
                                font.weight: Font.Medium
                            }
                            Text {
                                text: qsTr("Threshold: %1 / %2").arg(walletMeta.threshold||"-").arg(walletMeta.peers ? walletMeta.peers.length : "-")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                        }

                        RowLayout {
                            spacing: 16
                            Text {
                                text: qsTr("Wallet external ref: %1").arg(walletMeta.reference||"")
                                font.pixelSize: 10
                                color: themeManager.textSecondaryColor
                                font.family: "Monospace"
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                            }
                            Text {
                                text: qsTr("Unlocked: %1 XMR").arg(unlockedXmr.toFixed(12))
                                font.pixelSize: 12
                                color: themeManager.successColor
                                font.family: "Monospace"
                                font.weight: Font.Medium
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
                    text: "Destinations"
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
                    implicitHeight: destLayout.implicitHeight + 16
                    color: themeManager.backgroundColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    ColumnLayout {
                        id: destLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 8

                        Repeater {
                            model: destModel
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                AppInput {
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("XMR address")
                                    text: address
                                    font.family: "Monospace"
                                    font.pixelSize: 10
                                    onTextChanged: {
                                        destModel.setProperty(index, "address", text)
                                    }
                                }

                                AppIconButton {
                                    iconSource: "/resources/icons/book-bookmark.svg"
                                    size: 14
                                    onClicked: {
                                        selectedDestIndex = index;
                                        loadXMRAddressBook();
                                        xmrAddressDialog.open();
                                    }
                                }

                                AppInput {
                                    Layout.preferredWidth: 100
                                    placeholderText: "0.0"
                                    text: amount
                                    onTextChanged: {
                                        destModel.setProperty(index, "amount", text)
                                    }
                                }

                                RowLayout {
                                    visible: deductFeeFromDests
                                    spacing: 4
                                    Layout.alignment: Qt.AlignVCenter

                                    AppCheckBox {
                                        variant: "warning"
                                        checked: feeDeductIdxs.indexOf(index) !== -1
                                        onToggled: {
                                            let arr = (feeDeductIdxs || []).slice()
                                            const pos = arr.indexOf(index)
                                            if (checked) {
                                                if (pos === -1) arr.push(index)
                                            } else {
                                                if (pos !== -1) arr.splice(pos, 1)
                                            }
                                            feeDeductIdxs = arr
                                            ensureFeePickValidity()
                                        }
                                    }

                                    Text {
                                        text: qsTr("deduct fee")
                                        font.pixelSize: 9
                                        color: themeManager.textSecondaryColor
                                        Layout.alignment: Qt.AlignVCenter
                                    }
                                }

                                AppIconButton {
                                    iconSource: "/resources/icons/close-circle.svg"
                                    iconColor:destModel.count > 1? themeManager.errorColor : themeManager.textSecondaryColor
                                    size: 14
                                    enabled: destModel.count > 1
                                    onClicked: {
                                        removeFeeIdxAndShift(index)
                                        destModel.remove(index)
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            AppButton {
                                text: qsTr("Add Destination")
                                iconSource: "/resources/icons/add-circle.svg"
                                variant: "secondary"
                                implicitHeight: 28
                                onClicked: {
                                    destModel.append({address:"", amount:""})
                                    if (deductFeeFromDests) {
                                        addFeeIdxIfNeeded(destModel.count - 1)
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }

                            AppSwitch {
                                label: qsTr("Deduct fee from destinations")
                                checked: deductFeeFromDests
                                onToggled: {
                                    deductFeeFromDests = checked
                                    if (checked) {
                                        selectAllFeeIdxs()
                                    } else {
                                        feeDeductIdxs = []
                                    }
                                    ensureFeePickValidity()
                                }
                            }
                        }

                        AppAlert {
                            visible: !!feePickError
                            text: feePickError
                            variant: "error"
                            Layout.fillWidth: true
                        }
                        AppAlert {
                            visible: !!addrError
                            text: addrError
                            variant: "error"
                            Layout.fillWidth: true
                        }
                        AppAlert {
                            visible: !!balanceError
                            text: balanceError
                            variant: "error"
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
                    text: "Transfer Settings"
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
                    implicitHeight: settingsLayout.implicitHeight + 16
                    color: themeManager.backgroundColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    ColumnLayout {
                        id: settingsLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                text: qsTr("Fee priority:")
                                font.pixelSize: 12
                                color: themeManager.textColor
                                Layout.alignment: Qt.AlignVCenter
                            }

                            ComboBox {
                                model: [qsTr("Default"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
                                currentIndex: feePriority
                                onCurrentIndexChanged: feePriority = currentIndex
                                Layout.preferredWidth: 120

                                background: Rectangle {
                                    color: themeManager.surfaceColor
                                    border.color: themeManager.borderColor
                                    border.width: 1
                                    radius: 2
                                }

                                contentItem: Text {
                                    text: parent.displayText
                                    font.pixelSize: 12
                                    color: themeManager.textColor
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: 8
                                    rightPadding: 24
                                }
                            }

                            Item { Layout.fillWidth: true }

                            AppSwitch {
                                label: qsTr("Inspect before sending")
                                checked: inspectBeforeSending
                                onToggled: inspectBeforeSending = checked
                            }
                        }


                        Text {
                            text: qsTr("Signing Peers")
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: themeManager.textColor
                        }

                        Repeater {
                            model: peerModel
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                AppCheckBox {
                                    checked: include
                                    enabled: !own
                                    onToggled: {

                                        peerModel.setProperty(index, "include", checked)
                                        validateInputs(false)
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 28
                                    color: themeManager.backgroundColor
                                    border.color: themeManager.borderColor
                                    border.width: 1
                                    radius: 2

                                    Text {
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        text: own
                                              ? onion + qsTr(" (you)")
                                              : (label && label.length ? (onion + " (" + label + ")") : onion)
                                        font.family: "Monospace"
                                        font.pixelSize: 10
                                        color: themeManager.textColor
                                        elide: Text.ElideMiddle
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }

                        AppAlert {
                            visible: !!peerError
                            text: peerError
                            variant: "error"
                            Layout.fillWidth: true
                        }
                    }
                }
            }


            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Start Transfer")
                    iconSource: "/resources/icons/arrow-right-up.svg"
                    enabled: torServer.running === true && WalletManager.walletInstance(walletName)
                    implicitHeight: 36
                    onClicked: startSession()
                    ToolTip.visible: (hovered && torServer.running === false) || (hovered && !!WalletManager.walletInstance(walletName))
                    ToolTip.text: qsTr("Connect to Tor")
                    ToolTip.delay: 500
                }
            }
        }
    }


    AppAddressBookDialog {
        id: xmrAddressDialog
        titleText: qsTr("Select XMR Address")
        descriptionText: qsTr("Choose an address from your address book")
        model: xmrAddressBookModel
        addressBookType: "xmr"
        primaryField: "label"
        secondaryField: "xmr_address"
        emptyStateText: qsTr("No XMR addresses in your address book yet.")

        onItemSelected: function(item, index) {
            if (selectedDestIndex >= 0 && selectedDestIndex < destModel.count) {
                destModel.setProperty(selectedDestIndex, "address", item.xmr_address)
            }
        }

        onQuickAddRequested: function() {
            const tabMap = { "peer": 0, "trusted": 1, "xmr": 2, "daemon": 3 };
            const tabIndex = tabMap[xmrAddressDialog.addressBookType] || 0;

            var pageComponent = Qt.resolvedUrl("UnifiedAddressBook.qml");
            middlePanel.currentPageUrl = pageComponent;
            middlePanel.stackView.replace(pageComponent, { currentTab: tabIndex });
        }
    }

    Component.onCompleted: {
        walletMeta = WalletManager.getWalletMeta(walletName) || {}

        WalletManager.connectWallet(walletName)
        walletObj = WalletManager.walletInstance(walletName)
        if (walletObj && walletObj.getBalance) walletObj.getBalance()
        if (walletObj && walletObj.startSync && !walletObj.activeSyncTimer) walletObj.startSync(30)

        peerModel.clear()

        const peersRaw = (WalletManager.peersForWallet(walletName) || [])
        const peers = peersRaw.map(normalizeOnion)

        let myOnions = []
        if (accountManager && accountManager.is_authenticated) {
            const accOnions = accountManager.torOnions() || []
            for (let i = 0; i < accOnions.length; i++) myOnions.push(normalizeOnion(accOnions[i]))
        }

        const matches = peers.filter(p => myOnions.indexOf(p) !== -1)
        const my = (matches.length === 1) ? matches[0] : ""

        const labels = _peerLabelsByOnion();


        if (my && peers.indexOf(my) !== -1)
            peerModel.append({ onion: my, include: true, own: true,  label: labels[my] || "" })

        for (let i = 0; i < peers.length; i++) {
            const p = peers[i]
            if (p === my) continue
            peerModel.append({ onion: p, include: false, own: false, label: labels[p] || "" })
        }


        if (destModel.count===0) destModel.append({address:"", amount:""})

        loadXMRAddressBook();
    }
}
