import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects

Button {
    id: control

    property string iconSource: ""
    property int size: 16
    property color iconColor: themeManager.iconColor

    implicitWidth: size + 8
    implicitHeight: size + 8

    background: Rectangle {
        implicitWidth: control.size + 8
        implicitHeight: control.size + 8
        radius: 1
        color: getBackgroundColor()
        border.color: themeManager.borderColor
        border.width: hovered ? 1 : 0
    }

    contentItem: Item {
        width: size
        height: size
        anchors.centerIn: parent

        Image {
            id: iconImage
            anchors.fill: parent
            source: iconSource !== "" ? ("qrc:" + iconSource) : ""
            fillMode: Image.PreserveAspectFit
            smooth: true
            sourceSize.width: size * 2
            sourceSize.height: size * 2
            visible: iconSource !== ""
        }

        ColorOverlay {
            anchors.fill: iconImage
            source: iconImage
            color: iconColor
            visible: iconSource !== ""
        }
    }

    function getBackgroundColor() {
        if (pressed) return themeManager.pressedColor
        if (hovered) return themeManager.hoverColor
        return "transparent"
    }
}
