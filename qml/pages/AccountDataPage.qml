import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects
import "components"

Page {
    id: root
    title: qsTr("Account Data")

    property string jsonText: ""
    property bool readOnly: true

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            AppBackButton {
                backText: qsTr("Back to Account")
                onClicked: {
                    leftPanel.buttonClicked("AccountPage")
                }
            }

            Text {
                text: qsTr("Account Data (JSON)")
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
                text: "JSON Editor Tools"
                font.pixelSize: 14
                font.weight: Font.Medium
                color: themeManager.textColor
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: themeManager.borderColor
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 8

                AppSwitch {
                    id: readOnlySwitch
                    label: qsTr("Read‑only")
                    checked: root.readOnly
                    onToggled: root.readOnly = readOnlySwitch.checked
                }

                AppButton {
                    text: qsTr("Format JSON")
                    variant: "secondary"
                    enabled: !root.readOnly
                    onClicked: {
                        try {
                            var parsed = JSON.parse(jsonArea.text)
                            jsonArea.text = JSON.stringify(parsed, null, 2)
                            jsonText = jsonArea.text
                        } catch(e) {
                            statusAlert.text = qsTr("Invalid JSON format")
                            statusAlert.variant = "error"
                            statusAlert.visible = true
                            statusTimer.restart()
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                AppCopyButton {
                    textToCopy: jsonText
                    visible: jsonText.length > 0
                    size: 14
                }

                AppButton {
                    text: qsTr("Save JSON")
                    enabled: !root.readOnly && jsonText.length > 0
                    onClicked: {
                        try {
                            JSON.parse(jsonArea.text)
                            const ok = accountManager.saveAccountData(jsonArea.text)
                            statusAlert.text = ok ? qsTr("JSON saved successfully") : qsTr("Failed to save JSON")
                            statusAlert.variant = ok ? "success" : "error"
                            statusAlert.visible = true
                            statusTimer.restart()
                            if (ok) {
                                jsonText = jsonArea.text
                                loadAccountData()
                            }
                        } catch (e) {
                            statusAlert.text = qsTr("Cannot save - Invalid JSON format")
                            statusAlert.variant = "error"
                            statusAlert.visible = true
                            statusTimer.restart()
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            spacing: 4

            Text {
                text: "JSON Content"
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
                Layout.fillHeight: true
                Layout.topMargin: 8
                color: themeManager.backgroundColor
                border.color: themeManager.borderColor
                border.width: 1
                radius: 2

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true

                    ScrollBar.vertical.policy: ScrollBar.AsNeeded
                    ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                    ScrollBar.vertical.interactive: true
                    ScrollBar.horizontal.interactive: true

                    TextArea {
                        id: jsonArea
                        text: jsonText
                        wrapMode: TextEdit.NoWrap
                        font.family: "Courier New, monospace"
                        font.pixelSize: 10
                        readOnly: root.readOnly
                        selectByMouse: true
                        selectByKeyboard: true
                        color: themeManager.textColor
                        selectionColor: themeManager.primaryColor
                        selectedTextColor: "#ffffff"

                        background: Rectangle {
                            color: "transparent"
                        }

                        onTextChanged: {
                            if (!root.readOnly) {
                                jsonText = text
                            }
                        }
                        property bool isValidJson: {
                            if (text.trim() === "") return true
                            try {
                                JSON.parse(text)
                                return true
                            } catch (e) {
                                return false
                            }
                        }
                    }
                }


                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.margins: 6
                    width: counterText.width + 8
                    height: counterText.height + 6
                    color: themeManager.surfaceColor
                    opacity: 0.9
                    radius: 2

                    Text {
                        id: counterText
                        anchors.centerIn: parent
                        text: qsTr("Lines: %1 | Chars: %2").arg(jsonArea.text.split('\n').length).arg(jsonArea.text.length)
                        font.pixelSize: 9
                        color: themeManager.textSecondaryColor
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.margins: 6
                    width: validationText.width + 8
                    height: validationText.height + 6
                    color: jsonArea.isValidJson ? themeManager.successColor : themeManager.errorColor
                    opacity: 0.9
                    radius: 2
                    visible: !root.readOnly && jsonArea.text.trim() !== ""

                    Text {
                        id: validationText
                        anchors.centerIn: parent
                        text: jsonArea.isValidJson ? qsTr("Valid JSON") : qsTr("Invalid JSON")
                        font.pixelSize: 9
                        color: "#ffffff"
                        font.weight: Font.Medium
                    }
                }
            }
        }

        AppAlert {
            id: statusAlert
            Layout.fillWidth: true
            visible: false
            closable: true
        }

        Timer {
            id: statusTimer
            interval: 5000
            onTriggered: statusAlert.visible = false
        }
    }

    function loadAccountData() {
        if (accountManager && accountManager.is_authenticated) {
            try {
                jsonText = accountManager.loadAccountData()
                jsonArea.text = jsonText
            } catch (e) {
                console.log("Error loading account data:", e)
                statusAlert.text = qsTr("Failed to load account data")
                statusAlert.variant = "error"
                statusAlert.visible = true
                statusTimer.restart()
            }
        }
    }

    Component.onCompleted: {
        loadAccountData()
    }
}
