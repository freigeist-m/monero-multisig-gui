import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import Qt5Compat.GraphicalEffects
import "."

Rectangle {
    id: loginScreen
    color: themeManager.backgroundColor

    property bool isCreatingAccount: false
    property bool passwordsMatch: passwordField.text === confirmPasswordField.text

    Rectangle {
        anchors.fill: parent
        color: themeManager.backgroundColor

        Rectangle {
            anchors.fill: parent
            color: themeManager.surfaceColor
            opacity: 0.1
        }
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
                    source: "/resources/icons/monero_rotated_blue.svg"
                    width: 32
                    height: 32
                    color: themeManager.primaryColor
                    anchors.centerIn: parent
                }
            }

            Text {
                text: "Monero Multisig"
                font.pixelSize: 24
                font.weight: Font.Bold
                color: themeManager.textColor
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: isCreatingAccount ? "Create New Account" : "Welcome Back"
                font.pixelSize: 14
                color: themeManager.textSecondaryColor
                Layout.alignment: Qt.AlignHCenter
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: formLayout.implicitHeight + 24
            color: themeManager.surfaceColor
            border.color: themeManager.borderColor
            border.width: 1
            radius: 2

            ColumnLayout {
                id: formLayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 8


                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: isCreatingAccount ? "Account Name" : "Select Account"
                        color: themeManager.textColor
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }

                    ComboBox {
                        id: accountCombo
                        Layout.fillWidth: true
                        visible: !isCreatingAccount
                        textRole: "name"
                        valueRole: "path"
                        font.pixelSize: 12
                        implicitHeight: 28

                        background: Rectangle {
                            color: themeManager.backgroundColor
                            border.color: themeManager.borderColor
                            border.width: 1
                            radius: 2
                        }

                        contentItem: Text {
                            text: accountCombo.displayText
                            font: accountCombo.font
                            color: themeManager.textColor
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 8
                            rightPadding: 32
                        }

                        Component.onCompleted: {
                            refreshAccountList()
                        }

                        function refreshAccountList() {
                            if (!isCreatingAccount) {
                                model = accountManager.getAvailableAccounts()
                            }
                        }
                    }

                    AppInput {
                        id: accountNameField
                        Layout.fillWidth: true
                        visible: isCreatingAccount
                        placeholderText: "Enter account name"
                    }
                }

                // Password input
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    AppInput {
                        id: passwordField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: "Enter password"
                        iconSource: "/resources/icons/lock-password.svg"
                        onAccepted: {
                            if (isCreatingAccount && confirmPasswordField.visible) {
                                confirmPasswordField.forceActiveFocus()
                            } else {
                                loginButton.clicked()
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    visible: isCreatingAccount

                    AppInput {
                        id: confirmPasswordField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: "Confirm password"
                        iconSource: "/resources/icons/lock-password.svg"
                        onAccepted: loginButton.clicked()
                    }
                }

                AppAlert {
                    id: passwordMismatchAlert
                    Layout.fillWidth: true
                    visible: isCreatingAccount && passwordField.text !== "" && confirmPasswordField.text !== "" && !passwordsMatch
                    variant: "warning"
                    text: "Passwords do not match"
                }

                AppAlert {
                    id: errorAlert
                    Layout.fillWidth: true
                    visible: false
                    variant: "error"
                    closable: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    AppButton {
                        id: loginButton
                        text: isCreatingAccount ? "Create Account" : "Login"
                        Layout.fillWidth: true
                        implicitHeight: 32
                        enabled: {
                            if (isCreatingAccount) {
                                return accountNameField.text.trim() !== "" &&
                                       passwordField.text !== "" &&
                                       confirmPasswordField.text !== "" &&
                                       passwordsMatch
                            } else {
                                return accountCombo.currentIndex >= 0 && passwordField.text !== ""
                            }
                        }

                        onClicked: {
                            errorAlert.visible = false

                            if (isCreatingAccount) {
                                if (!passwordsMatch) {
                                    errorAlert.text = "Passwords do not match"
                                    errorAlert.visible = true
                                    return
                                }
                                var success = accountManager.createAccount(accountNameField.text.trim(), passwordField.text)
                                if (!success) {
                                    // Error will be shown via signal connection
                                }
                            } else {
                                if (accountCombo.currentIndex >= 0) {
                                    var accountPath = accountCombo.currentValue
                                    var success = accountManager.login(accountPath, passwordField.text)
                                    if (!success) {
                                        // Error will be shown via signal connection
                                    }
                                }
                            }
                        }
                    }

                    AppButton {
                        text: isCreatingAccount ? "Back to Login" : "Create New Account"
                        iconSource: isCreatingAccount ? "/resources/icons/arrow-left.svg" : ""
                        variant: "secondary"
                        Layout.fillWidth: true
                        implicitHeight: 28
                        onClicked: {
                            isCreatingAccount = !isCreatingAccount
                            passwordField.clear()
                            confirmPasswordField.clear()
                            errorAlert.visible = false

                            if (!isCreatingAccount) {
                                accountCombo.refreshAccountList()
                            }
                        }
                    }
                }

                // Rectangle {
                //     Layout.fillWidth: true
                //     height: 1
                //     color: themeManager.borderColor
                //     visible: !isCreatingAccount
                //     Layout.topMargin: 6
                //     Layout.bottomMargin: 6
                //     opacity: 0.5
                // }


                AppButton {
                    text: "Choose Data Folder…"
                    variant: "secondary"
                    Layout.fillWidth: true
                    implicitHeight: 28
                    onClicked: dataFolderDialog.open()
                }
            }
        }
    }

    FolderDialog {
        id: dataFolderDialog
        title: "Select where to store monero-multisig-gui-data"
        onAccepted: {
            // selectedFolder is a url like "file:///home/user/..."
            var p = selectedFolder.toString()
            // let C++ handle file:// too, but removing here is fine
            p = p.replace("file://", "")
            var ok = accountManager.setDataRootParentDir(p)
            if (!ok) {
                // error will be raised via errorOccurred
            } else {
                errorAlert.variant = "success"
                errorAlert.text = "Data folder set to:\n" + accountManager.dataRootPath
                errorAlert.visible = true
                accountCombo.refreshAccountList()
            }
        }
    }


    Connections {
        target: accountManager

        function onLoginFailed(error) {
            errorAlert.text = error
            errorAlert.visible = true
        }

        function onErrorOccurred(error) {
            errorAlert.text = error
            errorAlert.visible = true
        }

        function onLoginSuccess(accountName) {
            passwordField.clear()
            confirmPasswordField.clear()
            errorAlert.visible = false
        }

        function onAccountCreated(accountName) {
            passwordField.clear()
            accountNameField.clear()
            confirmPasswordField.clear()
            isCreatingAccount = false
            errorAlert.text = "New account created"
            errorAlert.visible = true
            errorAlert.variant = "success"

            accountCombo.refreshAccountList()

        }

        function onLogoutOccurred() {
            accountCombo.refreshAccountList()
        }
    }
}
