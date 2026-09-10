# ContextDeck

An application-aware command deck for the **Logitech G213 Prodigy** keyboard,
built for **Linux / KDE Plasma 6 / Wayland**.

> **Status: early proof of concept — active development, nothing installable
> yet.** The plan lives in [ROADMAP.md](ROADMAP.md) and the design in
> [docs/architecture.md](docs/architecture.md).

## The idea

When the foreground application changes, ContextDeck changes what the keyboard
does — automatically, quietly, and only where you configured it:

- **Per-application key profiles.** F1–F12 and the media keys can emit
  shortcuts you choose per app (for example Play/Pause → `Ctrl+B` in Cursor).
  Keys you never configured keep their normal behavior.
- **Per-application lighting.** Each app profile gets one base color, applied
  honestly across the G213's **five physical RGB zones**. The G213 has no
  per-key RGB, and this project never pretends otherwise.
- **A quiet tray indicator.** Connection state, automatic mode, the current
  app and the active profile at a glance.
- **Two deck buttons.** Game Mode → Suspend and Backlight → Displays Off —
  but only if hardware probing proves these buttons are safely remappable.
  Until then the project promises nothing about them.

Safety is a first-class feature: any code that intercepts keyboard input ships
only after crash recovery is proven on real hardware. A crash must never leave
you unable to type.

## Deliberately narrow

- Only the Logitech G213 Prodigy (USB `046d:c336`). No generic remapper.
- Only Linux: CachyOS/Arch, KDE Plasma 6, KWin, Wayland, systemd.
- No macros, no scripting, no cloud, no telemetry, no web UI.
- A polished tool for one person and one keyboard first.

## Status

| Area | State |
|------|-------|
| Plan | Reconciled — foundation planning accepted; see [ROADMAP.md](ROADMAP.md) |
| Code | None yet — the first slice (`mvp-context-lighting`: profiles, KWin context, tray UI, per-app RGB) is next |
| Hardware evidence | Partial — physical-button probe and RGB trial are open gates |
| Build / install | None yet — do not expect a build system or installer |

## Documents

| File | Purpose |
|------|---------|
| [ROADMAP.md](ROADMAP.md) | The plan: milestones, evidence gates, current state |
| [docs/architecture.md](docs/architecture.md) | Accepted architecture and boundaries |
| [handout.md](handout.md) | Original bootstrap brief (historical) |
| [LICENSE](LICENSE) | MIT file; the final product licensing decision is still pending |

## Thanks

Research references that shaped the plan: [OpenRGB](https://openrgb.org),
[G213Tray](https://github.com/Agundur-KDE/G213Tray),
[input-remapper](https://github.com/sezanzeb/input-remapper). No third-party
source code is copied; see [docs/architecture.md](docs/architecture.md) for
the licensing stance.

## Disclaimer

Unofficial project, not affiliated with or endorsed by Logitech. "Logitech",
"G213" and "Prodigy" are trademarks of their respective owners.
