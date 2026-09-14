# ContextDeck — Architecture (plan of record)

Reconciled from the foundation Planner report (logical whole
`g213-contextdeck-foundation-architecture`, exchange 01/01, status PARTIAL).
This is the accepted design for routing the next implementation slices.
Individual decisions will be formalized as ADRs under `docs/adr/` as they land.
Implementation authority comes only from Orchestrator-issued Worker prompts —
this document grants none.

## Process model

| Component | Responsibility | Hard boundaries |
|-----------|----------------|-----------------|
| Session application (Qt6/KF6: `QApplication` + `KStatusNotifierItem`, lazy Kirigami/QML settings) | Tray, settings, profiles, KWin context bridge, virtual-desktop observation, app inventory, RGB client, approved desktop actions | No raw keyboard-device access |
| Input broker (C++20, libevdev + uinput) | Verified G213 event routing, policy application, virtual input, lifecycle safety | No GUI/QML, no RGB, no network, no shell execution, no ordinary-key logging |
| OpenRGB (external service) | G213 lighting transport over the SDK | Separately reviewed device access; loopback only |

- Closing the settings window never stops the tray process; a settings crash
  never affects input ownership.
- Losing the session application expires the broker's authority and disarms
  remapping.
- No additional daemons for inventory, configuration, or diagnostics.

## Input path

- Match real USB ancestry, exact VID/PID `046d:c336`, approved interfaces, and
  verified device identity; reject virtual devices and unrelated keyboards.
- libevdev filter + uinput virtual keyboard. The broker starts **disabled**;
  autostart is opt-in only after G4 acceptance.
- Operating contract essentials:
  - open both G213 sources without grabbing, verify identity, and measure the
    live capability union (`EV_KEY` union, `EV_LED` from if00 only, `EV_MSC`;
    never `EV_REP`) before creating the virtual device, then grab;
  - a runtime virtual-device write failure is `sink-write-failed` and disarms
    with ungrab-first cleanup; compositor `EV_LED` returns to physical if00
    only and a runtime LED write failure does not disarm typing;
  - multi-node acquisition is non-atomic — if any claim fails, release all;
  - freeze the resolved action at physical key-down;
  - keep separate physical/synthetic key-ownership records;
  - balance every synthetic press with a release; never drop a modifier that
    another device might own;
  - on `SYN_DROPPED`, reconcile state — reconstructed presses must never
    trigger commands;
  - cancel pending mapped actions on invalid context, lock, session loss, or
    disconnect; never replay queued actions after recovery.
- v1 shortcut semantics: one completed chord per deliberate press; native
  repetition preserved for pass-through controls.
- Never: force-detach USB interface 01, `stop_all` on input-remapper, broad
  `input`-group membership, or CLI spawns per focus change.

## RGB path

- OpenRGB `1.0rc3` (source commit `6fbcf62`), SDK **protocol 5** — the master
  SDK docs describe newer protocols and must not be mixed in.
- The inspected G213 backend exposes one linear zone with **five color
  entries**, corresponding to the five physical zones. Use whole-device
  five-color updates.
- Small, independently authored QtNetwork client: explicit protocol
  negotiation, bounded packet/string parsing, partial-read and timeout
  handling, enumeration with exact G213 selection, device-list-change
  handling, fresh re-enumeration after reconnect; latest desired color wins,
  stale queued colors are discarded.
- Controller indices are not durable identities — re-enumerate after changes.
- Loopback binding only (the SDK is not authentication between local users).
- "Sent successfully" is not hardware acceptance: the UI must distinguish
  desired state, connection state, and failure without claiming physical
  readback.

## Foreground context

- Event-driven KWin script (`windowActivated` / window add/remove) plus an
  own D-Bus receiver — proposed name `io.github.cisarik.ContextDeck.Context1`
  (a new product API, not an existing KWin method).
- Identity resolution order: normalized `desktopFileName` → exact
  `resourceClass` (with `resourceName` where needed) → bounded, cycle-safe
  parent resolution for dialogs. Captions, PID, and executable paths are not
  normal identity sources.
- Every message carries a bridge instance, sequence number, and context
  snapshot; the receiver authenticates the expected bus owner, rejects stale
  messages, and compiles an immutable policy revision. A heartbeat detects
  bridge loss without title polling.
- Accepted race model (COOPERATOR decision): measured best-effort —
  cancel actions known to be stale, pass through when uncertainty is detected
  before consumption, and measure the residual interval. Exact delivery to the
  original window across an unobserved compositor focus transition is **not**
  promised.

## Virtual desktop observation

Workspace-aware lighting observes `org.kde.KWin` `/VirtualDesktopManager`
from the session application (`WorkspaceReceiver` on the named connection
`contextdeck-workspace`). Desktop state is not duplicated through the KWin
script.

- Subscribe to service-owner changes and desktop-manager signals, then call
  `Properties.GetAll` for a complete snapshot (`count`, `current`, `desktops`).
- Treat each signal as an invalidation. Coalesce bursts; keep one in-flight
  snapshot plus one pending refresh; discard replies from replaced owners or
  superseded revisions.
- Validate count, identities, names, positions, and current-desktop membership
  before activating state. Unknown workspace with an active layout resolves to
  `untouched` (recorded device default).
- A 2 s refresh deadline, then 1/2/4/8/16/30 s recovery, covers owner loss and
  stalled replies. Idle desktop state does not expire merely because no signal
  arrives; a silent compositor hang without an observable event is undetectable.
- Diagnostics pause/resume is a simulated observation interruption. It does
  not stop KWin or the session bus.

The five-slot resolver composes desktop indicators and application colors into
the existing OpenRGB protocol-5 path. Transport, broker, and the KWin bridge
are unchanged by this whole.

| Condition | Mapping behavior |
|-----------|------------------|
| Identified app with profile | App overrides over global defaults |
| Identified app without profile | Global defaults |
| Unknown/stale context, lock screen, no active window | Pass-through |
| ContextDeck settings / unsuitable shell surface | Neutral / pass-through |
| Reconnect or resume (lighting / context) | Wait for fresh context + policy sync |
| System sleep while broker inactive or failed | Sleep hook is a no-op; no start after resume |
| System sleep while broker active | Hook stops the unit on `pre` (orderly SIGTERM); after `post` starts it once, always disarmed, only if a valid active-before-sleep marker exists |
| Session app reconnect after resume | Existing bounded retry; `STATUS` only; no silent `LEASE`/`ARM` |

## Workspace session management (M4 Slice A)

M4 keeps the session application inside an already running Plasma session. It
adds named desktop sessions and per-application assignments without touching
live desktop state in Slice A:

- `WorkspaceReceiver` observation is extended with `rows` and
  `navigationWrappingAround`, decoded from the same `GetAll` snapshot; the two
  extra signals (`rowsChanged`, `navigationWrappingAroundChanged`) are
  subscribed as invalidations only. Request ownership, coalescing,
  one-in-flight, owner generations, and stale-reply rejection are unchanged.
- Durable sessions are named ordinal layouts stored in schema 4 (see
  [ADR 0003](adr/0003-workspace-assignment-schema.md)), not live desktop UUIDs.
  Live UUIDs stay runtime-only; desktop names and captions are never logged.
- `WorkspacePlan` is a pure component: given a session and an observed
  workspace snapshot it computes create/rename/rows/wrapping intent, an
  explicit drift/extra-desktop indicator, and per-assignment launch intent
  (`would_launch`, `already_running`, `missing_desktop_file`, `disabled`). It
  performs no D-Bus, KIO, compositor, or mutation call. Launch-attempt debounce
  and transaction trigger classification are pure in-memory helpers.
- No live desktop mutation, no application launch, and no `kwinrulesrc` write
  in Slice A. A later separately authorized slice owns
  `createDesktop`/`setDesktopName`/`removeDesktop`, typed launch, and
  placement (see [ADR 0002](adr/0002-host-desktop-mutation-authority.md) and
  [ADR 0004](adr/0004-typed-application-launch.md)).
- M5/G8 owns Plasma-login autostart and systemd/session integration. M4 must
  not start at login and must not add `[Install]`, `graphical-session.target`,
  or autostart entries.

## Configuration contract

- One authoritative versioned document:
  `$XDG_CONFIG_HOME/contextdeck/profiles.json`
  (fallback `$HOME/.config/contextdeck/profiles.json`). Schema 4 is the
  activatable version. Schemas 1, 2, and 3 load in memory with preserving
  lighting and assignment mapping and are not rewritten until explicit save.
  Schema 4 adds `workspace_sessions` and per-application `workspace`
  assignments; legacy documents migrate with those empty.
- Resolution: session override → active workspace layout → application
  preset → global preset. `Inherit Global` (app level) resolves through the
  global profile; it is invalid on the global profile itself. `Disabled` is
  explicit consumption, never inferred from absence.
- Typed actions only: `InheritGlobal`, `PassThrough`, `Disabled`,
  `EmitShortcut` (one validated chord), `ApprovedSystemAction` (`Suspend`,
  `DisplaysOff` — IDs only until G7).
- Persistence: validate the entire draft before activation → preserve exact
  previous bytes in `.bak` through `QSaveFile` → atomic `QSaveFile` replacement
  (no direct-write fallback). Invalid, unsupported, or migration-fallback
  files are not overwritten. Cold start with no valid configuration resolves
  to pass-through plus untouched lighting.
- Application matcher stores friendly display identity plus technical match
  identity. Unknown semantic fields are rejected, not silently discarded.
- M4 assignment resolution reuses the typed matcher only. The opt-in title
  fallback compares a user-authored pattern against a caption in memory for one
  call; captions are never stored, logged, or added to `MatchSpec`.
- KConfig may hold window geometry/presentation only — never a second owner of
  profile semantics. No executable config, no shell strings, no scripting.

## Security posture

- Dedicated, unprivileged broker identity with narrowly scoped G213 event
  device + uinput access; the GUI account keeps no raw input permissions and
  no broad `input` group membership.
- **Important:** the G213's RGB HID interface also carries keyboard reports —
  granting hidraw access to the session account "for lighting" would expand
  input visibility. The external RGB service gets its own restricted identity.
- Broker IPC: Unix-domain socket `/run/contextdeck/broker.sock` (systemd
  `RuntimeDirectory=contextdeck`). The peer is authenticated with kernel
  `SO_PEERCRED` (never client-supplied UID/GID). The UID must belong to an
  active local seated graphical session (`wayland`/`x11` via logind). Direct
  `sd_pid_get_session` is used first. If that lookup reports no session, the
  broker enumerates `sd_uid_get_sessions` and accepts exactly one eligible
  active local seated graphical session. Root, remote, inactive, unseated,
  non-graphical, UID-mismatched, and ambiguous sessions are rejected. Group
  membership is not authorization.
  Bounded messages only (`STATUS`, `LEASE`, `HEARTBEAT`, `ARM`, `DISARM`,
  `RELEASE`); no "inject arbitrary keys" operation. One lease may arm the
  broker; disconnect, expiry, malformed framing, or broker shutdown disarms
  first (ungrab-first teardown). Session lock and lifecycle are observed
  independently of the lighting D-Bus path.
- Diagnostics are bounded to state transitions, error classes, and counters.
  No ordinary keystrokes, window captions, device serials, or raw HID reports
  in logs; disable core dumps in the input process.
- Known limits: D-Bus names and window metadata are not a strong boundary
  against a compromised desktop session; same-user compromise remains a
  residual risk owned by the COOPERATOR.

### Packaged OpenRGB udev exposure (measured on the target host, M1)

The foundation plan warned that "lighting-only" device access can expand input
visibility. Installing the distribution `openrgb` package proved that warning
true, and further. `60-openrgb.rules` applies `TAG+="uaccess"` broadly, so
`logind` granted the session user ACLs well beyond the keyboard:

| Node | Rule that granted it | Consequence |
|------|----------------------|-------------|
| `/dev/port` (group `kmem`) | `KERNEL=="port", TAG+="uaccess"` | raw x86 I/O port access from a user session |
| `/dev/i2c-*` | `KERNEL=="i2c-[0-99]*", TAG+="uaccess"` | SMBus access; OpenRGB's own help warns it can brick boards |
| `/dev/input/event7`, `event8` (G213) | `SUBSYSTEMS=="usb\|hidraw"` + `046d:c336` — `SUBSYSTEMS` walks the parent chain, so the rule also matches the keyboard's **input** devices | any process running as the session user can read raw G213 keystrokes |
| `/dev/hidraw2`, `hidraw3` (G213) | same rule, intended target | required for RGB; granted as `uaccess`, **not** `MODE=0666` |

COOPERATOR decision (M1): **narrow revert**. Packaging
`61-contextdeck-input-guard.rules` removes the `uaccess` **tag** from the G213
input devices, `/dev/port`, and `/dev/i2c-*`, and leaves the hidraw grant
intact so keyboard lighting keeps working. Tag removal does not delete an
already materialized POSIX ACL; a later add/change re-probe can restore a
session ACL even when `CURRENT_TAGS` lacks `uaccess`.
`99-contextdeck-input-acl-guard.rules` therefore strips extended ACLs with
`setfacl -b` on those same three classes after `73-seat-late.rules`, without
matching hidraw or `/dev/uinput` and without setting `OWNER`/`GROUP`/`MODE`.
That source model is not host-install proof. Consequences accepted: OpenRGB
loses motherboard/GPU RGB control on a host that applies the guard.

Durable rules for this project:

- Audit packaged udev rules **before** installing, not after; a package install
  is a host-policy mutation even when the package looks like a user tool.
- Never rely on a package's device rules as the product's access model. M2's
  broker needs its own narrow, reviewed rule or a dedicated identity.
- Lighting must never be the reason an input event node becomes readable by the
  session user.

## Lifecycle and recovery

- Leases and acknowledgements: a policy is effective only after the broker
  acknowledges its revision; a stale GUI cannot keep the broker armed;
  restarts start disarmed; the listening socket never arms by itself;
  hardware reconnect never replays actions; a crash never triggers an
  automatic re-grab loop. The session-app Unix-socket lease (S4) is the only
  arming authority: an explicit user action sends `LEASE` then `ARM`, renewed
  by `HEARTBEAT`. Default lease is 6000 ms. Socket existence, session-app
  `start()`, and `STATUS` never arm. Production ARM enumerates the G213 by USB
  ancestry and interface number (never remembered `eventN`), then opens both
  sources without grabbing, measures the live capability union, creates the
  virtual device, and only then grabs. Losing the peer, a
  malformed frame, failed `SO_PEERCRED`, lease expiry, or orderly broker stop
  calls `Acquisition::disarm()` before teardown. A runtime sink write failure
  is `sink-write-failed` and takes the same ungrab-first disarm path.
- The systemd watchdog (`WatchdogSec=2`) is fed from the broker event-loop
  thread with `sd_notify("WATCHDOG=1")` after each wait return (including idle
  timeout) and after that iteration's ingest work. A hung wait or hung ingest
  stops feeding. There is no helper thread or detached timer. 2 s is
  configured, not a measured recovery time on this kernel (G4). `WATCHDOG=1`
  does not extend stop/abort timeouts. The unit pins `TimeoutStopSec=5` and
  `TimeoutAbortSec=5` and has no `RuntimeMaxSec`.
- An invocation-bound transient cutoff timer (default `OnActiveSec=30s`,
  `AccuracySec=1us`) may be armed against the live `InvocationID` after the
  broker is running and before any authenticated `ARM`. Expiry re-checks that
  identity and `SIGKILL`s only the matching main process. A cutoff kill is a
  controlled recovery event, not a watchdog PASS. Cancel the timer after a
  normal disarm and before stopping the broker. Setup failure means do not ARM.
  For any live G4 or physical-grab procedure, a second physical keyboard or
  SSH from another device must be independently verified before the broker
  is started or ARM is attempted and must remain available through the trial.
  Either route is sufficient. The cutoff helper is supplemental evidence,
  never a substitute. Device-free S3 tests do not require that path.
  PID1/user manager, kernel descriptor close, machine power, and session
  death are outside this userspace path.
- Recovery order for an orderly disarm: ungrab physical sources first, then
  best-effort synthetic releases, then destroy the virtual device. The real
  keyboard must stay usable. Kernel close behavior (grab release on evdev
  close, uinput teardown on close) is verified in source. Named physical
  slices recorded as accepted on candidate `cb72ae0` / docs descendant
  `9a89095`: explicit ARM, sampled pass-through, matching-invocation cutoff,
  typing after descriptor close (Worker 16); armed watchdog abort with a held
  modifier (Worker 19). Remaining G4 claims (LED return, all-control
  fidelity, live host suspend/resume, production autostart) still need
  acceptance.
- System suspend/resume is a lifecycle boundary, not a watchdog failure.
  `packaging/systemd/contextdeck-sleep.sh` (installed as
  `/usr/lib/systemd/system-sleep/contextdeck-broker`) stops an **active**
  broker on sleep `pre` so `WatchdogSec=2` cannot abort a frozen process, and
  starts that unit once on `post` only when an ephemeral root-owned marker
  proves it was active before that sleep. The marker lives in
  `/run/contextdeck-sleep`, not `RuntimeDirectory`. Post-resume start is the
  existing disarmed path: no lease, no virtual device, no automatic re-ARM.
  An inactive or failed broker is not started. Failed stop or start is
  bounded and fail-closed. See [ADR 0001](adr/0001-broker-suspend-resume-sleep-hook.md).
  Device-free hook tests are not live suspend evidence.
- RGB failure disables lighting only; input behavior is unaffected. Never
  auto-switch to direct HID or restart unrelated RGB software.
- Uninstall reverses only owned units/rules/files; user profiles are preserved
  by default.

## Licensing posture (pending COOPERATOR decision)

- Repository evidence: MIT file. Recommendation: retain MIT for independently
  authored code, dynamic-link Qt/KF6 under their reviewed LGPL routes, keep
  OpenRGB (GPL-2.0-or-later) as a separate external process, and never copy
  source from G213Tray (GPL-3.0-or-later, research evidence only) or
  input-remapper (GPL-3.0-or-later, existing independent installation).
- No third-party source has been copied into the product. Final decision and
  dependency provenance review are gate G6.

## Verified host and toolchain baseline

Measured by the ORCHESTRATOR on the target workstation; Workers should recheck
rather than trust, but these removed real guesswork from the M1 routing:

| Fact | Evidence |
|------|----------|
| No `extra-cmake-modules`, no KF6 umbrella config | `find_package(KF6 COMPONENTS …)` fails; `/usr/lib/cmake/KF6` absent |
| Per-component KF6 lookups work without ECM | A throwaway `find_package(KF6<Pkg> REQUIRED)` + compile + link probe succeeded for `StatusNotifierItem`, `Kirigami`, `Screen`, `Config`, `WindowSystem`, `DBusAddons`, `Crash`, `Service` |
| There is no `KF6::Config` target | Use `KF6::ConfigCore` / `KF6::ConfigGui`; package name stays `KF6Config` |
| Display-off API | `KF6::ScreenDpms`, `#include <KScreenDpms/Dpms>`, `KScreen::Dpms::switchMode(KScreen::Dpms::Off)`; LGPL-2.1-or-later |
| Suspend policy | `login1.Manager.CanSuspend` → `"yes"`; logind only, never `/sys/power/state` |
| KWin script dev loop | `org.kde.KWin /Scripting` exposes `loadScript(path, pluginName)`, `start()`, `isScriptLoaded`, `unloadScript` — no packaging needed to test the bridge; `kpackagetool6` is the persistent route |
| OpenRGB not installed | Available as `openrgb 1.0rc3` (`extra`) / `1.0rc3-3.1` (`cachyos-extra-v3`); `hidraw*` is `root:root 0600` |
| G213 topology | `046d:c336`; `event7` = if00, `event8` = if01; `hidraw2`/`hidraw3`; event nodes `root:input 0660` with no user ACL, and the session user is **not** in `input` |
| `/dev/uinput` | Session-user ACL from KDE Connect `uaccess`; broker `user:contextdeck-broker:rw-` is added by `99-contextdeck-broker-uinput.rules` after that builtin so the session ACL is not replaced |

Consequence for routing: the lighting + context vertical needs no new
privilege, while the input vertical cannot run at all without a udev rule or a
privileged broker identity. That asymmetry is why M1 excludes input
interception entirely and M2 owns it.

## Open evidence gates

See ROADMAP.md: G1 physical controls, G2 RGB behavior, G3 device authority,
G4 input safety (named slice accepted; remainder open), G5 desktop behavior,
G6 licensing, G7 power actions, G8 release lifecycle.
