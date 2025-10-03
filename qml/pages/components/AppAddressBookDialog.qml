// AppAddressBookDialog.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: control


    property string titleText: ""
    property string descriptionText: ""
    property alias model: listView.model
    property string confirmButtonText: qsTr("Select")
    property string cancelButtonText: qsTr("Cancel")
    property string emptyStateText: qsTr("No items available")


    property string addressBookType: "peer"
    property bool showQuickAddButton: true

    property var selectedItem: null
    property int selectedIndex: -1


    property string primaryField: "label"
    property string secondaryField: "address"
    property string iconSource: "/resources/icons/check-circle.svg"
    property color iconColor: themeManager.successColor
    property bool showStatusIndicator: false
    property string statusField: "online"

    signal itemSelected(var item, int index)
    signal quickAddRequested(string addressBookType)

    modal: true
    anchors.centerIn: parent
    width: Math.min(420, parent ? parent.width * 0.9 : 420)
    height: 320

    background: Rectangle {
        color: themeManager.surfaceColor
        border.color: themeManager.borderColor
        border.width: 1
        radius: 2
    }

    contentItem: ColumnLayout {
        spacing: 8

        Text {
            text: titleText
            font.pixelSize: 14
            font.weight: Font.Medium
            color: themeManager.textColor
            visible: titleText !== ""
            Layout.alignment: Qt.AlignHCenter
        }

        Text {
            text: descriptionText
            color: themeManager.textSecondaryColor
            font.pixelSize: 12
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: descriptionText !== ""
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ListView {
                id: listView
                spacing: 6

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 44
                    color: themeManager.backgroundColor
                    border.color: selectedIndex === index ? themeManager.accentColor : themeManager.borderColor
                    border.width: selectedIndex === index ? 2 : 1
                    radius: 2

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8

                        AppIcon {
                            source: "/resources/icons/shield-network.svg"
                            width: 16
                            height: 16
                            color: showStatusIndicator && model[statusField] ? themeManager.successColor : themeManager.textSecondaryColor
                            visible: showStatusIndicator
                            Layout.alignment: Qt.AlignCenter
                        }

                        // Text content
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: model[primaryField] || ""
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                color: themeManager.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                text: formatSecondaryText(model)
                                font.family: "Monospace"
                                font.pixelSize: 10
                                color: themeManager.textSecondaryColor
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }

                        AppStatusIndicator {
                            status: showStatusIndicator && model[statusField] ? "online" : "offline"
                            dotSize: 5
                            visible: showStatusIndicator
                            Layout.alignment: Qt.AlignCenter
                        }

                        AppIconButton {
                            iconSource: control.iconSource
                            iconColor: selectedIndex === index ? themeManager.accentColor : control.iconColor
                            size: 16
                            Layout.alignment: Qt.AlignCenter
                            onClicked: selectItem(index)
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: selectItem(index)
                    }
                }
            }
        }

        Text {
            visible: listView.count === 0
            text: emptyStateText
            color: themeManager.textSecondaryColor
            font.pixelSize: 12
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 12
            Layout.bottomMargin: 12
        }


        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppButton {
                text: qsTr("Add New")
                iconSource: "/resources/icons/add-circle.svg"
                variant: "secondary"
                visible: showQuickAddButton
                implicitHeight: 28
                onClicked: {
                    control.quickAddRequested(addressBookType)
                    control.close()
                }
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: cancelButtonText
                variant: "secondary"
                onClicked: {
                    control.rejected()
                    control.close()
                }
            }

            AppButton {
                text: confirmButtonText
                enabled: selectedItem !== null
                onClicked: {
                    control.itemSelected(selectedItem, selectedIndex)
                    control.close()
                }
            }
        }
    }


    function formatSecondaryText(modelItem) {

        if (addressBookType === "daemon") {
            return (modelItem.url || "") + ":" + (modelItem.port || "")
        } else if (addressBookType === "xmr") {
            return modelItem.xmr_address || modelItem[secondaryField] || ""
        } else {
            return modelItem[secondaryField] || ""
        }
    }

    function selectItem(index) {
        selectedIndex = index
        if (index >= 0 && listView.model) {
            selectedItem = listView.model.get ? listView.model.get(index) : listView.model[index]

            control.itemSelected(selectedItem, selectedIndex)
            control.close()
        }
    }


    function getTabIndex(type) {
        switch(type) {
        case "peer": return 0
        case "trusted": return 1
        case "xmr": return 2
        case "daemon": return 3
        default: return 0
        }
    }


    onOpened: {
        selectedItem = null
        selectedIndex = -1
    }


    onQuickAddRequested: function(type) {
        if (typeof leftPanel !== "undefined") {
            leftPanel.buttonClicked("UnifiedAddressBook")
        }
    }
}
