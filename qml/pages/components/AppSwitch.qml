import QtQuick 2.15
import QtQuick.Controls 2.15

Switch {
    id: control

    property string label: ""

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                           implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                            implicitContentHeight + topPadding + bottomPadding)


    indicator: Rectangle {
        implicitWidth: 36
        implicitHeight: 20
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: 10
        color: control.checked ? themeManager.primaryColor : themeManager.borderColor
        border.color: control.checked ? themeManager.primaryColor : themeManager.borderColor

        Behavior on color {
            ColorAnimation { duration: 150 }
        }

        Rectangle {
            x: control.checked ? parent.width - width - 2 : 2
            y: 2
            width: 16
            height: 16
            radius: 8
            color: "#ffffff"
            border.color: themeManager.borderColor

            Behavior on x {
                NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
            }
        }
    }

    contentItem: Text {
        text: control.label || control.text
        font.pixelSize: 12
        color: themeManager.textColor
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
