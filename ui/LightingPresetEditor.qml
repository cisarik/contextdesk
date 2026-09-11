import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    property string profileId: ""
    property string currentMode: "untouched"
    property var zones: []
    property bool applicationLevel: false

    spacing: Kirigami.Units.smallSpacing

    Controls.ComboBox {
        id: presetBox
        Layout.fillWidth: true
        model: app.lightingPresets
        currentIndex: Math.max(0, app.lightingPresets.indexOf(root.currentMode))
        onActivated: {
            if (root.applicationLevel) {
                app.setApplicationLightingMode(root.profileId, currentText)
            } else {
                app.setGlobalLightingMode(currentText)
            }
        }
    }

    Repeater {
        model: 5
        delegate: RowLayout {
            Layout.fillWidth: true
            Controls.Label {
                text: app.zoneNames[index]
                Layout.preferredWidth: 160
            }
            Rectangle {
                width: 22
                height: 22
                radius: 3
                color: (root.zones && root.zones[index]) ? root.zones[index] : "#000000"
                border.color: Kirigami.Theme.textColor
            }
            Controls.TextField {
                id: zoneField
                text: (root.zones && root.zones[index]) ? root.zones[index] : "#000000"
                placeholderText: "#rrggbb"
                Layout.fillWidth: true
                onEditingFinished: {
                    if (root.applicationLevel) {
                        app.setApplicationZoneColor(root.profileId, index, text)
                    } else {
                        app.setGlobalZoneColor(index, text)
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Controls.TextField {
            id: startField
            placeholderText: "start #rrggbb"
            Layout.fillWidth: true
        }
        Controls.TextField {
            id: endField
            placeholderText: "end #rrggbb"
            Layout.fillWidth: true
        }
        Controls.Button {
            text: "Fill five-zone gradient"
            onClicked: {
                if (root.applicationLevel) {
                    app.applyApplicationGradient(root.profileId, startField.text, endField.text)
                } else {
                    app.applyGlobalGradient(startField.text, endField.text)
                }
            }
        }
    }

    Controls.Label {
        text: "Zone colors apply in Direct mode. This is five-zone lighting, not per-key RGB."
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        opacity: 0.8
    }
}
