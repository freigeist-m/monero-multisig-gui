import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: qsTr("Transfer Tracker")

    property string transferRef: ""
    property var    snapshot: ({})
    property real   totalAmountXmr: 0.0
    property var    sessionObj: null

    ListModel { id: recipientsModel }
    ListModel { id: peersModel }

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    property string myOnionForSession: ""

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
        else if (o && o.transfer_description && Array.isArray(o.transfer_description.signing_order) && o.transfer_description.signing_order.length)
            order = o.transfer_description.signing_order
        else if (o && o.peers)
            order = Object.keys(o.peers)
        order = (order || []).map(normOnion)

        let chosen = ""
        for (let p of order) { if (ours.indexOf(p) !== -1) { chosen = p; break } }
        if (!chosen && ours && ours.length === 1) chosen = normOnion(ours[0])

        myOnionForSession = chosen
    }

    function isSelfPeer(peerOnion, myOnion) {
        if (!myOnion) return false
        return normOnion(peerOnion) === myOnion
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


    // Normalize onions to lowercase + ".onion"
    function normOnion(s) {
        if (!s) return ""
        s = ("" + s).trim().toLowerCase()
        return s.endsWith(".onion") ? s : (s + ".onion")
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
                console.log("[TransferTracker] trusted peers JSON parse error:", e);
            }
        }
        return out;
    }


    function parseSnapshot() {
        try {
            let jsonStr = TransferManager.getSavedTransferDetails(transferRef)
            if (!jsonStr) return

            let o = JSON.parse(jsonStr)

            let feeXmr = 0
            if (o.transfer_description && o.transfer_description.fee !== undefined && o.transfer_description.fee !== null) {
                feeXmr = Number(o.transfer_description.fee)
            }
            o.feeXmr = feeXmr / 1e12

            // recipients
            recipientsModel.clear()
            let totalA = 0
            let recipients = o.transfer_description?.recipients || o.destinations || []
            for (let r of recipients) {
                let amtA = Number(r.amount || 0)
                totalA += amtA
                recipientsModel.append({
                    address: r.address,
                    amountAtomic: amtA,
                    amountXmr: (amtA/1e12).toFixed(12)
                })
            }
            totalAmountXmr = (totalA + feeXmr) /1e12



            let unlock_time = 0
            if (o.transfer_description.unlock_time !== undefined && o.transfer_description.unlock_time !== null) {
                unlock_time = Number(o.transfer_description.unlock_time)
            }

            o.unlock_time = unlock_time

            snapshot = o
            resolveMyOnionForSession(o)
            peersModel.clear()
            if (o.peers) {
                const labels = _peerLabelsByOnion()
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
                            stage: p[0] || "",
                            received: !!p[1],
                            signed: !!p[2],
                            status: p[3] || "",
                            isOwn: isSelfPeer(on, myOnion),
                            orderPos: i + 1
                        })
                    } else {
                        peersModel.append({
                            onion: on,
                            label: labels[on] || "",
                            stage: p.stage || "",
                            received: !!(p.ready || p.received),
                            signed: !!p.signed,
                            status: p.status || "",
                            orderPos: i + 1
                        })
                    }
                }
            }

        } catch(e) {
            console.warn("Error parsing snapshot:", e)
        }
    }

    function formatDate(timestamp) {
        if (!timestamp) return "—"
        return new Date(timestamp * 1000).toLocaleString()
    }

    function getStageIcon() {
        const stage = snapshot.stage || ""
        switch(stage.toUpperCase()) {
            case "COMPLETE": return "/resources/icons/check-circle.svg"
            case "ERROR": return "/resources/icons/danger-circle.svg"
            case "DECLINED": return "/resources/icons/forbidden-circle.svg"
            case "CHECKING_STATUS": return "/resources/icons/refresh-circle.svg"
            default: return "/resources/icons/refresh-circle.svg"
        }
    }

    function getStageColor() {
        const stage = snapshot.stage || ""
        switch(stage.toUpperCase()) {
            case "COMPLETE": return themeManager.successColor
            case "ERROR": return themeManager.errorColor
            case "DECLINED": return themeManager.errorColor
            case "CHECKING_STATUS": return themeManager.warningColor
            default: return themeManager.primaryColor
        }
    }

    Timer {
        id: refreshTimer
        interval: 3000
        repeat: true
        running: true
        onTriggered: parseSnapshot()
    }

    Connections {
        target: sessionObj
        enabled: sessionObj !== null
        ignoreUnknownSignals: true

        function onFinished(tref, result) {
            refreshTimer.running = false
            const url = Qt.resolvedUrl("SavedTransfer.qml")
            leftPanel.currentPageUrl =  url
            middlePanel.currentPageUrl = url
            middlePanel.stackView.replace(url, { transferRef: root.transferRef })
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
                    text: qsTr("Transfer Tracker")
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: themeManager.textColor
                    Layout.fillWidth: true
                }

                AppButton {
                    text: qsTr("Refresh")
                    iconSource: "/resources/icons/refresh.svg"
                    variant: "secondary"
                    implicitHeight: 28
                    onClicked: parseSnapshot()
                }

                AppButton {
                    text: qsTr("Stop Tracking")
                    iconSource: "/resources/icons/stop-circle.svg"
                    variant: "error"
                    implicitHeight: 28
                    onClicked: {
                        const ok = TransferManager.stopTracker(root.transferRef)
                        if (ok) {
                            refreshTimer.running = false

                            // leftPanel.buttonClicked("SessionsOverview")
                        }
                    }
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
                    text: "Status & Progress"
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
                                text: snapshot.stage || "—"
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
                                text: snapshot.status || "—"
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
                                text: (snapshot.wallet_name || "—")
                                font.pixelSize: 12
                                color: themeManager.textColor
                                elide: Text.ElideRight
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
                                text: qsTr("Created:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                            Text {
                                text: formatDate(snapshot.created_at)
                                font.pixelSize: 12
                                color: themeManager.textColor
                                font.family: "Monospace"
                            }

                            Text {
                                text: qsTr("Last Update:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                            Text {
                                text: formatDate(snapshot.time)
                                font.pixelSize: 12
                                color: themeManager.textColor
                                font.family: "Monospace"
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
                                    text: snapshot.tx_id || "pending"
                                    font.pixelSize: 12
                                    color: snapshot.tx_id ? themeManager.successColor : themeManager.textSecondaryColor
                                    font.family: "Monospace"
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }

                                AppCopyButton {
                                    textToCopy: snapshot.tx_id || ""
                                    size: 14
                                    visible: snapshot.tx_id && snapshot.tx_id !== "pending"
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

                        // Total row
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
                                        color: isOwn ? themeManager.primaryColor : themeManager.textSecondaryColor
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

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
                            text: qsTr("No signing peers")
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

    Component.onCompleted:{
        sessionObj = TransferManager.getTrackerSession(transferRef)
        parseSnapshot()}
}
