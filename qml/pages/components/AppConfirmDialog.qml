import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: control

    property string titleText: ""
    property string messageText: ""
    property string confirmButtonText: qsTr("OK")
    property string cancelButtonText: qsTr("Cancel")
    property string confirmVariant: "primary"
    property string iconSource: ""

    signal accepted()
    signal rejected()

    modal: true
    anchors.centerIn: parent
    width: 300
    height: messageText !== "" ? 160 : 120

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

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: messageText !== ""

            AppIcon {
                source: iconSource
                width: 16
                height: 16
                color: getIconColor()
                visible: iconSource !== ""
            }

            Text {
                text: messageText
                font.pixelSize: 12
                color: themeManager.textColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }

        Item { Layout.fillHeight: true }

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
                variant: confirmVariant
                onClicked: {
                    control.accepted()
                    control.close()
                }
            }
        }
    }

    function getIconColor() {
        switch(confirmVariant) {
            case "error": return themeManager.errorColor
            case "warning": return themeManager.warningColor
            case "success": return themeManager.successColor
            default: return themeManager.primaryColor
        }
    }
}
