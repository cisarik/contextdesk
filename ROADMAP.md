# ContextDeck — Roadmap

Human-friendly plan of record. Authority lives elsewhere: the repository is the
source of truth for code, the pinned AP protocol governs process, and META
stores the exact Worker prompt/report history. This file summarizes "where we
are and where we are going" and is maintained by the ORCHESTRATOR after each
reconciliation.

## Where we are now

- Product repo: `cisarik/contextdesk`, branch `main`, HEAD `6b4e4b3` — two
  commits ahead of `origin/main` (`ca3ac07`); nothing pushed yet.
- AP pinned: `.ap/` gitlink `7ef45da`.
- Foundation planning (whole `g213-contextdeck-foundation-architecture`):
  Planner report 01/01 reconciled and accepted as **PARTIAL**, archived in META
  (local commit `f420ec6`, unpushed). PARTIAL is the correct outcome — the
  architecture is routable, but hardware evidence gates remain open.
- Next whole: **V1 `g213-contextdeck-profile-contract`** — the typed profile
  model, no hardware, no GUI. The Worker prompt is prepared; it runs in a fresh
  session with Plan Mode off.

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
| V1 | `g213-contextdeck-profile-contract` | Typed model, resolver, validated atomic persistence, tests, `docs/specification.md` | G0 | **Next** |
| P1 | `g213-contextdeck-control-evidence` | Approved physical control matrix (COOPERATOR-assisted probe) | — | Planned (parallel) |
| V2 | `g213-contextdeck-kwin-context` | Tray, KWin event bridge, deduplicated app inventory | V1 | Planned |
| P2 | `g213-contextdeck-rgb-evidence` | Approved OpenRGB access + five-zone evidence | — | Planned (parallel) |
| V3 | `g213-contextdeck-application-lighting` | First usable profile→lighting workflow | V2, P2 | Planned |
| V4 | `g213-contextdeck-input-passthrough-safety` | Narrow broker, pass-through only, crash/recovery evidence | P1, G3 | Planned |
| V5 | `g213-contextdeck-one-context-shortcut` | One verified key mapped in one app | V2, V4 | Planned |
| V6 | `g213-contextdeck-key-profiles` | Expand mappings + shortcut recorder to verified controls | V5 | Planned |
| V7 | `g213-contextdeck-system-actions` | Separately accepted Displays Off and Suspend | G7 | Planned |
| V8 | `g213-contextdeck-release-lifecycle` | Packaging, removal, opt-in autostart, release acceptance | G6, G8 | Planned |

Dependency order: V1 → V2 → V3; P1 and P2 run independently of the code
verticals; V4 needs P1 + G3 + a documented recovery procedure; V5 needs an
accepted V4; V7 needs G7; V8 needs G6/G8.

## Evidence gates

Hardware and authority decisions that must be proven before related claims
ship:

| Gate | Needed evidence | Blocks | Status |
|------|-----------------|--------|--------|
| G0 | Baseline ownership confirmed | Any repository mutation | **Frozen at `6b4e4b3`** |
| G1 | Routing matrix for all 20 requested controls | Special-button remapping | Open (physical probe) |
| G2 | OpenRGB trial: five zones, reconnect, coexistence | Shipping the RGB route | Open (needs approval) |
| G3 | Accepted input/RGB access boundaries | Services, udev rules, broker deployment | Planned |
| G4 | Interception, crash, hang, release, recovery acceptance | Enabling remapping | Planned |
| G5 | KWin lifecycle, identity, focus-race measurements | Contextual behavior claims | Planned |
| G6 | License decision + dependency provenance | Release | Planned |
| G7 | Authorized display-off and suspend acceptance | Enabling power actions | Planned |
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

### V1 profile contract — next whole

- Implements: typed profile/action model, deterministic resolver, validated
  atomic persistence (`QSaveFile` + bounded last-valid backup), CTest suite,
  root CMake, and the `docs/specification.md` sections that own this behavior.
- Locks the dangerous semantics before any device code exists:
  inherit ≠ pass-through ≠ disabled; missing global assignment = pass-through;
  unknown fields/schema versions are rejected (never silently dropped);
  no shell/executable config.
- Control catalog in the model: F1–F12, Previous, Play/Pause, Next, Mute,
  Volume Down, Volume Up, Game Mode, Backlight (the last two labelled
  conditional on G1).
- Allowlist: `CMakeLists.txt`, `.gitignore`, `src/core/`, `tests/unit/`,
  `docs/specification.md`. No devices, GUI, KWin, RGB, packaging.

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
