import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import MoneroMultisigGui 1.0
import "components"

Page {
    id: root
    title: qsTr("Create Standard Wallet")

    property string walletNameError: ""
    property string createError: ""
    property string selectedNetwork: (accountManager.networkType || "mainnet")

    background: Rectangle { color: themeManager.backgroundColor }

    function walletExists(name) {
        return walletManager.nameExists ? walletManager.nameExists(name)
                                        : walletManager.walletExists(name)
    }

    function validateWalletName() {
        const n = nameField.text.trim()
        if (!n.length) { walletNameError = qsTr("Wallet name cannot be empty"); return }
        if (walletExists(n)) { walletNameError = qsTr("Wallet %1 already exists").arg(n); return }
        walletNameError = ""
    }

    function formReady() {
        const nOk = walletNameError === "" && nameField.text.trim().length > 0
        const networkOK = (selectedNetwork === accountManager.networkType)
        return nOk && networkOK
    }

    function createWalletFrontend() {
        createError = ""
        validateWalletName()
        if (!formReady()) return

        const name = nameField.text.trim()
        const pwd  = passwordField.text

        const ok = walletManager.createStandardWallet(name, pwd, selectedNetwork)
        if (!ok) {
            createError = qsTr("Failed to start wallet creation")
        }
    }

    Connections {
        target: walletManager

        function onStandardWalletCreated(walletName) {
            const name = nameField.text.trim()
            if (walletName !== name) return

            const pageComponent = "WalletDetails.qml"
            middlePanel.currentPageUrl = pageComponent
            middlePanel.stackView.replace(pageComponent, { walletName: walletName })
        }

        function onRpcError(walletName, msg) {
            // Show only errors relevant to this flow (or show all)
            createError = msg
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            AppBackButton {
                backText: qsTr("Back")
                onClicked: leftPanel.buttonClicked("MultisigLandingPage")
            }

            Text {
                text: qsTr("Create Standard Wallet")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: themeManager.textColor
                Layout.fillWidth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                AppInput {
                    id: nameField
                    placeholderText: qsTr("Enter wallet name")
                    Layout.fillWidth: true
                    errorText: walletNameError
                    onTextChanged: validateWalletName()
                }

                AppInput {
                    id: passwordField
                    placeholderText: qsTr("Enter password to encrypt wallet (optional)")
                    echoMode: TextInput.Password
                    Layout.fillWidth: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        text: qsTr("Network:")
                        color: themeManager.textSecondaryColor
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 120
                    }

                    ComboBox {
                        Layout.preferredWidth: 140
                        model: [ "mainnet", "stagenet", "testnet" ]
                        currentIndex: {
                            const want = (selectedNetwork || "mainnet").toLowerCase()
                            const idx = model.indexOf(want)
                            return idx >= 0 ? idx : 0
                        }
                        onCurrentIndexChanged: selectedNetwork = model[currentIndex]

                        background: Rectangle {
                            color: themeManager.surfaceColor
                            border.color: themeManager.borderColor
                            border.width: 1
                            radius: 2
                        }

                        contentItem: Text {
                            text: parent.displayText
                            font.pixelSize: 12
                            color: themeManager.textColor
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 8
                            rightPadding: 24
                        }

                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Select Monero network")
                        ToolTip.delay: 400
                    }
                }

                AppButton {
                    id: create_button
                    text: qsTr("Create Wallet")
                    enabled: formReady()
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 160
                    onClicked: {
                        create_button.enabled =  false
                        create_button.text =  "Creating..."
                        createWalletFrontend()
                    }

                    ToolTip.visible: hovered && selectedNetwork !== accountManager.networkType
                    ToolTip.text: qsTr("Change network type in settings to create wallet")
                    ToolTip.delay: 400
                }

                AppAlert {
                    text: createError
                    variant: "error"
                    visible: createError !== ""
                    Layout.fillWidth: true
                }
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 12 }
        }
    }
}
