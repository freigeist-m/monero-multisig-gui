import QtQuick 2.15
import QtQuick.Controls 2.15

AppIconButton {
    id: control

    property string textToCopy: ""
    property bool showFeedback: true

    iconSource: copied ? "/resources/icons/check-circle.svg" : "/resources/icons/copy.svg"
    iconColor: copied ? themeManager.successColor : themeManager.iconColor

    property bool copied: false

    signal copyCompleted()

    onClicked: {
        if (textToCopy.length === 0) return

        var temp = Qt.createQmlObject('import QtQuick 2.15; TextEdit { visible: false }', control)
        temp.text = textToCopy
        temp.selectAll()
        temp.copy()
        temp.destroy()

        if (showFeedback) {
            copied = true
            copyTimer.start()
        }

        copyCompleted()
    }

    Timer {
        id: copyTimer
        interval: 2000
        onTriggered: copied = false
    }

    ToolTip.text: copied ? qsTr("Copied!") : qsTr("Copy to clipboard")
    ToolTip.visible: hovered
}
