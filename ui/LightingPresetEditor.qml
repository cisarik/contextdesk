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
    property string startHex: "#ff0000"
    property string endHex: "#0000ff"
    property int speedPercent: 50
    property string breathingHex: "#7c3aed"
    property bool workspaceLayoutActive: false
    property var zoneSlots: []
    property var displayZones: []
    property var previewBands: []
    property string modeOverride: ""
    property bool readyToSave: false
    readonly property string effectiveMode: modeOverride.length > 0 ? modeOverride : currentMode
    readonly property bool animatedMode: effectiveMode === "wave" || effectiveMode === "cycle" || effectiveMode === "breathing"
    readonly property color startColor: startHex
    readonly property color endColor: endHex
    readonly property color breathingColor: breathingHex

    spacing: Kirigami.Units.smallSpacing

    onStartHexChanged: root.refreshPreview()
    onEndHexChanged: root.refreshPreview()
    Component.onCompleted: {
        root.syncDisplayZones();
        root.refreshPreview();
    }
    onZonesChanged: root.syncDisplayZones()
    onCurrentModeChanged: {
        if (currentMode === "direct") {
            modeOverride = "";
        }
        root.syncDisplayZones();
    }

    function toRrggbb(value) {
        if (value === undefined || value === null) {
            return "";
        }
        if (typeof value === "string") {
            let text = value.trim().toLowerCase();
            if (text.length > 0 && text.charAt(0) !== "#") {
                text = "#" + text;
            }
            if (/^#[0-9a-f]{6}$/.test(text)) {
                return text;
            }
            if (/^#[0-9a-f]{8}$/.test(text)) {
                // Qt QColor can stringify as #aarrggbb; keep the last six RGB digits.
                return "#" + text.slice(text.length - 6);
            }
            return "";
        }
        const red = Number(value.r);
        const green = Number(value.g);
        const blue = Number(value.b);
        if (isNaN(red) || isNaN(green) || isNaN(blue)) {
            return "";
        }
        const scaled = (red <= 1 && green <= 1 && blue <= 1);
        const r = Math.round(scaled ? red * 255 : red);
        const g = Math.round(scaled ? green * 255 : green);
        const b = Math.round(scaled ? blue * 255 : blue);
        const hex = (n) => Math.max(0, Math.min(255, n)).toString(16).padStart(2, "0");
        return "#" + hex(r) + hex(g) + hex(b);
    }

    function acceptedPickerHex() {
        const hex = root.toRrggbb(picker.selectedColor);
        if (hex.length === 7) {
            return hex;
        }
        return picker.pendingHex;
    }

    function syncDisplayZones() {
        if (root.zones && root.zones.length === 5) {
            root.displayZones = Array.prototype.slice.call(root.zones);
        }
    }

    function refreshPreview() {
        const start = root.toRrggbb(root.startHex);
        const end = root.toRrggbb(root.endHex);
        if (start.length !== 7 || end.length !== 7) {
            root.previewBands = [];
            return;
        }
        root.previewBands = app.previewGradient(start, end);
    }

    function displayZoneHex(index) {
        if (root.effectiveMode === "untouched") {
            return "";
        }
        if (root.displayZones && root.displayZones.length === 5 && root.displayZones[index]) {
            return root.displayZones[index];
        }
        if (root.zones && root.zones[index]) {
            return root.zones[index];
        }
        return "#7c3aed";
    }

    function applyMode(modeName) {
        root.modeOverride = "";
        if (root.applicationLevel) {
            app.setApplicationLightingMode(root.profileId, modeName);
        } else {
            app.setGlobalLightingMode(modeName);
        }
    }

    function applyZone(index, hex) {
        const clean = root.toRrggbb(hex);
        if (clean.length !== 7) {
            return;
        }
        const next = (root.displayZones && root.displayZones.length === 5)
            ? Array.prototype.slice.call(root.displayZones)
            : ["#7c3aed", "#7c3aed", "#7c3aed", "#7c3aed", "#7c3aed"];
        next[index] = clean;
        root.displayZones = next;
        root.modeOverride = "direct";
        root.readyToSave = true;
        if (root.applicationLevel) {
            app.setApplicationZoneColor(root.profileId, index, clean);
        } else {
            app.setGlobalZoneColor(index, clean);
        }
    }

    function applyGradient() {
        const start = root.toRrggbb(root.startHex);
        const end = root.toRrggbb(root.endHex);
        const generated = app.previewGradient(start, end);
        if (!generated || generated.length !== 5) {
            return;
        }
        root.displayZones = Array.prototype.slice.call(generated);
        root.modeOverride = "direct";
        root.previewBands = generated;
        root.readyToSave = true;
        if (root.applicationLevel) {
            app.applyApplicationGradient(root.profileId, start, end);
        } else {
            app.applyGlobalGradient(start, end);
        }
    }

    function openPicker(kind, index, hex) {
        picker.pendingKind = kind;
        picker.pendingIndex = index;
        picker.pendingHex = hex;
        picker.selectedColor = hex;
        picker.open();
    }

    function openZonePicker(index) {
        const hex = root.displayZoneHex(index);
        root.openPicker("zone", index, hex.length > 0 ? hex : "#7c3aed");
    }

    function openGradientPicker(kind) {
        root.openPicker(kind, -1, kind === "start" ? root.startHex : root.endHex);
    }

    function openBreathingPicker() {
        root.openPicker("breathing", -1, root.breathingHex);
    }

    function applySpeed(percent) {
        const value = Math.round(percent);
        root.readyToSave = true;
        if (root.applicationLevel) {
            app.setApplicationSpeed(root.profileId, value);
        } else {
            app.setGlobalSpeed(value);
        }
    }

    function applyBreathingColor(hex) {
        const clean = root.toRrggbb(hex);
        if (clean.length !== 7) {
            return;
        }
        root.breathingHex = clean;
        root.readyToSave = true;
        if (root.applicationLevel) {
            app.setApplicationBreathingColor(root.profileId, clean);
        } else {
            app.setGlobalBreathingColor(clean);
        }
    }

    Controls.ComboBox {
        id: presetBox
        Layout.fillWidth: true
        model: app.lightingPresetLabels
        currentIndex: Math.max(0, app.lightingPresets.indexOf(root.effectiveMode))
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
        visible: root.effectiveMode === "breathing"
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
            color: root.breathingHex
            border.width: 1
            border.color: breathingArea.activeFocus ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
            MouseArea {
                id: breathingArea
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Farba dýchania"
                Accessible.onPressAction: root.openBreathingPicker()
                Keys.onReturnPressed: root.openBreathingPicker()
                Keys.onSpacePressed: root.openBreathingPicker()
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
                text: {
                    const slot = (root.zoneSlots && root.zoneSlots.length === 5) ? root.zoneSlots[index] : null;
                    const role = slot && slot.role ? slot.role : "static";
                    if (role === "desktop_indicator") {
                        const ordinal = slot && slot.ordinal ? slot.ordinal : 0;
                        return ordinal > 0 ? (app.zoneNames[index] + " · plocha " + ordinal) : (app.zoneNames[index] + " · indikátor");
                    }
                    if (role === "app_color") {
                        return app.zoneNames[index] + " · aplikácia";
                    }
                    if (role === "off") {
                        return app.zoneNames[index] + " · vypnuté";
                    }
                    return app.zoneNames[index];
                }
                Layout.preferredWidth: 168
                wrapMode: Text.WordWrap
            }

            Controls.ComboBox {
                visible: !root.applicationLevel
                Layout.preferredWidth: 140
                Accessible.description: app.zoneNames[index]
                model: ["Statická", "Indikátor plochy", "Farba aplikácie", "Vypnuté"]
                property var roleIds: ["static", "desktop_indicator", "app_color", "off"]
                currentIndex: {
                    const slot = (root.zoneSlots && root.zoneSlots.length === 5) ? root.zoneSlots[index] : null;
                    const role = slot && slot.role ? slot.role : "static";
                    const found = roleIds.indexOf(role);
                    return found >= 0 ? found : 0;
                }
                onActivated: app.setGlobalZoneRole(index, roleIds[currentIndex])
            }

            Rectangle {
                id: swatch
                width: 36
                height: 36
                radius: 6
                color: root.effectiveMode === "untouched"
                       ? "transparent"
                       : (root.displayZones.length === 5 ? root.displayZones[index] : root.displayZoneHex(index))
                border.width: swatchArea.activeFocus ? 1 : (root.effectiveMode === "untouched" ? 0 : 1)
                border.color: swatchArea.activeFocus ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor

                Canvas {
                    anchors.fill: parent
                    visible: root.effectiveMode === "untouched"
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
                    id: swatchArea
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: "Pick color for " + app.zoneNames[index]
                    Accessible.onPressAction: root.openZonePicker(index)
                    Keys.onReturnPressed: root.openZonePicker(index)
                    Keys.onSpacePressed: root.openZonePicker(index)
                    onClicked: root.openZonePicker(index)
                }
            }

            Controls.TextField {
                visible: hexSwitch.checked
                Accessible.description: app.zoneNames[index]
                text: root.displayZoneHex(index)
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
                color: root.startHex
                border.width: startSwatchArea.activeFocus ? 1 : 0
                border.color: startSwatchArea.activeFocus ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                MouseArea {
                    id: startSwatchArea
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: "Start Color"
                    Accessible.onPressAction: root.openGradientPicker("start")
                    Keys.onReturnPressed: root.openGradientPicker("start")
                    Keys.onSpacePressed: root.openGradientPicker("start")
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
                color: root.endHex
                border.width: endSwatchArea.activeFocus ? 1 : 0
                border.color: endSwatchArea.activeFocus ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                MouseArea {
                    id: endSwatchArea
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: "End Color"
                    Accessible.onPressAction: root.openGradientPicker("end")
                    Keys.onReturnPressed: root.openGradientPicker("end")
                    Keys.onSpacePressed: root.openGradientPicker("end")
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
            model: root.previewBands
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
        visible: !root.applicationLevel && app.workspaceLayoutActive
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: "Použitie gradientu nahradí dynamické role pevnými statickými zónami."
    }
    Controls.Label {
        visible: root.applicationLevel && app.workspaceLayoutActive
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: "Kým je aktívne workspace rozloženie, tento preset plní len aplikačné sloty. Dynamické globálne role sa sem nekopírujú."
    }

    Controls.Label {
        visible: root.readyToSave
        text: "Zmeny sú pripravené — stlač Uložiť."
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        color: Kirigami.Theme.positiveTextColor
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
        property string pendingHex: "#7c3aed"
        onAccepted: {
            const hex = root.acceptedPickerHex();
            if (hex.length !== 7) {
                return;
            }
            if (pendingKind === "start") {
                root.startHex = hex;
            } else if (pendingKind === "end") {
                root.endHex = hex;
            } else if (pendingKind === "breathing") {
                root.applyBreathingColor(hex);
            } else {
                root.applyZone(pendingIndex, hex);
            }
        }
    }
}
