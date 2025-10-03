import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: control

    property alias content: contentContainer.children
    property string title: ""
    property bool elevated: false

    implicitWidth: 200
    implicitHeight: titleLabel.height + contentContainer.childrenRect.height + 40

    color: themeManager.surfaceColor
    border.color: themeManager.borderColor
    border.width: elevated ? 0 : 1
    radius: 8

    Rectangle {
        anchors.fill: parent
        anchors.margins: -2
        radius: parent.radius + 2
        color: "transparent"
        border.color: themeManager.darkMode ? "#00000040" : "#00000020"
        border.width: elevated ? 1 : 0
        z: -1
        visible: elevated
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        Text {
            id: titleLabel
            text: title
            font.pixelSize: 16
            font.weight: Font.Medium
            color: themeManager.textColor
            visible: title !== ""
            Layout.fillWidth: true
        }

        Item {
            id: contentContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: childrenRect.height
        }
    }
}
