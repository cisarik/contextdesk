import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: ""

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Heading {
            text: "Farby"
            level: 2
        }
        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.85
            text: "Päť fyzických zón G213. Toto nie je per-key RGB."
        }
        LightingPresetEditor {
            Layout.fillWidth: true
            currentMode: app.globalMode
            zones: app.globalZones
            applicationLevel: false
        }
        Controls.Button {
            text: "Uložiť"
            highlighted: true
            onClicked: app.save()
        }
        Controls.Label {
            text: app.saveStatus()
            opacity: 0.8
        }
    }
}
