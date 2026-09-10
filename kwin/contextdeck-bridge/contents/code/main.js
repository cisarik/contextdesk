const SERVICE = "io.github.cisarik.ContextDeck";
const PATH = "/io/github/cisarik/ContextDeck/Context1";
const IFACE = "io.github.cisarik.ContextDeck.Context1";
const BRIDGE_ID = "kwin-contextdeck-bridge";
const MAX_INVENTORY = 200;
const MAX_PARENT_WALK = 8;

var sequence = 0;

function nextSequence() {
    sequence += 1;
    return sequence;
}

function bounded(value) {
    var text = value === undefined || value === null ? "" : String(value);
    if (text.length > 256) {
        return text.substring(0, 256);
    }
    return text;
}

function windowIdNumber(window) {
    if (!window) {
        return 0;
    }
    if (window.internalId !== undefined && window.internalId !== null) {
        var id = String(window.internalId);
        var hash = 0;
        for (var i = 0; i < id.length; i++) {
            hash = ((hash << 5) - hash + id.charCodeAt(i)) | 0;
        }
        return hash;
    }
    if (typeof window.windowId === "number") {
        return window.windowId;
    }
    return 0;
}

function skipWindow(window) {
    if (!window) {
        return true;
    }
    if (window.desktopWindow || window.dock || window.splash || window.menu) {
        return true;
    }
    return false;
}

function resolveRoot(window) {
    var current = window;
    var seen = {};
    var steps = 0;
    while (current && current.transient && current.transientFor && steps < MAX_PARENT_WALK) {
        var id = String(current.internalId);
        if (seen[id]) {
            break;
        }
        seen[id] = true;
        current = current.transientFor;
        steps += 1;
    }
    return current;
}

function identityOf(window) {
    var root = resolveRoot(window);
    if (!root) {
        return { desktopFileName: "", resourceClass: "", resourceName: "", parentWindowId: 0 };
    }
    return {
        desktopFileName: bounded(root.desktopFileName),
        resourceClass: bounded(root.resourceClass),
        resourceName: bounded(root.resourceName),
        parentWindowId: windowIdNumber(root)
    };
}

function sendContext(window) {
    var identity = { desktopFileName: "", resourceClass: "", resourceName: "", parentWindowId: 0 };
    if (window && !skipWindow(window)) {
        identity = identityOf(window);
    }
    callDBus(SERVICE, PATH, IFACE, "ContextReport",
             BRIDGE_ID,
             nextSequence(),
             identity.desktopFileName,
             identity.resourceClass,
             identity.resourceName,
             identity.parentWindowId);
}

function identityKey(entry) {
    return entry.desktopFileName + "\x1f" + entry.resourceClass + "\x1f" + entry.resourceName;
}

function sendInventory() {
    var windows = workspace.windowList();
    var seen = {};
    var entries = [];
    for (var i = 0; i < windows.length; i++) {
        var window = windows[i];
        if (skipWindow(window)) {
            continue;
        }
        var identity = identityOf(window);
        if (!identity.desktopFileName && !identity.resourceClass && !identity.resourceName) {
            continue;
        }
        var key = identityKey(identity);
        if (seen[key]) {
            continue;
        }
        seen[key] = true;
        entries.push({
            desktop_file_name: identity.desktopFileName,
            resource_class: identity.resourceClass,
            resource_name: identity.resourceName
        });
        if (entries.length >= MAX_INVENTORY) {
            break;
        }
    }
    var payload = JSON.stringify({ entries: entries });
    callDBus(SERVICE, PATH, IFACE, "InventoryReport", BRIDGE_ID, nextSequence(), payload);
}

function sendHeartbeat() {
    callDBus(SERVICE, PATH, IFACE, "Heartbeat", BRIDGE_ID, nextSequence());
}

var debounce = new QTimer();
debounce.interval = 250;
debounce.singleShot = true;
var pendingWindow = null;
var inventoryDirty = false;

function flushDebounce() {
    if (pendingWindow !== null || inventoryDirty) {
        sendContext(workspace.activeWindow);
        sendInventory();
        pendingWindow = null;
        inventoryDirty = false;
    }
}

debounce.timeout.connect(flushDebounce);

function schedule(window) {
    pendingWindow = window;
    debounce.restart();
}

function scheduleInventory() {
    inventoryDirty = true;
    debounce.restart();
}

workspace.windowActivated.connect(function (window) {
    schedule(window);
});

workspace.windowAdded.connect(function (window) {
    scheduleInventory();
    if (window && window === workspace.activeWindow) {
        schedule(window);
    }
});

workspace.windowRemoved.connect(function () {
    scheduleInventory();
    schedule(workspace.activeWindow);
});

var heartbeat = new QTimer();
heartbeat.interval = 5000;
heartbeat.singleShot = false;
heartbeat.timeout.connect(sendHeartbeat);
heartbeat.start();

sendContext(workspace.activeWindow);
sendInventory();
sendHeartbeat();
