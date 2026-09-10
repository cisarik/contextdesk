import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: "Diagnostics"

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Repeater {
            model: [
                "bridgeConnected",
                "degraded",
                "policyRevision",
                "lightingConnection",
                "lightingEnabled",
                "hasG213",
                "identityUpdates",
                "lightingUpdates",
                "inventoryCount",
                "lastError"
            ]
            delegate: Kirigami.FormLayout {
                Controls.Label {
                    Kirigami.FormData.label: modelData
                    text: String(app.diagnostics[modelData])
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Information
            text: "Diagnostics are state transitions, error classes, and counters. Ordinary keystrokes, window captions, device serials, and raw HID are never logged."
        }

        RowLayout {
            Controls.Button {
                text: "Displays Off"
                onClicked: app.displaysOff()
            }
            Controls.Button {
                text: "Suspend…"
                enabled: app.canSuspend()
                onClicked: {
                    suspendPrompt.open()
                }
            }
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
