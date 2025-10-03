import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects

Button {
    id: control

    property string backText: qsTr("Back")

    implicitWidth: contentRow.implicitWidth + 16
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                            implicitContentHeight + topPadding + bottomPadding)

    leftPadding: 8
    rightPadding: 12
    topPadding: 6
    bottomPadding: 6

    background: Rectangle {
        implicitWidth: 60
        implicitHeight: 28
        radius: 2
        color: getBackgroundColor()
        border.color: themeManager.borderColor
        border.width: 1

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }

    contentItem: RowLayout {
        id: contentRow
        spacing: 4
        anchors.centerIn: parent

        Item {
            width: 12
            height: 12

            Image {
                id: backIconImage
                anchors.fill: parent
                source: "qrc:/resources/icons/arrow-left.svg"
                fillMode: Image.PreserveAspectFit
                smooth: true
                sourceSize.width: 24
                sourceSize.height: 24
                visible: true
            }

            ColorOverlay {
                anchors.fill: backIconImage
                source: backIconImage
                color: themeManager.textColor
            }
        }

        Text {
            text: backText
            font.pixelSize: 12
            color: themeManager.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    function getBackgroundColor() {
        if (pressed) return themeManager.pressedColor
        if (hovered) return themeManager.hoverColor
        return "transparent"
    }
}
