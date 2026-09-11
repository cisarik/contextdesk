import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: ""

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Heading {
            text: "Diagnostika"
            level: 2
        }
        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.85
            text: "Technický stav. Bežné stlačenia, titulky okien, USB sériové čísla a surový HID sa nelogujú."
        }

        Kirigami.FormLayout {
            wideMode: true

            Controls.Label {
                Kirigami.FormData.label: "OpenRGB"
                text: String(app.diagnostics.lightingConnection)
            }
            Controls.Label {
                Kirigami.FormData.label: "socket"
                text: String(app.diagnostics.socketState)
            }
            Controls.Label {
                Kirigami.FormData.label: "SDK endpoint"
                text: String(app.diagnostics.sdkEndpoint)
            }
            Controls.Label {
                Kirigami.FormData.label: "hasG213"
                text: String(app.diagnostics.hasG213)
            }
            Controls.Label {
                Kirigami.FormData.label: "lightingEnabled"
                text: String(app.diagnostics.lightingEnabled)
            }
            Controls.Label {
                Kirigami.FormData.label: "lightingLabel"
                text: String(app.diagnostics.lightingLabel)
            }
            Controls.Label {
                Kirigami.FormData.label: "sessionLighting"
                text: String(app.diagnostics.sessionLighting)
            }
        }

        Kirigami.FormLayout {
            wideMode: true

            Controls.Label {
                Kirigami.FormData.label: "brokerIpcState"
                text: String(app.diagnostics.brokerIpcState)
            }
            Controls.Label {
                Kirigami.FormData.label: "D-Bus service"
                text: String(app.diagnostics.dbusService)
            }
            Controls.Label {
                Kirigami.FormData.label: "object path"
                text: String(app.diagnostics.dbusObjectPath)
            }
            Controls.Label {
                Kirigami.FormData.label: "interface"
                text: String(app.diagnostics.dbusInterface)
            }
            Controls.Label {
                Kirigami.FormData.label: "bridgeId"
                text: String(app.diagnostics.bridgeId)
            }
            Controls.Label {
                Kirigami.FormData.label: "bridgeConnected"
                text: String(app.diagnostics.bridgeConnected)
            }
            Controls.Label {
                Kirigami.FormData.label: "degraded"
                text: String(app.diagnostics.degraded)
            }
            Controls.Label {
                Kirigami.FormData.label: "policyRevision"
                text: String(app.diagnostics.policyRevision)
            }
            Controls.Label {
                Kirigami.FormData.label: "CurrentIdentity"
                text: String(app.diagnostics.currentIdentity)
                wrapMode: Text.WrapAnywhere
            }
            Controls.Label {
                Kirigami.FormData.label: "isSelfWindow"
                text: String(app.diagnostics.isSelfWindow)
            }
            Controls.Label {
                Kirigami.FormData.label: "lastExternalApplication"
                text: String(app.diagnostics.lastExternalApplication)
            }
        }

        Kirigami.FormLayout {
            wideMode: true

            Controls.Label {
                Kirigami.FormData.label: "identityUpdates"
                text: String(app.diagnostics.identityUpdates)
            }
            Controls.Label {
                Kirigami.FormData.label: "lightingUpdates"
                text: String(app.diagnostics.lightingUpdates)
            }
            Controls.Label {
                Kirigami.FormData.label: "inventoryCount"
                text: String(app.diagnostics.inventoryCount)
            }
            Controls.Label {
                Kirigami.FormData.label: "lastError"
                text: String(app.diagnostics.lastError)
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            Controls.Button {
                text: "Arm G213 pass-through…"
                onClicked: armPrompt.open()
            }
            Controls.Button {
                text: "Disarm pass-through"
                onClicked: app.disarmPassThrough()
            }
            Controls.Button {
                text: "Release broker lease"
                onClicked: app.releaseBrokerLease()
            }
        }

        RowLayout {
            Controls.Button {
                text: "Displays Off"
                onClicked: app.displaysOff()
            }
            Controls.Button {
                text: "Suspend…"
                enabled: app.canSuspend()
                onClicked: suspendPrompt.open()
            }
        }

        Controls.Dialog {
            id: armPrompt
            title: "Arm G213 pass-through"
            modal: true
            standardButtons: Controls.Dialog.Ok | Controls.Dialog.Cancel
            Controls.Label {
                wrapMode: Text.WordWrap
                text: "Arm G213 pass-through now? This sends LEASE then ARM. The broker claims the keyboard until Disarm, Release, Quit, or lease expiry. Recover from another keyboard, SSH, or an already-open TTY if the G213 goes silent."
            }
            onAccepted: app.armPassThrough()
        }

        Controls.Dialog {
            id: suspendPrompt
            title: "Suspend"
            modal: true
            standardButtons: Controls.Dialog.Ok | Controls.Dialog.Cancel
            Controls.Label { text: "Suspend this computer now?" }
            onAccepted: app.suspend()
        }
    }
}
