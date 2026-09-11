import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: "Profiles"

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Heading {
            text: "Global lighting preset"
            level: 2
        }
        LightingPresetEditor {
            Layout.fillWidth: true
            currentMode: app.globalMode
            zones: app.globalZones
            applicationLevel: false
        }

        Controls.Label {
            text: "Per-application profiles (picker lists identities from the KWin bridge; selecting a profile never launches or closes an app). Each profile can have its own lighting preset."
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Controls.ComboBox {
            id: inventoryBox
            Layout.fillWidth: true
            model: app.inventory
            textRole: "label"
        }
        Controls.Button {
            text: "Add profile from inventory"
            enabled: inventoryBox.count > 0
            onClicked: app.addProfileFromInventory(inventoryBox.currentIndex)
        }

        Repeater {
            model: app.profiles
            delegate: ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Label {
                        text: modelData.displayName
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Controls.Button {
                        text: "Remove"
                        onClicked: app.removeProfile(modelData.id)
                    }
                }
                LightingPresetEditor {
                    Layout.fillWidth: true
                    profileId: modelData.id
                    currentMode: modelData.mode
                    zones: modelData.zones
                    applicationLevel: true
                }
            }
        }

        Controls.Button {
            text: "Save profiles.json"
            onClicked: app.save()
        }
        Controls.Label {
            text: app.saveStatus()
        }
    }
}
