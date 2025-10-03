import QtQuick 2.15
import QtQuick.Controls 2.15

CheckBox {
    id: control

    property string variant: "default"


    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                           implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                            implicitContentHeight + topPadding + bottomPadding)


    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: 2
        color: control.checked ? getCheckedColor() : themeManager.backgroundColor
        border.color: control.checked ? getCheckedColor() : themeManager.borderColor
        border.width: 1

        Rectangle {
            width: 8
            height: 8
            anchors.centerIn: parent
            radius: 0
            color: "#ffffff"
            visible: control.checked
        }

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    contentItem: Text {
        text: control.text
        font.pixelSize: 12
        color: themeManager.textColor
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }

    function getCheckedColor() {
        switch(variant) {
            case "warning": return themeManager.warningColor
            default: return themeManager.primaryColor
        }
    }
}
