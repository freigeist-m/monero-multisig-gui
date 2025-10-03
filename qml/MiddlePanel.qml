import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: middlePanel
    width: parent.width - 150
    height: parent.height
    color: themeManager.backgroundColor

    property alias stackView: stackView

    StackView {
        id: stackView
        anchors.fill: parent
        anchors.margins: 1
        clip: true
        initialItem: Qt.resolvedUrl("pages/AccountPage.qml")

        pushEnter: Transition {}
        pushExit: Transition {}
        popEnter: Transition {}
        popExit: Transition {}
        replaceEnter: Transition {
            NumberAnimation {
                target: stackView.contentItem
                property: "x"
                from: -stackView.width
                to: 0
                duration: 150
                easing.type: Easing.InCubic
            }
        }
        replaceExit: Transition {}
    }
}
