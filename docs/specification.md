# ContextDeck specification

Product behavior for the M1 configuration and lighting contract. This file is
the owner of terminology, assignment states, matchers, persistence, the control
catalog, lighting, context fallbacks, and explicit non-goals. Architecture and
process live elsewhere; operations and IRL tests have their own owners.

## Terminology

- **G213**: Logitech G213 Prodigy keyboard, USB vendor `046d`, product `c336`.
  Five physical RGB zones. Not per-key RGB.
- **Session application**: the `contextdeck` process. Owns profiles, the KWin
  context receiver, the OpenRGB client, the tray, and approved desktop actions.
  It never reads, grabs, or injects keyboard or HID events.
- **Profile document**: `$XDG_CONFIG_HOME/contextdeck/profiles.json`, falling
  back to `$HOME/.config/contextdeck/profiles.json`.
- **Assignment**: the typed action bound to one catalog control in a profile.
- **Context identity**: `desktop_file_name`, `resource_class`, and
  `resource_name` as reported by the KWin bridge. Captions, PIDs, and
  executable paths are not identity.

## Layering

1. Typed profile document (schema_version 2).
2. Deterministic resolver: identity + control → assignment, and identity →
   lighting preset. No I/O, D-Bus, device access, or GUI.
3. Context bridge supplies identity to the session application.
4. Lighting client applies the resolved **desired state** (a device mode plus,
   for `direct`, five zone colors) through OpenRGB SDK protocol 5 on loopback.
   `untouched` produces no device traffic.
5. The input broker consumes the same assignments in a later whole. Until
   remapping is armed, `emit_shortcut` values are stored and shown, not
   executed. S4 session IPC can lease/arm the broker; M2 still has no mapped
   actions (pass-through only once S5 grabs).

## Assignment states

`assignment.action` is one of:

| Action | Meaning |
|--------|---------|
| `inherit_global` | Valid only on an application profile. Resolve through the matching global assignment. |
| `pass_through` | Do not consume the control. Absence of an assignment means this, never `disabled`. |
| `disabled` | Explicit consumption with no effect. Never inferred from a missing key. |
| `emit_shortcut` | Store one chord: a single key from the bounded Linux key table plus modifiers ⊆ {`ctrl`, `shift`, `alt`, `super`}. No sequences, macros, text, or command fields. |
| `approved_system_action` | `action_id` ∈ {`suspend`, `displays_off`}. Stored and offered as user-initiated tray/UI actions in M1. Never bound to a physical key in M1. |

Unset means inherit, not swallow. An unidentified or stale context resolves
through the global profile. A cold start with no valid file resolves to
pass-through plus **untouched** lighting and writes nothing.

`inherit_global` on the global profile is rejected.

## Matchers

An application profile `match` object may contain `desktop_file_name`,
`resource_class`, and `resource_name` (all optional strings; at least one
required). Every specified field must equal the observed identity. Caption,
title, PID, and executable path are rejected as unknown semantic fields.

When several profiles agree with the identity, ranking is:

1. `desktop_file_name` equality
2. `resource_class` equality
3. `resource_name` as a tiebreaker

Equal rank keeps file order (first wins). An empty identity matches no
application profile and uses the global profile.

## schema_version policy

The only activatable schema is integer `2`. A missing, non-integer, or other
`schema_version` is refused. A **future** version (greater than 2) is refused
without rewriting the file. Unknown semantic fields are rejected rather than
silently discarded. Device scope other than vendor `046d`, product `c336`,
model `logitech-g213-prodigy` is rejected.

A version-1 file is read, migrated in memory to schema 2, and used. It is
**never rewritten on disk** unless the user saves. Version-1 `automatic`
becomes `untouched` (the color is kept as `base_color` for later Direct use);
`lights_off` becomes `off`; `temporary_color` becomes `direct`. A failed
version-1 lighting migration preserves the original bytes and yields
pass-through plus untouched lighting.

## Persistence and recovery

- The entire draft is validated before activation.
- Replacement uses `QSaveFile` with no direct-write fallback.
- Replacing a previously valid file keeps exactly one sibling backup,
  `profiles.json.bak`.
- Any validation or write failure leaves the previous bytes untouched and
  returns a structured error: reason, JSON path or field, and `preserved`.
- Configuration root is injectable so tests never touch the real user config.
- Maximum document size is 1 MiB.

## Control catalog

Always-in-model: F1–F12, Previous, PlayPause, Next, Mute, VolumeDown,
VolumeUp.

**Conditional on hardware evidence (G1):** GameMode, Backlight. They are named
in the catalog and may be stored as `pass_through`, `disabled`, or
`inherit_global`. They must not be bound to `emit_shortcut` or
`approved_system_action` in M1. PrintScreen and Pause/Break are never silent
substitutes for them.

## Lighting

Five physical zones, never per-key color. Zone names, in order: Left Area,
Middle Area, Right Area, Arrow and Homekeys, Numpad. Each zone entry is a
small typed value (a color today) so a later zone-role model can be added
without reshaping the document. Zone roles, desktop awareness, and workspace
logic are out of this whole.

The keyboard has **no readback**. "Sent successfully" is never hardware
acceptance. Desired state, connection state, and last error are three
separate truths. Only the operator's eyes close a lighting claim.

`lighting` is a preset object:

| Field | Meaning |
|-------|---------|
| `mode` | ∈ {`untouched`, `direct`, `wave`, `cycle`, `breathing`, `off`} |
| `zones` | `null` or exactly five `#rrggbb` entries. Meaningful for `direct`. |
| `base_color` | Optional `#rrggbb`. Migration source from version 1, the single-color form of `direct`, and the **mode-specific Breathing color**. When Breathing is selected without a color, ContextDeck sends `#7c3aed`. |
| `restore_mode` | Device mode to return to (`direct`, `wave`, `cycle`, `breathing`, `off`). Defaults to `wave`. Not `untouched`. |
| `speed` | Optional non-negative integer. OpenRGB protocol speed for `wave`, `cycle`, and `breathing`. Absent means the controller's default for that mode. Encoded values are clamped to the mode's `speed_min`/`speed_max` (G213 ranges may be inverted: slower is a larger number). |

Unknown mode names, wrong zone counts, and unknown semantic fields are
rejected. `direct` requires `base_color` or exactly five zones. `speed` is
ignored for `untouched`, `direct`, and `off` at encode time but may still be
stored.

Default is **non-destructive**: until the user expresses intent, ContextDeck
does not touch the device. The honest lighting label remains
`untouched — device default` and is never displayed as Automatic. The Overview
Hero must not paint `untouched` as solid black; black means `off`. Untouched
zones are hollow/dashed placeholders with a badge such as
`Device default (Wave)`.

Resolver lighting: application preset wins; otherwise global preset; otherwise
`untouched`. An unidentified or stale context resolves to the global preset.
A temporary override (session, not a document field) outranks the resolved
preset, expires on the next *external* application-identity change, and is
never expired by opening this application's UI.

`Restore device default` returns the device to the recorded `restore_mode`
(assumed `wave` when unknown) and stops touching it.

Zone-accent for mapped keys is specified with an explicit control-to-zone
table. Every entry ships `verified: false` until the IRL probe fills
`docs/hardware/g213-zone-map.md`. While unverified, accent writes to hardware
are inert; the UI may show a labelled preview. No physical key-to-zone fact
is claimed without measurement.

## Context conditions and fallbacks

| Condition | Mapping / lighting |
|-----------|--------------------|
| Identified app with profile | Application overrides, else global, else pass-through |
| Identified app without profile | Global, else pass-through |
| Unknown, stale, or missing identity | Global profile; lighting stays on the global preset, never forced to off |
| Bridge lost (three missed 5 s heartbeats) | Same as unknown identity, with one bounded warning |
| ContextDeck settings / unsuitable shell surface | Neutral / pass-through; does not expire a temporary lighting override |

## Power actions

`displays_off` uses a **long-lived** `KScreen::Dpms` member (not a stack
temporary) after `isSupported()`, without changing KScreen topology,
debounced 2 s. The object must outlive the asynchronous `switchMode()` call.

`suspend` uses `org.freedesktop.login1.Manager.Suspend(false)` on the system
bus after `CanSuspend`, with confirmation, debounced 5 s.

Never `/sys/power/state`, never `systemctl`, never `QProcess`.

## Explicit non-goals

- No input interception of any kind (no libevdev, uinput, `/dev/input`, HID
  claims, grabbing, filtering, replay, or injection).
- No per-key RGB.
- No macros, shell strings, or executable configuration.
- No other keyboards, operating systems, or generic remappers.
- No plugins, telemetry, cloud, or web UI.

Chord recording, when present in the settings window, captures keys only while
its own control has focus inside that window. That is not input interception.

## Context D-Bus API

Session-bus name `io.github.cisarik.ContextDeck` is requested **without**
replacing an existing owner. If the name is taken, the application stays up in
a degraded read-only state and does not steal the name.

Object `/io/github/cisarik/ContextDeck/Context1`, interface
`io.github.cisarik.ContextDeck.Context1`:

| Member | Kind | Arguments / type |
|--------|------|------------------|
| `ContextReport` | method | `bridge_id s`, `sequence u`, `desktop_file_name s`, `resource_class s`, `resource_name s`, `parent_window_id x` |
| `InventoryReport` | method | `bridge_id s`, `sequence u`, `payload_json s` (hard cap 64 KiB, max 200 unique identity entries) |
| `Heartbeat` | method | `bridge_id s`, `sequence u` |
| `CurrentIdentity` | property | `s` (JSON of the three identity fields only) |
| `BridgeConnected` | property | `b` |
| `PolicyRevision` | property | `u` |

Every argument is untrusted and bounded. Stale or out-of-order sequence numbers
are rejected. There is no method that injects input or executes anything.
Heartbeat interval is 5 s; three missed heartbeats mark the bridge lost.
The KWin script sends a full `ContextReport` on every heartbeat interval as
well as on window events, so an application restart recovers identity without
a focus change. The receiver treats a repeated identical identity as a refresh:
it does not bump `PolicyRevision`, does not emit a context-change, and does
not rewrite lighting.

The KWin script lives at `kwin/contextdeck-bridge/` as a `KWin/Script` package.
It uses `workspace.windowActivated`, `windowAdded`, and `windowRemoved`
(KWin 6.7.5 scripting has no `windowClosed` on Workspace; that name is an
effects API). Identity is resolved by walking `transientFor` with a cycle
guard. Captions, PIDs, and executable paths are never sent.

KWin 6.7.5 `callDBus` is varargs (up to nine extra arguments) and converts
JavaScript numbers to doubles. The receiver coerces integer-valued doubles
(and strings of digits) into `u`/`x` rather than changing the advertised
signature.

## OpenRGB client (protocol 5)

Loopback only (`127.0.0.1:6742`). Client name `ContextDeck`. Protocol version
is negotiated first; a server version newer than 5 is rejected (lighting
disabled, application stays up). Controllers are selected by Logitech + G213
identity, not by a stored index. `DEVICE_LIST_UPDATED` and reconnects
re-enumerate. Enumeration on connect must not change device state: no mode
selection and no color write without explicit resolved lighting intent. The
device must never be left in Direct with all-zero colors without intent.

Mode changes go through `UPDATEMODE` (packet 1101) using the Mode Data block
from the server's controller description (protocol 5 includes `mode_value`,
which the client echoes). For `wave` and `cycle`, an optional stored `speed` is
written into that block (clamped to the mode range). For `breathing`, the same
speed field is written and **exactly one mode-specific color** is required:
OpenRGB's G213 backend reads `MODE_COLORS_MODE_SPECIFIC` and otherwise breathes
black. ContextDeck therefore copies `base_color` (default `#7c3aed`) into
`mode.colors[0]` before encoding. Per-zone colors in `direct` go through
whole-device `UPDATELEDS` for five little-endian `0x00BBGGRR` colors.
`untouched` sends no frame. `SETCUSTOMMODE` is not used as a connect-time
default. If OpenRGB reports the active mode as Direct at first enumeration,
ContextDeck treats that as unknown and records `wave` for restore (OpenRGB's
own init must not be confused with the keyboard firmware effect).

Frames: magic `ORGB`, 16-byte little-endian header (device index, packet id,
payload size). Wrong magic, truncated frames, and payloads above 1 MiB are
rejected. Connect timeout 2 s, request timeout 5 s. Latest desired color
wins; updates coalesce at 20 Hz. Reconnect uses bounded backoff. Failure
disables lighting only. "Sent successfully" is not hardware confirmation:
desired color, connection state, and last error are separate.

## Session lighting overrides

Tray and Overview can set a **temporary override**, **lights off** (`off`),
**restore automatic** (follow the resolved preset), or **restore device
default**. An override is never labelled Automatic. A temporary override
expires on the next *external* application-identity change; opening this
application's tray or settings does not expire it. `off` holds until
automatic is restored or device default is restored.

## Settings UI (Kirigami, Plasma 6 desktop)

The settings window is a task-oriented desktop UI, not a map of internal
code units. Primary navigation is a persistent sidebar (not a cramped overlay
drawer):

| Section | Purpose |
|---------|---------|
| **Stav** (Overview) | Five-zone Hero preview, one human-readable status sentence, empty-state CTA |
| **Farby** | Global lighting preset, visual zone pickers, gradient helper, animation speed, Breathing color |
| **Aplikácie** | Per-application lighting presets from the KWin inventory |
| **Diagnostika** | D-Bus names, bridge id, socket/SDK state, counters, power actions |
| **Pokročilé** | Inactive M2 shortcut catalog and chord recorder |

Overview must be readable in about two seconds: whether OpenRGB is connected,
which context is in view, and whether lights are still firmware Wave or a
ContextDeck preset. Technical fields (`ready`, `policyRevision`, the D-Bus
name `io.github.cisarik.ContextDeck`, bridge id, socket state) belong only on
Diagnostika.

When the settings window itself has focus, Overview must not present the raw
D-Bus service name or an empty identity as the live context. It shows
`ContextDeck (toto okno)` or the last observed external application
(`Posledná aplikácia: …`).

Hero presentation:

| Resolved lighting | Hero |
|-------------------|------|
| `untouched` | Hollow/dashed zone strips + `Device default (Wave)` (or the recorded restore mode) |
| `direct` | Five solid strips in the resolved hex colors |
| `wave` / `cycle` / `breathing` | Effect badge; strips are not claimed as measured per-zone colors |
| `off` | Solid black strips + `Off` (black means off) |

Zone swatches, Breathing color, and gradient start/end open a system
`ColorDialog`. The accepted color is stored as `#rrggbb` (an 8-digit
`#aarrggbb` alpha prefix is discarded). Hex text fields are an advanced
option on Farby, not on Overview. The gradient helper stores start and end as
`#rrggbb` on the editor (not on the dialog), shows a live five-band preview as
those colors change, and **Použiť gradient** immediately paints the five zone
swatches, switches the preset to `direct`, and sends the colors. Persistence is
still **Uložiť**. Wave, Cycle, and Breathing show **Rýchlosť animácie** (0–100,
mapped onto the OpenRGB mode speed range). Breathing also shows **Farba
dýchania**. Overview's primary action is **Nastaviť farby**; **Follow
profile** appears only when a preset exists; **Restore device default** and
**Lights off** sit in overflow.

Key remapping is not on the primary path. Pokročilé states that remapping
becomes active in M2 and that M1 lights and detects context.

## Optional user unit

`packaging/systemd/contextdeck-session.service` is a user unit with **no**
`[Install]` section. It is started manually. It is not enabled by default.
