import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page
    title: ""

    function sessionIndex(sessionId) {
        const options = app.workspaceSessionOptions;
        for (let i = 0; i < options.length; ++i) {
            if (options[i].id === sessionId) {
                return i;
            }
        }
        return 0;
    }

    function titleModeIndex(mode) {
        const modes = ["contains", "exact", "prefix"];
        const index = modes.indexOf(mode);
        return index >= 0 ? index : 0;
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Heading {
            text: "Aplikácie"
            level: 2
        }
        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Per-application profiles (picker lists identities from the KWin bridge; selecting a profile never launches or closes an app). Each profile can have its own lighting preset."
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
                    speedPercent: modelData.speedPercent
                    breathingHex: modelData.breathingColor
                }
                Kirigami.Separator {
                    Layout.fillWidth: true
                }
                Controls.Label {
                    text: "Plocha — priradenie (ukladá sa; nič nespúšťa)"
                    font.bold: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Label {
                        text: "Relácia:"
                    }
                    Controls.ComboBox {
                        Layout.fillWidth: true
                        model: app.workspaceSessionOptions
                        textRole: "label"
                        currentIndex: page.sessionIndex(modelData.workspaceSessionId)
                        onActivated: app.setApplicationWorkspaceSession(modelData.id,
                                                                        app.workspaceSessionOptions[index].id)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Label {
                        text: "Poradie plochy:"
                    }
                    Controls.SpinBox {
                        from: 1
                        to: 32
                        value: modelData.workspaceDesktopOrdinal
                        onValueModified: app.setApplicationWorkspaceDesktop(modelData.id, value)
                    }
                    Controls.CheckBox {
                        text: "Spustiť"
                        checked: modelData.workspaceLaunch
                        onToggled: app.setApplicationWorkspaceLaunch(modelData.id, checked)
                    }
                    Controls.CheckBox {
                        text: "Maximalizovať"
                        checked: modelData.workspaceMaximize
                        onToggled: app.setApplicationWorkspaceMaximize(modelData.id, checked)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Controls.Label {
                        text: "Desktop file:"
                    }
                    Controls.TextField {
                        Layout.fillWidth: true
                        text: modelData.workspaceLaunchDesktopFile
                        placeholderText: "napr. org.kde.dolphin.desktop"
                        onEditingFinished: app.setApplicationWorkspaceLaunchFile(modelData.id, text)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Controls.CheckBox {
                        id: fallbackToggle
                        text: "Title fallback"
                        checked: modelData.workspaceTitleFallbackEnabled
                        onToggled: app.setApplicationTitleFallback(modelData.id, checked,
                                                                   modeBox.currentText, patternField.text)
                    }
                    Controls.ComboBox {
                        id: modeBox
                        model: ["contains", "exact", "prefix"]
                        currentIndex: page.titleModeIndex(modelData.workspaceTitleFallbackMode)
                        onActivated: app.setApplicationTitleFallback(modelData.id, fallbackToggle.checked,
                                                                     currentText, patternField.text)
                    }
                    Controls.TextField {
                        id: patternField
                        Layout.fillWidth: true
                        text: modelData.workspaceTitleFallbackPattern
                        placeholderText: "vzor (nikdy sa neloguje)"
                        onEditingFinished: app.setApplicationTitleFallback(modelData.id, fallbackToggle.checked,
                                                                           modeBox.currentText, text)
                    }
                }
            }
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
