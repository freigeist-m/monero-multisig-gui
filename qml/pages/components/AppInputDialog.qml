import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: control

    property string titleText: ""
    property string descriptionText: ""
    property string placeholderText: ""
    property string inputText: ""
    property bool isPassword: false
    property string confirmButtonText: qsTr("OK")
    property string cancelButtonText: qsTr("Cancel")
    property string errorText: ""
    property string iconSource: ""

    signal accepted(string text)

    modal: true
    anchors.centerIn: parent
    width: 300
    height: descriptionText !== "" ? 200 : 180

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

        AppInput {
            id: inputField
            Layout.fillWidth: true
            placeholderText: control.placeholderText
            text: inputText
            echoMode: isPassword ? TextInput.Password : TextInput.Normal
            iconSource: control.iconSource
            errorText: control.errorText
            onAccepted: acceptButton.clicked()
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
                id: acceptButton
                text: confirmButtonText
                enabled: inputField.text.length > 0
                onClicked: {
                    control.accepted(inputField.text)
                    control.close()
                }
            }
        }
    }

    onOpened: {
        inputField.text = inputText
        inputField.forceActiveFocus()
    }
}
