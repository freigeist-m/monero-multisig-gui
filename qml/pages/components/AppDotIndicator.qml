import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

RowLayout {
    id: control

    property string status: "unknown"
    property string text: ""
    property int dotSize: 8

    spacing: 8

    Rectangle {
        width: dotSize
        height: dotSize
        radius: dotSize / 2
        color: getStatusColor()
        Layout.alignment: Qt.AlignVCenter

        SequentialAnimation on opacity {
            running: status === "pending"
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 0.3; duration: 800 }
            NumberAnimation { from: 0.3; to: 1.0; duration: 800 }
        }
    }


    function getStatusColor() {
        switch(status) {
            case "online": return themeManager.successColor
            case "offline": return themeManager.errorColor
            case "pending": return themeManager.warningColor
            case "success": return themeManager.successColor
            case "error": return themeManager.errorColor
            case "primary": return themeManager.primaryColor
            default: return themeManager.textSecondaryColor
        }
    }

}
