# ContextDeck — Roadmap

Human-friendly plan of record. Authority lives elsewhere: the repository is the
source of truth for code, the pinned AP protocol governs process, and META
stores the exact Worker prompt/report history. This file summarizes "where we
are and where we are going" and is maintained by the ORCHESTRATOR after each
reconciliation.

## Where we are now

- Product repo: `cisarik/contextdesk`, branch `main`, clean; ahead of
  `origin/main` (`ca3ac07`) by the AP-adoption and documentation commits.
  Nothing is pushed — publication stays a separate COOPERATOR decision.
- AP pinned: `.ap/` gitlink `7ef45da`.
- Foundation planning (whole `g213-contextdeck-foundation-architecture`):
  Planner report 01/01 reconciled and accepted as **PARTIAL**, archived in META.
  PARTIAL is the correct outcome — the architecture is routable, but hardware
  evidence gates remain open.
- Current whole: **M1 `g213-contextdeck-mvp-context-lighting`** — implemented,
  corrected, and redesigned across three Worker sessions (fifteen commits
  `1b024e4`..`1781a40`), 3/3 CTest units green, independently rebuilt by the
  ORCHESTRATOR. **Awaiting COOPERATOR IRL acceptance** with `docs/operations.md`
  and `docs/testing.md` (which includes the five-step zone-map probe for
  `docs/hardware/g213-zone-map.md`). M1 merges profiles, KWin context,
  non-destructive OpenRGB lighting with device modes, typed power actions,
  and a polished task-oriented desktop UI. **It contains no input interception.**

## Routing decisions taken by the COOPERATOR (this revision)

- **Aggressive MVP routing.** Small code-only slices were rejected as too slow.
  M1 delivers something the COOPERATOR can physically test, not a library.
- **Division of labour.** Workers write code; the COOPERATOR runs the host
  enablement and the IRL hardware acceptance and reports what actually works;
  the ORCHESTRATOR reads logs and reports and routes the next slice.
- **Minimal safety-only tests.** Three CTest units covering the dangerous
  semantics (resolver, config rejection, protocol frame bounds). No GUI,
  integration, or coverage-driven test work.
- **Input interception stays out of M1.** Verified host state: `event7`/`event8`
  are `root:input 0660` and the session user is not in `input`, so a broker
  needs a privileged identity or a udev rule. That authority is granted in
  principle but **reserved for M2** and requires the G1 physical probe first.
  Nothing in M1 may grab, read, or inject keyboard input.

## Guiding principles

Immediate · Predictable · Honest · Recoverable · Native · Minimal ·
Inspectable · Private · Narrow. (handout §44 — the full wording is the
project's product bar.)

## Milestones (logical wholes)

Each milestone is one bounded Worker exchange. Status: next / planned /
done-as-planned.

| # | Identity | What it delivers | Depends on | Status |
|---|----------|------------------|------------|--------|
| V0 | baseline reconciliation | Confirmed baseline, trace continuation | — | Done (G0) |
| M1 | `g213-contextdeck-mvp-context-lighting` | Build skeleton, typed profile model + validated atomic persistence, KWin context bridge, tray + Kirigami settings UI, OpenRGB protocol-5 client (five zones, one base color), typed `DisplaysOff`/`Suspend` actions, three safety test units, COOPERATOR IRL test pack | G0, P2 | **Implemented — awaiting IRL acceptance** |
| P2 | host enablement (COOPERATOR-run) | `openrgb` install incl. its udev rules, loopback SDK server, KWin script load — the G2 five-zone evidence | — | Granted, parallel |
| P1 | `g213-contextdeck-control-evidence` | Physical control matrix for all 20 controls (COOPERATOR-run probe) — G1 | — | Planned, parallel |
| M2 | `g213-contextdeck-input-passthrough-safety` | Narrow libevdev/uinput broker, pass-through only, crash/hang/recovery evidence | P1, M1, G3 | Planned |
| M3 | `g213-contextdeck-context-shortcuts` | Per-app chord emission across the verified control catalog + recorder | M2 | Planned |
| M4 | `g213-contextdeck-release-lifecycle` | Packaging, removal, opt-in autostart, release acceptance | M3, G6, G8 | Planned |

Dependency order: P2 unblocks the RGB half of M1 (the code itself does not);
M1 → M2 → M3 → M4; P1 must land before M2 because the physical routing matrix
is the only honest basis for remapping; G7 (power actions) is accepted during
M1's IRL testing rather than as its own slice.

## Evidence gates

Hardware and authority decisions that must be proven before related claims
ship:

| Gate | Needed evidence | Blocks | Status |
|------|-----------------|--------|--------|
| G0 | Baseline ownership confirmed | Any repository mutation | Confirmed; ORCHESTRATOR-owned docs commits moved `main` past `6b4e4b3` — M1's exact baseline is the re-route commit |
| G1 | Routing matrix for all 20 requested controls | Special-button remapping | Open (physical probe) |
| G2 | OpenRGB trial: five zones, reconnect, coexistence | Shipping the RGB route | **IRL pending** — code and protocol codec are in; the physical five-zone result is M1 test step 3 |
| G3 | Accepted input/RGB access boundaries | Services, udev rules, broker deployment | Granted in principle, **reserved for M2** — unused by M1 |
| G4 | Interception, crash, hang, release, recovery acceptance | Enabling remapping | Planned |
| G5 | KWin lifecycle, identity, focus-race measurements | Contextual behavior claims | Planned |
| G6 | License decision + dependency provenance | Release | Planned |
| G7 | Authorized display-off and suspend acceptance | Enabling power actions | **IRL pending** — M1 test steps 7–8 (`CanSuspend=yes` verified; `KScreen::Dpms` linked; see the known suspect note) |
| G8 | Independent acceptance, install/remove, autostart recovery | Shipping an automatically grabbing install | Planned |

## Notes per logical whole

### Foundation architecture — planning done (report 01/01, PARTIAL)

- **Stack:** C++20, Qt6, KF6, CMake, small Kirigami/QML settings UI.
- **Process model:** two owned processes — session app (tray, profiles, KWin
  bridge, RGB client; no raw device access) and input broker (libevdev +
  uinput; starts disabled). OpenRGB stays an external service.
- **Input route:** libevdev/uinput broker with an explicit all-or-nothing
  acquisition contract. input-remapper stays as-is (mouse preset preserved);
  KGlobalAccel only for optional app commands.
- **RGB route:** OpenRGB 1.0rc3, SDK protocol 5, one persistent loopback
  connection; the G213 appears as one linear zone with five color entries.
  No CLI process per focus change, no second backend.
- **Foreground context:** event-driven KWin script; identity =
  `desktopFileName` → `resourceClass` → `resourceName`; captions never the
  default matcher; own D-Bus receiver for context snapshots.
- **Corrections made during planning:** `sleep.target`/`suspend.target` are
  loaded, not masked, and logind reports `CanSuspend=yes` (supersedes the old
  handout assumption). Display-off route: `KScreen::Dpms` (`KF6::ScreenDpms`);
  the older PowerDevil DPMS D-Bus object is absent.
- **Special buttons stay conditional:** Game Mode / Backlight are firmware
  functions until G1 proves a safely remappable host event. PrintScreen and
  Pause/Break must never silently substitute for them.
- **Verified environment:** CachyOS (Arch), kernel 6.18.48-cachyos-lts,
  Plasma/KWin 6.7.5, systemd 261.2, Qt6 6.11.2, KF6 6.30.0, CMake 4.4.3,
  GCC 16.2.1 / Clang 22.1.8, libevdev 1.13.7. `extra-cmake-modules` is absent
  — early slices must not require it.

### M1 context + lighting — implemented, awaiting IRL acceptance

One slice, five stages, one commit per stage (all five green):

| Stage | Delivers |
|-------|----------|
| S1 | Build skeleton + typed profile model, resolver, validated atomic persistence, safety tests 1–2 |
| S2 | KWin script + own D-Bus context receiver + deduplicated application inventory |
| S3 | OpenRGB SDK protocol-5 client (bounded frame codec) + safety test 3 |
| S4 | Session app: tray, Kirigami settings, profile → lighting → context wiring, typed `DisplaysOff`/`Suspend` |
| S5 | COOPERATOR hand-off pack: specification, operations (exact host commands), IRL test script |

- Locks the dangerous semantics before any device code exists:
  inherit ≠ pass-through ≠ disabled; missing global assignment = pass-through;
  unknown fields/schema versions are rejected (never silently dropped);
  no shell/executable config.
- Control catalog in the model: F1–F12, Previous, Play/Pause, Next, Mute,
  Volume Down, Volume Up, Game Mode, Backlight (the last two labelled
  conditional on G1 and **not wired to any action in M1**).
- M1 writes lighting and reads window context only. It must not read, grab,
  filter, or inject any input event.

#### As-built corrections (Worker report 01/01, reconciled by the ORCHESTRATOR)

- KWin's Workspace signal is `windowRemoved`, not `windowClosed` (that one
  belongs to the effects API). The bridge uses `windowRemoved`.
- `login1.Manager.CanSuspend` is a **method**, not a property; it returns
  `"yes"` on this host. `Suspend(false)` is called on the system bus.
- `callDBus` carries all six `ContextReport` arguments; JS numbers arrive as
  doubles and the receiver coerces integer-valued doubles without changing the
  advertised D-Bus signature.
- The OpenRGB `UPDATELEDS` payload frames each LED as `0x00BBGGRR`; the header is
  little-endian on this host, not network byte order.
- Not wired in M1 (accepted, deferred): KService/desktop-file friendly names in
  the app picker (labels use `desktop_file_name`, then `resource_class`), and
  KConfig window-geometry persistence.
- **Defect closeouts (Worker session 02 / exchange 01, commits `4416f4b`..`042fa15`):**
  - **D1 (destructive lighting on connect):** `OpenRgbClient` no longer sends
    `SETCUSTOMMODE` on connect. `untouched` emits no frames. Connect is
    completely non-destructive; device keeps its firmware effect.
  - **D2 (stack `KScreen::Dpms`):** `PowerActions` now owns a long-lived
    `KScreen::Dpms` member.
  - **D3 (empty context after app restart):** KWin bridge now sends a full
    `ContextReport` on every 5 s heartbeat. Receiver refreshes context without
    policy churn.
- **Expressive lighting added (session 02):** Schema 2 with in-memory v1 migration,
  device modes (`wave`, `cycle`, `breathing`, `off`, `direct`), `UpdateMode`
  packet 1101, five named zone swatches, gradient helper, per-app presets,
  tray **Restore device default**, and unverified zone-accent mechanism.
- **UX ergonomics redesign (session 03, commits `e46a572`..`1781a40`):**
  - **Hero 5-Zone Preview:** Visual representation of G213 zones on Overview;
    untouched state renders hollow/dashed placeholder strips (never misleading black).
  - **Single-Sentence Status:** Clean summary on Overview and tray tooltip.
  - **Task-Oriented Navigation:** Persistent sidebar (Stav, Farby, Aplikácie,
    Diagnostika, Pokročilé) without cramped overlay drawers.
  - **Visual Color Pickers:** Qt `ColorDialog` swatches, live gradient preview,
    clean `Uložiť` actions.
  - **M2 Demotion:** Controls moved behind Pokročilé with inactive M2 banner.
  - **Self-Context Filtering:** Shows `ContextDeck (toto okno)` or last app.
- **Zone map:** `docs/hardware/g213-zone-map.md` established with initial
  hypotheses; empty results table ready for COOPERATOR's 5-step IRL probe.
- Worker environment note: this coding client needed a clean `PATH` for CMake
  (`CMAKE_ROOT`). The ORCHESTRATOR's independent rebuild in a normal shell
  configured, built, and passed 3/3 without that workaround.

### Verified build baseline (ORCHESTRATOR-measured, no ECM needed)

`extra-cmake-modules` is absent and there is no KF6 umbrella config, so
`find_package(KF6 COMPONENTS ...)` fails. Per-component config-mode lookups
work and were proven by a throwaway configure+compile+link probe:

- `find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml QuickControls2 DBus Network Svg Test)`
- `find_package(KF6<Pkg> REQUIRED)` for `StatusNotifierItem`, `Kirigami`,
  `Screen`, `Config`, `WindowSystem`, `DBusAddons`, `Crash`, `Service`
- Targets: `KF6::StatusNotifierItem`, `KF6::Kirigami`, `KF6::ScreenDpms`,
  `KF6::ConfigCore` (there is no `KF6::Config` target)
- Display-off: `#include <KScreenDpms/Dpms>` → `KScreen::Dpms::switchMode(KScreen::Dpms::Off)`
- Suspend: `login1.Manager.CanSuspend` returns `"yes"`; use logind, never `/sys/power/state`
- KWin 6.7.5 exposes `org.kde.KWin /Scripting` with `loadScript(path, pluginName)`,
  `start()`, `isScriptLoaded`, `unloadScript` — the fast dev loop for the bridge
- G213 present: `046d:c336`, `event7` (if00) + `event8` (if01), `hidraw2`/`hidraw3`,
  hidraw `root:root 0600`, event nodes `root:input 0660`

## Backlog — classified COOPERATOR brainstorms

Recorded by the ORCHESTRATOR. Classification only — none of this is
implementation authority, and none of it may appear in a Worker prompt until it
is routed as its own logical whole.

### Workspace-aware lighting — future whole `g213-contextdeck-workspace-aware-lighting`

**Need (COOPERATOR):** read from the keyboard which virtual desktop is active
and which application is focused — for example four zones tracking the desktop
and one zone tracking the app, with the global preset itself configurable.

**Classification:** future-logical-whole. Not a blocker, not a risk.

**Verified evidence (ORCHESTRATOR, read-only, this host):**

- KWin scripting exposes `currentDesktop`, `desktops`, `desktopChanged`,
  `currentActivity`, and `activityChanged` (symbols present in the installed
  `libkwin.so.6` next to `windowActivated` and `callDBus`).
- `org.kde.KWin` `/VirtualDesktopManager`, interface
  `org.kde.KWin.VirtualDesktopManager`: properties `count` (u — **3 here**),
  `rows` (u — 1), `current` (s — a desktop **UUID**, not an index), `desktops`
  (a(iss) — position, id, name); signals `currentChanged`, `countChanged`,
  `desktopCreated`, `desktopRemoved`, `desktopDataChanged`.
- Recommended source: subscribe to that interface **directly** from the session
  application rather than routing desktop state through the KWin bridge, so
  desktop context survives bridge loss and yields a stable position plus name.

**Design sketch (unconfirmed, hardware-truthful):**

- Zones become *slots* with roles: `profile_color`, `desktop_indicator`,
  `app_color`, `static`, `off`, later `mapped_key_accent`. One **global** zone
  layout; per-application profiles fill the app slot. That keeps per-app presets
  from fighting over the same five zones.
- Desktop encoding: position lighting (zone N = desktop N, active bright, others
  dim) is readable up to four desktops; color-per-desktop scales past that and
  must degrade honestly when `count` exceeds the available slots.
- Schema impact: zone entries become objects, so this needs schema version 3
  with a migration from 2 — the migration mechanism M1/02 builds is what makes
  that cheap.

**Hard dependencies:** M1/02 (zone model, non-destructive device behavior) and
the verified control-to-zone map its IRL probe produces, because "zone 1 =
desktop 1" is only intuitive once we know which physical keys sit in which zone.
It does **not** depend on the input broker, so it can be sequenced before or
after M2 — a COOPERATOR choice when the time comes.

**Stated limits:** five zones is a hard ceiling, so desktop, application, and
accent roles compete for the same slots; there is no per-key anything; there is
no readback, so only the COOPERATOR's eyes close a claim.

**Same brainstorm, lower priority:** activities awareness, extra native-effect
parameters, software animation by streaming five colors (explicitly declined for
now).

## Explicitly out of scope for v1

Other keyboards and OSes, per-key RGB fiction, macros, scripting languages,
plugin marketplace, cloud sync, account systems, telemetry, web/Electron UI,
generic desktop automation, remote control, profile downloads.

## Target experience (why this is worth building)

Open Cursor → keyboard turns Cursor's purple, F5 sends `Ctrl+Shift+D`,
Play/Pause sends `Ctrl+B`. Switch to Brave → one color change, Brave's
mappings, everything else untouched. The tray says: connected, current app,
active profile, automatic mode on. No restarts, no cockpit — the keyboard just
follows the work.
