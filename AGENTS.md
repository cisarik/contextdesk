# AGENTS.md — G213 ContextDesk

Project-owned rules for any agent session in this repo. Universal AP semantics are
not stored here; the AP-managed block at the bottom of this file points to the
pinned `.ap/` submodule, and everything outside that block stays authoritative for
this project.

## Current repository state

- **M1 is implemented, not yet accepted.** ContextDeck is a Linux/KDE/Wayland
  control utility for the Logitech G213 Prodigy keyboard only. Canonical repo:
  `https://github.com/cisarik/contextdesk`.
- Tree: `handout.md`, `AGENTS.md`, `README.md`, `ROADMAP.md`, `LICENSE`, `docs/`,
  `CMakeLists.txt`, `cmake/`, `src/`, `ui/`, `kwin/`, `tests/unit/`,
  `packaging/systemd/`, and the pinned `.ap/` protocol submodule.
- Real build/test commands (verified by the ORCHESTRATOR, not invented):
  `cmake -S . -B build -G Ninja` ; `cmake --build build` ;
  `ctest --test-dir build --output-on-failure` (3 units). There is still no lint
  config and no CI. Implementation happens only under an explicit
  Orchestrator-issued Worker prompt; nothing in this file grants it.
- `handout.md` is the COOPERATOR-to-ORCHESTRATOR bootstrap contract (47 sections,
  ~2300 lines). Read it before planning or routing; treat it as immutable history.
  Fast path: §1–6 (roles, manual dispatch, AP/META/trace), §33–34 (mandatory first
  Planner), §46–47 (required first response).

## Roles, language, authority

- The human is the **COOPERATOR** and owns the objective, product/UX,
  security/privacy, licensing, publication, and risky machine changes.
- An agent here is the **ORCHESTRATOR** unless a Worker prompt says otherwise.
  Workers act only inside one complete bounded prompt and lose authority at
  terminal report.
- COOPERATOR-facing chat is **Slovak**. Worker prompts and Worker reports are
  **English**.
- Foundation planning is done: Planner report 01/01 (logical whole
  `g213-contextdeck-foundation-architecture`) was reconciled and accepted as
  PARTIAL, archived in META. The accepted plan is summarized in `ROADMAP.md` and
  `docs/architecture.md` — read them before proposing new components.
- M1 `g213-contextdeck-mvp-context-lighting` is **implemented, corrected, and redesigned**
  (twenty-one local commits `1b024e4`..`4276f5b` on `main`, 3/3 CTest units green,
  independently rebuilt by the ORCHESTRATOR) and **awaiting COOPERATOR IRL acceptance**
  via `docs/operations.md` + `docs/testing.md` (including the 5-step zone-map probe).
  G2 (five-zone lighting) and G7 (power actions) are decided by that IRL run,
  not by code review.
- The next route is chosen after IRL results: an M1 final acceptance reconciliation,
  or **M2 `g213-contextdeck-input-passthrough-safety`**, which still requires the
  G1 physical-control probe and the reserved event-node access grant. **No input
  interception exists in the tree today, and none may be added outside M2.**
  Only the ORCHESTRATOR routes further.
- COOPERATOR-granted mutation classes for M1 host enablement (named, bounded):
  install `openrgb` from the repo including its udev rules; run the OpenRGB SDK
  server on loopback; install/load the KWin script via `kpackagetool6` or
  `org.kde.KWin /Scripting`; a systemd **user** unit started manually with no
  autostart; local commits on `main` without push. A udev rule or privileged
  identity for G213 **event nodes** was granted in principle but is **reserved
  for the input whole (M2)** and must not be used by M1.
- Verified toolchain reality (measured, do not re-derive): `extra-cmake-modules`
  is absent and there is **no KF6 umbrella config**, so `find_package(KF6
  COMPONENTS ...)` fails. Use per-component config-mode lookups
  (`find_package(KF6Kirigami REQUIRED)`, …). There is no `KF6::Config` target —
  it is `KF6::ConfigCore`/`KF6::ConfigGui`. Display-off is `KF6::ScreenDpms`
  (`#include <KScreenDpms/Dpms>`); suspend goes through logind
  (`CanSuspend=yes` verified). `org.kde.KWin /Scripting` exposes
  `loadScript/start/unloadScript/isScriptLoaded` for the bridge dev loop.

## Hard rule: no automated Worker dispatch

- This project requires the **Read-Only Orchestrator** profile: the COOPERATOR
  manually carries every prompt and report (explicit opt-out from AP default agent
  dispatch).
- Never use the Task/subagent tool or any agent-spawning mechanism to dispatch,
  substitute, or simulate a Worker. Never claim a Worker ran unless the COOPERATOR
  returned its actual terminal report.
- Treat returned reports as claim/evidence packages, not truth by declaration.
  "Continue", "approve", native Plan Mode approval, and retained context never
  grant implementation authority.

## Protocol and trace sources

- AP: `https://github.com/cisarik/ap` — `AP.md` is the sole live normative owner.
  Local checkout (verify identity/status before trusting): `/home/agile/Projects/ap`.
  Never resurrect old AP generations from Git history.
- META: `https://github.com/cisarik/meta` — historical evidence only, never current
  truth, task authority, or a roadmap. Local checkout: `/home/agile/meta`. Follow the
  storage contract in its `README.md`; do not assume an old layout or hardcode dates.
  This project's trace lives under `projects/contextdesk/` (foundation exchange
  archived locally as `f420ec6`); always verify actual META Git state before treating
  anything as archived or complete.
- Trace policy (handout §5): handout proposed the provisional key
  `g213-contextdeck`; the operative META project key is `contextdesk` (matches the
  repo name and the actual trace directory). Trace is public,
  `historical-evidence-only`, archival owner ORCHESTRATOR. Archive the exact issued
  prompt and exact actual terminal report together, only after the report exists, in
  the same first-add commit. Workers never self-archive; reports are never rewritten
  or prettified. Archives must be public-safe: no secrets, tokens, private URLs,
  personal data, hidden reasoning, or raw tool logs. Local add/commit only; remote
  push is publication and needs separate authority.
- AP is pinned as the `.ap/` submodule (the gitlink is the exact AP version); the
  managed block at the bottom of this file is owned by `./.ap/ap init`. Check health
  with `./.ap/ap doctor`; never hand-edit inside the managed markers. Treat `.ap/` as
  read-only during ordinary project work; AP updates (`./.ap/ap update --check`,
  `./.ap/ap update --apply`) are a separate explicitly authorized task.

## Do not create

- No live orchestration state files (`BOOT_*`, `NEXT_*`, `WORKERS.md`,
  `SESSION_STATE.md`, `CURRENT_AGENT.md`) and no session diaries. Durable meaning
  belongs to its owner: spec / ADR / security / operations / roadmap / tests / META.
- No second live task queues (`TODOs.md`, `NOTES.md`, ad-hoc milestone files):
  `ROADMAP.md` is the single human plan of record, deferred work goes to the
  roadmap or issues, and history goes to META.

## Documentation

- Repo documentation is **English** and public-facing; keep it human-readable and
  scannable — this is an open-source project for people, not just agents.
- Owners: `README.md` = front door; `ROADMAP.md` = human plan of record (updated by
  the ORCHESTRATOR after each reconciliation); `docs/` = durable technical owners
  (`specification.md`, `architecture.md`, `adr/`, hardware evidence, testing,
  operations); META = exact prompt/report history.
- Never commit local machine paths, workstation details, event-node numbers, or any
  non-public data into these files — the repository is public.

## Mutation, Git, and safety

- Read-only reconnaissance first. Package installs, `/etc` or udev edits, service
  changes, input-remapper/OpenRGB/KWin mutations, USB/HID claims, device grabbing,
  suspend, DPMS-off, and autostart all need authority naming the mutation class;
  read-only inspection never implies it.
- Never `reset`, `clean`, `stash`, `checkout`, or discard user work; fail closed on
  ambiguous repo state. Commit and push need separate explicit authority.
- Input grabbing must not ship before the handout §29 safety acceptance: a crash
  leaves the real keyboard usable, no stuck modifiers, no duplicate/phantom events,
  documented TTY recovery. Never autostart an unproven grabbing path.
- Never log ordinary typed keystrokes. Suspend goes through logind/systemd policy,
  never `/sys/power/state`. Planner evidence (report 01/01) found
  `sleep.target`/`suspend.target` loaded and `CanSuspend=yes` — the old "masked
  targets" assumption was disproven; never bypass policy either way. Display-off
  must not alter KScreen topology.

## Product invariants easy to get wrong

- G213 only: no generic remapper, no other keyboards, no Windows/macOS. Target
  CachyOS/Arch, Plasma 6, KWin, Wayland, systemd.
- The G213 has **five RGB zones, not per-key RGB**. Never design or show per-key
  color control as a real capability.
- Foreground context is event-driven via the KWin scripting API — no
  `xdotool`/`wmctrl`/`xprop`, no title polling. Prefer KWin
  `desktopFileName`/`resourceClass` identity over `/proc` or window captions.
- Actions are a typed model; no arbitrary shell strings. Fail-safe default is
  pass-through; unset means inherit, not swallow.
- Accepted architecture (plan of record in `docs/architecture.md`): the session app
  never touches raw keyboard devices — only the input broker does, and it starts
  disabled. One persistent OpenRGB SDK connection on loopback, never a CLI process
  per focus change; one RGB backend at a time.
- G213 controls in scope: F1–F12, Previous, Play/Pause, Next, Mute, Volume Down,
  Volume Up, Game Mode, Backlight. The last two are conditional on hardware evidence
  (gate G1) — never silently substitute PrintScreen or Pause for them.
- Licensing is unresolved: root `LICENSE` is MIT, but handout §25 says the
  COOPERATOR has not selected the license. Never copy external code (G213Tray is
  GPL-3.0-or-later) before an explicit compatible decision.


<!-- BEGIN MANAGED AP INTEGRATION -->
## Analytic Programming

This project uses Analytic Programming through the pinned Git submodule at `.ap/`.
The exact AP version is the commit recorded by this repository's `.ap` gitlink.

Required reading:
- All participants read `.ap/AP.md`.
- Orchestrators also read `.ap/AP_ORCHESTRATOR.md`.
- Workers also read `.ap/AP_WORKER.md`.
- Prompt structures are in `.ap/PROMPT_CONTRACTS.md`.

Project-specific rules outside this managed block remain authoritative within
their scope. Task authority comes only from the current authoritative
Orchestrator prompt.

Treat `.ap/` as read-only during ordinary project work. Protocol updates require
a separate explicit AP update task.
<!-- END MANAGED AP INTEGRATION -->
