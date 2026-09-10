import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    title: "Profiles"

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Controls.TextField {
                id: globalColorField
                Kirigami.FormData.label: "Global base color"
                text: app.globalColor
                placeholderText: "#rrggbb"
            }
            Controls.Button {
                text: "Apply global color"
                onClicked: app.setGlobalColor(globalColorField.text)
            }
        }

        Controls.Label {
            text: "Per-application profiles (picker lists identities from the KWin bridge; selecting a profile never launches or closes an app)."
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
            delegate: RowLayout {
                Layout.fillWidth: true
                Controls.Label {
                    text: modelData.displayName
                    Layout.fillWidth: true
                }
                Controls.TextField {
                    id: colorField
                    text: modelData.color
                    Layout.preferredWidth: 110
                }
                Controls.Button {
                    text: "Set color"
                    onClicked: app.setApplicationColor(modelData.id, colorField.text)
                }
                Controls.Button {
                    text: "Remove"
                    onClicked: app.removeProfile(modelData.id)
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
