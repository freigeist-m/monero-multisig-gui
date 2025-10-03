import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects

TextField {
    id: control

    property string errorText: ""
    property bool hasError: errorText !== ""
    property string iconSource: ""

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                           contentWidth + leftPadding + rightPadding, 200)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                            contentHeight + topPadding + bottomPadding, 28)

    leftPadding: iconSource !== "" ? 28 : 8
    rightPadding: 8
    topPadding: 6
    bottomPadding: 6

    font.pixelSize: 12
    color: themeManager.textColor
    placeholderTextColor: themeManager.textSecondaryColor

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 28
        radius: 2
        color: themeManager.backgroundColor
        border.color: getBorderColor()
        border.width: 1

        Behavior on border.color {
            ColorAnimation { duration: 150 }
        }
    }

    Item {
        width: 14
        height: 14
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: iconSource !== ""

        Image {
            id: leftIconImage
            anchors.fill: parent
            source: iconSource !== "" ? ("qrc:" + iconSource) : ""
            fillMode: Image.PreserveAspectFit
            smooth: true
            sourceSize.width: 28
            sourceSize.height: 28
            visible: false
        }

        ColorOverlay {
            anchors.fill: leftIconImage
            source: leftIconImage
            color: themeManager.textSecondaryColor
        }
    }

    function getBorderColor() {
        if (hasError) return themeManager.errorColor
        if (activeFocus) return themeManager.primaryColor
        if (hovered) return themeManager.textSecondaryColor
        return themeManager.borderColor
    }
}
