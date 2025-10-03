import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: control

    property string titleText: ""
    property string descriptionText: ""
    property alias model: listView.model
    property alias delegate: listView.delegate
    property string confirmButtonText: qsTr("Select")
    property string cancelButtonText: qsTr("Cancel")
    property var selectedItem: null
    property int selectedIndex: -1

    signal itemSelected(var item, int index)
    signal rejected()

    modal: true
    anchors.centerIn: parent
    width: Math.min(500, parent.width * 0.9)
    height: 400

    background: Rectangle {
        color: themeManager.surfaceColor
        border.color: themeManager.borderColor
        border.width: 1
        radius: 2
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Text {
            text: titleText
            font.pixelSize: 14
            font.weight: Font.Medium
            color: themeManager.textColor
            visible: titleText !== ""
        }

        Text {
            text: descriptionText
            font.pixelSize: 12
            color: themeManager.textSecondaryColor
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: descriptionText !== ""
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ListView {
                id: listView
                spacing: 4

                // Handle selection
                onCurrentIndexChanged: {
                    selectedIndex = currentIndex
                    if (currentIndex >= 0 && model) {
                        selectedItem = model.get ? model.get(currentIndex) : model[currentIndex]
                    } else {
                        selectedItem = null
                    }
                }
            }
        }

        Text {
            text: qsTr("No items available")
            font.pixelSize: 12
            color: themeManager.textSecondaryColor
            Layout.alignment: Qt.AlignCenter
            visible: listView.count === 0
        }

        RowLayout {
            Layout.alignment: Qt.AlignCenter
            spacing: 8

            AppButton {
                text: cancelButtonText
                variant: "secondary"
                onClicked: {
                    control.rejected()
                    control.close()
                }
            }

            AppButton {
                text: confirmButtonText
                enabled: selectedItem !== null
                onClicked: {
                    control.itemSelected(selectedItem, selectedIndex)
                    control.close()
                }
            }
        }
    }

    onOpened: {
        selectedItem = null
        selectedIndex = -1
        listView.currentIndex = -1
    }
}
