import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import "components"

Page {
    id: root
    title: qsTr("Account Management")

    property bool jsonVisible: false
    property string jsonText: ""
    property bool readOnly: true
    property string selectedNetwork: (accountManager.networkType || "mainnet" )


    property int __pendingPort: 0
    property bool hasNetworkChanges: (
        (daemonUrlField.text || "") !== (accountManager.daemon_url || "") ||
        (parseInt(daemonPortField.text) || 0) !== (accountManager.daemon_port || 0) ||
        useTorSwitch.checked !== accountManager.use_tor_for_daemon || selectedNetwork !== (accountManager.networkType  || "mainnet")
    )
    property bool hasAnyChanges: (
        hasNetworkChanges ||
        inspectGuardSwitch.checked !== accountManager.inspect_guard || torAutoSwitch.checked !== accountManager.tor_autoconnect || (parseInt(lockTimeoutField.text) || 0) !== (accountManager.lock_timeout_minutes || 0)
    )

    function isLoopback(url) {
        const s = (url || "").trim().toLowerCase()

        let hostname = s;
        if (s.startsWith("http://")) {
            hostname = s.substring(7).split("/")[0].split(":")[0];
        } else if (s.startsWith("https://")) {
            hostname = s.substring(8).split("/")[0].split(":")[0];
        }

        return hostname === "localhost" || hostname === "127.0.0.1" ||
               s.startsWith("http://localhost") || s.startsWith("http://127.0.0.1") ||
               s.startsWith("https://localhost") || s.startsWith("https://127.0.0.1")
    }

    function isValidDaemonUrl(url) {
        if (typeof url !== "string") return false;
        const s = url.trim().toLowerCase();
        if (s === "") return false;

        let hostname = s;
        if (s.startsWith("http://")) {
            hostname = s.substring(7).split("/")[0].split(":")[0];
        } else if (s.startsWith("https://")) {
            hostname = s.substring(8).split("/")[0].split(":")[0];
        }
        if (hostname === "localhost") return true;

        const ipv4 = /^(25[0-5]|2[0-4]\d|[01]?\d\d?)\.(25[0-5]|2[0-4]\d|[01]?\d\d?)\.(25[0-5]|2[0-4]\d|[01]?\d\d?)\.(25[0-5]|2[0-4]\d|[01]?\d\d?)$/;
        if (ipv4.test(hostname)) return true;

        const onion = /^[a-z2-7]{56}\.onion$/;
        if (onion.test(hostname)) return true;

        const hn = /^(?=.{1,253}$)(?!-)[a-z0-9-]+(\.[a-z0-9-]+)*$/;
        return hn.test(hostname);
    }

    function isOnionAddress(url) {
        if (typeof url !== "string") return false;
        const s = url.trim().toLowerCase();

        let hostname = s;
        if (s.startsWith("http://")) {
            hostname = s.substring(7).split("/")[0].split(":")[0];
        } else if (s.startsWith("https://")) {
            hostname = s.substring(8).split("/")[0].split(":")[0];
        }

        const onion = /^[a-z2-7]{56}\.onion$/;
        return onion.test(hostname);
    }

    ListModel { id: daemonPickerModel }

    function daemonPickerModelReload() {
        daemonPickerModel.clear()
        if (!accountManager || !accountManager.is_authenticated) return
        const raw = accountManager.getDaemonAddressBook() || []
        for (let i = 0; i < raw.length; i++) {
            const it = raw[i]
            daemonPickerModel.append({
                label: String(it.label || ""),
                url:   String(it.url   || ""),
                port:  parseInt(it.port || 0)
            })
        }
    }

    background: Rectangle { color: themeManager.backgroundColor }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            id: mainColumn
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            Text {
                text: qsTr("Account Management")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: themeManager.textColor
                Layout.alignment: Qt.AlignLeft
                Layout.bottomMargin: 4
            }

            // Account Information
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text { text: "Account Information"; font.pixelSize: 14; font.weight: Font.Medium; color: themeManager.textColor }
                Rectangle { Layout.fillWidth: true; height: 1; color: themeManager.borderColor }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 8

                    Text { text: "Account Name:"; color: themeManager.textSecondaryColor; font.pixelSize: 12; Layout.preferredWidth: 120 }
                    Text {
                        text: accountManager.current_account || "Not logged in"
                        color: themeManager.textColor
                        font.weight: Font.Medium
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    Text { text: "Inspect Guard:"; color: themeManager.textSecondaryColor; font.pixelSize: 12; Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 120 }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: inspectGuardSwitch.height

                        AppSwitch {
                            id: inspectGuardSwitch
                            checked: accountManager.inspect_guard
                            anchors.left: parent.left
                            onToggled: (checked) => { /* staged change only */ }

                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Aways request user confirmation if fee > 0.5% of transfer amount")
                            ToolTip.delay: 500
                        }
                    }

                    Text { text: "Start Tor on Login:"; color: themeManager.textSecondaryColor; font.pixelSize: 12; Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 120 }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: torAutoSwitch.height

                        AppSwitch {
                            id: torAutoSwitch
                            checked: accountManager.tor_autoconnect
                            anchors.left: parent.left
                            onToggled: (checked) => { /* staged change only */ }
                        }
                    }

                    Text { text: "Auto-lock (minutes):"; color: themeManager.textSecondaryColor; font.pixelSize: 12; Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 120 }
                    AppInput {
                        id: lockTimeoutField
                        text: String(accountManager.lock_timeout_minutes || 0)
                        implicitWidth: 100
                        placeholderText: "0 = never"
                        validator: IntValidator { bottom: 0; top: 1440 }
                        inputMethodHints: Qt.ImhDigitsOnly
                        errorText: {
                            const v = parseInt(text)
                            if (isNaN(v) || v < 0 || v > 1440) return qsTr("Enter 0–1440")
                            return ""
                        }
                    }


                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Text { text: "Daemon Settings"; font.pixelSize: 14; font.weight: Font.Medium; color: themeManager.textColor }
                Rectangle { Layout.fillWidth: true; height: 1; color: themeManager.borderColor }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 8

                    Text { text: "Daemon URL:"; color: themeManager.textSecondaryColor; font.pixelSize: 12; Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 120 }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        AppInput {
                            id: daemonUrlField
                            text: accountManager.daemon_url
                            Layout.fillWidth: true
                            placeholderText: "Enter daemon URL (e.g., http://localhost or https://example.com)"
                            errorText: {
                                const t = text.trim();
                                if (t !== "" && !isValidDaemonUrl(t))
                                    return qsTr("Invalid URL format");
                                return "";
                            }
                            onTextChanged: {
                                if (isOnionAddress(text)) {
                                    useTorSwitch.checked = true;
                                }
                            }
                        }
                        AppIconButton {
                            iconSource: "/resources/icons/book-bookmark.svg"
                            size: 18
                            onClicked: { daemonPickerModelReload(); daemonPicker.open() }
                        }
                    }

                    Text { text: "Daemon Port:"; color: themeManager.textSecondaryColor; font.pixelSize: 12; Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 120 }
                    AppInput {
                        id: daemonPortField
                        text: accountManager.daemon_port.toString()
                        implicitWidth: 100
                        placeholderText: "Port (1-65535)"
                        validator: IntValidator { bottom: 1; top: 65535 }
                        inputMethodHints: Qt.ImhDigitsOnly
                    }

                    Text { text: "Use Tor for Daemon:"; color: themeManager.textSecondaryColor; font.pixelSize: 12; Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 120 }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: useTorSwitch.height

                        AppSwitch {
                            id: useTorSwitch
                            checked: accountManager.use_tor_for_daemon
                            anchors.left: parent.left
                            enabled: !isOnionAddress(daemonUrlField.text)
                            onCheckedChanged:  {
                                if (checked && isLoopback(daemonUrlField.text)) {

                                    useTorSwitch.checked = false
                                    statusAlert.text = "Tor proxy doesn't apply to local daemons (127.0.0.1)."
                                    statusAlert.variant = "warning"
                                    statusAlert.visible = true
                                    statusTimer.restart()
                                }
                            }
                        }
                    }


                Text {
                    text: "Network:"
                    color: themeManager.textSecondaryColor
                    font.pixelSize: 12
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 120
                }
                ComboBox {
                    id: networkCombo
                    Layout.fillWidth: false
                    model: [ "mainnet", "stagenet", "testnet" ]
                    // initialize from page property
                    currentIndex: {
                        const want = (selectedNetwork || "mainnet").toLowerCase()
                        const idx = model.indexOf(want)
                        return idx >= 0 ? idx : 0
                    }
                    onCurrentIndexChanged: {
                        selectedNetwork = model[currentIndex]
                    }

                    Layout.preferredWidth: 100

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

                    // optional: help text
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Select Monero network")
                    ToolTip.delay: 400
                }


                }

                AppAlert {

                    Layout.fillWidth: true
                    visible: isOnionAddress(daemonUrlField.text)
                    variant: "warning"
                    text: qsTr("Tor service is required for .onion daemon addresses")
                }

                AppAlert {
                    id: networkChangeWarning
                    Layout.fillWidth: true
                    visible: hasNetworkChanges
                    variant: "warning"
                    text: qsTr("Changing network, daemon URL/port or Tor routing will disconnect all open wallets and background sync, and may require a short resync. Click 'Save Settings' to apply.")
                }
            }



            // Actions
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Text { text: "Actions"; font.pixelSize: 14; font.weight: Font.Medium; color: themeManager.textColor }
                Rectangle { Layout.fillWidth: true; height: 1; color: themeManager.borderColor }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 8



                    AppButton {
                        text: "Change Password"
                        variant: "secondary"
                        enabled: accountManager.is_authenticated
                        onClicked: changePasswordDialog.open()
                    }

                    AppButton {
                        text: "Show Account Data"
                        variant: "secondary"
                        enabled: accountManager.is_authenticated
                        onClicked: pwdDialog.open()
                    }

                    AppButton {
                        text: "Save Settings"
                        visible: accountManager.is_authenticated && hasAnyChanges
                        enabled: accountManager.is_authenticated && hasAnyChanges
                        variant: "warning"
                        onClicked: attemptSave()
                    }

                    Item { Layout.fillWidth: true }

                    AppButton {
                        text: "Logout"
                        variant: "error"
                        enabled: accountManager.is_authenticated
                        onClicked: accountManager.logout()
                    }
                }
            }


            // App info
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4

                Rectangle { Layout.fillWidth: true; height: 1; color: themeManager.borderColor }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 8

                    Text { text: "Version:"; color: themeManager.textSecondaryColor; font.pixelSize: 12; Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 120 }
                    Text { text: "v" + Qt.application.version ; color: themeManager.textSecondaryColor; font.pixelSize: 12; Layout.alignment: Qt.AlignVCenter; Layout.preferredWidth: 120 }


                }

            }

            AppAlert {
                id: statusAlert
                Layout.fillWidth: true
                visible: false
                closable: true
            }
            AppAlert {
                visible: accountManager.networkType === "testnet" || accountManager.networkType === "stagenet"
                text: qsTr("%1 mode. Make sure a %2 daemon is connected.").arg(accountManager.networkType).arg(accountManager.networkType)
                closable: true
                Layout.fillWidth: true
            }

            Timer { id: statusTimer; interval: 5000; onTriggered: statusAlert.visible = false }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 12 }
        }
    }

    AppFormDialog {
        id: confirmNetworkChange
        titleText: qsTr("Apply Network Changes")
        confirmButtonText: qsTr("Apply Changes")
        confirmEnabled: true

        content: [
            Text {
                text: qsTr("Changing the network, daemon URL/port or enabling/disabling Tor will:")
                color: themeManager.textColor
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                Layout.fillWidth: true
            },
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 4
                Text { text: "• Disconnect all open wallets"; color: themeManager.textSecondaryColor; font.pixelSize: 12 }
                Text { text: "• Pause/stop background sync"; color: themeManager.textSecondaryColor; font.pixelSize: 12 }
                Text { text: "• Possibly trigger a brief resync"; color: themeManager.textSecondaryColor; font.pixelSize: 12 }
            },
            Text {
                text: qsTr("Make sure your Tor service is running if you enable Tor.")
                color: themeManager.textSecondaryColor
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                Layout.fillWidth: true
                Layout.topMargin: 8
            }
        ]

        onAccepted: doSave(__pendingPort)

        onOpened: {
            errorText = ""
        }
    }

    AppFormDialog {
        id: pwdDialog
        titleText: qsTr("Enter Password")
        confirmButtonText: qsTr("View Account Data")
        confirmEnabled: pwdField.text.length > 0

        content: [
            Text {
                text: qsTr("Enter your password to view account data")
                color: themeManager.textColor
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            },
            AppInput {
                id: pwdField
                Layout.fillWidth: true
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                iconSource: "/resources/icons/lock-password.svg"
                onAccepted: {
                    if (pwdField.text.length > 0) {
                        pwdDialog.accepted()
                    }
                }
            }
        ]

        onAccepted: {
            if (accountManager.verifyPassword(pwdField.text)) {
                jsonText = accountManager.loadAccountData()
                if (typeof leftPanel !== "undefined") leftPanel.buttonClicked("AccountDataPage")
                errorText = ""
            } else {
                errorText = qsTr("Incorrect password")
                pwdDialog.open()
            }
        }

        onOpened: {
            pwdField.text = ""
            errorText = ""
            pwdField.forceActiveFocus()
        }
    }

    // Change Password dialog using AppFormDialog
    AppFormDialog {
        id: changePasswordDialog
        titleText: qsTr("Change Password")
        confirmButtonText: qsTr("Change Password")
        confirmEnabled: currentPwdField.text.length > 0 && newPwdField1.text.length > 0 && newPwdField1.text === newPwdField2.text

        content: [
            AppInput {
                id: currentPwdField
                Layout.fillWidth: true
                placeholderText: qsTr("Current password")
                echoMode: TextInput.Password
                iconSource: "/resources/icons/lock-password.svg"
            },
            AppInput {
                id: newPwdField1
                Layout.fillWidth: true
                placeholderText: qsTr("New password")
                echoMode: TextInput.Password
                iconSource: "/resources/icons/key.svg"
            },
            AppInput {
                id: newPwdField2
                Layout.fillWidth: true
                placeholderText: qsTr("Confirm new password")
                echoMode: TextInput.Password
                iconSource: "/resources/icons/key.svg"
                errorText: newPwdField1.text !== newPwdField2.text && newPwdField2.text.length > 0 ? qsTr("Passwords don't match") : ""
            }
        ]

        onAccepted: {
            const ok = accountManager.updatePassword(currentPwdField.text, newPwdField1.text)
            if (ok) {
                statusAlert.text = qsTr("Password changed successfully")
                statusAlert.variant = "success"
                statusAlert.visible = true
                statusTimer.restart()
                errorText = ""
            } else {
                errorText = qsTr("Failed to change password")
                changePasswordDialog.open()
            }
        }

        onOpened: {
            currentPwdField.text = ""
            newPwdField1.text = ""
            newPwdField2.text = ""
            errorText = ""
            currentPwdField.forceActiveFocus()
        }
    }

    AppAddressBookDialog {
        id: daemonPicker
        titleText: qsTr("Select Daemon")
        descriptionText: qsTr("Choose a daemon endpoint from your address book")
        model: daemonPickerModel
        addressBookType: "daemon"
        primaryField: "label"
        emptyStateText: qsTr("No daemon endpoints in your address book yet.")

        onItemSelected: function(item, index) {
            daemonUrlField.text = item.url
            daemonPortField.text = String(item.port)
        }

        onQuickAddRequested: function() {
            const tabMap = { "peer": 0, "trusted": 1, "xmr": 2, "daemon": 3 };
            const tabIndex = tabMap[daemonPicker.addressBookType] || 0;

            var pageComponent = Qt.resolvedUrl("UnifiedAddressBook.qml");
            middlePanel.currentPageUrl = pageComponent;
            middlePanel.stackView.replace(pageComponent, { currentTab: tabIndex });
        }
    }


    // Connections
    Connections {
        target: accountManager
        function onSettingsChanged() {
            inspectGuardSwitch.checked = accountManager.inspect_guard
            daemonUrlField.text = accountManager.daemon_url
            daemonPortField.text = accountManager.daemon_port.toString()
            useTorSwitch.checked = accountManager.use_tor_for_daemon
            torAutoSwitch.checked = accountManager.tor_autoconnect
            lockTimeoutField.text = String(accountManager.lock_timeout_minutes || 0)
            selectedNetwork = accountManager.networkType || "mainnet"
        }
        function onErrorOccurred(err) {
            statusAlert.text = err
            statusAlert.variant = "error"
            statusAlert.visible = true
            statusTimer.restart()
        }
        function onAddressDaemonBookChanged() { daemonPickerModelReload() }
    }

    // NEW: save helpers
    function attemptSave() {
        __pendingPort = Math.max(1, parseInt(daemonPortField.text) || 18081)
        if (hasNetworkChanges) {
            confirmNetworkChange.open()
        } else {
            doSave(__pendingPort)
        }
    }

    function doSave(portValue) {
        const ok = accountManager.updateSettings(
            inspectGuardSwitch.checked,
            daemonUrlField.text,
            portValue,
            useTorSwitch.checked,
            torAutoSwitch.checked,
            Math.max(0, parseInt(lockTimeoutField.text) || 0),
            selectedNetwork
        )
        statusAlert.text = ok ? "Settings saved successfully" : "Failed to save settings"
        statusAlert.variant = ok ? "success" : "error"
        statusAlert.visible = true
        statusTimer.restart()
    }
}
