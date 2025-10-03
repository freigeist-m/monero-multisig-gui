import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects

Button {
    id: control

    property string iconSource: ""
    property int iconSize: 16
    property string variant: "navigation"
    property string textAlignment: "left"
    property int buttonHeight: 36
    property string dot_status: "pending"
    property bool dot_visible: true

    property bool active: false

    implicitWidth: parent ? parent.width : 200
    implicitHeight: buttonHeight

    background: Rectangle {
        anchors.fill: parent
        radius: 0
        color: getBackgroundColor()

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 3
            color: themeManager.primaryColor
            visible: control.active && control.variant === "navigation"
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: themeManager.borderColor
            opacity: 0.2
            visible: variant === "navigation"
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: themeManager.borderColor
            opacity: 0.4
            visible: variant === "navigation"
        }

        // Behavior on color {
        //     enabled: !control.hovered
        //     ColorAnimation { duration: 150 }
        // }
    }

    contentItem: Item {
        anchors.fill: parent
        anchors.leftMargin: getLeftMargin()
        anchors.rightMargin: 16

        Row {
            id: contentRow
            spacing: 12
            anchors.verticalCenter: parent.verticalCenter


            anchors.left: (textAlignment === "left" || textAlignment === "indent") ? parent.left : undefined
            anchors.horizontalCenter: textAlignment === "center" ? parent.horizontalCenter : undefined
            anchors.right: textAlignment === "right" ? parent.right : undefined


            Item {
                width: iconSize
                height: iconSize
                visible: iconSource !== ""
                anchors.verticalCenter: parent.verticalCenter

                Image {
                    id: iconImage
                    anchors.fill: parent
                    source: iconSource !== "" ? ("qrc:" + iconSource) : ""
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    antialiasing: true
                    sourceSize.width: iconSize * 2
                    sourceSize.height: iconSize * 2
                    visible: iconSource !== ""
                }

                ColorOverlay {
                    anchors.fill: iconImage
                    source: iconImage
                    color: getTextColor()
                    visible: iconSource !== ""
                }
            }


            Text {
                text: control.text
                font.pixelSize: 14
                font.weight: Font.Normal
                color: getTextColor()
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: getTextHorizontalAlignment()
                verticalAlignment: Text.AlignVCenter
            }


            AppDotIndicator {
                anchors.verticalCenter: parent.verticalCenter
                status: dot_status
                visible: dot_visible
                dotSize: 6
            }
        }
    }

    function getLeftMargin() {
        switch(textAlignment) {
            case "indent": return 32
            default: return 16
        }
    }

    function getBackgroundColor() {
        if (!enabled) return themeManager.disabledColor

        switch(variant) {
            case "navigation":
                if (pressed) return themeManager.pressedColor
                if (hovered || control.active) return themeManager.hoverColor
                return "transparent"
            case "primary":
                if (pressed) return Qt.darker(themeManager.primaryColor, 1.2)
                if (hovered) return Qt.darker(themeManager.primaryColor, 1.1)
                return themeManager.primaryColor
            case "secondary":
                if (pressed) return themeManager.pressedColor
                if (hovered) return themeManager.hoverColor
                return themeManager.surfaceColor
            default:
                return "transparent"
        }
    }

    function getTextColor() {
        if (!enabled) return themeManager.disabledColor

        switch(variant) {
            case "navigation": return themeManager.textColor
            case "primary": return "#ffffff"
            case "secondary": return themeManager.textColor
            default: return themeManager.textColor
        }
    }

    function getTextHorizontalAlignment() {
        switch(textAlignment) {
            case "left": return Text.AlignLeft
            case "center": return Text.AlignHCenter
            case "right": return Text.AlignRight
            case "indent": return Text.AlignLeft
            default: return Text.AlignLeft
        }
    }
}
