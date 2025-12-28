import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: qsTr("Incoming Transfer")

    property string transferRef: ""
    property var    sessionObj: null
    property var    snapshot: ({})
    property string stageName: ""
    property string statusText: ""
    property string myOnionForSession: ""
    property real   totalAmountXmr: 0.0

    ListModel { id: recipientsModel }
    ListModel { id: peersModel }

    background: Rectangle {
        color: themeManager.backgroundColor
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


    // Build onion -> label map from Address Book + Trusted Peers
    function _peerLabelsByOnion() {
        let out = {};
        if (accountManager && accountManager.is_authenticated) {
            // Address Book
            const ab = accountManager.getAddressBook() || [];
            for (let i = 0; i < ab.length; ++i) {
                const it = ab[i];
                const on = normOnion(String(it.onion || ""));
                const lb = String(it.label || "");
                if (on && lb) out[on] = lb;
            }

            // Trusted Peers (JSON keyed by onion)
            try {
                const tp = JSON.parse(accountManager.getTrustedPeers() || "{}");
                for (const onRaw in tp) {
                    if (!tp.hasOwnProperty(onRaw)) continue;
                    const on = normOnion(String(onRaw || ""));
                    const lb = String((tp[onRaw] && tp[onRaw].label) || "");
                    if (on && lb) out[on] = lb;
                }
            } catch (e) {
                console.log("[IncomingPage] trusted peers JSON parse error:", e);
            }
        }
        return out;
    }

    // Update labels in peersModel without rebuilding it
    function refreshPeerLabels() {
        const labels = _peerLabelsByOnion();
        for (let i = 0; i < peersModel.count; ++i) {
            const row = peersModel.get(i);
            const on = normOnion(row.onion || "");
            const lb = labels[on] || "";
            if ((row.label || "") !== lb) {
                peersModel.setProperty(i, "label", lb);
            }
        }
    }


    function resolveMyOnionForSession(o) {

        let ours = []
        try {
            if (accountManager && accountManager.torOnions)
                ours = accountManager.torOnions() || []
        } catch(e) { /* ignore */ }
        if ((!ours || !ours.length) && torServer && torServer.onionAddress)
            ours = [ torServer.onionAddress ]
        ours = (ours || []).map(normOnion)


        let order = []
        if (o && Array.isArray(o.signing_order) && o.signing_order.length)
            order = o.signing_order
        else if (o && o.peers)
            order = Object.keys(o.peers)
        order = (order || []).map(normOnion)

        let chosen = ""
        for (let p of order) { if (ours.indexOf(p) !== -1) { chosen = p; break } }
        if (!chosen && ours && ours.length === 1) chosen = normOnion(ours[0])

        myOnionForSession = chosen
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

    function loadSavedSnapshot() {
        try {
            const s = TransferManager.getSavedTransferDetails(transferRef) || ""
            if (!s) return
            const o = JSON.parse(s)


            let feeXmr = 0
            if (o.transfer_description && o.transfer_description.fee !== undefined && o.transfer_description.fee !== null) {
                feeXmr = Number(o.transfer_description.fee)
            }
            o.feeXmr = feeXmr / 1e12

            let unlock_time = 0
            if (o.transfer_description.unlock_time !== undefined && o.transfer_description.unlock_time !== null) {
                unlock_time = Number(o.transfer_description.unlock_time)
            }

            o.unlock_time = unlock_time

            snapshot = o
            stageName = (o.stage || "")
            statusText = (o.status || "")
            resolveMyOnionForSession(o)

            // recipients
            recipientsModel.clear()
            let totalA = 0
            let recips = []
            if (o.transfer_description && o.transfer_description.recipients)
                recips = o.transfer_description.recipients
            else if (o.destinations)
                recips = o.destinations

            for (let r of (recips || [])) {
                const amtA = Number(r.amount || 0)
                totalA += amtA
                recipientsModel.append({
                    address: r.address || "",
                    amountAtomic: amtA,
                    amountXmr: (amtA/1e12).toFixed(12)
                })
            }

            totalAmountXmr = (totalA +feeXmr) /1e12

            peersModel.clear()
            const labels = _peerLabelsByOnion()
            const myOnion = myOnionForSession

            const order = signingOrderFrom(o)
            const pmap  = peersMapByNorm(o.peers)

            for (let i = 0; i < order.length; ++i) {
                const on = order[i]
                const p = pmap[on]
                if (!p) continue

                if (Array.isArray(p)) {
                    peersModel.append({
                        onion: on,
                        label: labels[on] || "",
                        stage: p[0] || "UNKNOWN",
                        received: !!p[1],
                        signed: !!p[2],
                        status: p[3],
                        isOwn: isSelfPeer(on, myOnion),
                        orderPos: i + 1
                    })
                } else {
                    peersModel.append({
                        onion: on,
                        label: labels[on] || "",
                        stage: p.stage || "UNKNOWN",
                        received: !!(p.ready || p.received || p.received_transfer),
                        signed: !!p.signed,
                        status: p.status || "",
                        isOwn: isSelfPeer(on, myOnion),
                        orderPos: i + 1
                    })
                }
            }

        } catch (e) {
            console.warn("[IncomingPage] parse error:", e)
        }
    }

    function refreshOnce() {

        if (sessionObj && sessionObj.getTransferDetailsJson) {
            try {
                const j = sessionObj.getTransferDetailsJson()
                if (j && j.length) {
                    const o = JSON.parse(j)
                    stageName = o.stage || stageName
                    snapshot.stage = stageName

                    if (o && o.my_onion) {
                        myOnionForSession = normOnion(o.my_onion)
                    } else {
                        resolveMyOnionForSession(snapshot)
                    }
                }
            } catch (e) { /* ignore */ }
        } else {
            loadSavedSnapshot()
        }
    }


    function openTrackerNow() {
        const pageComponent = Qt.resolvedUrl("TransferTracker.qml")
        if (middlePanel) {
            middlePanel.currentPageUrl = pageComponent
            middlePanel.stackView.replace(pageComponent, { transferRef: transferRef })
        } else if (stackView) {
            stackView.push(pageComponent, { transferRef: transferRef })
        }
    }

    function getStageIcon() {
        switch(stageName.toUpperCase()) {
            case "COMPLETE": return "/resources/icons/check-circle.svg"
            case "ERROR": return "/resources/icons/danger-circle.svg"
            case "CHECKING_STATUS": return "/resources/icons/refresh-circle.svg"
            case "DECLINED": return "/resources/icons/forbidden-circle.svg"
            case "RECEIVED": return "/resources/icons/arrow-left-down.svg"
            case "VALIDATING": return "/resources/icons/clock-circle.svg"
            default: return "/resources/icons/refresh-circle.svg"
        }
    }

    function getStageColor() {
        switch(stageName.toUpperCase()) {
            case "COMPLETE": return themeManager.successColor
            case "ERROR": return themeManager.errorColor
            case "CHECKING_STATUS": return themeManager.warningColor
            case "RECEIVED": return themeManager.primaryColor
            case "DECLINED": return themeManager.errorColor
            case "VALIDATING": return themeManager.warningColor
            default: return themeManager.primaryColor
        }
    }

    function canDeclineTransfer() {
        const stage = stageName || (snapshot.stage || "")
        const terminalStages = ["COMPLETE", "ABORTED", "DECLINED", "BROADCAST_SUCCESS", "ERROR", "FAILED"]
        return !terminalStages.includes(stage.toUpperCase())
    }

    Timer {
        id: refreshTimer
        interval: 1500
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
        function onSubmittedSuccessfully(/*ref*/) {
            leftPanel.buttonClicked("SessionsOverview")
        }
        function onFinished(/*ref, result*/) {
            refreshOnce()
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
                    backText : qsTr("Transfers")
                    onClicked: {
                        leftPanel.buttonClicked("SessionsOverview")
                    }
                }

                Text {
                    text: qsTr("Incoming Transfer")
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
                                text: stageName || (snapshot.stage || "—")
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
                                text: statusText || (snapshot.status || "—")
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
                                text: snapshot.wallet_name || snapshot.wallet_ref || "—"
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
                                text: (snapshot.feeXmr !== 0 && snapshot.feeXmr !== undefined ) ? snapshot.feeXmr.toFixed(12) + " XMR" : "—"
                                font.pixelSize: 12
                                color: themeManager.textColor
                                font.family: "Monospace"
                            }

                            Text {
                                text: qsTr("Tx ID:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    id: txIdText
                                    text: snapshot.tx_id || "—"
                                    font.pixelSize: 12
                                    color: snapshot.tx_id ? themeManager.successColor : themeManager.textSecondaryColor
                                    font.family: "Monospace"
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }

                                AppCopyButton {
                                    textToCopy: snapshot.tx_id || ""
                                    size: 14
                                    visible: snapshot.tx_id && snapshot.tx_id !== "—"
                                }
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

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Text {
                    text: qsTr("Peers (%1)").arg(peersModel.count)
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
                            height: 48
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
                                    color: isOwn ? themeManager.primaryColor : themeManager.textSecondaryColor
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: isOwn
                                              ? (onion + qsTr(" (you)"))
                                              : ((label && label.length) ? (onion + " (" + label + ")") : onion)
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                        font.family: "Monospace"
                                        font.pixelSize: 10
                                        color: themeManager.textColor
                                        wrapMode: Text.NoWrap
                                    }

                                    RowLayout {
                                        spacing: 12

                                        Text {
                                            text: stage
                                            font.pixelSize: 10
                                            color: themeManager.textColor
                                            wrapMode: Text.NoWrap
                                        }

                                        AppStatusIndicator {
                                            visible: signed
                                            status: "success"
                                            text: qsTr("Signed")
                                            dotSize: 5
                                        }

                                        Text {
                                            text: status
                                            font.pixelSize: 10
                                            color: themeManager.textColor
                                            wrapMode: Text.NoWrap
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: peersModel.count === 0
                        text: qsTr("No peers")
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

                AppButton {
                    text: qsTr("Approve & Sign + Relay")
                    iconSource: "/resources/icons/check-circle.svg"
                    enabled: torServer.running === true && WalletManager.walletInstance(snapshot.wallet_name)
                    visible: (!sessionObj) &&
                             ((snapshot.stage || "").toUpperCase() === "RECEIVED" ||
                              (snapshot.stage || "").toUpperCase() === "VALIDATING" ||
                              (snapshot.stage || "") === "")
                    implicitHeight: 36
                    onClicked: {
                        const ref = TransferManager.startIncomingTransfer(transferRef)
                        if (ref && ref.length) {
                            sessionObj = TransferManager.getIncomingSession(transferRef)
                            refreshOnce()
                        } else {
                            console.warn("[IncomingPage] failed to start incoming session")
                        }
                    }
                    ToolTip.visible: (hovered && torServer.running === false) || (hovered && !WalletManager.walletInstance(snapshot.wallet_name))
                    ToolTip.text: qsTr("Connect Wallet & Tor")
                    ToolTip.delay: 500
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Resume")
                    iconSource: "/resources/icons/refresh.svg"
                    variant: "secondary"
                    visible: !!sessionObj && (stageName !== "COMPLETE" && stageName !== "ERROR")
                    implicitHeight: 32
                    onClicked: refreshOnce()
                }


                AppButton {
                    text: qsTr("Open Tracker")
                    iconSource: "/resources/icons/layers.svg"
                    variant: "secondary"
                    visible: (stageName === "CHECKING_STATUS")
                    implicitHeight: 32
                    onClicked: openTrackerNow()
                }

                AppButton {
                    text: qsTr("Decline")
                    iconSource: "/resources/icons/stop-circle.svg"
                    variant: "warning"
                    visible: canDeclineTransfer()
                    implicitHeight: 32
                    onClicked: confirmDecline.open()
                }

                AppButton {
                    text: qsTr("Cancel")
                    iconSource: "/resources/icons/stop-circle.svg"
                    variant: "error"
                    visible: !!sessionObj && (stageName !== "COMPLETE" && stageName !== "ERROR")
                    implicitHeight: 32
                    onClicked: {
                        if (sessionObj && sessionObj.cancel) sessionObj.cancel()
                    }
                }
            }
        }
    }


    Dialog {
        id: confirmDecline
        modal: true
        title: ""
        anchors.centerIn: parent
        width: Math.min(380, root.width * 0.9)
        height: Math.min(260, root.height * 0.8)

        background: Rectangle {
            color: themeManager.surfaceColor
            border.color: themeManager.borderColor
            border.width: 1
            radius: 2
        }

        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            AppIcon {
                source: "/resources/icons/stop-circle.svg"
                width: 32
                height: 32
                color: themeManager.warningColor
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: qsTr("Decline Transfer?")
                font.pixelSize: 16
                font.weight: Font.Bold
                color: themeManager.textColor
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: qsTr("Are you sure you want to decline this incoming transfer?")
                font.pixelSize: 12
                color: themeManager.textColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                text: transferRef
                font.pixelSize: 10
                font.family: "Monospace"
                color: themeManager.textSecondaryColor
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
            }

            Text {
                text: qsTr("This will stop processing and mark the transfer as declined.")
                font.pixelSize: 11
                color: themeManager.textSecondaryColor
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                AppButton {
                    text: qsTr("Cancel")
                    variant: "secondary"
                    Layout.fillWidth: true
                    onClicked: confirmDecline.close()
                }

                AppButton {
                    text: qsTr("Decline")
                    iconSource: "/resources/icons/stop-circle.svg"
                    variant: "warning"
                    Layout.fillWidth: true
                    onClicked: {
                        if (sessionObj && sessionObj.decline) {

                            sessionObj.decline()
                        } else {

                            TransferManager.declineIncomingTransfer(transferRef)
                        }
                        confirmDecline.close()

                    }
                }
            }
        }
    }

    Component.onCompleted: {
        loadSavedSnapshot()
        resolveMyOnionForSession(snapshot)
        sessionObj = TransferManager.getIncomingSession(transferRef)
        refreshOnce()
    }
}
