import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Column {
    id: expandableNavButton

    property string text: ""
    property string iconSource: ""
    property bool expanded: false
    property alias subButtons: subButtonsColumn.children

    property bool active: false

    signal clicked()
    signal subButtonClicked(string pageName)
    signal collapseRequested()

    width: parent.width
    spacing: 0

    function collapse() {
        expanded = false
    }

    // Main button
    AppNavButton {
        id: mainButton
        text: expandableNavButton.text
        iconSource: expandableNavButton.iconSource
        width: parent.width
        active: expandableNavButton.active
        // onActiveChanged: if (active) expanded = true

        onClicked: {
            expandableNavButton.expanded = !expandableNavButton.expanded
            expandableNavButton.clicked()
        }
    }

    Column {
        id: subButtonsColumn
        width: parent.width
        spacing: 0

        height: expanded ? implicitHeight : 0
        clip: true
        opacity: expanded ? 1.0 : 0.0
        visible: expanded

        Behavior on height {
            NumberAnimation {
                duration: 200
                easing.type: Easing.OutQuart
            }
        }

        Behavior on opacity {
            NumberAnimation {
                duration: 150
                easing.type: Easing.OutQuart
            }
        }
    }
}
