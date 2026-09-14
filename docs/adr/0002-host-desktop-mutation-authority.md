# ADR 0002 — Host desktop mutation authority and no `kwinrulesrc`

Status: accepted for the M4 tree. Slice B implements this decision in code; the
live desktop mutation itself remains gated by an explicit user Apply and has no
standing host authority from this repository. Running it against a real session
is a separate COOPERATOR grant.

## Context

M4 wants named desktop sessions, per-application assignment, in-session
launch, and window placement. Those operations change the user's live Plasma
desktop configuration: `createDesktop`, `setDesktopName`, `removeDesktop`,
`rows`, `navigationWrappingAround`, and `current`. They are destructive-ish,
user-visible, and hard to reverse when a user has arranged their own layout.

The repository also observed that persistent KWin window rules
(`kwinrulesrc`) can enforce placement without ContextDeck running.

## Decision

- **Observation and dry-run are the base.** The application may read
  `VirtualDesktopManager`, decode `rows`/`navigationWrappingAround`, compute a
  pure dry-run `WorkspacePlan`, and save named sessions and assignments through
  the explicit `Uložiť` action.
- **Live mutation is explicit and bounded (Slice B).** The mutation path exists
  in the M4 tree behind the user's Apply action: checkpoint first, then
  create/conditional rename/`rows`/wrapping, then the separately opted-in
  `current` switch and explicit extra removals. It still has no standing host
  authority: a real-session run needs its own COOPERATOR grant.
- **Default Apply is create + rename + rows/wrapping only.** `removeDesktop`
  is never part of the default preview or default Apply; it is explicit,
  separately confirmed user action with its own revert consequence.
- **Do not fight the user.** Live Plasma desktop edits never auto-rewrite a
  saved session. Drift is reported; Apply is explicit, and a changed live state
  refuses the stale preview.
- **Checkpoint and revert.** Before Apply, snapshot the observed state into a
  user-local `workspace-checkpoint.json` (never the profile document, never
  META, never logs). Revert removes exactly the UUIDs this Apply created and
  reapplies the checkpoint names/rows/wrapping; a missing checkpoint current
  UUID is skipped as a bounded residual. Removal-revert keeps ordinal-based
  assignment rebinding because no UUID is stored in the profile document.
- **No `kwinrulesrc` writes.** The product keeps `kwinrulesrc` out of scope:
  persistent rules would fight user rules, duplicate an external owner of
  window placement, and are unnecessary while ContextDeck only places windows
  during its own authoritative run.

## Consequences

- The mutation path keeps the blast radius at one revertable commit and one
  user-visible transaction with a checkpoint.
- Placement/maximization remains event-driven through the existing KWin
  bridge; `ContextReport` keeps its 6-argument input signature.
- A user's own desktop layout is never silently overwritten; conflicts are
  visible as drift before any Apply, and stale previews are refused.
- The checkpoint file is a product runtime artifact under the user config
  root and is never part of the profile document, prompts, reports, or META.
