import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: qsTr("Transfer Session")

    property string transferRef: ""
    property var    sessionObj: null
    property var    snapshot: ({})
    property string stageName: ""
    property string statusText: ""
    property bool navigated: false
    property real   totalAmountXmr: 0.0
    property real   feeXmr: 0.0

    ListModel { id: recipientsModel }
    ListModel { id: peersModel }

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    // Build onion -> label map from Address Book + Trusted Peers
    function _peerLabelsByOnion() {
        let out = {};
        if (accountManager && accountManager.is_authenticated) {
            // Peer Address Book
            const ab = accountManager.getAddressBook() || [];
            for (let i = 0; i < ab.length; ++i) {
                const it = ab[i];
                const on = normOnion(String(it.onion || ""));
                const lb = String(it.label || "");
                if (on && lb) out[on] = lb;
            }

            // Trusted Peers (JSON string keyed by onion)
            try {
                const tp = JSON.parse(accountManager.getTrustedPeers() || "{}");
                for (const onRaw in tp) {
                    if (!tp.hasOwnProperty(onRaw)) continue;
                    const on = normOnion(String(onRaw || ""));
                    const lb = String((tp[onRaw] && tp[onRaw].label) || "");
                    if (on && lb) out[on] = lb;
                }
            } catch (e) {
                console.log("Trusted peers JSON parse error:", e);
            }
        }
        return out;
    }


    function goToTracker(tref) {
        var pageComponent = Qt.resolvedUrl("TransferTracker.qml");
        middlePanel.currentPageUrl = pageComponent;
        middlePanel.stackView.replace(pageComponent, { transferRef: tref });
    }



    function normOnion(s) {
        if (!s) return ""
        s = ("" + s).trim().toLowerCase()
        return s.endsWith(".onion") ? s : (s + ".onion")
    }

    function isSelfPeer(peerOnion, myOnion) {
        if (!myOnion) return false
        return normOnion(peerOnion) === myOnion
    }

    function parseSnapshot(j) {
        try {
            let o = JSON.parse(j || "{}")
            snapshot = o
            stageName = o.stage || ""
            const my = normOnion(o.my_onion || "")

            let _feeXmr = 0
            if (o.fee !== undefined && o.fee !== null) {
                _feeXmr = Number(o.fee)
            }
            feeXmr = _feeXmr / 1e12

            recipientsModel.clear()
            let totalA = 0
            if (o.destinations && o.destinations.length) {
                for (let i=0;i<o.destinations.length;i++) {
                    const r = o.destinations[i]
                    let amtA = Number(r.amount || 0)
                    totalA += amtA
                    recipientsModel.append({
                        address: r.address,
                        amountAtomic: amtA,
                        amountXmr: (amtA/1e12).toFixed(12)
                    })
                }
            }

            totalAmountXmr = (totalA + _feeXmr) /1e12
            peersModel.clear()
            if (o.peers) {
                const labels = _peerLabelsByOnion()
                const my = normOnion(o.my_onion || "")
                const order = signingOrderFrom(o)
                const pmap = peersMapByNorm(o.peers)

                // Populate in signing order
                for (let i = 0; i < order.length; ++i) {
                    const on = order[i]
                    if (isSelfPeer(on, my)) continue  // keep your current behavior (no "you" row)
                    const p = pmap[on]
                    if (!p) continue

                    peersModel.append({
                        onion: on,
                        label: labels[on] || "",
                        orderPos: i + 1,
                        online: !!p.online,
                        ready: !!p.ready,
                        received: !!p.received_transfer,
                        signed: !!p.signed,
                        ts: p.multisig_info_timestamp || ""
                    })
                }

                // Fallback if order was empty for some reason
                if (order.length === 0) {
                    for (let k in pmap) {
                        if (isSelfPeer(k, my)) continue
                        const p = pmap[k]
                        peersModel.append({
                            onion: k,
                            label: labels[k] || "",
                            orderPos: 0,
                            online: !!p.online,
                            ready: !!p.ready,
                            received: !!p.received_transfer,
                            signed: !!p.signed,
                            ts: p.multisig_info_timestamp || ""
                        })
                    }
                }
            }




        } catch(e) { /* ignore */ }


    }

    function signingOrderFrom(o) {
        let order = []

        // Most important: explicit signing order from backend
        if (o && Array.isArray(o.signing_order) && o.signing_order.length)
            order = o.signing_order
        // Some of your saved snapshots nest data under transfer_description
        else if (o && o.transfer_description && Array.isArray(o.transfer_description.signing_order) && o.transfer_description.signing_order.length)
            order = o.transfer_description.signing_order
        // Fallback (NOT ideal, but deterministic enough if backend didn't store order)
        else if (o && o.peers)
            order = Object.keys(o.peers)

        return (order || []).map(normOnion)
    }

    function peersMapByNorm(peersObj) {
        const m = {}
        if (!peersObj) return m
        for (let k in peersObj) {
            const nk = normOnion(k)
            m[nk] = peersObj[k]
        }
        return m
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

        function onStageChanged(s) { stageName = s; refreshOnce() }
        function onStatusChanged(t) { statusText = t }
        function onPeerStatusChanged() { refreshOnce() }
        function onFinished(tref, result) {
            if (navigated) return
            navigated = true
            refreshOnce()
            refreshTimer.running = false
            leftPanel.buttonClicked("SessionsOverview")
        }
        function onSubmittedSuccessfully(tref) {
            if (navigated) return
            navigated = true
            refreshTimer.running = false
            goToTracker(tref || transferRef)
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
                    text: qsTr("Transfer Session")
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
                                text: qsTr("Wallet:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                            Text {
                                text: snapshot.wallet_name || "—"
                                font.pixelSize: 12
                                color: themeManager.textColor
                                font.family: "Monospace"
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }

                            Text {
                                text: qsTr("Wallet external ref:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                            Text {
                                text: (snapshot.wallet_ref || "—")
                                font.pixelSize: 12
                                color: themeManager.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                text: qsTr("Unlock time:")
                                font.pixelSize: 12
                                color: (snapshot.unlock_time !== "" && snapshot.unlock_time !== undefined && Number(snapshot.unlock_time)>0  ) ?  themeManager.errorColor : themeManager.textSecondaryColor
                            }
                            Text {
                                text: (snapshot.unlock_time !== "" && snapshot.unlock_time !== undefined ) ? snapshot.unlock_time + " blocks" : "—"
                                font.pixelSize: 12
                                color: (snapshot.unlock_time !== "" && snapshot.unlock_time !== undefined && Number(snapshot.unlock_time)>0 ) ?  themeManager.errorColor : themeManager.textColor
                                font.family: "Monospace"
                            }

                            Text {
                                text: qsTr("Fee:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                            Text {
                                text: (feeXmr !== 0 && feeXmr !== undefined ) ? feeXmr.toFixed(12) + " XMR" : "—"
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

            // Peers section
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Text {
                    text: qsTr("Signing Peers (%1)").arg(peersModel.count)
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
                    id: peersLayout
                    Layout.fillWidth: true

                    spacing: 6

                    Repeater {
                        model: peersModel
                        delegate: Rectangle {
                            Layout.fillWidth: true
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
                                    width: 12
                                    height: 12
                                    color: online ? themeManager.successColor : themeManager.textSecondaryColor
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        text:  (label && label.length) ? (onion + " (" + label + ")") : onion
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                        font.family: "Monospace"
                                        font.pixelSize: 10
                                        color: themeManager.textColor
                                        wrapMode: Text.NoWrap
                                    }

                                    RowLayout {
                                        spacing: 12

                                        AppStatusIndicator {
                                            status: online ? "online" : "offline"
                                            text: online ? qsTr("Online") : qsTr("Offline")
                                            dotSize: 5
                                        }

                                        AppStatusIndicator {
                                            status: ready ? "success" : "pending"
                                            text: ready ? qsTr("Ready") : qsTr("Not Ready")
                                            dotSize: 5
                                        }

                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: peersModel.count === 0
                        text: qsTr("No signing peers")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignCenter
                        Layout.topMargin: 12
                        Layout.bottomMargin: 12
                    }
                }

            }

            // Action buttons
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Approve & Submit")
                    iconSource: "/resources/icons/check-circle.svg"
                    visible: stageName === "APPROVING"
                    enabled: torServer.running === true && WalletManager.walletInstance(snapshot.wallet_name)
                    implicitHeight: 32
                    onClicked: TransferManager.proceedAfterApproval(transferRef)
                    ToolTip.visible: (hovered && torServer.running === false) ||  (hovered && !WalletManager.walletInstance(snapshot.wallet_name))
                    ToolTip.text: qsTr("Connect to Tor")
                    ToolTip.delay: 500
                }

                AppButton {
                    text: qsTr("Abort Transfer")
                    iconSource: "/resources/icons/stop-circle.svg"
                    variant: "error"
                    visible: stageName !== "COMPLETE" && stageName !== "ERROR"
                    implicitHeight: 32
                    onClicked: TransferManager.abortOutgoingTransfer(transferRef)
                }
            }
        }
    }

    Component.onCompleted: {
        sessionObj = TransferManager.getOutgoingSession(transferRef)
        refreshOnce()
    }
}
