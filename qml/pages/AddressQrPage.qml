import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root

    property string address: ""
    property string label: ""
    property int accountIndex: 0
    property int addressIndex: 0
    property string walletName: ""

    title: qsTr("Address QR Code")

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    header: Rectangle {
        height: 48
        color: themeManager.backgroundColor
        border.color: themeManager.borderColor
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            AppBackButton {
                onClicked: pageStack.pop()
            }

            Text {
                text: root.title
                color: themeManager.textColor
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 16

            // Address info section
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: qsTr("Address Information")
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: themeManager.borderColor
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6

                    Text {
                        text: qsTr("Wallet:")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                    }
                    Text {
                        text: walletName
                        color: themeManager.textColor
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTr("Label:")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                    }
                    Text {
                        text: label || qsTr("(no label)")
                        color: themeManager.textColor
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTr("Index:")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                    }
                    Text {
                        text: qsTr("%1/%2").arg(accountIndex).arg(addressIndex)
                        color: themeManager.textColor
                        font.pixelSize: 12
                        font.family: "Monospace"
                        Layout.fillWidth: true
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                Layout.alignment: Qt.AlignCenter

                Text {
                    text: qsTr("QR Code")
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                    Layout.alignment: Qt.AlignCenter
                }

                Rectangle {
                    Layout.preferredWidth: 240
                    Layout.preferredHeight: 240
                    Layout.alignment: Qt.AlignCenter
                    color: "white"
                    border.color: themeManager.borderColor
                    border.width: 2
                    radius: 2

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8

                        Rectangle {
                            width: 64
                            height: 64
                            color: themeManager.primaryColor
                            radius: 8
                            Layout.alignment: Qt.AlignCenter

                            Text {
                                anchors.centerIn: parent
                                text: "QR"
                                color: "white"
                                font.pixelSize: 20
                                font.weight: Font.Bold
                            }
                        }

                        Text {
                            text: qsTr("QR Code Here")
                            color: themeManager.textSecondaryColor
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignCenter
                        }

                        Text {
                            text: qsTr("Implement with qrcode.js\nor C++ libqrencode")
                            color: themeManager.textSecondaryColor
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignHCenter
                            Layout.alignment: Qt.AlignCenter
                        }
                    }
                }

                Text {
                    text: qsTr("Scan this QR code to get the address")
                    color: themeManager.textSecondaryColor
                    font.pixelSize: 11
                    Layout.alignment: Qt.AlignCenter
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: qsTr("Address Text")
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
                    Layout.preferredHeight: 80
                    color: themeManager.backgroundColor
                    border.color: themeManager.borderColor
                    border.width: 1
                    radius: 2

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true

                            TextArea {
                                id: addressTextArea
                                text: root.address
                                readOnly: true
                                wrapMode: TextEdit.Wrap
                                font.family: "Monospace"
                                font.pixelSize: 10
                                color: themeManager.textColor
                                selectByMouse: true

                                background: Rectangle {
                                    color: "transparent"
                                }
                            }
                        }

                        AppCopyButton {
                            textToCopy: root.address
                            size: 16
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 8

                AppButton {
                    text: qsTr("Copy Address")
                    variant: "secondary"
                    onClicked: {
                        addressTextArea.selectAll()
                        addressTextArea.copy()
                        addressTextArea.deselect()
                    }
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: qsTr("Close")
                    variant: "primary"
                    onClicked: pageStack.pop()
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 12
            }
        }
    }
}
