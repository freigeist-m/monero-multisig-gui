import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import "components"

Page {
    id: root
    title: qsTr("Tor Service")

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    property bool   torRunning:        torServer.online
    property bool   torInitializing:   torServer.initializing
    property bool   torInstalling:     torServer.installing
    property int    bootstrapProgress: torServer.bootstrapProgress
    property string currentStatus:     torServer.currentStatus
    property string downloadErrorCode: torServer.downloadErrorCode
    property string downloadErrorMsg:  torServer.downloadErrorMsg
    property var    identities: []
    property var    onlineOnions: torServer.onionAddresses

    function refreshIdentities() {
        try {
            identities = accountManager.getTorIdentities()
        } catch(e) {
            console.warn("getTorIdentities failed:", e)
        }
        onlineOnions = torServer.onionAddresses
    }

    Component.onCompleted: refreshIdentities()

    ScrollView {
        anchors.fill: parent
        anchors.margins: 8
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 8


            Text {
                text: qsTr("Tor Service")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: themeManager.textColor
                Layout.alignment: Qt.AlignLeft
                Layout.bottomMargin: 4
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "Service Control"
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
                    implicitHeight: controlLayout.implicitHeight + 16
                    color: themeManager.backgroundColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    ColumnLayout {
                        id: controlLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            AppStatusIndicator {
                                status: torRunning ? "online" :
                                        (torInstalling ? "pending" :
                                         (torInitializing ? "pending" : "offline"))
                                text: torInstalling ? "Downloading..." :
                                      torInitializing ? "Initializing..." :
                                      torRunning ? "Running" : "Stopped"
                                dotSize: 6
                            }

                            Item { Layout.fillWidth: true }

                            RowLayout {
                                spacing: 6
                                visible: !torInitializing && !torRunning && !torInstalling

                                AppSwitch {
                                    id: forceDownloadSwitch
                                    checked: false
                                }
                                Text {
                                    text: qsTr("Force download Tor binaries")
                                    color: themeManager.textColor
                                    font.pixelSize: 12
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            AppButton {
                                text: torInstalling ? qsTr("Downloading Tor...") :
                                                      torInitializing ? qsTr("Initializing Tor...") :
                                                      torRunning ? qsTr("Tor Service Running") :
                                                                   qsTr("Start Tor Service")
                                iconSource: torRunning ? "/resources/icons/check-circle.svg" : "/resources/icons/power.svg"
                                enabled: !torInitializing && !torRunning && !torInstalling
                                onClicked: torServer.start(forceDownloadSwitch.checked)
                                implicitHeight: 32
                                Layout.preferredWidth: 200
                            }

                            AppButton {
                                text: qsTr("Stop")
                                iconSource: "/resources/icons/stop-circle.svg"
                                variant: "error"
                                enabled: torRunning || torInitializing
                                implicitHeight: 32
                                onClicked: torServer.stopTorServer()
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: torInitializing || (bootstrapProgress > 0 && bootstrapProgress < 100)

                            Rectangle {
                                Layout.fillWidth: true
                                height: 6
                                color: themeManager.borderColor
                                radius: 3

                                Rectangle {
                                    width: parent.width * (bootstrapProgress / 100)
                                    height: parent.height
                                    color: themeManager.primaryColor
                                    radius: 3
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: currentStatus
                                    font.pixelSize: 12
                                    color: themeManager.textSecondaryColor
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    text: bootstrapProgress + "%"
                                    visible: bootstrapProgress > 0
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    color: themeManager.textColor
                                }
                            }


                        }



                            AppAlert {
                                visible: downloadErrorCode !== ""
                                Layout.fillWidth: true
                                // variant: "warning"
                                text: downloadErrorMsg
                                closable: true
                            }




                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4
                visible: torRunning

                Text {
                    text: "Service Information"
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
                    implicitHeight: infoLayout.implicitHeight + 16
                    color: themeManager.backgroundColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    ColumnLayout {
                        id: infoLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 8

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 16
                            rowSpacing: 6

                            Text {
                                text: qsTr("SOCKS Port:")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                                Layout.preferredWidth: 120
                            }
                            Text {
                                text: torServer.socksPort.toString()
                                color: themeManager.textColor
                                font.pixelSize: 12
                                font.family: "Monospace"
                                Layout.fillWidth: true
                            }

                            Text {
                                text: qsTr("Control Port:")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                            }
                            Text {
                                text: torServer.controlPort.toString()
                                color: themeManager.textColor
                                font.pixelSize: 12
                                font.family: "Monospace"
                                Layout.fillWidth: true
                            }

                            Text {
                                text: qsTr("Online Onions:")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                            }
                            Text {
                                text: qsTr("%1").arg(onlineOnions.length)|| qsTr("none")
                                color: themeManager.textColor
                                font.pixelSize: 12
                                font.family: "Monospace"
                                wrapMode: Text.WordWrap
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
                    text: "Onion Identities"
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
                        id: identitiesLayout
                        Layout.fillWidth: true
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            AppInput {
                                id: newLabel
                                Layout.fillWidth: true
                                placeholderText: qsTr("Label for new identity (required)")
                                iconSource: "/resources/icons/hashtag.svg"
                            }

                            AppButton {
                                text: qsTr("Create New Onion Identity")
                                iconSource: "/resources/icons/power.svg"
                                implicitHeight: 28
                                enabled: newLabel.text.trim().length > 0
                                onClicked: {
                                    torServer.addNewService(newLabel.text.trim())
                                    refreshIdentities()
                                    newLabel.text = ""
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Requires a Label and Tor running)")
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: themeManager.borderColor
                            opacity: 0.5
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: identities.length > 0

                            Text {
                                text: qsTr("Label")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textSecondaryColor
                                Layout.preferredWidth: 100
                            }

                            Text {
                                text: qsTr("Onion Identity")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textSecondaryColor
                                Layout.fillWidth: true
                            }

                            Text {
                                text: qsTr("Status")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textSecondaryColor
                                Layout.preferredWidth: 160
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                text: qsTr("Requests")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textSecondaryColor
                                Layout.preferredWidth: 30
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                text: qsTr("Actions")
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textSecondaryColor
                                Layout.preferredWidth: 120
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: themeManager.borderColor
                            opacity: 0.3
                            visible: identities.length > 0
                        }


                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Repeater {
                                model: identities
                                delegate: ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6

                                    property bool isEditing: false
                                    property string editingText: ""

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8


                                        AppInput {
                                            id: labelInput
                                            Layout.preferredWidth: 100
                                            text: isEditing ? editingText : (modelData.label || "")
                                            placeholderText: qsTr("Label")
                                            font.pixelSize: 11
                                            readOnly: !isEditing
                                            onTextChanged: if (isEditing) editingText = text
                                            onAccepted: saveEdit()

                                            function saveEdit() {
                                                const newLbl = editingText.trim()
                                                if (newLbl.length > 0 && newLbl !== (modelData.label || "")) {
                                                    accountManager.renameTorIdentity(modelData.onion, newLbl)
                                                    refreshIdentities()
                                                }
                                                isEditing = false
                                            }
                                        }

                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 28
                                            color: themeManager.backgroundColor
                                            border.color: themeManager.borderColor
                                            border.width: 1
                                            radius: 2

                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.margins: 6
                                                spacing: 6

                                                Text{
                                                    id: onionText
                                                    Layout.fillWidth: true
                                                    text: modelData.onion || qsTr("(pending)")
                                                    font.family: "Monospace"
                                                    font.pixelSize: 10
                                                    color: themeManager.textColor
                                                    clip: true
                                                    verticalAlignment: TextInput.AlignVCenter
                                                    wrapMode: Text.NoWrap
                                                    elide: Text.ElideMiddle
                                                }

                                                AppCopyButton {
                                                    textToCopy: onionText.text
                                                    size: 12
                                                    visible: onionText.text !== qsTr("(pending)")
                                                }
                                            }
                                        }


                                        RowLayout {
                                            spacing: 1
                                            Layout.preferredWidth: 160

                                            AppStatusIndicator {
                                                status: modelData.online ? "online" : "offline"
                                                text: modelData.online ? qsTr("Online") : qsTr("Offline")
                                                dotSize: 5
                                                Layout.alignment: Qt.AlignHCenter
                                            }

                                            AppSwitch {
                                                checked: !!modelData.online
                                                Layout.alignment: Qt.AlignHCenter
                                                onToggled: {
                                                    if ((modelData.onion || "").length === 0 && checked) {
                                                        torServer.addNewService(newLabel.text.trim())
                                                    } else if ((modelData.onion || "").length > 0) {
                                                        torServer.setServiceOnline(modelData.onion, checked)
                                                    }
                                                    refreshIdentities()
                                                }
                                            }
                                        }

                                        Text {

                                            text: (torServer.requestCounts[modelData.onion] || 0).toString()
                                            font.pixelSize: 12
                                            color: themeManager.textColor
                                            Layout.preferredWidth: 30
                                            horizontalAlignment: Text.AlignHCenter
                                        }


                                        RowLayout {
                                            spacing: 4
                                            Layout.preferredWidth: 120

                                            AppButton {
                                                text: isEditing ? qsTr("Save") : qsTr("Edit")
                                                variant: isEditing ? "primary" : "secondary"
                                                implicitHeight: 24
                                                implicitWidth: 50
                                                onClicked: {
                                                    if (isEditing) {
                                                        labelInput.saveEdit()
                                                    } else {
                                                        isEditing = true
                                                        editingText = modelData.label || ""
                                                        labelInput.forceActiveFocus()
                                                        labelInput.selectAll()
                                                    }
                                                }
                                            }

                                            AppButton {
                                                text: qsTr("Remove")
                                                variant: "error"
                                                implicitHeight: 24
                                                implicitWidth: 60
                                                enabled: (modelData.onion || "").length > 0 || true
                                                onClicked: {
                                                    if (isEditing) {
                                                        isEditing = false
                                                        editingText = ""
                                                    }
                                                    confirmRemove.openWith(modelData.onion, modelData.label || "")
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: identities.length === 0
                                text: qsTr("No identities yet. Create one above.")
                                color: themeManager.textSecondaryColor
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignCenter
                                Layout.topMargin: 12
                                Layout.bottomMargin: 12
                            }
                        }
                    }
                // }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("Add onion manually")
                    variant: "secondary"
                    implicitHeight: 28
                    onClicked: manualImportDialog.open()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Import an existing Tor identity (onion + ED25519-V3 key)")
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 12
            }
        }
    }

    Dialog {
        id: confirmRemove
        modal: true
        title: ""
        anchors.centerIn: parent
        width: Math.min(380, root.width * 0.9)
        height: Math.min(300, root.height * 0.8)

        property string identityOnion: ""
        property string identityLabel: ""

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
                text: qsTr("Remove Identity?")
                font.pixelSize: 16
                font.weight: Font.Bold
                color: themeManager.textColor
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: qsTr("This will permanently remove the Tor identity:")
                font.pixelSize: 12
                color: themeManager.textColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                text: confirmRemove.identityLabel || qsTr("(no label)")
                font.pixelSize: 12
                font.weight: Font.Medium
                color: themeManager.textColor
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
            }

            Text {
                text: qsTr("Please enter your account password to confirm:")
                font.pixelSize: 12
                color: themeManager.textColor
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
            }

            AppInput {
                id: passwordInput
                Layout.fillWidth: true
                placeholderText: qsTr("Account password")
                echoMode: TextInput.Password
                onAccepted: confirmButton.clicked()
            }

            Text {
                id: errorText
                text: ""
                font.pixelSize: 11
                color: themeManager.errorColor
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                visible: text !== ""
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                AppButton {
                    text: qsTr("Cancel")
                    variant: "secondary"
                    Layout.fillWidth: true
                    onClicked: confirmRemove.close()
                }

                AppButton {
                    id: confirmButton
                    text: qsTr("Remove")
                    iconSource: "/resources/icons/trash-bin-2.svg"
                    variant: "error"
                    Layout.fillWidth: true
                    enabled: passwordInput.text.trim().length > 0
                    onClicked: {
                        if (accountManager.verifyPassword(passwordInput.text)) {
                            if ((confirmRemove.identityOnion || "").length > 0) {
                                torServer.removeService(confirmRemove.identityOnion)
                            } else {
                                accountManager.removeTorIdentity("")
                            }
                            refreshIdentities()
                            confirmRemove.close()
                        } else {
                            errorText.text = qsTr("Incorrect password")
                        }
                    }
                }
            }
        }

        function openWith(onion, label) {
            identityOnion = onion || ""
            identityLabel = label || ""
            passwordInput.text = ""
            errorText.text = ""
            open()
            passwordInput.forceActiveFocus()
        }

        onClosed: {
            passwordInput.text = ""
            errorText.text = ""
        }
    }

    Dialog {
        id: manualImportDialog
        modal: true
        title: ""
        anchors.centerIn: parent
        width: Math.min(450, root.width * 0.9)
        height: Math.min(400, root.height * 0.8)

        background: Rectangle {
            color: themeManager.surfaceColor
            border.color: themeManager.borderColor
            border.width: 1
            radius: 2
        }

        property string err: ""

        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8


                Text {
                    text: qsTr("Import Tor Identity")
                    font.pixelSize: 16
                    font.weight: Font.Bold
                    color: themeManager.textColor
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: themeManager.borderColor
                opacity: 0.5
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                AppInput {
                    id: importLabel
                    Layout.fillWidth: true
                    placeholderText: qsTr("Label (optional)")
                    iconSource: "/resources/icons/hashtag.svg"
                }

                AppInput {
                    id: importOnion
                    Layout.fillWidth: true
                    placeholderText: qsTr("Onion address (56 chars + .onion)")
                    iconSource: "/resources/icons/shield-network.svg"
                    font.family: "Monospace"
                }

                AppInput {
                    id: importPriv
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60  // Make it taller for the long key
                    placeholderText: qsTr("Private key in format: ED25519-V3:<base64>")
                    iconSource: "/resources/icons/key.svg"
                    wrapMode: TextEdit.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: false

                    AppSwitch {
                        id: importOnline
                        checked: true
                    }

                    Text {
                        text: qsTr("Set online after import")
                        color: themeManager.textColor
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Text {
                text: manualImportDialog.err
                color: themeManager.errorColor
                font.pixelSize: 11
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                text: qsTr("After going online, confirm that you can reach the public address you provided.")
                color: themeManager.textSecondaryColor
                font.pixelSize: 11
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                text: qsTr("If the address is not reachable there might be a mistake in your onion address + private key combination")
                color: themeManager.textSecondaryColor
                font.pixelSize: 11
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Item {
                Layout.fillHeight: true
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: themeManager.borderColor
                opacity: 0.3
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                AppButton {
                    text: qsTr("Cancel")
                    variant: "secondary"
                    Layout.fillWidth: true
                    onClicked: manualImportDialog.close()
                }

                AppButton {
                    text: qsTr("Import")
                    iconSource: "/resources/icons/check-circle.svg"
                    Layout.fillWidth: true
                    enabled: importOnion.text.trim().length > 0 && importPriv.text.trim().length > 0
                    onClicked: {
                        manualImportDialog.err = ""
                        const ok = accountManager.importTorIdentity(
                                      importLabel.text.trim(),
                                      importOnion.text.trim(),
                                      importPriv.text.trim(),
                                      false
                                  )
                        if (!ok) {
                            manualImportDialog.err = qsTr("Import failed")
                            return
                        }
                        if (importOnline.checked) {
                            torServer.setServiceOnline(importOnion.text.trim(), true)
                        }
                        refreshIdentities()
                        manualImportDialog.close()
                        importLabel.text = ""
                        importOnion.text = ""
                        importPriv.text = ""
                        importOnline.checked = false
                    }
                }
            }
        }

        onClosed: {
            importLabel.text = ""
            importOnion.text = ""
            importPriv.text = ""
            importOnline.checked = false
            err = ""
        }
    }



    Connections {
        target: torServer

        function onErrorOccurred(msg) {
            // Error handling - backend will update currentStatus property
        }

        function onRunningChanged() {
            refreshIdentities()
        }

        function onOnionAddressChanged(addr) {
            refreshIdentities()
        }

        function onStarted() {
            refreshIdentities()
        }

        function onStopped() {
            refreshIdentities()
        }

        function onOnionAddressesChanged() {
            refreshIdentities()
        }
    }

    Connections {
        target: accountManager
        function onCurrentAccountChanged() { refreshIdentities() }
        function onSettingsChanged()       { refreshIdentities() }
        function onLogoutOccurred()        { identities = []; onlineOnions = [] }
        function onLoginSuccess()          { refreshIdentities() }
        function onErrorOccurred(e)        { console.warn("Account error:", e) }
    }
}
