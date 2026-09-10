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

1. Typed profile document (schema_version 1).
2. Deterministic resolver: identity + control → assignment, and identity →
   lighting. No I/O, D-Bus, device access, or GUI.
3. Context bridge supplies identity to the session application.
4. Lighting client applies the resolved five-zone color through OpenRGB SDK
   protocol 5 on loopback.
5. The input broker (later whole) will consume the same assignments. Until it
   exists, `emit_shortcut` values are stored and shown, not executed.

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
pass-through and writes nothing.

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

The current schema is integer `1`. A missing, non-integer, or other
`schema_version` is refused. A **future** version is refused without rewriting
the file. Unknown semantic fields in schema 1 are rejected rather than
silently discarded. Device scope other than vendor `046d`, product `c336`,
model `logitech-g213-prodigy` is rejected.

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

Five physical zones, never per-key color.

`lighting.mode` ∈ {`automatic`, `temporary_color`, `lights_off`}.
`base_color` is `#rrggbb`. `zones` is either `null` (all five follow
`base_color`) or exactly five `#rrggbb` entries.

Session semantics (applied by the session application):

- **automatic**: the resolved profile owns all five zones.
- **temporary_color**: holds until the next *external* application-identity
  change. Opening this application's tray or settings does not expire it.
- **lights_off**: explicit session override until automatic is resumed.

An override must never be labelled as Automatic. Unknown lighting modes are
rejected.

v1 lighting applies one base color to all five zones unless `zones` is set.

## Context conditions and fallbacks

| Condition | Mapping / lighting |
|-----------|--------------------|
| Identified app with profile | Application overrides, else global, else pass-through |
| Identified app without profile | Global, else pass-through |
| Unknown, stale, or missing identity | Global profile; lighting stays on the global colors, never forced to lights-off |
| Bridge lost (three missed 5 s heartbeats) | Same as unknown identity, with one bounded warning |
| ContextDeck settings / unsuitable shell surface | Neutral / pass-through |

## Power actions

`displays_off` uses `KScreen::Dpms` after `isSupported()`, without changing
KScreen topology, debounced 2 s.

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
