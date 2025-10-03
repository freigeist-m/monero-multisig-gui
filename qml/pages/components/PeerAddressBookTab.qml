import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Item {
    id: root


    ListModel { id: addressModel }

    property string validationError: ""
    property int editingIndex: -1
    property string originalOnion: ""


    function isValidOnion(addr) {
        return /^[a-z0-9]{56}\.onion$/.test(String(addr).trim().toLowerCase());
    }

    function isDuplicateOnion(addr) {
        if (editingIndex >= 0) return false;

        const normalizedAddr = addr.trim().toLowerCase();
        for (let i = 0; i < addressModel.count; i++) {
            if (addressModel.get(i).onion === normalizedAddr) {
                return true;
            }
        }
        return false;
    }

    function validateForm(strict) {
        strict = strict === true;
        validationError = "";

        const lbl = labelField.text.trim();
        const on  = onionField.text.trim().toLowerCase();

        if (strict && lbl.length === 0) {
            validationError = qsTr("Label cannot be empty");
        } else if (strict && !isValidOnion(on)) {
            validationError = qsTr("Invalid onion address format");
        } else if (strict && editingIndex < 0) {

            for (let i = 0; i < addressModel.count; ++i) {
                if (addressModel.get(i).onion === on) {
                    validationError = qsTr("Address already exists");
                    break;
                }
            }
        }
        const hasErrors = validationError !== "" || lbl.length === 0 || !isValidOnion(on) || isDuplicateOnion(onionField.text);
        return !hasErrors;
    }

    function loadBook() {

        addressModel.clear();
        if (!accountManager || !accountManager.is_authenticated) {
            console.log("PeerAddressBookTab: not authenticated, skipping load");
            return;
        }
        try {
            const raw = accountManager.getAddressBook();

            for (let i = 0; i < raw.length; ++i) {
                const itm = raw[i];
                addressModel.append({
                    label: String(itm.label),
                    onion: String(itm.onion).toLowerCase()
                });
            }

        } catch (e) {
            console.log("PeerAddressBookTab: Error loading addresses:", e);
        }
    }

    function clearForm() {
        labelField.text = "";
        onionField.text = "";
        editingIndex = -1;
        originalOnion = "";
        validationError = "";
    }

    function startEdit(index) {
        const item = addressModel.get(index);
        labelField.text = item.label;
        onionField.text = item.onion;
        editingIndex = index;
        originalOnion = item.onion;
        validationError = "";
    }

    function removeEntry(index) {
        const item = addressModel.get(index);
        if (accountManager.removeAddressBookEntry(item.onion)) {
            addressModel.remove(index);
            if (index === editingIndex) clearForm();
        }
    }

    function addOrUpdate() {
        if (!validateForm(true)) return;

        const lbl = labelField.text.trim();
        const on  = onionField.text.trim().toLowerCase();

        if (editingIndex >= 0) {

            const ok = accountManager.updateAddressBookEntry(originalOnion, lbl, on);
            if (ok) {
                addressModel.setProperty(editingIndex, "label", lbl);
                addressModel.setProperty(editingIndex, "onion", on);
                clearForm();
            } else {
                validationError = qsTr("Backend refused – update failed");
            }
        } else {

            if (accountManager.addAddressBookEntry(lbl, on)) {
                addressModel.append({ label: lbl, onion: on });
                clearForm();
            } else {
                validationError = qsTr("Backend refused – add failed");
            }
        }
    }


    ColumnLayout {
        anchors.fill: parent
        spacing: 8


        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                text: editingIndex >= 0 ? qsTr("Edit Peer Address") : qsTr("Add New Peer Address")
                font.pixelSize: 14
                font.weight: Font.Medium
                color: editingIndex >= 0 ? themeManager.warningColor :   themeManager.textColor
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

                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true

                    AppInput {
                        id: onionField
                        placeholderText: qsTr("56-char v3 onion + .onion")
                        Layout.fillWidth: true
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

                    AppInput {
                        id: labelField
                        placeholderText: qsTr("Peer label")
                        Layout.preferredWidth: 160
                        errorText: text.trim() === "" && text.length > 0 ? qsTr("Label cannot be empty") : ""
                    }
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
                text: qsTr("Saved Peer Addresses (%1)").arg(addressModel.count)
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
                    model: addressModel
                    spacing: 6

                    delegate: Rectangle {
                        width: listView.width
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
                                text: model.onion
                                font.family: "Monospace"
                                font.pixelSize: 10
                                color: themeManager.textSecondaryColor
                                Layout.fillWidth: true
                                readOnly: true
                                selectByMouse: true
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
                                    onClicked: startEdit(index)
                                }

                                AppButton {
                                    text: qsTr("Delete")
                                    variant: "error"
                                    onClicked: removeEntry(index)
                                }
                            }
                        }
                    }
                }
            }

            Text {
                text: qsTr("No peer addresses saved")
                font.pixelSize: 12
                color: themeManager.textSecondaryColor
                Layout.alignment: Qt.AlignCenter
                Layout.topMargin: 20
                visible: addressModel.count === 0
            }
        }
    }

    Component.onCompleted: loadBook()
}
