# ContextDeck

An application-aware command deck for the **Logitech G213 Prodigy** keyboard,
built for **Linux / KDE Plasma 6 / Wayland**.

> **Status: early development.** Five-zone lighting and application context
> (M1) are implemented and recorded as accepted on hardware. The input broker
> (M2) exists in this repository and can be installed **inactive**. The named
> live G4 slices are accepted, but the M2 logical whole remains open. M2 is
> parked with G3 host-mitigated on the authorized reference host. The M4
> state and ledger reconciliation is closed on `235d467...`. The code health
> and refactoring whole is closed on `ba87ba08...` (acceptance-PASS;
> publication-PASS; QML runtime validation parked). The current bounded whole is
> **ui-ux-refinement**: presentation-only refinement of the session
> application's six sections, tray, accessibility, and one bounded
> COOPERATOR-executed runtime QML validation; no behavior, broker, packaging, or
> license change. **Full G4 remains open**: the named live slices
> (Sessions 16, 19, 22, 23, 24) are accepted, and the remaining work is
> production/autostart readiness, hibernate/hybrid-sleep, and general
> input-remapper coexistence. Independent G3 re-audit of the residual ACL gap
> remains host-mitigated only. M3 workspace-aware lighting is code-accepted on
> `502ae75...`; its physical five-zone IRL observation is deferred by explicit
> COOPERATOR decision. M3 is not closed, and code acceptance is not physical
> acceptance. M4 workspace session manager is code-accepted (Slice A on
> `aca6c68`, Slice B on `db9ddc1`); its live IRL run is deferred by explicit
> COOPERATOR decision. M4 is not closed, and code acceptance is not live or
> physical acceptance. Nothing changes unless the user presses Apply, and
> nothing launches because the session began. The plan lives in
> [ROADMAP.md](ROADMAP.md) and the design in
> [docs/architecture.md](docs/architecture.md).

## The idea

When the foreground application changes, ContextDeck changes what the keyboard
does — automatically, quietly, and only where you configured it:

- **Per-application key profiles.** F1–F12 and the media keys can emit
  shortcuts you choose per app (for example Play/Pause → `Ctrl+B` in Cursor).
  Keys you never configured keep their normal behavior. Remapping itself is a
  later whole; M2 is pass-through only.
- **Per-application lighting.** Each app profile gets one base color, applied
  honestly across the G213's **five physical RGB zones**. The G213 has no
  per-key RGB, and this project never pretends otherwise.
- **Workspace-aware lighting (opt-in).** A global five-slot layout can mix
  virtual-desktop indicators with application color. Existing presets keep
  their previous behavior until those roles are configured.
- **Named workspace sessions (code-accepted; live IRL deferred).** M4 Slice B
  implements named desktop layouts and per-application assignments, observes
  live desktop state, previews the intended diff, and applies it only when you
  press **Použiť** — with a checkpoint revert, typed in-session launch, and
  event-driven placement. Plasma-login autostart stays in M5.
- **A quiet tray indicator.** Connection state, automatic mode, the current
  app and the active profile at a glance.
- **Two deck buttons.** Game Mode → Suspend and Backlight → Displays Off —
  only if those buttons are safely remappable. Hardware evidence classifies
  both as **firmware-only** (no host event), so they stay out of the remap
  catalog.

Safety is a first-class feature: a crash must never leave you unable to type.
Any live G4 grab needs an independently verified second physical keyboard or
SSH from another device **before** the broker is started or armed. A cutoff
timer is extra evidence, never a substitute. Either recovery route is
enough; device-free tests do not need that path.

## Deliberately narrow

- Only the Logitech G213 Prodigy (USB `046d:c336`). No generic remapper.
- Only Linux: CachyOS/Arch, KDE Plasma 6, KWin, Wayland, systemd.
- No macros, no scripting, no cloud, no telemetry, no web UI.
- A polished tool for one person and one keyboard first.

## Status

| Area | State |
|------|-------|
| Plan | Foundation planning accepted; M2 parked; M3 code-accepted with physical observation deferred; M4 Slice A + Slice B code-accepted with live IRL deferred — [ROADMAP.md](ROADMAP.md) |
| Lighting (M1) | Implemented; recorded as COOPERATOR-accepted IRL (five zones, not per-key) |
| Workspace lighting (M3) | M3 workspace-aware lighting is code-accepted on `502ae75...`; its physical five-zone IRL observation is deferred by explicit COOPERATOR decision. M3 is not closed, and code acceptance is not physical acceptance. Later IRL steps: [docs/testing-m3.md](docs/testing-m3.md) |
| Workspace sessions (M4) | Slice A code-accepted on `aca6c68`; Slice B code-accepted on `db9ddc1` (schema 4 sessions/assignments, extended desktop observation, pure dry-run plan, fail-closed explicit Apply with user-local checkpoint/revert, typed in-session launch, `PlacementHint` placement/maximize). Default Apply creates/renames/sets rows/wrapping only; removal, `current`, launch, and maximize are separate opt-ins. **Live IRL deferred by explicit COOPERATOR decision; M4 not closed; code acceptance is not live or physical acceptance; no autostart.** Later IRL steps: [docs/testing-m4.md](docs/testing-m4.md) |
| Input broker (M2) | Parked. Production path in tree (enumerator, explicit ARM, IPC lease, watchdog, install + late uinput ACL, suspend/resume sleep hook). Inactive install recorded; named Sessions 16/19/22/23/24 slices accepted; **full G4 remains open**; residual independent G3 evidence gap is not closed by this whole |
| Hardware evidence | G1 control matrix closed (Game Mode / Backlight firmware-only); G2 lighting closed; G4 named live slices accepted (Sessions 16, 19, 22, 23, 24: ARM/pass-through/cutoff, armed watchdog/held modifier, one live suspend/resume cycle, LED return + all 18 host-remappable controls, one bounded input-remapper mapping); full G4 still open (production/autostart readiness, hibernate/hybrid-sleep, general input-remapper coexistence) |
| Build / tests | CMake + Ninja; registered CTest suite owned by [CMakeLists.txt](CMakeLists.txt) |
| Host install | Packaging files are in the repo. Installed vs not-installed is host evidence — see [docs/operations.md](docs/operations.md). The broker unit has no `[Install]` section and must not autostart |

## Documents

| File | Purpose |
|------|---------|
| [ROADMAP.md](ROADMAP.md) | The plan: milestones, evidence gates, current state |
| [docs/architecture.md](docs/architecture.md) | Accepted architecture and boundaries |
| [docs/adr/](docs/adr/) | Product ADRs 0001–0004 (sleep hook, host mutation authority, schema 4, typed launch) |
| [docs/operations.md](docs/operations.md) | Host enablement, G3/S5 install, recovery notes |
| [docs/testing-m2.md](docs/testing-m2.md) | M2 / G4 test pack (named slices accepted; full G4 not passed) |
| [docs/testing-m3.md](docs/testing-m3.md) | M3 IRL checklist (later acceptance; no host mutation) |
| [docs/testing-m4.md](docs/testing-m4.md) | M4 workspace sessions IRL checklist (later acceptance; no host mutation) |
| [handout.md](handout.md) | Original bootstrap brief (historical) |
| [LICENSE](LICENSE) | MIT file; the final product licensing decision is still pending |

## Thanks

Research references that shaped the plan: [OpenRGB](https://openrgb.org),
[G213Tray](https://github.com/Agundur-KDE/G213Tray),
[input-remapper](https://github.com/sezanzeb/input-remapper). No third-party
source code is copied; see [docs/architecture.md](docs/architecture.md) for
the licensing stance. input-remapper stays enabled for its existing mouse
preset; this project must not reconfigure it.

## Disclaimer

Unofficial project, not affiliated with or endorsed by Logitech. "Logitech",
"G213" and "Prodigy" are trademarks of their respective owners.
