# ContextDeck — Roadmap

Human-friendly plan of record. Authority lives elsewhere: the repository is the
source of truth for code, the pinned AP protocol governs process, and META
stores the exact Worker prompt/report history. This file summarizes "where we
are and where we are going" and is maintained after each reconciliation. Access
profile: ChatOrchestrator; delivery remains manual. META Git archival is a
COOPERATOR action, separate from any authorized report-file preparation.

## Where we are now

- Product repo: `cisarik/contextdesk`, branch `main`. Git identity belongs to
  `git` / GitHub, not to this file.
- AP is pinned at the `.ap/` gitlink (exact SHA in Git). Protocol updates are
  explicit tasks; a newer public AP `main` is not an adopted pin.
- Foundation planning (whole `g213-contextdeck-foundation-architecture`):
  Planner report 01/01 reconciled and accepted as **PARTIAL**, archived in META.
  PARTIAL is the correct outcome — the architecture is routable, but hardware
  evidence gates remain open.
- Completed whole: **M1 `g213-contextdeck-mvp-context-lighting`** — recorded as
  **accepted IRL by the COOPERATOR** (historical; not a later-session retest).
  Five-zone lighting, native device modes (Wave/Cycle/Breathing/Off/Direct),
  animation speed, 5-zone gradient helper, KWin event-driven context,
  non-destructive startup, and desktop UI. Unrelated gates such as G7 are not
  closed by restating that acceptance.
- M3 `g213-contextdeck-workspace-aware-lighting`: M3 workspace-aware lighting
  is code-accepted on `502ae75...`; its physical five-zone IRL observation is
  deferred by explicit COOPERATOR decision. M3 is not closed, and code
  acceptance is not physical acceptance. Later IRL steps live in
  `docs/testing-m3.md`.
- Current whole: **M4 `g213-contextdeck-workspace-session-manager`** — Slice B
  implementation candidate in the product tree (schema 4 named
  sessions/assignments, `rows`/wrapping observation, pure dry-run
  `WorkspacePlan`, explicit Apply with a user-local checkpoint/revert,
  `DesktopMutator`, typed `ApplicationLauncher`, `PlacementResolver`, the
  `Plochy` editor, and bridge `PlacementHint`). **Not accepted and not
  live-verified; nothing mutates without the user's Apply and no launch happens
  at login.** Later IRL steps live in `docs/testing-m4.md`.
- Parked whole: **M2 `g213-contextdeck-input-passthrough-safety`**. Planning
  and production implementation are in the repository. Exact candidate
  `cb72ae0388307b514182efc6936712e3da42cda4` was installed and verified on
  the reference host while the broker remained inactive (`deployment-PASS`;
  META Worker 14). Named physical slices from Sessions 16, 19, 22, 23, and 24
  are recorded as accepted (ARM / pass-through / cutoff / armed watchdog with
  a held modifier). Trace:
  `projects/contextdesk/00/02-g213-contextdeck-input-passthrough-safety/`.
  **Full G4 remains open.** Independent G3 re-audit of the residual ACL gap
  stays host-mitigated (`G3-ACL-REPROBE-01`) and is not closed here.
  Remaining M2 work is a separately authorized fresh task — not granted by
  M3. A systemd-sleep hook in the tree can stop an active broker before sleep
  and start it once afterwards, always disarmed; that is not live suspend
  evidence.

## Routing decisions taken by the COOPERATOR (this revision)

- **Aggressive MVP routing.** Small code-only slices were rejected as too slow.
  M1 delivers something the COOPERATOR can physically test, not a library.
- **Division of labour.** Workers write code; the COOPERATOR runs the host
  enablement and the IRL hardware acceptance and reports what actually works;
  the ORCHESTRATOR reads logs and reports and routes the next slice.
- **Minimal safety-only tests (M1 routing).** Three CTest units covering the
  dangerous profile/OpenRGB semantics. M2 later added further `add_test` names;
  the live suite is owned by `CMakeLists.txt`, not by a count in this file.
- **Input interception stays out of M1.** At M1 routing time, G213 event nodes
  were `root:input 0660` and the session user was not in `input`, so a broker
  needs a privileged identity or a udev rule. That authority was granted in
  principle but **reserved for M2**. Nothing in M1 may grab, read, or inject
  keyboard input. Event-node numbers are not identity.

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
| M1 | `g213-contextdeck-mvp-context-lighting` | Build skeleton, typed profile model, KWin context bridge, tray + Kirigami settings UI, OpenRGB protocol-5 client (5 zones verified IRL), typed `DisplaysOff`/`Suspend`, IRL test pack | G0, P2 | **Done (Accepted IRL)** |
| P2 | host enablement (COOPERATOR-run) | `openrgb` install, loopback SDK server, KWin script load — G2 five-zone evidence | — | **Done IRL** (five zones confirmed physically) |
| P1 | `g213-contextdeck-control-evidence` | Physical control matrix for all 20 controls (COOPERATOR-run probe) — G1 | — | Planned, parallel |
| M2 | `g213-contextdeck-input-passthrough-safety` | Narrow libevdev/uinput broker, pass-through only, crash/hang/recovery evidence | P1, M1, G3 | **Parked** — production path in repo including suspend/resume sleep hook; inactive install (`deployment-PASS`); named Sessions 16/19/22/23/24 slices (`acceptance-PASS`); **full G4 open**; residual independent G3 gap not closed |
| M3 | `g213-contextdeck-workspace-aware-lighting` | Opt-in five-slot layout: desktop indicators + application color through existing OpenRGB | M1 | **Implementation-candidate** — not accepted |
| M4 | `g213-contextdeck-workspace-session-manager` | In-session workspace orchestrator: named sessions, app assignment to virtual desktops, explicit-apply / in-transaction launch (never at Plasma login), placement, opt-in title fallback | M3 | **Slice B implementation-candidate** — not accepted |
| M5 | `g213-contextdeck-system-integration-and-autostart` | Full KDE Plasma session autostart, systemd user integration, packaging, complete lifecycle | M2, M4, G6, G8 | Planned |

Dependency order: P2 confirmed five-zone lighting; M1 → M2 (input safety) OR
M1 → M3 (workspace lighting); M3 → M4 (workspace session manager); M4 + M2 → M5
(full autostart release). P1 must land before M2 remapping. G7 (power actions)
tested in M1 IRL.

## Evidence gates

Hardware and authority decisions that must be proven before related claims
ship:

| Gate | Needed evidence | Blocks | Status |
|------|-----------------|--------|--------|
| G0 | Baseline ownership confirmed | Any repository mutation | Confirmed; ORCHESTRATOR-owned docs commits moved `main` past `6b4e4b3` — M1's exact baseline is the re-route commit |
| G1 | Routing matrix for all 20 requested controls | Special-button remapping | **Closed** — probe measured 2026-09-11: F1–F12 on if00 (59–68/87/88), media+volume on if01 (165/164/163, 113/114/115) all host-remappable; Game Mode and Backlight emit **zero** host events — firmware-only, permanently out of the remap catalog (`docs/hardware/g213-control-matrix.md`) |
| G2 | OpenRGB trial: five zones, reconnect, coexistence | Shipping the RGB route | **Closed — proven IRL** during M1 (five zones, modes, speed, gradient all verified physically) |
| G3 | Accepted input/RGB access boundaries | Services, udev rules, broker deployment | **Model accepted.** Repo packaging: system user `contextdeck-broker`, guard udev (G213 event + `/dev/port` + `i2c`, hidraw kept), narrow event grant, late uinput ACL after seat `uaccess`. Whether a host has those files installed is operations evidence, not implied by the tree. Never autostart an unproven broker |
| G4 | Interception, crash, hang, release, recovery acceptance | Enabling remapping | **Open.** Named physical slices accepted on candidate `cb72ae0` (ARM/pass-through/cutoff, Worker 16; armed watchdog abort / held modifier, Worker 19). Remaining: LED return, all-control fidelity, live host suspend/resume, input-remapper coexistence beyond those samples, production/autostart. A documentation or hook-implementation commit does not close G4 |
| G5 | KWin lifecycle, identity, focus-race measurements | Contextual behavior claims | Partially exercised in M1 (bridge, heartbeat, self-context) |
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

### M1 context + lighting — implemented, recorded as accepted IRL

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
- **Breathing color/speed & gradient fix (session 04, commits `b3739fc`..`2bff1c1`):**
  - **Breathing Mode:** Now encodes 1 mode-specific color (`colors[0]`) and clamped
    speed value into `UpdateMode` payload; no longer breathes invisible black.
  - **Animation Speed Slider:** Exposes 0-100% speed mapping in UI and schema 2
    for Wave, Cycle, and Breathing.
  - **Gradient Apply:** Fixed `ColorDialog` start/end bindings and immediate 5-zone
    swatch reactive refresh on "Použiť gradient".
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
- G213 present as USB `046d:c336` with two input interfaces (`00`, `01`) and
  matching hidraw nodes. Event-node numbers are not identity. hidraw was
  `root:root` with session ACL after OpenRGB; event nodes started as
  `root:input` until G3 policy.

## Backlog — classified COOPERATOR brainstorms

Recorded by the ORCHESTRATOR. Classification only — none of this is
implementation authority, and none of it may appear in a Worker prompt until it
is routed as its own logical whole.

### Workspace-aware lighting — code-accepted, physical observation deferred `g213-contextdeck-workspace-aware-lighting`

Opt-in global five-slot layout: desktop indicators plus application color,
composed into the existing OpenRGB protocol-5 client. Schema 3 stores zone
roles; valid schema-2 files keep their previous lighting until the user
explicitly enables roles. Virtual-desktop state is observed from the session
app (`org.kde.KWin` `/VirtualDesktopManager`), not through the context bridge.

M3 workspace-aware lighting is code-accepted on `502ae75...`; its physical
five-zone IRL observation is deferred by explicit COOPERATOR decision. M3 is
not closed, and code acceptance is not physical acceptance. It does not close
M2, G4, independent G3, remapping, the deck layer, M4, or M5. Later IRL steps:
[docs/testing-m3.md](docs/testing-m3.md).

**Need (COOPERATOR):** read from the keyboard which virtual desktop is active
and which application is focused — for example four zones tracking the desktop
and one zone tracking the app, with the global preset itself configurable.

**Verified evidence (ORCHESTRATOR, read-only, historical):**

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

**Implemented contract (candidate, not accepted):**

- Slot roles: `static`, `desktop_indicator`, `app_color`, `off`. One global
  layout; application presets fill `app_color` slots only.
- Desktop encoding: first K indicators, active at full brightness, inactive at
  20%, overflow explicit. Default layout is four indicators plus one app slot.
- Schema 3 with preserving schema-1/2 migration. Explicit save only.
- Unknown workspace with an active layout releases to the recorded device
  default. All-black workspace composition uses device Off.

The unfilled control-to-zone Results table remains unmeasured and does not
block ordinary five-slot lighting. M4 is now its own current whole; remapping,
the deck layer, and M5 stay separate future wholes.

### Workspace session manager — current whole, Slice B implementation candidate `g213-contextdeck-workspace-session-manager`

**Need (COOPERATOR):** transform ContextDesk into a true KDE Plasma Context &
Workspace Manager:
1. Configure and manage virtual desktops (count, names, custom sessions).
2. Assign specific applications to specific virtual desktops.
3. Launch assigned applications on in-session session-management events and
   maximize them to their respective virtual desktops.
4. Window-title-based matching as a fallback when an unassigned window has focus.

**Classification:** current logical whole (milestone M4). Slice B is an
implementation candidate, not acceptance and not live-verified. All live
desktop mutation, launch, and bridge reload remain separately granted
COOPERATOR operations. Nothing here grants host or desktop mutation.

**Implemented Slice B contract (candidate, not accepted):**
- Schema 4: `workspace_sessions[]` ordinal layouts plus per-application
  `workspace` assignment; preserving schema-1/2/3 migration; explicit save only.
- `WorkspaceReceiver` additionally decodes `rows` and
  `navigationWrappingAround`, subscribes the two extra invalidation signals,
  and exposes a payload-free in-transaction `desktopCreatedObserved` event
  surface (no second `GetAll`, no polling, no desktop identity logged).
- Pure `WorkspacePlan` dry-run: create/rename/rows/wrapping diff, drift and
  extra-desktop indicator, launch intent, debounce, and trigger classification.
- `DesktopMutator`: checkpoint-write-and-verify first, then create ascending,
  conditional rename ascending, `rows`, wrapping, the opted-in `current`
  switch, and explicit extra removals last. Any error stops and reverts from
  the user-local `WorkspaceCheckpoint`; `removeDesktop` is opt-in only.
- `ApplicationLauncher`: typed `.desktop` id through
  `KService`/`KIO::ApplicationLauncherJob` (`KF6::Service` + `KF6::KIOGui`),
  triggered only by explicit Apply or the matching in-transaction
  `desktopCreated`; inventory skip, 2 s debounce, one bounded retry.
- `PlacementResolver` + bridge `PlacementHint` on `windowAdded`: identity wins
  over the opt-in title fallback; empty id is a no-op; no `kwinrulesrc`.
- Opt-in title fallback: user-authored pattern compared in memory for one
  call; the caption is sent only while the global flag is on and is discarded
  after the call, never stored or logged.
- `Plochy` editor and per-profile assignment fields; Apply/revert are explicit.
- Later IRL steps: [docs/testing-m4.md](docs/testing-m4.md).

**Slice history (recorded):** Slice A (schema 4, observation, dry-run,
editor) was implemented and corrected to accepted candidate `aca6c68`; its
code acceptance is superseded as the base for Slice B and is not live
behavior evidence.

**Route corrections measured during M4 planning (supersede the old notes):**
- `window.desktops` is writable in the installed KWin script API;
  `window.maximized` is **not** — scripts must call
  `window.setMaximize(true, true)`. Persistent `kwinrulesrc` writes are
  **rejected** by the product (ADR 0002).
- Application launch is typed through `KIO::ApplicationLauncherJob` in the
  later mutation slice; `systemd-run --user`, `kstart`, and shell/`Exec=`
  wrapping are rejected (ADR 0004). Launch is never at Plasma login.
- Title fallback is opt-in and event-driven, never polling and never default
  identity.

### KDE Plasma autostart & full lifecycle — future whole `g213-contextdeck-system-integration-and-autostart`

**Need (COOPERATOR):** ContextDesk starts automatically upon KDE Plasma login,
restoring the full automated context ecosystem without manual steps.

**Classification:** future-logical-whole (milestone M5).
- Clean systemd user unit bound to `graphical-session.target` or standard XDG
  `~/.config/autostart/contextdeck.desktop`.

### Immediate M1 IRL observations & defects to resolve

1. **Breathing mode requires color & speed:**
   - *Source evidence (`RGBController_LogitechG213.cpp`):* The G213 Breathing mode
     has `MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_SPEED`, requiring
     `modes[active_mode].colors[0]` and `modes[active_mode].speed`. When sent
     with empty colors, it breathes black (invisible).
   - *Fix:* Expose speed control and a primary color for Breathing, Wave, and
     Cycle, serializing them into the `UpdateMode` payload.
2. **Gradient UI application:**
   - In `LightingPresetEditor.qml`, ensure `ColorDialog` binds to valid mutable
     properties and that "Použiť gradient" updates both live swatches and the
     effective model seamlessly.
3. **5 physical zones confirmed working IRL:**
   - Individual zone color control on the Logitech G213 is physically proven!

### M2 input passthrough safety — parked

Planner report 01/01 PASS (native Plan Mode, archived in META). Implementation
in the repository (tree content, not a G4 close):

- **S1 engine** — fail-closed identity matcher, balanced synthetic ledger, 1:1
  forwarding with SYN pairing, SYN_DROPPED reconciliation, all-or-nothing
  acquisition with ungrab-first teardown on fakes.
- **S2 host files + grab seam** — guard udev, narrow G213 event grant to
  `contextdeck-broker`, systemd system unit (`DevicePolicy=closed`, no
  `[Install]`), exclusive grab behind `IGrabber`.
- **S3 watchdog** — `Type=notify`, `WatchdogSec=2`, feed from the event-loop
  thread, `watchdog-selftest`.
- **S4 session IPC** — authenticated Unix socket, single lease, explicit
  `LEASE`/`ARM`/`DISARM`/`RELEASE`.
- **S5 production path** — USB-ancestry enumerator, `RealSink`/`EvdevGrabber`
  constructed only on ARM, CMake install of `contextdeck-broker`, session-app
  ARM UI.
- **uinput ACL ordering** — additive `setfacl` for `contextdeck-broker` from
  `99-contextdeck-broker-uinput.rules` after seat `uaccess` (not from `62-*`).
- **Suspend/resume hook** — `packaging/systemd/contextdeck-sleep.sh` stops an
  active broker on systemd-sleep `pre` and may start it once, disarmed, on
  `post` only with a valid active-before-sleep marker. Not autostart. Not live
  suspend evidence. ADR 0001.

**G4 remains open.** Named slices from Sessions 16, 19, 22, 23, and 24 are
recorded as accepted (ARM / pass-through / cutoff; armed watchdog abort with
a held modifier). Neither those slices nor later documentation close this
whole. Independent G3 re-audit of the residual ACL gap remains
host-mitigated and is not a fresh audit. Remaining: LED-return behavior,
all-control fidelity, live host suspend/resume, input-remapper coexistence
beyond those samples, and production/autostart readiness. A host may or may
not have installed the packaging files; that is operations evidence. The
broker must stay static/inactive with no autostart until those remaining
claims are separately authorized and accepted.

For any live G4 grab, an independently verified second physical keyboard or
SSH from another device is a mandatory safety precondition before the broker
is started or ARM is attempted, and it must remain available through the trial.
Either route is sufficient. A cutoff timer is additional evidence and never a
replacement. Device-free S3 procedures may remain keyboard-free.

**Next remaining M2 work:** named slices are accepted. A separately
authorized fresh task remains for LED return / all-control fidelity, live
host suspend/resume acceptance, or production/autostart. Documentation here
does not grant grab, ARM, autostart, live suspend, or host mutation, and does
not choose which remainder comes next. Production safety gaps already visible
in source (silent uinput write errors; virtual-device capabilities from
`passthroughCapabilities()` rather than measured source bits / LED return
path; logind `sd_pid_get_session` vs user-manager-launched session apps) stay
in that remainder unless a later prompt names them.

Registered tests live in `CMakeLists.txt`. Historical “N/N CTest” lines in META
are not current suite truth.

### Deck layer — COOPERATOR brainstorm, classified future whole

**Idea (COOPERATOR):** one spare key (the Windows/Super key was proposed)
behaves like **Shift for the whole deck**: hold it and the keyboard switches to
a temporary layer — the lighting changes to a distinctive signature and keys
gain temporary functions (including launching a chosen program); release and
everything returns to exactly the previous state, both functions and lighting.
Per-key customization inside the layer comes later.

**Classification:** future-logical-whole (natural extension of M3 remapping):
`g213-contextdeck-deck-layer`. Not implementation authority.

**Why it fits the existing architecture (ORCHESTRATOR analysis):**

- The Super key (`KEY_LEFTMETA`, 125, if00) is an ordinary host event — the G1
  probe filtered it silently as a typing key, which proves it reaches evdev.
  It is remappable in principle; a one-time confirmation press in the G1
  style is still required before binding it.
- A layer is exactly the existing **temporary lighting override** (M1, already
  shipped) combined with a **temporary key policy** — both scoped to a hold,
  both expiring on release. The broker crash case is inherently safe: crash
  releases the grab, which exits the layer and returns Win to native behavior.
- "Launch a program on layer entry" must stay a **typed action** (a desktop
  file / KService id), never a shell string — same rule as every other action.
- Product decisions the COOPERATOR will own later: tap-vs-hold semantics for
  Super (tap keeps the KDE launcher? hold opens the layer), what each key does
  inside the layer, the layer's lighting signature, and whether Super-chords
  (Super+E …) pass through while held.

**Hard dependencies:** M2 grab + G3/G4 (the layer cannot exist without
exclusive claiming), then the M3 consume-and-inject mechanism it reuses.

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
