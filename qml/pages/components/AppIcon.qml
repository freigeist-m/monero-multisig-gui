import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects

Item {
    id: root

    property string source: ""
    property color color: themeManager.iconColor
    property alias fillMode: image.fillMode
    property alias smooth: image.smooth
    property alias mipmap: image.mipmap

    implicitWidth: 24
    implicitHeight: 24


    Image {
        id: image
        anchors.fill: parent
        source: root.source !== "" ? ("qrc:" + root.source) : ""
        fillMode: Image.PreserveAspectFit
        smooth: true
        sourceSize.width: root.width * 2
        sourceSize.height: root.height * 2
        cache: true

    }

    ColorOverlay {
        anchors.fill: image
        source: image
        color: root.color
        visible: root.source !== ""
    }
}
