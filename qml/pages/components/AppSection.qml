import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15


Rectangle {
    id: control

    property alias content: contentContainer.children
    property string title: ""

    implicitWidth: 200
    implicitHeight: headerLayout.height + contentContainer.childrenRect.height + 16

    color: themeManager.backgroundColor
    border.color: themeManager.borderColor
    border.width: 1
    radius: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            id: headerLayout
            Layout.fillWidth: true
            visible: title !== ""

            Text {
                text: title
                font.pixelSize: 14
                font.weight: Font.Medium
                color: themeManager.textColor
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: themeManager.borderColor
                Layout.leftMargin: 8
            }
        }

        Item {
            id: contentContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: childrenRect.height
        }
    }
}
