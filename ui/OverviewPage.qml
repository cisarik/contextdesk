import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: "Overview"

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

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
                Kirigami.FormData.label: "Lighting mode"
                text: app.lightingMode
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
                text: "Automatic"
                highlighted: app.lightingMode === "automatic"
                onClicked: app.restoreAutomatic()
            }
            Controls.Button {
                text: "Lights off"
                highlighted: app.lightingMode === "lights_off"
                onClicked: app.lightsOff()
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Information
            text: "The G213 has five RGB zones, not per-key color. v1 lighting applies one base color to all five zones."
        }
    }
}
