import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import "pages/components"
import MoneroMultisigGui 1.0
import Qt5Compat.GraphicalEffects

ApplicationWindow {
    id: mainWindow
    visible: true
    width: 1000
    height: 700
    title: "Monero Multisig"
    color: themeManager.backgroundColor


    property bool waitingForShutdown: false
    property bool isAuthenticated: accountManager ? accountManager.is_authenticated : false


    onIsAuthenticatedChanged: {
        rootContainer.state = isAuthenticated ? "authenticated" : "login";
    }

    Item {
        id: rootContainer
        anchors.fill: parent

        states: [
            State {
                name: "login"
                PropertyChanges { target: loginScreen; visible: true }
                PropertyChanges { target: leftPanel; visible: false }
                PropertyChanges { target: middlePanel; visible: false }
            },
            State {
                name: "authenticated"
                PropertyChanges { target: loginScreen; visible: false }
                PropertyChanges { target: leftPanel; visible: true }
                PropertyChanges { target: middlePanel; visible: true }
            }
        ]

        Component.onCompleted: {
            state = mainWindow.isAuthenticated ? "authenticated" : "login";
        }
    }

    LoginScreen {
        id: loginScreen
        anchors.fill: parent
        z: 100
    }

    // Left panel
    LeftPanel {
        id: leftPanel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        currentPageUrl: middlePanel.currentPageUrl

        onButtonClicked: function(pageName) {
            var newPageUrl;
            var pageComponent;

            if ( pageName === 'NewMultisig'){
                pageComponent = Qt.resolvedUrl("pages/NewMultisig.qml");
                middlePanel.currentPageUrl = pageComponent;
                middlePanel.stackView.replace( pageComponent, { sessionRef: "" } );
            }
            else if (pageName.startsWith("NewMultisig:")){
                var ref = pageName.split(":")[1];
                pageComponent = Qt.resolvedUrl("pages/NewMultisig.qml");
                middlePanel.currentPageUrl = pageComponent;
                middlePanel.stackView.replace( pageComponent, { sessionRef: ref} );
            }
            else {
                newPageUrl = Qt.resolvedUrl("pages/" + pageName + ".qml");
                if (String(middlePanel.currentPageUrl) === String(newPageUrl))
                    return;
                leftPanel.currentPageUrl =  newPageUrl
                middlePanel.currentPageUrl = newPageUrl;
                middlePanel.stackView.replace(newPageUrl);
            }
        }
    }

    MiddlePanel {
        id: middlePanel
        property string currentPageUrl: ""
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: leftPanel.right
        anchors.right: parent.right
    }

    Connections {
        target: accountManager

        function onLoginSuccess(accountName) {
            mainWindow.title = "Monero Multisig - " + accountName;
            LockController.locked = false
            LockController.idleTimer.stop()

            if (middlePanel.currentPageUrl === "") {
                var defaultPageUrl = Qt.resolvedUrl("pages/AccountPage.qml");
                middlePanel.currentPageUrl = defaultPageUrl;
                middlePanel.stackView.replace(defaultPageUrl);
            }
        }

        function onAccountCreated(accountName) {
            mainWindow.title = "Monero Multisig - " + accountName;
            LockController.locked = false
            LockController.idleTimer.stop()

            if (middlePanel.currentPageUrl === "") {
                var defaultPageUrl = Qt.resolvedUrl("pages/AccountPage.qml");
                middlePanel.currentPageUrl = defaultPageUrl;
                middlePanel.stackView.replace(defaultPageUrl);
            }
        }

        function onLogoutOccurred() {
            mainWindow.title = "Monero Multisig";
            LockController.locked = false
            LockController.idleTimer.stop()

            if (typeof walletManager !== 'undefined') {
                // walletManager.stopAllWallets();
            }
            if (typeof torServer !== 'undefined') {
                torServer.stopTorServer();
            }
        }
    }

    // Global lock overlay
    Item {
        id: overlay
        anchors.fill: parent
        visible: LockController.locked
        z: 99

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            preventStealing: true
            propagateComposedEvents: false
            onPressed: { /* swallow */ }
            focus: true
            Keys.onPressed: { /* swallow keys */ }
        }

        Rectangle {
            anchors.fill: parent
            color: themeManager.backgroundColor
            opacity: 1
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: 280
            spacing: 12

            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6

                Rectangle {
                    width: 40
                    height: 40
                    Layout.alignment: Qt.AlignHCenter
                    color: "transparent"

                    AppIcon {
                        source: "/resources/icons/lock-keyhole.svg"
                        width: 32
                        height: 32
                        color: themeManager.primaryColor
                        anchors.centerIn: parent
                    }
                }


                Text {
                    text: "Screen Locked"
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    color: themeManager.textColor
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: "Enter your password to continue"
                    font.pixelSize: 14
                    color: themeManager.textSecondaryColor
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: lockFormLayout.implicitHeight + 24
                color: themeManager.surfaceColor
                border.color: themeManager.borderColor
                border.width: 1
                radius: 2

                ColumnLayout {
                    id: lockFormLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        AppInput {
                            id: user_name
                            Layout.fillWidth: true
                            text: accountManager.current_account
                            readOnly: true
                            iconSource: "/resources/icons/user-circle.svg"

                        }

                        AppInput {
                            id: pwd
                            Layout.fillWidth: true
                            placeholderText: "Enter password"
                            echoMode: TextInput.Password
                            iconSource: "/resources/icons/lock-password.svg"
                            onAccepted: unlockBtn.clicked()
                        }
                    }

                    AppAlert {
                        id: err
                        Layout.fillWidth: true
                        visible: false
                        variant: "error"
                        closable: true
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        AppButton {
                            id: unlockBtn
                            text: "Unlock"
                            iconSource: "/resources/icons/lock-keyhole-unlocked.svg"
                            Layout.fillWidth: true
                            implicitHeight: 32
                            onClicked: {
                                if (accountManager.verifyPassword(pwd.text)) {
                                    LockController.unlock()
                                    err.visible = false
                                    pwd.text = ""
                                } else {
                                    err.text = "Wrong password"
                                    err.visible = true
                                }
                            }
                        }

                        AppButton {
                            text: "Logout"
                            iconSource: "/resources/icons/logout-2.svg"
                            variant: "secondary"
                            Layout.fillWidth: true
                            implicitHeight: 28
                            onClicked: {
                                accountManager.logout()
                                LockController.unlock()
                            }
                        }
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        z: -1
        acceptedButtons: Qt.AllButtons
        propagateComposedEvents: true
        onPressed: LockController.userActivity()
        onReleased: LockController.userActivity()
        onPositionChanged: LockController.userActivity()
        onWheel: LockController.userActivity()
        Keys.onPressed: LockController.userActivity()
    }

    Timer {
        id: refreshReceived
        interval: 5000
        repeat: true
        running: true
        onTriggered: {
            transferManager.restoreAllSaved()
        }
    }

    onClosing: function(close) {
        if (!waitingForShutdown) {
            waitingForShutdown = true
            close.accepted = false
            torServer.stopTorServer()
            // Add shutdown logic
            Qt.quit()
        }
    }
}
