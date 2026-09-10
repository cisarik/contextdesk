# AGENTS.md — G213 ContextDesk

Project-owned rules for any agent session in this repo. Universal AP semantics are
not stored here; when AP is integrated, its managed block will also live in this
file, and everything outside that block stays authoritative for this project.

## Current repository state

- Pre-implementation bootstrap for **G213 ContextDeck**: a Linux/KDE/Wayland control
  utility for the Logitech G213 Prodigy keyboard only. Canonical repo:
  `https://github.com/cisarik/contextdesk`.
- Tree is only `handout.md`, a one-line `README.md` stub, and `LICENSE`. No source,
  build system, tests, lint, CI, or `.ap/` submodule. Do not invent build/test
  commands and do not start implementation or scaffolding.
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
- The first Worker must be a plan-only fresh Planner: logical whole
  `g213-contextdeck-foundation-architecture`, `Worker session ordinal: 01`,
  `Worker exchange ordinal: 01`, `Worker session target: fresh-worker-session`,
  `Native planning mode: required`. Do not issue an implementation prompt before
  its terminal report returns.

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
  It currently has an untracked `projects/contextdesk/00/00_handout.md` — verify Git
  state before treating anything as archived.
- Trace policy (handout §5): project key `g213-contextdeck`, public,
  `historical-evidence-only`, archival owner ORCHESTRATOR. Archive the exact issued
  prompt and exact actual terminal report together, only after the report exists, in
  the same first-add commit. Workers never self-archive; reports are never rewritten
  or prettified. Archives must be public-safe: no secrets, tokens, private URLs,
  personal data, hidden reasoning, or raw tool logs. Local add/commit only; remote
  push is publication and needs separate authority.
- AP adoption is planned, not done: pinned `.ap/` submodule plus `./.ap/ap init`
  (which manages a block in this file). Do not create `.ap/`, run `ap init`, or
  hand-edit a managed block without explicit bootstrap authority.

## Do not create

- No live orchestration state files (`BOOT_*`, `NEXT_*`, `WORKERS.md`,
  `SESSION_STATE.md`, `CURRENT_AGENT.md`) and no session diaries. Durable meaning
  belongs to its owner: spec / ADR / security / operations / roadmap / tests / META.

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
  never `/sys/power/state` (this host reportedly had `sleep.target`/`suspend.target`
  masked after an upgrade — verify, don't bypass). Display-off must not alter KScreen
  topology.

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
