# AGENTS.md — G213 ContextDesk

Project-owned rules for any agent session in this repo. Universal AP semantics are
not stored here; the AP-managed block at the bottom of this file points to the
pinned `.ap/` submodule, and everything outside that block stays authoritative for
this project.

## Current repository state

- ContextDeck is a Linux/KDE/Wayland control utility for the Logitech G213
  Prodigy keyboard only. Canonical repo: `https://github.com/cisarik/contextdesk`.
  `ROADMAP.md` is the human plan of record for milestone state, evidence gates,
  and ledger dispositions.
- **M1 `g213-contextdeck-mvp-context-lighting` is implemented and recorded as
  COOPERATOR-accepted IRL.** That acceptance is historical evidence (META notes
  for that whole). It is not a fresh hardware run from later sessions and does
  not close unrelated remaining gates such as G4 or G7.
- **M2 `g213-contextdeck-input-passthrough-safety` is parked, not closed.** The
  named live G4 slices are accepted (Sessions 16, 19, 22, 23, and 24, as
  recorded in the M2 trace), but the M2 logical whole remains open. M2 is parked
  with G3 host-mitigated on the authorized reference host. **Full G4 remains
  open**: the named slices are not rerun or reopened, and the remaining work is
  production/autostart readiness, hibernate/hybrid-sleep, and general
  input-remapper coexistence. The production broker path is in the tree
  (enumerator, explicit `LEASE`/`ARM`, Unix-socket IPC lease, watchdog, broker
  install rule, late uinput ACL rule, suspend/resume sleep hook); an inactive
  install of candidate `cb72ae0` is recorded as `deployment-PASS`.
  `G3-ACL-REPROBE-01` is host-mitigated but was not independently re-audited.
  Do not claim whole M2, whole G4, independent G3 closure, install/remove/
  rollback readiness, production readiness, autostart safety, hibernate/
  hybrid-sleep support, or general coexistence.
- **M3 `g213-contextdeck-workspace-aware-lighting`**: M3 workspace-aware
  lighting is code-accepted on `502ae75...`; its physical five-zone IRL
  observation is deferred by explicit COOPERATOR decision. M3 is not closed, and
  code acceptance is not physical acceptance.
- **M4 `g213-contextdeck-workspace-session-manager`**: M4 workspace session
  manager is code-accepted (Slice A on `aca6c68`, Slice B on `db9ddc1`); its
  live IRL run is deferred by explicit COOPERATOR decision. M4 is not closed,
  and code acceptance is not live or physical acceptance. Nothing mutates
  without the user's explicit Apply and no launch happens at Plasma login.
- Tree: `handout.md`, `AGENTS.md`, `README.md`, `ROADMAP.md`, `LICENSE`, `docs/`,
  `CMakeLists.txt`, `cmake/`, `src/` (session app + `src/broker/`), `ui/`,
  `kwin/`, `tests/unit/`, `packaging/` (systemd, udev, sysusers), and the pinned
  `.ap/` protocol submodule.
- Build: `cmake -S . -B build -G Ninja` then `cmake --build build`. The
  registered CTest suite is owned by `CMakeLists.txt` (`add_test` names). There
  is still no lint config and no CI. Commands in this file are not a grant to
  run them. Implementation happens only under an explicit Orchestrator-issued
  Worker prompt.
- Repository artifacts are not installed host state. Presence of packaging
  files, a local `build/` binary, or `/usr` copies on one machine does not mean
  every host is installed or verified. Host enablement, named physical
  acceptance, and remaining G4/G8 claims remain COOPERATOR-owned operations
  evidence. A documentation commit does not rerun hardware acceptance.
- `handout.md` is the original COOPERATOR-to-ORCHESTRATOR bootstrap contract
  (historical). Read it for intent and safety constraints; it is not a renewed
  bootstrap task. Fast path: §1–6 (roles, delivery, AP/META/trace), §29 (grab
  safety), §33–34 (first Planner), §46–47 (first response).

## Roles, language, authority

- The human is the **COOPERATOR** and owns the objective, product/UX,
  security/privacy, licensing, publication, and risky machine changes.
- An agent here is the **ORCHESTRATOR** unless a Worker prompt says otherwise.
  Workers act only inside one complete bounded prompt and lose authority at
  terminal report.
- Access profile: **ChatOrchestrator** (mediated through the COOPERATOR; an
  inspection clone is not the COOPERATOR’s uncommitted worktree). Selected
  delivery for this project remains **manual** across subsequent exchanges.
  Dispatch availability in a client does not change that selection.
- COOPERATOR-facing chat is **Slovak**. Worker prompts, Worker reports, and
  repository documentation are **English**.
- Foundation planning is done: Planner report 01/01 (logical whole
  `g213-contextdeck-foundation-architecture`) was reconciled and accepted as
  PARTIAL, archived in META. The accepted plan is summarized in `ROADMAP.md` and
  `docs/architecture.md` — read them before proposing new components.
- M1 is implemented and recorded as COOPERATOR-accepted IRL (five-zone lighting
  and context). G2 is closed. G7 remains the power-action evidence gate and is
  not closed by later documentation work.
- M2 planning passed and the production wiring is in the tree (enumerator +
  `RealSink`/`EvdevGrabber` behind explicit `LEASE`/`ARM`, session IPC,
  watchdog, G3 packaging, and the late uinput ACL rule). **Nothing in
  documentation grants live grab or autostart.** Full G4 remains open; see
  "Current repository state" above and `ROADMAP.md` for the park claims and the
  named accepted slices. The Super-key **deck layer** brainstorm remains a
  future whole after M3, recorded in `ROADMAP.md`.
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

## Hard rule: manual delivery, no automated Worker dispatch

- This project’s access profile is **ChatOrchestrator** with **manual**
  delivery preserved: the COOPERATOR carries every prompt and report.
- Never use the Task/subagent tool or any agent-spawning mechanism to dispatch,
  substitute, or simulate a Worker. Never claim a Worker ran unless the COOPERATOR
  returned its actual terminal report.
- Treat returned reports as claim/evidence packages, not truth by declaration.
  "Continue", "approve", native Plan Mode approval, and retained context never
  grant implementation authority.

## Protocol and trace sources

- AP: `https://github.com/cisarik/ap` — `AP.md` is the sole live normative owner.
  Never resurrect old AP generations from Git history. Verify the pinned `.ap/`
  checkout before trusting local protocol files.
- META: `https://github.com/cisarik/meta` — historical evidence only, never current
  truth, task authority, or a roadmap. Follow the storage contract in its
  `README.md`; do not assume an old layout or hardcode dates. This project's
  trace lives under `projects/contextdesk/`. Always verify actual META Git state
  before treating anything as archived or complete.
- Trace policy (handout §5): handout proposed the provisional key
  `g213-contextdeck`; the operative META project key is `contextdesk` (matches the
  repo name and the actual trace directory). Trace is public and
  `historical-evidence-only`. **Exact report-file preparation** and **META Git
  archival are separate.** A Worker prepares a report file only under an explicit
  persistence grant; the COOPERATOR owns META add/commit/push in this
  ChatOrchestrator workflow. Archive the exact issued prompt and exact actual
  terminal report together, only after the report exists, in the same first-add
  commit. Reports are never rewritten or prettified. Archives must be public-safe:
  no secrets, tokens, private URLs, personal data, hidden reasoning, or raw tool
  logs. Remote push is publication and needs separate authority.
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
  documented TTY recovery. Never autostart an unproven grabbing path. For any
  live G4 grab, a second physical keyboard or SSH from another device must be
  independently verified before the broker is started or ARM is attempted and
  must remain available through the trial. Either route is sufficient. A cutoff
  timer is supplemental evidence, never a substitute. Device-free S3 tests do
  not require that path.
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
  Volume Up, Game Mode, Backlight. G1 measured Game Mode and Backlight as
  firmware-only (no host event) — never silently substitute PrintScreen or Pause.
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
