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

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.Heading {
            text: "Plochy"
            level: 2
        }
        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.85
            text: "M4 Slice B: ContextDeck vie na výslovné Použitie vytvoriť chýbajúce plochy, premenovať odlišné, nastaviť mriežku/obtáčanie a voliteľne prepnúť plochu alebo odstrániť prebytočné plochy. Predvolený Apply nič neodstraňuje. Nič sa nedeje automaticky pri štarte relácie ani pri zmene fokusu."
        }

        Controls.CheckBox {
            text: "Povoliť správu plôch (ukladá sa ako workspace_management_enabled)"
            checked: app.workspaceManagementEnabled
            onToggled: app.setWorkspaceManagementEnabled(checked)
        }
        Controls.CheckBox {
            text: "Opt-in porovnanie podľa titulku okna (title_fallback_enabled)"
            checked: app.titleFallbackEnabled
            onToggled: app.setTitleFallbackEnabled(checked)
        }
        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.8
            text: "Title fallback je opt-in a vyžaduje zapnutie aj na profile. Porovnáva sa iba používateľom zadaný vzor v pamäti počas jedného volania; živé titulky sa nikdy neukladajú, nelogujú ani nezobrazujú."
        }

        Kirigami.Heading {
            text: "Pozorovaný stav"
            level: 3
        }
        GridLayout {
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing
            Controls.Label {
                text: "Dostupné:"
            }
            Controls.Label {
                text: app.workspaceObserved.available ? "áno" : "nie"
            }
            Controls.Label {
                text: "Počet plôch:"
            }
            Controls.Label {
                text: app.workspaceObserved.count
            }
            Controls.Label {
                text: "Aktuálna plocha:"
            }
            Controls.Label {
                text: app.workspaceObserved.currentOrdinal > 0 ? app.workspaceObserved.currentOrdinal : "—"
            }
            Controls.Label {
                text: "Mriežka (rows):"
            }
            Controls.Label {
                text: (app.workspaceObserved.rows === undefined || app.workspaceObserved.rows === null)
                      ? "—" : app.workspaceObserved.rows
            }
            Controls.Label {
                text: "Obtáčanie:"
            }
            Controls.Label {
                text: (app.workspaceObserved.wrapping === undefined || app.workspaceObserved.wrapping === null)
                      ? "—" : (app.workspaceObserved.wrapping ? "zapnuté" : "vypnuté")
            }
            Controls.Label {
                text: "Stav pozorovania:"
            }
            Controls.Label {
                text: app.workspaceObserved.errorClass.length > 0
                      ? app.workspaceObserved.errorClass : "ok"
            }
        }

        Kirigami.Heading {
            text: "Aktívna relácia"
            level: 3
        }
        Controls.ComboBox {
            Layout.fillWidth: true
            model: app.workspaceSessionOptions
            textRole: "label"
            Accessible.description: "Aktívna relácia"
            currentIndex: page.sessionIndex(app.activeWorkspaceSession)
            onActivated: app.setActiveWorkspaceSession(app.workspaceSessionOptions[index].id)
        }

        Kirigami.Heading {
            text: "Pomenované relácie"
            level: 3
        }
        RowLayout {
            Layout.fillWidth: true
            Controls.TextField {
                id: newSessionName
                Layout.fillWidth: true
                placeholderText: "Názov novej relácie"
                Accessible.description: "Názov novej relácie"
            }
            Controls.Button {
                text: "Pridať reláciu"
                enabled: newSessionName.text.length > 0
                onClicked: {
                    app.addWorkspaceSession(newSessionName.text);
                    newSessionName.text = "";
                }
            }
        }
        Repeater {
            model: app.workspaceSessions
            delegate: Controls.Frame {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true
                        Controls.Label {
                            text: modelData.displayName + " (" + modelData.id + ")"
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Controls.Label {
                            text: modelData.active ? "aktívna" : ""
                            opacity: 0.7
                        }
                        Controls.Button {
                            text: "Odstrániť"
                            onClicked: app.removeWorkspaceSession(modelData.id)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Controls.Label {
                            text: "Názov:"
                        }
                        Controls.TextField {
                            Layout.fillWidth: true
                            text: modelData.displayName
                            Accessible.description: "Názov:"
                            onEditingFinished: app.renameWorkspaceSession(modelData.id, text)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Controls.Label {
                            text: "Počet plôch:"
                        }
                        Controls.SpinBox {
                            from: 1
                            to: 32
                            value: modelData.desktopCount
                            Accessible.description: "Počet plôch:"
                            onValueModified: app.setWorkspaceSessionDesktopCount(modelData.id, value)
                        }
                        Controls.CheckBox {
                            text: "Mriežka"
                            checked: modelData.hasRows
                            onToggled: app.setWorkspaceSessionRows(modelData.id, checked ? 1 : 0)
                        }
                        Controls.SpinBox {
                            from: 1
                            to: 32
                            value: modelData.hasRows ? modelData.rows : 1
                            enabled: modelData.hasRows
                            onValueModified: app.setWorkspaceSessionRows(modelData.id, value)
                        }
                        Controls.CheckBox {
                            text: "Obtáčanie"
                            checked: modelData.hasWrapping && modelData.wrapping
                            onToggled: app.setWorkspaceSessionWrapping(modelData.id, checked)
                        }
                    }
                }
            }
        }

        Kirigami.Heading {
            text: "Názvy plôch"
            level: 3
        }
        Repeater {
            model: app.workspaceDesktopEntries
            delegate: RowLayout {
                Layout.fillWidth: true
                Controls.Label {
                    text: modelData.sessionLabel + " · Plocha " + modelData.ordinal
                    Layout.preferredWidth: 200
                }
                Controls.TextField {
                    Layout.fillWidth: true
                    text: modelData.name
                    Accessible.description: modelData.sessionLabel + " · Plocha " + modelData.ordinal
                    onEditingFinished: app.setWorkspaceSessionDesktopName(modelData.sessionId, modelData.ordinal, text)
                }
            }
        }

        Kirigami.Heading {
            text: "Náhľad zmien (dry-run)"
            level: 3
        }
        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.8
            text: {
                const plan = app.workspacePlanPreview;
                if (!plan.sessionFound) {
                    return "Vyberte alebo vytvorte reláciu.";
                }
                if (!plan.observationAvailable) {
                    return "Pozorovanie plôch nie je dostupné; náhľad je neúplný a nič sa nemení.";
                }
                if (!plan.managementEnabled) {
                    return "Správa plôch je vypnutá; náhľad je informatívny a nič sa nemení.";
                }
                if (!plan.drift) {
                    return "Žiadny rozdiel medzi uloženou reláciou a bežiacimi plochami.";
                }
                return "Rozdiel oproti bežiacim plochám — Apply by neskôr vytvoril/premenoval iba chýbajúce položky; odstránenie extra plôch nie je predvolené.";
            }
        }
        Repeater {
            model: app.workspacePlanPreview.desktops
            delegate: Controls.Label {
                Layout.fillWidth: true
                text: "Plocha " + modelData.ordinal + " (" + modelData.name + "): "
                      + (modelData.create ? "vytvoriť"
                                          : (modelData.rename ? ("premenovať na " + modelData.name) : "bez zmeny"))
            }
        }
        Controls.Label {
            Layout.fillWidth: true
            visible: app.workspacePlanPreview.rowsChange
            text: "Mriežka (rows): " + app.workspacePlanPreview.observedRows + " → "
                  + app.workspacePlanPreview.desiredRows
        }
        Controls.Label {
            Layout.fillWidth: true
            visible: app.workspacePlanPreview.wrappingChange
            text: "Obtáčanie: " + app.workspacePlanPreview.observedWrapping + " → "
                  + app.workspacePlanPreview.desiredWrapping
        }
        Controls.Label {
            Layout.fillWidth: true
            visible: app.workspacePlanPreview.extraDesktop
            text: "Bežiace plochy obsahujú položky navyše; predvolený Apply ich neodstraňuje."
        }
        Repeater {
            model: app.workspacePlanPreview.launches
            delegate: Controls.Label {
                Layout.fillWidth: true
                text: modelData.displayName + " → plocha " + modelData.desktopOrdinal + " · spustenie: "
                      + modelData.intent + (modelData.maximize ? " · maximalizovať" : "")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Controls.Button {
                text: "Použiť"
                enabled: app.workspaceApplyAvailable && !app.workspaceApplyRunning
                onClicked: {
                    if (applyRemoveExtras.checked) {
                        removeExtrasDialog.open();
                    } else {
                        app.applyWorkspaceSession(applySwitchCurrent.checked, false);
                    }
                }
            }
            Controls.Button {
                text: "Vrátiť z checkpointu"
                enabled: app.workspaceCheckpointAvailable && !app.workspaceApplyRunning
                onClicked: app.revertWorkspaceApply()
            }
        }
        Controls.CheckBox {
            id: applySwitchCurrent
            text: "Prepnúť na prvú plochu relácie (voliteľné, mimo predvoleného Apply)"
        }
        Controls.CheckBox {
            id: applyRemoveExtras
            text: "Odstrániť prebytočné plochy (neexistuje vrátenie odstránených plôch späť)"
        }
        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.8
            text: "Predvolený Apply: vytvorenie chýbajúcich plôch, podmienené premenovanie, mriežka a obtáčanie. Spustenie aplikácií a maximalizácia sú samostatné voľby v profile aplikácie (karta Aplikácie). Vrátenie z checkpointu obnoví iba konfiguráciu plôch: už otvorené okná sa nepresúvajú a spustené aplikácie sa neukončujú."
        }
        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: app.workspaceApplyStatus.length > 0
            text: "Stav: " + app.workspaceApplyStatus
        }
        Controls.Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: app.workspaceLastResidual.length > 0
            text: "Obmedzený zvyšok: " + app.workspaceLastResidual
        }
        Controls.Dialog {
            id: removeExtrasDialog
            title: "Odstrániť prebytočné plochy?"
            modal: true
            standardButtons: Controls.Dialog.Ok | Controls.Dialog.Cancel
            Controls.Label {
                wrapMode: Text.WordWrap
                text: "Odstránené plochy sa nedajú vrátiť späť ako tie isté plochy; nové plochy dostanú nové identifikátory. Checkpoint obnoví pôvodný počet, názvy, mriežku a obtáčanie, ale odstránené plochy už nemajú pôvodné identifikátory. Pokračovať?"
            }
            onAccepted: app.applyWorkspaceSession(applySwitchCurrent.checked, true)
        }
        Controls.Button {
            text: "Uložiť"
            highlighted: true
            onClicked: app.save()
        }
        Controls.Label {
            text: app.saveStatus()
            opacity: 0.8
            visible: app.saveStatus().length > 0
        }
    }
}
