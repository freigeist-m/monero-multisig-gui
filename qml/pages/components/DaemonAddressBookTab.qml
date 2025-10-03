import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Item {
    id: root

    ListModel { id: daemonModel }

    property string validationError: ""
    property int editingIndex: -1


    function isValidHost(u) {
        if (typeof u !== "string") return false;
        const s = u.trim().toLowerCase();
        if (s === "") return false;

        let hostname = s;
        let hasProtocol = false;

        if (s.startsWith("http://")) {
            hostname = s.substring(7).split("/")[0].split(":")[0];
            hasProtocol = true;
        } else if (s.startsWith("https://")) {
            hostname = s.substring(8).split("/")[0].split(":")[0];
            hasProtocol = true;
        }

        if (hostname === "localhost") return true;

        const ipv4 = /^(25[0-5]|2[0-4]\d|[01]?\d\d?)\.(25[0-5]|2[0-4]\d|[01]?\d\d?)\.(25[0-5]|2[0-4]\d|[01]?\d\d?)\.(25[0-5]|2[0-4]\d|[01]?\d\d?)$/;
        if (ipv4.test(hostname)) return true;

        const onion = /^[a-z2-7]{56}\.onion$/;
        if (onion.test(hostname)) return true;

        // hostname validation
        const hn = /^(?=.{1,253}$)(?!-)[a-z0-9-]+(\.[a-z0-9-]+)*$/;
        return hn.test(hostname);
    }

    function isDuplicate(url, port) {
        if (editingIndex >= 0) return false;
        const u = (url || "").trim();
        const p = parseInt(port) || 0;
        for (let i = 0; i < daemonModel.count; i++) {
            const it = daemonModel.get(i);
            if (String(it.url).trim().toLowerCase() === u.toLowerCase() &&
                    parseInt(it.port) === p)
                return true;
        }
        return false;
    }

    function validateForm(strict) {
        strict = strict === true;
        validationError = "";

        const lbl = labelField.text.trim();
        const url = urlField.text.trim();
        const port = Math.max(1, Math.min(65535, parseInt(portField.text) || 0));

        if (strict && lbl.length === 0) {
            validationError = qsTr("Label cannot be empty");
        } else if (strict && !isValidHost(url)) {
            validationError = qsTr("Invalid URL format. Use hostname, IP, .onion address, or full URL with http:// or https://");
        } else if (strict && (port < 1 || port > 65535)) {
            validationError = qsTr("Port must be 1–65535");
        } else if (strict && isDuplicate(url, port)) {
            validationError = qsTr("This daemon (URL+port) already exists");
        }

        const ok = lbl.length > 0 && isValidHost(url) && (port >= 1 && port <= 65535);
        return ok && validationError === "";
    }

    // ── data ops ────────────────────────────────────────────────────────
    function loadFromAccount() {
        daemonModel.clear();
        if (!accountManager || !accountManager.is_authenticated) return;
        try {
            const raw = accountManager.getDaemonAddressBook();
            for (let i = 0; i < raw.length; i++) {
                const it = raw[i];
                const lbl = it.label !== undefined ? it.label : (Array.isArray(it) ? it[0] : "");
                const url = it.url   !== undefined ? it.url   : (Array.isArray(it) ? it[1] : "");
                const port = it.port !== undefined ? it.port  : (Array.isArray(it) ? it[2] : 0);
                if (String(url).trim() !== "" && (parseInt(port) > 0)) {
                    daemonModel.append({ label: String(lbl), url: String(url), port: parseInt(port) });
                }
            }
        } catch (e) { /* ignore */ }
    }

    function addOrUpdate() {
        if (!validateForm(true)) return;

        const lbl  = labelField.text.trim();
        const url  = urlField.text.trim();
        const port = Math.max(1, Math.min(65535, parseInt(portField.text) || 0));

        let ok = false;
        try {
            if (editingIndex >= 0) {
                const old = daemonModel.get(editingIndex);
                ok = accountManager.removeDaemonAddressBookEntry(old.url, parseInt(old.port));
                if (ok) ok = accountManager.addDaemonAddressBookEntry(lbl, url, port);
                if (ok) {
                    daemonModel.setProperty(editingIndex, "label", lbl);
                    daemonModel.setProperty(editingIndex, "url", url);
                    daemonModel.setProperty(editingIndex, "port", port);
                    clearForm();
                } else {

                    accountManager.addDaemonAddressBookEntry(old.label, old.url, parseInt(old.port));
                    validationError = qsTr("Backend refused – operation failed");
                }
            } else {
                ok = accountManager.addDaemonAddressBookEntry(lbl, url, port);
                if (ok) {
                    daemonModel.append({ label: lbl, url: url, port: port });
                    clearForm();
                } else {
                    validationError = qsTr("Backend refused – operation failed");
                }
            }
        } catch (e) {
            validationError = qsTr("Error: ") + e.toString();
        }
    }

    function editRow(i) {
        const it = daemonModel.get(i);
        labelField.text = it.label;
        urlField.text   = it.url;
        portField.text  = String(it.port);
        editingIndex = i;
    }

    function removeRow(i) {
        const it = daemonModel.get(i);
        if (accountManager.removeDaemonAddressBookEntry(it.url, parseInt(it.port)))
            daemonModel.remove(i);
    }

    function clearForm() {
        labelField.text = "";
        urlField.text   = "";
        portField.text  = "18081";
        editingIndex = -1;
        validationError = "";
    }


    ColumnLayout {
        anchors.fill: parent
        spacing: 8


        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                text: editingIndex >= 0 ? qsTr("Edit Daemon") : qsTr("Add New Daemon")
                font.pixelSize: 14
                font.weight: Font.Medium
                color:editingIndex >= 0 ? themeManager.warningColor : themeManager.textColor
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: editingIndex >= 0 ? themeManager.warningColor :  themeManager.borderColor
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 8

                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true

                    AppInput {
                        id: urlField
                        placeholderText: qsTr("URL (e.g., localhost, http://example.com)")
                        Layout.fillWidth: true
                        font.pixelSize: 10
                        errorText: {
                            const t = text.trim();
                            if (t !== "" && !isValidHost(t))
                                return qsTr("Invalid URL format");
                            return "";
                        }
                    }

                    AppInput {
                        id: portField
                        placeholderText: "18081"
                        Layout.preferredWidth: 80
                        text: "18081"
                        validator: IntValidator { bottom: 1; top: 65535 }
                        inputMethodHints: Qt.ImhDigitsOnly
                    }

                    AppInput {
                        id: labelField
                        placeholderText: qsTr("Label (e.g. Local, Remote)")
                        Layout.preferredWidth: 160
                        errorText: text.trim() === "" && text.length > 0 ? qsTr("Label cannot be empty") : ""
                    }
                }


                Text {
                    text: qsTr("Supported formats: localhost, 192.168.1.1, example.com, http://example.com, https://abc123...onion")
                    color: themeManager.textSecondaryColor
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }


                AppAlert {
                    text: validationError
                    variant: "error"
                    visible: validationError !== ""
                    Layout.fillWidth: true
                }


                RowLayout {
                    Layout.fillWidth: true

                    Item { Layout.fillWidth: true }

                    AppButton {
                        text: editingIndex >= 0 ? qsTr("Update") : qsTr("Add")
                        enabled: validateForm(false)
                        onClicked: addOrUpdate()
                        variant: editingIndex >= 0 ?  "warning" : "primary"

                    }

                    AppButton {
                        text: qsTr("Cancel")
                        variant: "secondary"
                        visible: editingIndex >= 0
                        onClicked: clearForm()
                    }
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
                text: qsTr("Saved Daemons (%1)").arg(daemonModel.count)
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
                    id: lv
                    model: daemonModel
                    spacing: 6

                    delegate: Rectangle {
                        width: lv.width
                        height: delegateLayout.height + 12
                        color: themeManager.backgroundColor
                        border.color: themeManager.borderColor
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
                            }


                            TextInput {
                                id: addressText
                                text: model.url + ":" + model.port
                                readOnly: true
                                selectByMouse: true
                                font.family: "Monospace"
                                font.pixelSize: 10
                                color: themeManager.textSecondaryColor
                                Layout.fillWidth: true
                                clip: true
                            }


                            RowLayout {
                                Item { Layout.fillWidth: true }

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
                                    text: qsTr("Edit")
                                    variant: "secondary"
                                    onClicked: editRow(index)
                                }

                                AppButton {
                                    text: qsTr("Delete")
                                    variant: "error"
                                    onClicked: removeRow(index)
                                }
                            }
                        }
                    }
                }
            }


            Text {
                text: qsTr("No daemon endpoints saved")
                font.pixelSize: 12
                color: themeManager.textSecondaryColor
                Layout.alignment: Qt.AlignCenter
                Layout.topMargin: 20
                visible: daemonModel.count === 0
            }
        }
    }

    Connections {
        target: accountManager
        function onAddressDaemonBookChanged() { loadFromAccount() }
    }

    Component.onCompleted: loadFromAccount()
}
