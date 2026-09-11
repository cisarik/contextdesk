import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    property string profileId: ""
    property string currentMode: "untouched"
    property var zones: []
    property bool applicationLevel: false
    property color startColor: "#ff0000"
    property color endColor: "#0000ff"
    property int speedPercent: 50
    property color breathingColor: "#7c3aed"
    readonly property bool animatedMode: currentMode === "wave" || currentMode === "cycle" || currentMode === "breathing"

    spacing: Kirigami.Units.smallSpacing

    function hexFromColor(c) {
        const r = Math.round(c.r * 255).toString(16).padStart(2, "0");
        const g = Math.round(c.g * 255).toString(16).padStart(2, "0");
        const b = Math.round(c.b * 255).toString(16).padStart(2, "0");
        return "#" + r + g + b;
    }

    function zoneHex(index) {
        if (root.currentMode === "untouched") {
            return "";
        }
        if (root.zones && root.zones[index]) {
            return root.zones[index];
        }
        return "#7c3aed";
    }

    function applyMode(modeName) {
        if (root.applicationLevel) {
            app.setApplicationLightingMode(root.profileId, modeName);
        } else {
            app.setGlobalLightingMode(modeName);
        }
    }

    function applyZone(index, hex) {
        if (root.applicationLevel) {
            app.setApplicationZoneColor(root.profileId, index, hex);
        } else {
            app.setGlobalZoneColor(index, hex);
        }
    }

    function applyGradient() {
        const start = hexFromColor(root.startColor);
        const end = hexFromColor(root.endColor);
        if (root.applicationLevel) {
            app.applyApplicationGradient(root.profileId, start, end);
        } else {
            app.applyGlobalGradient(start, end);
        }
    }

    function openZonePicker(index) {
        picker.pendingKind = "zone";
        picker.pendingIndex = index;
        const hex = zoneHex(index);
        picker.selectedColor = hex.length > 0 ? hex : "#7c3aed";
        picker.open();
    }

    function openGradientPicker(kind) {
        picker.pendingKind = kind;
        picker.pendingIndex = -1;
        picker.selectedColor = kind === "start" ? root.startColor : root.endColor;
        picker.open();
    }

    function openBreathingPicker() {
        picker.pendingKind = "breathing";
        picker.pendingIndex = -1;
        picker.selectedColor = root.breathingColor;
        picker.open();
    }

    function applySpeed(percent) {
        const value = Math.round(percent);
        if (root.applicationLevel) {
            app.setApplicationSpeed(root.profileId, value);
        } else {
            app.setGlobalSpeed(value);
        }
    }

    function applyBreathingColor(hex) {
        if (root.applicationLevel) {
            app.setApplicationBreathingColor(root.profileId, hex);
        } else {
            app.setGlobalBreathingColor(hex);
        }
    }

    Controls.ComboBox {
        id: presetBox
        Layout.fillWidth: true
        model: app.lightingPresetLabels
        currentIndex: Math.max(0, app.lightingPresets.indexOf(root.currentMode))
        onActivated: root.applyMode(app.lightingPresets[currentIndex])
    }

    Controls.Label {
        visible: root.animatedMode
        text: "Rýchlosť animácie"
        font.bold: true
        Layout.topMargin: Kirigami.Units.smallSpacing
    }

    RowLayout {
        visible: root.animatedMode
        Layout.fillWidth: true

        Controls.Slider {
            id: speedSlider
            Layout.fillWidth: true
            from: 0
            to: 100
            stepSize: 1
            value: root.speedPercent
            Accessible.name: "Rýchlosť animácie"
            onMoved: root.applySpeed(value)
        }
        Controls.Label {
            text: Math.round(speedSlider.value) + " %"
            Layout.preferredWidth: 48
        }
    }

    RowLayout {
        visible: root.currentMode === "breathing"
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Controls.Label {
            text: "Farba dýchania"
            font.bold: true
            Layout.fillWidth: true
        }
        Rectangle {
            width: 36
            height: 36
            radius: 6
            color: root.breathingColor
            border.width: 1
            border.color: Kirigami.Theme.disabledTextColor
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                Accessible.name: "Farba dýchania"
                onClicked: root.openBreathingPicker()
            }
        }
    }

    Repeater {
        model: 5
        delegate: RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Controls.Label {
                text: app.zoneNames[index]
                Layout.preferredWidth: 168
                wrapMode: Text.WordWrap
            }

            Rectangle {
                id: swatch
                width: 36
                height: 36
                radius: 6
                color: root.currentMode === "untouched" ? "transparent" : root.zoneHex(index)
                border.width: root.currentMode === "untouched" ? 0 : 1
                border.color: Kirigami.Theme.disabledTextColor

                Canvas {
                    anchors.fill: parent
                    visible: root.currentMode === "untouched"
                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();
                        ctx.strokeStyle = Kirigami.Theme.highlightColor;
                        ctx.lineWidth = 2;
                        ctx.setLineDash([4, 3]);
                        ctx.strokeRect(2, 2, width - 4, height - 4);
                    }
                    onVisibleChanged: if (visible) requestPaint()
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    Accessible.name: "Pick color for " + app.zoneNames[index]
                    onClicked: root.openZonePicker(index)
                }
            }

            Controls.TextField {
                visible: hexSwitch.checked
                text: root.zoneHex(index)
                placeholderText: "#rrggbb"
                Layout.fillWidth: true
                onEditingFinished: root.applyZone(index, text)
            }

            Item {
                visible: !hexSwitch.checked
                Layout.fillWidth: true
            }
        }
    }

    Controls.Switch {
        id: hexSwitch
        text: "Pokročilé: hex"
    }

    Controls.Label {
        text: "Gradient (päť pásov z dvoch farieb)"
        font.bold: true
        Layout.topMargin: Kirigami.Units.smallSpacing
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.largeSpacing

        ColumnLayout {
            Controls.Label { text: "Start Color" }
            Rectangle {
                width: 48
                height: 32
                radius: 6
                color: root.startColor
                border.color: Kirigami.Theme.disabledTextColor
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.openGradientPicker("start")
                }
            }
        }
        ColumnLayout {
            Controls.Label { text: "End Color" }
            Rectangle {
                width: 48
                height: 32
                radius: 6
                color: root.endColor
                border.color: Kirigami.Theme.disabledTextColor
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.openGradientPicker("end")
                }
            }
        }
        Item { Layout.fillWidth: true }
    }

    Row {
        Layout.fillWidth: true
        spacing: 2
        Repeater {
            model: app.previewGradient(root.hexFromColor(root.startColor), root.hexFromColor(root.endColor))
            delegate: Rectangle {
                width: Math.max(12, (root.width - 8) / 5)
                height: 22
                radius: 3
                color: modelData
                border.color: Kirigami.Theme.disabledTextColor
            }
        }
    }

    Controls.Button {
        text: "Použiť gradient"
        onClicked: root.applyGradient()
    }

    Controls.Label {
        text: "Zónové farby platia v režime Direct. Toto je päťzónové svetlo, nie per-key RGB."
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        opacity: 0.8
    }

    ColorDialog {
        id: picker
        title: "Farba zóny"
        property string pendingKind: "zone"
        property int pendingIndex: 0
        onAccepted: {
            const hex = root.hexFromColor(selectedColor);
            if (pendingKind === "start") {
                root.startColor = selectedColor;
            } else if (pendingKind === "end") {
                root.endColor = selectedColor;
            } else if (pendingKind === "breathing") {
                root.breathingColor = selectedColor;
                root.applyBreathingColor(hex);
            } else {
                root.applyZone(pendingIndex, hex);
            }
        }
    }
}
