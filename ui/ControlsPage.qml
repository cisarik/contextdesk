import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import io.github.cisarik.ContextDeck

Kirigami.ScrollablePage {
    title: ""

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Heading {
            text: "Pokročilé"
            level: 2
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Warning
            text: "Remapovanie klávesov bude aktívne v M2. V M1 svieti a deteguje kontext."
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Information
            text: "emit_shortcut assignments are stored but not active until the input broker exists (M2). GameMode and Backlight are conditional on hardware evidence (G1) and are not bound in M1. PrintScreen and Pause are never silent substitutes."
        }

        Controls.Label {
            text: "In-window chord recorder (captures keys only while this control has focus; cancels on focus loss or timeout). Keyboard layout: " + recorder.layoutContext
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ChordRecorder {
            id: recorder
            implicitHeight: 48
            Layout.fillWidth: true
            Accessible.role: Accessible.Grouping
            Accessible.description: recorder.display
            Rectangle {
                anchors.fill: parent
                radius: 4
                color: recorder.recording ? Kirigami.Theme.highlightColor : Kirigami.Theme.backgroundColor
                border.color: Kirigami.Theme.highlightColor
                Controls.Label {
                    anchors.centerIn: parent
                    text: recorder.display
                }
            }
        }

        RowLayout {
            Controls.Button {
                text: recorder.recording ? "Recording…" : "Record chord"
                Accessible.name: recorder.recording ? "Recording…" : "Record chord"
                onClicked: recorder.begin()
            }
            Controls.Button {
                text: "Cancel"
                Accessible.name: "Cancel"
                enabled: recorder.recording
                onClicked: recorder.cancel()
            }
            Controls.Button {
                text: "Store on global F5"
                Accessible.name: "Store on global F5"
                enabled: recorder.key.length > 0
                onClicked: app.assignEmitShortcut("F5", recorder.key, recorder.modifiers, false, "")
            }
        }

        Repeater {
            model: app.controls
            delegate: Kirigami.FormLayout {
                Layout.fillWidth: true
                Controls.Label {
                    Kirigami.FormData.label: modelData.name + (modelData.conditional ? " (conditional)" : "")
                    text: modelData.action + " — " + modelData.note
                    wrapMode: Text.WordWrap
                }
                Controls.Label {
                    visible: modelData.zoneName !== undefined
                    text: "Zone accent preview: " + modelData.zoneName + " (" + modelData.zonePreview + ")"
                    wrapMode: Text.WordWrap
                    opacity: 0.8
                }
            }
        }
    }
}
