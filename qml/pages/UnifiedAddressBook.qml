import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "components"

Page {
    id: root
    title: qsTr("Address Book")

    property int currentTab: 0 // 0: Peer, 1: Trusted Peers, 2: XMR , 3: Daemon

    background: Rectangle {
        color: themeManager.backgroundColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 0

        Text {
            text: qsTr("Address Book")
            font.pixelSize: 20
            font.weight: Font.Bold
            color: themeManager.textColor
            Layout.alignment: Qt.AlignLeft
            Layout.bottomMargin: 8
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0

            AppNavButton {
                text: qsTr("Peer Address Book")
                variant: currentTab === 0 ? "primary" : "navigation"
                Layout.fillWidth: true
                onClicked: currentTab = 0
            }

            AppNavButton {
                text: qsTr("Trusted Peers")
                variant: currentTab === 1 ? "primary" : "navigation"
                Layout.fillWidth: true
                onClicked: currentTab = 1
            }

            AppNavButton {
                text: qsTr("XMR Addresses")
                variant: currentTab === 2 ? "primary" : "navigation"
                Layout.fillWidth: true
                onClicked: currentTab = 2
            }

            AppNavButton { text: qsTr("Daemon Nodes");
                variant: currentTab === 3 ? "primary" : "navigation";
                Layout.fillWidth: true;
                onClicked: currentTab = 3
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: themeManager.borderColor
            Layout.topMargin: 4
            Layout.bottomMargin: 8
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: currentTab

            PeerAddressBookTab {
            }

            TrustedPeersTab {
            }

            XMRAddressBookTab {
            }

            DaemonAddressBookTab { }
        }
    }
}
