import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root
    implicitHeight: 176
    implicitWidth: 560

    property string kind: app.heroKind
    property string badge: app.heroBadge
    property var zones: app.heroZones
    property var zoneNames: app.zoneNames

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        Item {
            id: board
            Layout.fillWidth: true
            Layout.preferredHeight: 108

            Row {
                id: strips
                anchors.fill: parent
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: 5
                    delegate: Item {
                        width: Math.max(8, (strips.width - 4 * strips.spacing) / 5)
                        height: strips.height

                        Rectangle {
                            anchors.fill: parent
                            radius: 8
                            color: {
                                if (root.kind === "untouched") {
                                    return "transparent";
                                }
                                if (root.kind === "off") {
                                    return "#000000";
                                }
                                if (root.kind === "direct" && root.zones && root.zones[index]) {
                                    return root.zones[index];
                                }
                                return Qt.rgba(Kirigami.Theme.highlightColor.r,
                                               Kirigami.Theme.highlightColor.g,
                                               Kirigami.Theme.highlightColor.b,
                                               0.18);
                            }
                            border.width: root.kind === "untouched" ? 0 : 1
                            border.color: Kirigami.Theme.disabledTextColor
                        }

                        Canvas {
                            id: dash
                            anchors.fill: parent
                            visible: root.kind === "untouched"
                            onPaint: {
                                const ctx = getContext("2d");
                                ctx.reset();
                                ctx.strokeStyle = Kirigami.Theme.highlightColor;
                                ctx.lineWidth = 2;
                                ctx.setLineDash([7, 5]);
                                ctx.strokeRect(3, 3, width - 6, height - 6);
                            }
                            SequentialAnimation on opacity {
                                running: root.kind === "untouched"
                                loops: Animation.Infinite
                                NumberAnimation { from: 0.35; to: 0.95; duration: 1100; easing.type: Easing.InOutSine }
                                NumberAnimation { from: 0.95; to: 0.35; duration: 1100; easing.type: Easing.InOutSine }
                            }
                            Connections {
                                target: Kirigami.Theme
                                function onHighlightColorChanged() { dash.requestPaint(); }
                            }
                            onVisibleChanged: if (visible) requestPaint()
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                        }
                    }
                }
            }

            Rectangle {
                visible: root.kind === "effect"
                anchors.fill: parent
                radius: 8
                opacity: 0.28
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#7c3aed" }
                    GradientStop { position: 1.0; color: "#06b6d4" }
                }
            }

            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Kirigami.Units.smallSpacing
                radius: height / 2
                color: Kirigami.Theme.backgroundColor
                border.color: Kirigami.Theme.disabledTextColor
                implicitHeight: badgeLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
                implicitWidth: badgeLabel.implicitWidth + Kirigami.Units.largeSpacing

                Controls.Label {
                    id: badgeLabel
                    anchors.centerIn: parent
                    text: root.badge
                    font.bold: true
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: 5
                Controls.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.85
                    text: (root.zoneNames && root.zoneNames[index]) ? root.zoneNames[index] : ""
                }
            }
        }
    }
}
