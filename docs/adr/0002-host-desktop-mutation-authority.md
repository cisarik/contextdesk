# ADR 0002 — Host desktop mutation authority and no `kwinrulesrc`

Status: accepted for the M4 tree. Live desktop mutation is not implemented in
Slice A and has no standing authority.

## Context

M4 wants named desktop sessions, per-application assignment, in-session
launch, and window placement. Those operations change the user's live Plasma
desktop configuration: `createDesktop`, `setDesktopName`, `removeDesktop`,
`rows`, `navigationWrappingAround`, and `current`. They are destructive-ish,
user-visible, and hard to reverse when a user has arranged their own layout.

The repository also observed that persistent KWin window rules
(`kwinrulesrc`) can enforce placement without ContextDeck running.

## Decision

- **M4 Slice A is observational.** It may read `VirtualDesktopManager`, decode
  `rows`/`navigationWrappingAround`, compute a pure dry-run `WorkspacePlan`,
  and save named sessions and assignments through the explicit `Uložiť`
  action. It must not call any desktop mutation, launch an application, or
  write a KWin rules file.
- **Live mutation is a separate grant (Slice B).** A later prompt must
  separately authorize the mutation class, checkpoint, preview, and revert
  path before any Apply exists. Split rejection in Slice A is a bounded stop,
  not a reason to widen scope.
- **Default Apply is create + rename + rows/wrapping only.** `removeDesktop`
  is never part of the default preview or default Apply; it is explicit,
  separately confirmed user action with its own revert consequence.
- **Do not fight the user.** Live Plasma desktop edits never auto-rewrite a
  saved session. Drift is reported; Apply is explicit.
- **Checkpoint and revert (Slice B design).** Before Apply, snapshot the
  observed state into a user-local `workspace-checkpoint.json` (never the
  profile document, never META). Revert reapplies the checkpoint
  create/rename/rows/wrapping values. Removal-revert may mint new desktop
  UUIDs and must rebind assignments by ordinal.
- **No `kwinrulesrc` writes.** The product keeps `kwinrulesrc` out of scope:
  persistent rules would fight user rules, duplicate an external owner of
  window placement, and are unnecessary while ContextDeck only places windows
  during its own authoritative run.

## Consequences

- Slice A can ship a truthful dry-run and editor without any host mutation
  authority, keeping the blast radius at one revertable commit.
- Placement/maximization remains event-driven through the existing KWin
  bridge when a later slice is authorized; `ContextReport` keeps its
  6-argument signature.
- A user's own desktop layout is never silently overwritten; conflicts are
  visible as drift before any Apply.
- The checkpoint file is a product runtime artifact under the user config
  root and is never part of the profile document, prompts, reports, or META.
