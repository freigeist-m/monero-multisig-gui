import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: control

    property string titleText: ""
    property alias content: contentContainer.children
    property string confirmButtonText: qsTr("OK")
    property string cancelButtonText: qsTr("Cancel")
    property bool confirmEnabled: true
    property string errorText: ""


    modal: true
    anchors.centerIn: parent
    width: 320
    height: Math.min(400, contentLayout.implicitHeight + 60)

    background: Rectangle {
        color: themeManager.surfaceColor
        border.color: themeManager.borderColor
        border.width: 1
        radius: 2
    }

    ColumnLayout {
        id: contentLayout
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

        ColumnLayout {
            id: contentContainer
            Layout.fillWidth: true
            spacing: 8
        }

        AppAlert {
            Layout.fillWidth: true
            visible: errorText !== ""
            variant: "error"
            text: errorText
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
                enabled: confirmEnabled
                onClicked: {
                    control.accepted()
                    control.close()
                }
            }
        }
    }
}
