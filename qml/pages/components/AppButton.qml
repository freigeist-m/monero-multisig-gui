import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects

Button {
    id: control

    property string iconSource: ""
    property int iconSize: 14
    property string variant: "primary"
    property bool loading: false

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                           contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                            contentItem.implicitHeight + topPadding + bottomPadding)

    leftPadding: 8
    rightPadding: 8
    topPadding: 6
    bottomPadding: 6

    font.pixelSize: 12
    font.weight: Font.Normal

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight

        Row {
            id: contentRow
            spacing: 4
            anchors.centerIn: parent

            Image {
                id: iconImage
                width: iconSize
                height: iconSize
                visible: iconSource !== "" && !loading
                anchors.verticalCenter: parent.verticalCenter
                source: iconSource !== "" ? ("qrc:" + iconSource) : ""
                fillMode: Image.PreserveAspectFit
                smooth: true
                sourceSize.width: iconSize * 2
                sourceSize.height: iconSize * 2

                layer.enabled: iconSource !== ""
                layer.effect: ColorOverlay {
                    color: getTextColor()
                }
            }

            Text {
                text: control.text
                font: control.font
                color: getTextColor()
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    background: Rectangle {
        implicitWidth: 60
        implicitHeight: 28
        radius: 2
        color: getBackgroundColor()
        border.color: getBorderColor()
        border.width: variant === "secondary" ? 1 : 0

        Behavior on color {
            ColorAnimation { duration: 100 }
        }
    }

    function getBackgroundColor() {
        if (!enabled) return themeManager.disabledColor
        if (pressed) return getPressedColor()
        if (hovered) return getHoverColor()
        return getBaseColor()
    }

    function getBaseColor() {
        switch(variant) {
            case "primary": return themeManager.primaryColor
            case "secondary": return themeManager.surfaceColor
            case "success": return themeManager.successColor
            case "warning": return themeManager.warningColor
            case "error": return themeManager.errorColor
            default: return themeManager.primaryColor
        }
    }

    function getHoverColor() {
        switch(variant) {
            case "primary": return Qt.darker(themeManager.primaryColor, 1.1)
            case "secondary": return themeManager.hoverColor
            case "success": return Qt.darker(themeManager.successColor, 1.1)
            case "warning": return Qt.darker(themeManager.warningColor, 1.1)
            case "error": return Qt.darker(themeManager.errorColor, 1.1)
            default: return Qt.darker(themeManager.primaryColor, 1.1)
        }
    }

    function getPressedColor() {
        switch(variant) {
            case "primary": return Qt.darker(themeManager.primaryColor, 1.2)
            case "secondary": return themeManager.pressedColor
            case "success": return Qt.darker(themeManager.successColor, 1.2)
            case "warning": return Qt.darker(themeManager.warningColor, 1.2)
            case "error": return Qt.darker(themeManager.errorColor, 1.2)
            default: return Qt.darker(themeManager.primaryColor, 1.2)
        }
    }

    function getTextColor() {
        if (!enabled) return themeManager.textSecondaryColor
        if (variant === "secondary") return themeManager.textColor
        return "#ffffff"
    }

    function getBorderColor() {
        if (variant === "secondary") {
            if (hovered || pressed) return themeManager.primaryColor
            return themeManager.borderColor
        }
        return "transparent"
    }
}
