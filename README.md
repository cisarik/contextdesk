# ContextDeck

An application-aware command deck for the **Logitech G213 Prodigy** keyboard,
built for **Linux / KDE Plasma 6 / Wayland**.

> **Status: early development.** Five-zone lighting and application context
> (M1) are implemented and recorded as accepted on hardware. The input broker
> (M2) exists in this repository and can be installed **inactive**. Named
> physical slices (explicit ARM, sampled G213 pass-through, invocation-bound
> cutoff recovery, armed watchdog abort with a held modifier) are recorded as
> accepted on the reference host; **full G4** remains open, including live
> host suspend/resume. A systemd-sleep hook can stop an active broker before
> sleep and start it once afterwards, always disarmed; that path is not live
> accepted yet. The plan lives in [ROADMAP.md](ROADMAP.md) and the design in
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
| Plan | Foundation planning accepted; M2 in progress — [ROADMAP.md](ROADMAP.md) |
| Lighting (M1) | Implemented; recorded as COOPERATOR-accepted IRL (five zones, not per-key) |
| Input broker (M2) | Production path in tree (enumerator, explicit ARM, IPC lease, watchdog, install + late uinput ACL, suspend/resume sleep hook). Inactive install recorded; named physical slices accepted; **full G4 remains open** (live suspend/resume not accepted) |
| Hardware evidence | G1 control matrix closed (Game Mode / Backlight firmware-only); G2 lighting closed; G4 named ARM/pass-through/cutoff and armed-watchdog/held-modifier slices accepted; LED, all-control, live suspend/resume, and production autostart still open |
| Build / tests | CMake + Ninja; registered CTest suite owned by [CMakeLists.txt](CMakeLists.txt) |
| Host install | Packaging files are in the repo. Installed vs not-installed is host evidence — see [docs/operations.md](docs/operations.md). The broker unit has no `[Install]` section and must not autostart |

## Documents

| File | Purpose |
|------|---------|
| [ROADMAP.md](ROADMAP.md) | The plan: milestones, evidence gates, current state |
| [docs/architecture.md](docs/architecture.md) | Accepted architecture and boundaries |
| [docs/adr/](docs/adr/) | Product ADRs (suspend/resume sleep hook: ADR 0001) |
| [docs/operations.md](docs/operations.md) | Host enablement, G3/S5 install, recovery notes |
| [docs/testing-m2.md](docs/testing-m2.md) | M2 / G4 test pack (named slice accepted; full G4 not passed) |
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
