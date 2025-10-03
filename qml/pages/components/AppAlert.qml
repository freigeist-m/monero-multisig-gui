import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: control

    property string text: ""
    property string variant: "info"
    property bool closable: false

    signal closed()

    implicitWidth: textItem.implicitWidth + 16
    implicitHeight: textItem.implicitHeight + 8

    color: getBackgroundColor()
    border.color: getBorderColor()
    border.width: 1
    radius: 2

    Text {
        id: textItem
        anchors.left: parent.left
        anchors.right: closable ? closeButton.left : parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 8
        text: control.text
        color: getTextColor()
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    AppIconButton {
        id: closeButton
        visible: closable
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        iconSource: "/resources/icons/close-circle.svg"
        size: 12
        iconColor: getTextColor()
        onClicked: {
            control.visible = false
            control.closed()
        }
    }

    function getBackgroundColor() {
        switch(variant) {
            case "success": return themeManager.darkMode ? "#1b5e201a" : "#e8f5e8"
            case "warning": return themeManager.darkMode ? "#663c001a" : "#fff3cd"
            case "error": return themeManager.darkMode ? "#5f2c2c1a" : "#f8d7da"
            default: return themeManager.darkMode ? "#0c4a6e1a" : "#cce7ff"
        }
    }

    function getBorderColor() {
        switch(variant) {
            case "success": return themeManager.successColor
            case "warning": return themeManager.warningColor
            case "error": return themeManager.errorColor
            default: return themeManager.primaryColor
        }
    }

    function getTextColor() {
        switch(variant) {
            case "success": return themeManager.successColor
            case "warning": return themeManager.warningColor
            case "error": return themeManager.errorColor
            default: return themeManager.primaryColor
        }
    }
}
