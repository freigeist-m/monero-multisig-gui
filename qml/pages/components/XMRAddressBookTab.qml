import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "."

Item {
    id: root

    ListModel { id: addressModel }

    property string validationError: ""
    property int editingIndex: -1

    function isValidMoneroAddress(addr) {
        if (typeof addr !== "string") return false;

        const a = addr.replace(/\s+/g, "");
        const B58 = "[123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz]";

        // normalize network from AccountManager (fallback to mainnet)
        const net = (accountManager.networkType || "mainnet").toLowerCase();

        // prefixes per network
        let stdPrefix, subPrefix, intPrefix;
        switch (net) {
        case "testnet":
            stdPrefix = "[9]";  // standard
            subPrefix = "[B]";   // subaddress
            intPrefix = "[A]";  // integrated
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

    function isDuplicateXMRAddress(addr) {
        if (editingIndex >= 0) return false;

        const normalizedAddr = addr.trim();
        for (let i = 0; i < addressModel.count; i++) {
            if (addressModel.get(i).xmr_address === normalizedAddr) {
                return true;
            }
        }
        return false;
    }

    function validateForm(strict) {
        strict = strict === true;
        validationError = "";

        const lbl = labelField.text.trim();
        const addr = xmrField.text.trim();

        if (strict && lbl.length === 0) {
            validationError = qsTr("Label cannot be empty");
        } else if (strict && !isValidMoneroAddress(addr)) {
            validationError = qsTr("Invalid XMR address format");

        } else if (strict && isDuplicateXMRAddress(addr)) {
            validationError = qsTr("Address already exists");
        }

        const hasErrors = validationError !== "" || lbl.length === 0 || !isValidMoneroAddress(addr);
        return !hasErrors;
    }


    function loadFromAccount() {

        addressModel.clear();
        if (!accountManager || !accountManager.is_authenticated) {

            return;
        }

        try {
            const raw = accountManager.getXMRAddressBook();

            for (let i = 0; i < raw.length; i++) {
                const item = raw[i];
                let lbl, addr;

                if (item && item.label !== undefined && item.xmr_address !== undefined) {
                    lbl = item.label;
                    addr = item.xmr_address;
                } else if (Array.isArray(item) && item.length >= 2) {
                    lbl = item[0];
                    addr = item[1];
                } else {
                    continue;
                }

                addressModel.append({
                                        label: String(lbl),
                                        xmr_address: String(addr)
                                    });
            }
        } catch (e) {
            console.log("XMRAddressBookTab: Error loading XMR addresses:", e);
        }
    }

    function addOrUpdateAddress() {
        if (!validateForm(true)) {
            return;
        }

        const lbl = labelField.text.trim();
        const addr = xmrField.text.trim();

        let success = false;
        try {
            if (editingIndex >= 0) {
                const oldItem = addressModel.get(editingIndex);
                success = accountManager.removeXMRAddressBookEntry(oldItem.xmr_address);
                if (success) {
                    success = accountManager.addXMRAddressBookEntry(lbl, addr);
                    if (success) {
                        addressModel.setProperty(editingIndex, "label", lbl);
                        addressModel.setProperty(editingIndex, "xmr_address", addr);
                    } else {
                        accountManager.addXMRAddressBookEntry(oldItem.label, oldItem.xmr_address);
                    }
                }
            } else {
                success = accountManager.addXMRAddressBookEntry(lbl, addr);
                if (success) {
                    addressModel.append({
                                            label: lbl,
                                            xmr_address: addr
                                        });
                }
            }

            if (success) {
                clearForm();
            } else {
                validationError = qsTr("Backend refused – operation failed");
            }
        } catch (e) {
            validationError = qsTr("Error: ") + e.toString();
        }
    }

    function editAddress(index) {
        const item = addressModel.get(index);
        labelField.text = item.label;
        xmrField.text = item.xmr_address;
        editingIndex = index;
    }

    function removeAddress(index) {
        const item = addressModel.get(index);
        if (accountManager.removeXMRAddressBookEntry(item.xmr_address)) {
            addressModel.remove(index);
        }
    }

    function clearForm() {
        labelField.text = "";
        xmrField.text = "";
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
                text: editingIndex >= 0 ? qsTr("Edit XMR Address") : qsTr("Add New XMR Address")
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

                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true

                    AppInput {
                        id: xmrField
                        placeholderText: qsTr("95 or 106 character Monero address")
                        Layout.fillWidth: true
                        font.family: "Monospace"
                        font.pixelSize: 10
                        errorText: {
                            const addr = text.trim();
                            if (addr !== "" && !isValidMoneroAddress(addr)) {
                                return qsTr("Invalid XMR address format");
                            }
                            if (addr !== "" && isDuplicateXMRAddress(addr)) {
                                return qsTr("Address already exists");
                            }
                            return "";
                        }
                    }

                    AppInput {
                        id: labelField
                        placeholderText: qsTr("e.g. Work, Exchange...")
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
                        id: addBtn
                        text: editingIndex >= 0 ? qsTr("Update") : qsTr("Add")
                        enabled: validateForm(false)
                        onClicked: addOrUpdateAddress()
                        variant: editingIndex >= 0 ?  "warning" : "primary"

                    }

                    AppButton {
                        id: cancelBtn
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
                text: qsTr("Saved XMR Addresses (%1)").arg(addressModel.count)
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
                                text: model.xmr_address
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
                                    onClicked: editAddress(index)
                                }

                                AppButton {
                                    text: qsTr("Delete")
                                    variant: "error"
                                    onClicked: removeAddress(index)
                                }
                            }
                        }
                    }
                }
            }

            Text {
                text: qsTr("No XMR addresses saved")
                font.pixelSize: 12
                color: themeManager.textSecondaryColor
                Layout.alignment: Qt.AlignCenter
                Layout.topMargin: 20
                visible: addressModel.count === 0
            }
        }
    }

    Component.onCompleted: {
        loadFromAccount();
    }
}
