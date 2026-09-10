import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: "Controls"

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Warning
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
                onClicked: recorder.begin()
            }
            Controls.Button {
                text: "Cancel"
                enabled: recorder.recording
                onClicked: recorder.cancel()
            }
            Controls.Button {
                text: "Store on global F5"
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
            }
        }
    }
}
