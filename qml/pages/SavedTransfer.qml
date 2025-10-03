import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: qsTr("Saved Transfer")

    property string transferRef: ""
    property var    snapshot: ({})
    property real   totalAmountXmr: 0.0

    ListModel { id: recipientsModel }
    ListModel { id: peersModel }

    background: Rectangle {
        color: themeManager.backgroundColor
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

    function parseSnapshot() {
        try {
            let o = JSON.parse(TransferManager.getSavedTransferDetails(transferRef) || "{}")
            snapshot = o

            let feeXmr = 0
            if (o.transfer_description.fee !== undefined && o.transfer_description.fee !== null) {
                feeXmr = Number(o.transfer_description.fee)
            }
            o.feeXmr = feeXmr / 1e12

            recipientsModel.clear()
            let totalA = 0
            for (let r of (o.transfer_description?.recipients || [])) {
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

            peersModel.clear()
            const myOnion = normOnion(torServer.onionAddress || "")
            for (let k in o.peers) {
                let p = o.peers[k]
                peersModel.append({
                    onion: k,
                    stage: p[0],
                    received: p[1],
                    signed: p[2],
                    status: p[3] || "",
                    isOwn: isSelfPeer(k, myOnion)
                })
            }
        } catch(e) {
            console.warn("SavedTransfer parse error:", e)
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
            case "RECEIVED": return "/resources/icons/arrow-left-down.svg"
            case "CHECKING_STATUS": return "/resources/icons/refresh-circle.svg"
            default: return "/resources/icons/clock-circle.svg"
        }
    }

    function getStageColor() {
        const stage = snapshot.stage || ""
        switch(stage.toUpperCase()) {
            case "COMPLETE": return themeManager.successColor
            case "ERROR": return themeManager.errorColor
            case "DECLINED": return themeManager.errorColor
            case "CHECKING_STATUS": return themeManager.warningColor
            case "RECEIVED": return themeManager.primaryColor
            default: return themeManager.primaryColor
        }
    }

    function canDeclineTransfer() {
        const stage = snapshot.stage || ""
        const _type =  snapshot.type || ""
        const terminalStages = ["COMPLETE", "ABORTED", "DECLINED", "BROADCAST_SUCCESS","CHECKING_STATUS", "ERROR", "FAILED"]
        return !terminalStages.includes(stage.toUpperCase()) && _type === "MULTISIG"
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
                    text: qsTr("Saved Transfer")
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
                    text: "Status & Metadata"
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
                                text: qsTr("Wallet:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                            Text {
                                text: snapshot.wallet_name || "—"
                                font.pixelSize: 12
                                color: themeManager.textColor
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            Text {
                                text: qsTr("Wallet external ref:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                                visible: snapshot.type === "MULTISIG"
                            }
                            Text {
                                text: (snapshot.wallet_ref || "—")
                                font.pixelSize: 12
                                color: themeManager.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                visible: snapshot.type === "MULTISIG"
                            }

                            Text {
                                text: qsTr("Type:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                            }
                            Text {
                                text: snapshot.type || "—"
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                                font.family: "Monospace"
                                Layout.fillWidth: true
                            }

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
                                text: qsTr("Created:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                                visible: snapshot.created_at!== 0 && snapshot.created_at!== undefined
                            }
                            Text {
                                text: formatDate(snapshot.created_at)
                                font.pixelSize: 12
                                color: themeManager.textColor
                                font.family: "Monospace"
                                visible: snapshot.created_at!== 0 && snapshot.created_at!== undefined
                            }

                            Text {
                                text: qsTr("Submitted:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                                visible: snapshot.submitted_at!== 0 && snapshot.submitted_at!== undefined
                            }
                            Text {
                                text: formatDate(snapshot.submitted_at)
                                font.pixelSize: 12
                                color: themeManager.textColor
                                font.family: "Monospace"
                                visible: snapshot.submitted_at!== 0 && snapshot.submitted_at!== undefined
                            }

                            Text {
                                text: qsTr("Received:")
                                font.pixelSize: 12
                                color: themeManager.textSecondaryColor
                                visible: snapshot.received_at!== undefined && snapshot.received_at!== 0
                            }
                            Text {
                                text: formatDate(snapshot.received_at)
                                font.pixelSize: 12
                                color: themeManager.textColor
                                font.family: "Monospace"
                                visible: snapshot.received_at!== undefined && snapshot.received_at!== 0
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
                                text: (snapshot.feeXmr !== 0) ? snapshot.feeXmr.toFixed(12) + " XMR" : "—"
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
                                    id:tx_id
                                    text: snapshot.tx_id || "—"
                                    font.pixelSize: 12
                                    color: snapshot.tx_id ? themeManager.successColor : themeManager.textSecondaryColor
                                    font.family: "Monospace"
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }

                                AppCopyButton {
                                    textToCopy: snapshot.tx_id
                                    size: 14
                                    visible: tx_id.text !== "—"
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
                visible: (snapshot.type || "").toUpperCase() === "MULTISIG"

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
                                        text: isOwn ? onion + qsTr(" (you)") : onion
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

            // Action buttons
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8

                AppButton {
                    text: qsTr("Start Tracker")
                    iconSource: "/resources/icons/refresh-circle.svg"
                    variant: "primary"
                    visible: snapshot.stage === "CHECKING_STATUS"
                    implicitHeight: 32
                    onClicked: {
                        if (TransferManager.resumeTracker(transferRef)) {
                            var pageComponent = Qt.resolvedUrl("TransferTracker.qml");
                            middlePanel.currentPageUrl = pageComponent;
                            middlePanel.stackView.replace(pageComponent, { transferRef: transferRef });
                        } else {
                            console.log("Failed to start tracker for", transferRef)
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Open Incoming")
                    iconSource: "/resources/icons/arrow-left-down.svg"
                    variant: "secondary"
                    visible: snapshot.stage === "RECEIVED"
                    implicitHeight: 32
                    onClicked: {
                        const url = Qt.resolvedUrl("IncomingTransfer.qml")
                        middlePanel.currentPageUrl = url
                        middlePanel.stackView.replace(url, { transferRef: transferRef })
                    }
                }

                AppButton {
                    text: qsTr("Open Simple Transfer")
                    iconSource: "/resources/icons/arrow-left-down.svg"
                    variant: "secondary"
                    visible: snapshot.stage === "APPROVING" && snapshot.type === "SIMPLE" && TransferManager.getSimpleSession(transferRef)
                    enabled: WalletManager.walletInstance(snapshot.wallet_name)
                    implicitHeight: 32
                    onClicked: {
                        const url = Qt.resolvedUrl("SimpleTransferApproval.qml")
                        middlePanel.currentPageUrl = url
                        middlePanel.stackView.replace(url, { transferRef: transferRef })
                    }
                    ToolTip.visible: (hovered && !WalletManager.walletInstance(snapshot.wallet_name))
                    ToolTip.text: qsTr("Connect Wallet")
                    ToolTip.delay: 500
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
                    text: qsTr("Delete")
                    iconSource: "/resources/icons/trash-bin-2.svg"
                    variant: "error"
                    implicitHeight: 32
                    onClicked: confirmDelete.open()
                }
            }
        }
    }

    Dialog {
        id: confirmDelete
        modal: true
        title: ""
        anchors.centerIn: parent
        width: Math.min(380, root.width * 0.9)
        height: Math.min(240, root.height * 0.8)

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
                source: "/resources/icons/danger-triangle.svg"
                width: 32
                height: 32
                color: themeManager.warningColor
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: qsTr("Delete Transfer?")
                font.pixelSize: 16
                font.weight: Font.Bold
                color: themeManager.textColor
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: qsTr("Are you sure you want to delete this transfer?")
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
                text: qsTr("This action cannot be undone.")
                font.pixelSize: 11
                color: themeManager.textSecondaryColor
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                AppButton {
                    text: qsTr("Cancel")
                    variant: "secondary"
                    Layout.fillWidth: true
                    onClicked: confirmDelete.close()
                }

                AppButton {
                    text: qsTr("Delete")
                    iconSource: "/resources/icons/trash-bin-2.svg"
                    variant: "error"
                    Layout.fillWidth: true
                    onClicked: {
                        if (TransferManager.deleteSavedTransfer(transferRef)) {
                            confirmDelete.close()
                            leftPanel.buttonClicked("SessionsOverview")
                        } else {

                            console.warn("Failed to delete transfer:", transferRef)
                        }
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
                text: qsTr("Are you sure you want to decline this transfer?")
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
                text: qsTr("Declining will mark this transfer as rejected and stop any further processing.")
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
                        if (TransferManager.declineIncomingTransfer(transferRef)) {
                            confirmDecline.close()
                            parseSnapshot()
                        } else {
                            console.warn("Failed to decline transfer:", transferRef)
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        parseSnapshot()
    }
}
