import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    title: "ContextDeck"
    minimumWidth: 880
    minimumHeight: 560
    width: 1040
    height: 680

    property string currentSection: "status"

    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None
    pageStack.globalToolBar.showNavigationButtons: Kirigami.ApplicationHeaderStyle.NoNavigationButtons

    function showSection(section) {
        if (section === currentSection && pageStack.currentItem) {
            return;
        }
        currentSection = section;
        let url = "";
        switch (section) {
        case "status":
            url = Qt.resolvedUrl("OverviewPage.qml");
            break;
        case "colors":
            url = Qt.resolvedUrl("ColorsPage.qml");
            break;
        case "apps":
            url = Qt.resolvedUrl("ApplicationsPage.qml");
            break;
        case "diagnostics":
            url = Qt.resolvedUrl("DiagnosticsPage.qml");
            break;
        case "advanced":
            url = Qt.resolvedUrl("ControlsPage.qml");
            break;
        }
        if (url !== "") {
            pageStack.replace(url);
        }
    }

    globalDrawer: Kirigami.GlobalDrawer {
        title: "ContextDeck"
        titleIcon: "input-keyboard"
        isMenu: false
        modal: Kirigami.Settings.isMobile
        collapsible: false
        drawerOpen: !Kirigami.Settings.isMobile
        actions: [
            Kirigami.Action {
                text: "Stav"
                icon.name: "view-visible"
                checkable: true
                checked: root.currentSection === "status"
                onTriggered: root.showSection("status")
            },
            Kirigami.Action {
                text: "Farby"
                icon.name: "color-picker"
                checkable: true
                checked: root.currentSection === "colors"
                onTriggered: root.showSection("colors")
            },
            Kirigami.Action {
                text: "Aplikácie"
                icon.name: "object-group"
                checkable: true
                checked: root.currentSection === "apps"
                onTriggered: root.showSection("apps")
            },
            Kirigami.Action {
                text: "Diagnostika"
                icon.name: "help-about"
                checkable: true
                checked: root.currentSection === "diagnostics"
                onTriggered: root.showSection("diagnostics")
            },
            Kirigami.Action {
                text: "Pokročilé"
                icon.name: "configure"
                checkable: true
                checked: root.currentSection === "advanced"
                onTriggered: root.showSection("advanced")
            }
        ]
    }

    pageStack.initialPage: Qt.resolvedUrl("OverviewPage.qml")
}
