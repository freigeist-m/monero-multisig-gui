import QtQuick 2.15
import QtQuick.Layouts 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects
import "components"

Page {
    id: landing

    background: Rectangle { color: themeManager.backgroundColor }

    readonly property bool hasMultisigSessions: multisigManager.sessionsKeys.length > 0
    readonly property bool hasNotifierSessions: multisigManager.notifierKeys.length > 0
    readonly property bool hasAnySessions: hasMultisigSessions || hasNotifierSessions

    function splitKey(key) {
        const p = (key || "").indexOf("|")
        if (p <= 0) return { onion: "", ref: key || "" }
        return { onion: key.slice(0, p), ref: key.slice(p + 1) }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 8
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 8

            // Header
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    text: qsTr("Multisig & Notifier Sessions")
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: themeManager.textColor
                    Layout.fillWidth: true
                }

                AppButton {
                    text: qsTr("New Standard Wallet")
                    iconSource: "/resources/icons/add-circle.svg"
                    variant: "secondary"
                    visible: hasAnySessions
                    onClicked: leftPanel.buttonClicked("NewStandardWallet")
                }

                AppButton {
                    text: qsTr("New Notifier")
                    iconSource: "/resources/icons/bell.svg"
                    variant: "secondary"
                    visible: hasAnySessions
                    onClicked: leftPanel.buttonClicked("NotifierSetup")
                }

                AppButton {
                    text: qsTr("New Multisig")
                    iconSource: "/resources/icons/add-circle.svg"
                    visible: hasAnySessions
                    onClicked: leftPanel.buttonClicked("NewMultisig")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: hasMultisigSessions

                Text {
                    text: qsTr("Active Multisig Sessions (%1)").arg(multisigManager.sessionsKeys.length)
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: themeManager.borderColor }

                Repeater {
                    model: multisigManager.sessionsKeys
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        height: 64
                        color: themeManager.backgroundColor
                        border.color: themeManager.borderColor
                        border.width: 1
                        radius: 2

                        readonly property var k: splitKey(modelData)

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            AppIcon {
                                source: "/resources/icons/shield-network.svg"
                                width: 16
                                height: 16
                                color: themeManager.successColor
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: qsTr("Multisig Session")
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    color: themeManager.textColor
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: k.ref
                                    font.pixelSize: 10
                                    font.family: "Monospace"
                                    color: themeManager.textSecondaryColor
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }

                                Text {
                                    text: k.onion
                                    font.pixelSize: 10
                                    font.family: "Monospace"
                                    color: themeManager.textSecondaryColor
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }
                            }

                            AppButton {
                                text: qsTr("Open")
                                variant: "secondary"
                                implicitHeight: 28
                                onClicked: {
                                    var pageComponent = Qt.resolvedUrl("NewMultisig.qml")
                                    middlePanel.currentPageUrl = pageComponent
                                    middlePanel.stackView.replace(pageComponent, {
                                        sessionRef: k.ref,
                                        selectedUserOnion: k.onion
                                    })
                                }
                            }


                            AppCopyButton { textToCopy: k.ref; size: 14 }

                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: hasNotifierSessions

                Text {
                    text: qsTr("Active Notifier Sessions (%1)").arg(multisigManager.notifierKeys.length)
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: themeManager.textColor
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: themeManager.borderColor }

                Repeater {
                    model: multisigManager.notifierKeys
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        height: 64
                        color: themeManager.backgroundColor
                        border.color: themeManager.borderColor
                        border.width: 1
                        radius: 2

                        readonly property var k: splitKey(modelData)
                        property QtObject notifierObj: multisigManager.notifierFor(k.onion, k.ref)

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 8

                            AppIcon {
                                source: "/resources/icons/bell.svg"
                                width: 16
                                height: 16
                                color: {
                                    if (!notifierObj) return themeManager.textSecondaryColor
                                    switch (notifierObj.stage) {
                                    case "COMPLETE": return themeManager.successColor
                                    case "ERROR":    return themeManager.errorColor
                                    case "POSTING":  return themeManager.warningColor
                                    default:         return themeManager.textSecondaryColor
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: qsTr("Notifier Session")
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    color: themeManager.textColor
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: k.ref
                                    font.pixelSize: 10
                                    font.family: "Monospace"
                                    color: themeManager.textSecondaryColor
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }

                                Text {
                                    text: k.onion
                                    font.pixelSize: 10
                                    font.family: "Monospace"
                                    color: themeManager.textSecondaryColor
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }

                            }

                            AppButton {
                                text: qsTr("Open")
                                variant: "secondary"
                                implicitHeight: 28
                                onClicked: {
                                    var pageComponent = Qt.resolvedUrl("NotifierStatus.qml")
                                    middlePanel.currentPageUrl = pageComponent
                                    middlePanel.stackView.replace(pageComponent, {
                                        notifierRef: k.ref,
                                        selectedUserOnion: k.onion
                                    })
                                }
                            }

                            AppCopyButton { textToCopy: k.ref; size: 14 }

                        }
                    }
                }
            }


            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 8
                visible: hasMultisigSessions && hasNotifierSessions
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignCenter
                spacing: 12
                visible: !hasAnySessions

                AppIcon {
                    source: "/resources/icons/shield-keyhole.svg"
                    width: 32
                    height: 32
                    color: themeManager.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: qsTr("No active sessions")
                    font.pixelSize: 14
                    color: themeManager.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: qsTr("Create a new multisig wallet or notify peers about a proposed wallet")
                    font.pixelSize: 12
                    color: themeManager.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.maximumWidth: 300
                    horizontalAlignment: Text.AlignHCenter
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8

                    AppButton {
                        text: qsTr("Create Multisig Wallet")
                        iconSource: "/resources/icons/add-circle.svg"
                        onClicked: leftPanel.buttonClicked("NewMultisig")
                    }

                    AppButton {
                        text: qsTr("Notifier (Merchants)")
                        iconSource: "/resources/icons/bell.svg"
                        variant: "secondary"
                        onClicked: leftPanel.buttonClicked("NotifierSetup")
                    }

                    AppButton {
                        text: qsTr("Standard Wallet")
                        iconSource: "/resources/icons/add-circle.svg"
                        variant: "secondary"
                        onClicked: leftPanel.buttonClicked("NewStandardWallet")
                    }
                }
            }
        }
    }
}
