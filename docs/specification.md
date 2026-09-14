# ContextDeck specification

Product behavior for the configuration and lighting contract. This file is
the owner of terminology, assignment states, matchers, persistence, the control
catalog, lighting, workspace observation, context fallbacks, and explicit
non-goals. Architecture and process live elsewhere; operations and IRL tests
have their own owners.

## Terminology

- **G213**: Logitech G213 Prodigy keyboard, USB vendor `046d`, product `c336`.
  Five physical RGB zones. Not per-key RGB.
- **Session application**: the `contextdeck` process. Owns profiles, the KWin
  context receiver, virtual-desktop observation, the OpenRGB client, the tray,
  and approved desktop actions. It never reads, grabs, or injects keyboard or
  HID events.
- **Profile document**: `$XDG_CONFIG_HOME/contextdeck/profiles.json`, falling
  back to `$HOME/.config/contextdeck/profiles.json`.
- **Assignment**: the typed action bound to one catalog control in a profile.
- **Context identity**: `desktop_file_name`, `resource_class`, and
  `resource_name` as reported by the KWin bridge. Captions, PIDs, and
  executable paths are not identity.

## Layering

1. Typed profile document (schema_version 4).
2. Deterministic resolver: identity + control → assignment, and identity +
   workspace state → lighting preset. No I/O, D-Bus, device access, or GUI.
3. Context bridge supplies application identity to the session application.
   Virtual-desktop state is observed separately through KWin's
   `VirtualDesktopManager`.
4. Lighting client applies the resolved **desired state** (a device mode plus,
   for `direct`, five zone colors) through OpenRGB SDK protocol 5 on loopback.
   `untouched` produces no device traffic.
5. The input broker consumes the same assignments in a later whole. Until
   remapping is armed, `emit_shortcut` values are stored and shown, not
   executed. S4 session IPC can lease/arm the broker. S5 production ARM is an
   explicit session-app action (tray / Diagnostics) after an authenticated
   lease; M2 still has no mapped actions (pass-through only once G4 grabs).

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

### Workspace assignment resolution (M4)

`MatchSpec` remains the only identity matcher. The M4 assignment resolver is
pure and in-memory:

1. Identity match through the typed matcher wins. A matched profile contributes
   its `workspace` assignment when present.
2. Only when identity matching fails **and both** the global
   `title_fallback_enabled` preference and the profile's
   `title_fallback.enabled` are true, the user-authored `pattern` is compared
   with the profile's `mode` against the focused-window caption. A profile with
   no workspace assignment or an empty pattern cannot match.
3. A title match selects that profile's assignment for that one focus event
   only. The caption is discarded after the call; it is never stored, never
   logged, and never added to `match` or `MatchSpec`.
4. When either flag is false, no caption comparison occurs at all.

Modes are `exact`, `contains`, and `prefix`, compared case-sensitively.
Ordinary lighting resolution is unchanged by M4: it remains temporary override
→ workspace layout → matching application preset → global preset.

## schema_version policy

The only activatable schema is integer `4`. A missing, non-integer, or other
`schema_version` is refused. A **future** version (greater than 4) is refused
without rewriting the file. Unknown semantic fields are rejected rather than
silently discarded. Device scope other than vendor `046d`, product `c336`,
model `logitech-g213-prodigy` is rejected.

A version-1 file is read, migrated in memory to schema 4, and used. It is
**never rewritten on disk** unless the user saves. Version-1 `automatic`
becomes `untouched` (the color is kept as `base_color` for later Direct use);
`lights_off` becomes `off`; `temporary_color` becomes `direct`. A failed
version-1 lighting migration preserves the original bytes and yields
pass-through plus untouched lighting. The same in-memory mapping applies to
valid schema-2 documents. Valid schema-1/2/3 documents migrate with
`workspace_sessions` empty and no per-application assignments; lighting, keys,
matches, application order, and preferences are preserved exactly. New
schema-4-only keys are rejected as unknown semantic fields under schemas 1–3.

## Workspace sessions and assignments (schema 4)

Root keys are exactly `schema_version`, `device`, `global`, `applications`,
`preferences`, and `workspace_sessions`.

`preferences` adds `workspace_management_enabled` (default `false`),
`title_fallback_enabled` (default `false`), and the optional
`active_workspace_session_id`; existing preference fields are preserved.

`workspace_sessions[]` entries:

| Field | Rule |
|-------|------|
| `id` | non-empty, unique, ≤128 UTF-8 bytes, no control characters |
| `display_name` | non-empty, ≤256 UTF-8 bytes, no control characters |
| `rows` | optional integer 1–32 |
| `navigation_wrapping` | optional boolean |
| `desktops[]` | 1–32 entries of `{ordinal, name}`; `ordinal` is 1-based and contiguous; `name` is non-empty, ≤256 UTF-8 bytes, no control characters |

Per-application `workspace` (optional object):

| Field | Rule |
|-------|------|
| `session_id` | required; must reference a defined session |
| `desktop_ordinal` | required integer 1–32 and ≤ that session's desktop count |
| `launch` | optional boolean, default `false` |
| `maximize` | optional boolean, default `false` |
| `launch_desktop_file` | optional; no shell metacharacters; must look like a desktop id (`*.desktop` or reverse-DNS) |
| `title_fallback` | optional `{enabled, mode, pattern}`; `mode` ∈ {`exact`, `contains`, `prefix`}; `pattern` ≤128 UTF-8 bytes, no control characters |

A missing per-application `workspace` object means no launch and no placement.
`launch_desktop_file` defaults at resolve time to `match.desktop_file_name`
only when that field already looks like a desktop id; it is **never** derived
from `resource_class`. Captions are never valid in `match`. At least one
desktop per session is required, so a session with zero desktops is rejected.

Durable sessions are ordinal layouts, not live desktop UUIDs; live UUIDs are
runtime-only. Desktop names and captions are user-visible on screen but are
never logged, never persisted in META, and never used as default identity.

Schema 4 is observational in M4 Slice A: the application observes live desktop
state, computes a dry-run `WorkspacePlan` in memory, and can save named
sessions and assignments through the explicit `Uložiť` action. Slice A never
creates, renames, removes, or switches a live desktop, never launches an
application, and never writes `kwinrulesrc`. Live apply, launch, and placement
belong to a separately authorized later slice. Plasma-login autostart stays
out of M4 entirely (M5/G8).

## Persistence and recovery

- The entire draft is validated before activation.
- Replacement uses `QSaveFile` with no direct-write fallback.
- Replacing a previously valid file keeps exactly one sibling backup,
  `profiles.json.bak`, by writing the previous exact bytes through `QSaveFile`
  (no remove-then-copy). A backup failure cancels the primary replacement.
- Explicit **Uložiť** is the only persistence boundary. Reading never rewrites
  the file, never creates a backup, and never enables workspace roles.
- Replacement is refused when the existing document is unsupported, invalid, or
  only loads through a migration-error fallback. The previous bytes stay.
- After the first schema-4 save, an older binary refuses the file; keep a
  COOPERATOR-owned pre-upgrade copy if a downgrade must survive later saves.
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
Middle Area, Right Area, Arrow and Homekeys, Numpad. The keyboard has **no
readback**. "Sent successfully" is never hardware acceptance. Desired state,
connection state, and last error are three separate truths. Only the
operator's eyes close a lighting claim.

`lighting` is a preset object:

| Field | Meaning |
|-------|---------|
| `mode` | ∈ {`untouched`, `direct`, `wave`, `cycle`, `breathing`, `off`} |
| `zones` | Absent/`null`, or exactly five **objects**. Schema 2 stored five `#rrggbb` strings; those map to `static` objects in memory. |
| `base_color` | Optional `#rrggbb`. Direct may use it when zones are absent. Breathing without a color still sends `#7c3aed`. |
| `restore_mode` | Device mode to return to (`direct`, `wave`, `cycle`, `breathing`, `off`). Defaults to `wave`. Not `untouched`. |
| `speed` | Optional integer from 0 through `2147483647`. Encoded values are clamped to the mode's `speed_min`/`speed_max`. |

Unknown mode names, wrong slot counts or types, and unknown semantic fields
are rejected. `direct` requires `base_color` or five zones. Serialization
emits lowercase colors and schema 3.

Each zone object has a `role`:

| Role | Allowed fields | Meaning |
|------|----------------|---------|
| `static` | `role`, `color` | Fixed slot color |
| `desktop_indicator` | `role`, `color` | Indicator's full-brightness color |
| `app_color` | `role`, `color` | Application contribution; stored color is its fallback |
| `off` | `role` only | Black slot |

No other roles exist. Global lighting accepts all four. Application lighting
accepts only `static` and `off`; applications cannot replace the global
workspace layout.

A **workspace layout** is active precisely when global mode is `direct` and
the five global slots contain at least one `desktop_indicator` or `app_color`.
There is no extra enable flag. Roles stored under Wave/Cycle/Breathing/Off
stay dormant until Direct is selected again.

Existing valid schema-2 lighting keeps its user-visible colors until the user
explicitly chooses workspace roles. Merely loading the file does not rewrite
it or take over the device.

### Workspace composition

Resolution order:

1. Session override (temporary color, Lights Off, Device Default).
2. Active global workspace layout, which owns all five slots.
3. Ordinary lighting: application preset, else global preset.

Desktop indicators are numbered 1…K in physical order. Indicator N represents
desktop ordinal N. The current represented desktop uses the configured color;
an existing inactive desktop uses `floor(channel / 5)` (20% brightness); a
missing ordinal is black. More desktops than K show the first K only. If the
current ordinal is greater than K, every represented desktop is inactive and
the UI reports that the current desktop is not represented. There is no
cycling, paging, or hidden overflow encoding.

`app_color` slots use the existing application matcher. Direct-with-zones
contributes that slot's static/off color; Direct-with-base or Breathing
contributes the base color (Breathing default `#7c3aed` when unset); Off is
black; Untouched/Wave/Cycle and unmatched identity use the global fallback
color. Wave and Cycle are not sampled into a fabricated static color.

When a workspace layout is active and workspace state is Unknown, the desired
mode is `untouched` (release to the recorded device default). Before takeover
that writes nothing; after takeover the existing RGB client requests its
recorded restore mode and releases control. Restoration is not claimed if
transport is unavailable. Bridge loss alone does not invalidate a valid
desktop snapshot: indicators keep working and app slots use fallback colors.

Composition yields five RGB values, then a concrete Direct preset. Unresolved
dynamic roles never reach context-free color conversion. If all five computed
workspace colors are black, the device mode is `Off` while the preview stays
five blacks. That exception exists because the RGB client refuses all-black
Direct frames; it does not reinterpret migrated legacy presets.

The default explicit layout is four desktop indicators plus one application
slot. Creating an application override from global lighting copies concrete
fallback/static colors and never copies dynamic roles.

Zone-accent for mapped keys remains specified with an explicit control-to-zone
table. Every entry ships `verified: false` until the IRL probe fills
`docs/hardware/g213-zone-map.md`. While unverified, accent writes to hardware
are inert; the UI may show a labelled preview. No physical key-to-zone fact
is claimed without measurement.

Default is **non-destructive**: until the user expresses intent, ContextDeck
does not touch the device. The honest lighting label remains
`untouched — device default` and is never displayed as Automatic. The Overview
Hero must not paint `untouched` as solid black; black means `off`. Untouched
zones are hollow/dashed placeholders with a badge such as
`Device default (Wave)`.

A temporary override (session, not a document field) outranks the resolved
preset, expires on the next *external* application-identity change, and is
never expired by opening this application's UI or by changing only the
virtual desktop.

`Restore device default` returns the device to the recorded `restore_mode`
(assumed `wave` when unknown) and stops touching it.

## Context conditions and fallbacks

| Condition | Mapping / lighting |
|-----------|--------------------|
| Identified app with profile | Application overrides, else global, else pass-through |
| Identified app without profile | Global, else pass-through |
| Unknown, stale, or missing identity | Global profile; lighting stays on the global preset, never forced to off. With an active workspace layout, app slots use their fallback colors. |
| Bridge lost (three missed 5 s heartbeats) | Same as unknown identity, with one bounded warning. A valid desktop snapshot is **not** discarded. |
| Workspace snapshot unknown or paused | Active workspace layout resolves to `untouched` / recorded device default. Indicators are not shown as current. |
| ContextDeck settings / unsuitable shell surface | Neutral / pass-through; does not expire a temporary lighting override |

## Power actions

`displays_off` uses a **long-lived** `KScreen::Dpms` member (not a stack
temporary) after `isSupported()`, without changing KScreen topology,
debounced 2 s. The object must outlive the asynchronous `switchMode()` call.

`suspend` uses `org.freedesktop.login1.Manager.Suspend(false)` on the system
bus after `CanSuspend`, with confirmation, debounced 5 s.

Never `/sys/power/state`, never `systemctl`, never `QProcess`.

## Explicit non-goals

- M3 does not open keyboard devices, grab input, start the broker, or change
  host policy. The parked M2 broker remains a separate whole.
- No per-key RGB.
- No macros, shell strings, or executable configuration.
- No other keyboards, operating systems, or generic remappers.
- No plugins, telemetry, cloud, or web UI.
- No durable desktop UUIDs as identity and no per-desktop profile database.
  Named sessions are ordinal layouts; live UUIDs stay runtime-only.
- M4 Slice A does not create, rename, remove, or switch live desktops, does not
  launch or place applications, and does not write `kwinrulesrc`. Those remain
  separately authorized later work. M5/G8 owns Plasma-login autostart,
  packaging, and full lifecycle.
- No remapping or deck layer in M4; those remain separate future wholes.

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

## Virtual desktop observation

The session application observes KWin directly. It does not route desktop
state through the context bridge.

- Service `org.kde.KWin`, object `/VirtualDesktopManager`, interface
  `org.kde.KWin.VirtualDesktopManager`.
- Subscribe to owner changes and to `currentChanged`, `countChanged`,
  `desktopCreated`, `desktopRemoved`, and `desktopDataChanged` **before**
  calling `org.freedesktop.DBus.Properties.GetAll`.
- Authoritative state is a complete snapshot of `count`, `current`, and
  `desktops`. Partial signal payloads are only invalidations.
- `desktops` is treated as an array of `(position, id, name)` tuples.
  Positions may be signed or unsigned 32-bit; other shapes are refused.
- Accept 1–32 desktops whose count matches the array length. IDs are
  non-empty, unique, and at most 128 UTF-8 bytes; names are at most 256 UTF-8
  bytes (empty names display as `Plocha N`); neither may contain control
  characters. Positions are unique integers from 0 through 32. Retained
  metadata is capped at 16 KiB. `current` must identify exactly one entry.
- Sort by position and derive displayed ordinals from that order. IDs are not
  assumed to be indices or UUIDs.
- Coalesce invalidations in one event-loop turn. At most one snapshot request
  is in flight, with one pending-refresh flag. Replies are bound to owner
  generation and invalidation revision.
- After 2 seconds without a current valid snapshot, state becomes Unknown.
  Recovery delays are 1, 2, 4, 8, 16, then 30 seconds, then wait for a new
  service or signal event. There is no periodic desktop polling.
- Diagnostics report error classes and counters only. Desktop IDs and names
  are not logged.
- Diagnostics **Pozastaviť sledovanie plôch** pauses observation through the
  normal unavailable path. It is a simulated interruption, not proof of a
  compositor failure.

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
| **Aplikácie** | Per-application lighting presets from the KWin inventory, plus named-session assignment fields (observation-only in M4 Slice A) |
| **Plochy** | Observed desktop count/current/rows/wrapping, named-session editor, dry-run plan preview. Apply hidden/disabled until a later authorized slice |
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
