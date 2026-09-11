import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: "Overview"

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: app.temporaryOverrideActive
            type: Kirigami.MessageType.Warning
            text: "Temporary override is active (" + app.lightingLabel + "). It is not Automatic and expires on the next external application-identity change."
        }

        Kirigami.FormLayout {
            wideMode: true
            Controls.Label {
                Kirigami.FormData.label: "Current application"
                text: app.currentApplication
            }
            Controls.Label {
                Kirigami.FormData.label: "Resolved profile"
                text: app.currentProfile
            }
            Controls.Label {
                Kirigami.FormData.label: "Remapping"
                text: app.remappingState + " — stored chords are not emitted yet."
                wrapMode: Text.WordWrap
            }
            Controls.Label {
                Kirigami.FormData.label: "Lighting"
                text: app.lightingLabel
            }
            Controls.Label {
                Kirigami.FormData.label: "Session"
                text: app.sessionLighting === "automatic" ? "follow profile"
                    : (app.sessionLighting === "temporary" ? "temporary override"
                    : (app.sessionLighting === "device_default" ? "restore device default" : app.sessionLighting))
            }
            Controls.Label {
                Kirigami.FormData.label: "Lighting connection"
                text: app.lightingConnection
            }
            Controls.Label {
                Kirigami.FormData.label: "Bridge"
                text: app.bridgeConnected ? "connected" : (app.degraded ? "degraded" : "not connected")
            }
        }

        RowLayout {
            Controls.Button {
                text: "Follow profile"
                highlighted: app.sessionLighting === "automatic"
                onClicked: app.restoreAutomatic()
            }
            Controls.Button {
                text: "Lights off"
                highlighted: app.sessionLighting === "off"
                onClicked: app.lightsOff()
            }
            Controls.Button {
                text: "Restore device default"
                highlighted: app.sessionLighting === "device_default"
                onClicked: app.restoreDeviceDefault()
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Information
            text: "The G213 has five RGB zones, not per-key color. Until you pick a preset, lighting stays untouched — device default. There is no optical readback."
        }
    }
}
