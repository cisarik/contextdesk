import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    title: "ContextDeck"
    minimumWidth: 720
    minimumHeight: 520
    width: 900
    height: 640

    globalDrawer: Kirigami.GlobalDrawer {
        title: "ContextDeck"
        titleIcon: "input-keyboard"
        isMenu: true
        actions: [
            Kirigami.Action {
                text: "Overview"
                icon.name: "view-visible"
                onTriggered: root.pageStack.replace(overviewPage)
            },
            Kirigami.Action {
                text: "Profiles"
                icon.name: "object-group"
                onTriggered: root.pageStack.replace(profilesPage)
            },
            Kirigami.Action {
                text: "Controls"
                icon.name: "input-keyboard"
                onTriggered: root.pageStack.replace(controlsPage)
            },
            Kirigami.Action {
                text: "Diagnostics"
                icon.name: "help-about"
                onTriggered: root.pageStack.replace(diagnosticsPage)
            }
        ]
    }

    Component {
        id: overviewPage
        OverviewPage {}
    }
    Component {
        id: profilesPage
        ProfilesPage {}
    }
    Component {
        id: controlsPage
        ControlsPage {}
    }
    Component {
        id: diagnosticsPage
        DiagnosticsPage {}
    }

    pageStack.initialPage: overviewPage
}
