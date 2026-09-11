import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page
    title: ""

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: app.temporaryOverrideActive
            type: Kirigami.MessageType.Warning
            text: "Temporary override is active (" + app.lightingLabel + "). It is not Automatic and expires on the next external application-identity change."
        }

        ZoneHero {
            Layout.fillWidth: true
        }

        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: app.statusSummary
            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.12
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !app.hasSavedProfiles && app.sessionLighting !== "temporary" && app.sessionLighting !== "off"
            type: Kirigami.MessageType.Information
            text: "Kým neuložíš preset, G213 ostáva na predvolenom firmware efekte (Wave). Čierna znamená Off, nie predvolené firmware."
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Controls.Button {
                text: "Nastaviť farby"
                highlighted: true
                onClicked: applicationWindow().showSection("colors")
            }
            Controls.Button {
                text: "Follow profile"
                visible: app.hasSavedProfiles
                highlighted: app.sessionLighting === "automatic"
                onClicked: app.restoreAutomatic()
            }
            Item {
                Layout.fillWidth: true
            }
            Controls.ToolButton {
                icon.name: "overflow-menu"
                Accessible.name: "Ďalšie akcie"
                onClicked: overflowMenu.popup()

                Controls.Menu {
                    id: overflowMenu
                    Controls.MenuItem {
                        text: "Restore device default"
                        onTriggered: app.restoreDeviceDefault()
                    }
                    Controls.MenuItem {
                        text: "Lights off"
                        onTriggered: app.lightsOff()
                    }
                }
            }
        }
    }
}
