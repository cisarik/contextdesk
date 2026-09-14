# ADR 0003 — Workspace assignment schema (schema 4)

Status: accepted for the M4 tree. Implementation-candidate; not accepted.

## Context

The M4 need is to configure named virtual-desktop sessions and assign
applications to desktops. Live KWin desktop identifiers are UUIDs that change
when desktops are removed and recreated, so they cannot be durable profile
identity. The existing document schema (3) stores lighting presets and typed
key assignments and rejects unknown semantic fields.

## Decision

- Add schema **4** to the existing profile document; do not create a second
  configuration file or database.
- New root key `workspace_sessions[]`: `id`, `display_name`, optional `rows`,
  optional `navigation_wrapping`, and `desktops[]` of `{ordinal, name}` with
  1-based contiguous ordinals, 1–32 entries.
- `preferences` gains `workspace_management_enabled` (default false),
  `title_fallback_enabled` (default false), and optional
  `active_workspace_session_id`.
- Per-application `workspace` object: `session_id`, `desktop_ordinal`,
  `launch`, `maximize`, optional `launch_desktop_file`, and optional
  `title_fallback` `{enabled, mode, pattern}`. A missing object means no
  launch and no placement.
- Strict validation: ids non-empty/unique/≤128 UTF-8 bytes/control-free;
  ordinal in 1–32 and ≤ the session's desktop count; desktop names non-empty
  and bounded; `launch_desktop_file` must look like a desktop id
  (`*.desktop` or reverse-DNS) with no shell metacharacters; title pattern
  ≤128 UTF-8 bytes and control-free; mode ∈ {`exact`, `contains`, `prefix`}.
  Unknown fields, wrong types/counts, dangling references, and future schemas
  are rejected.
- Preserving migration: schemas 1, 2, and 3 load in memory with
  `workspace_sessions` empty and no assignments; lighting, keys, matches,
  application order, and preferences are preserved exactly. New schema-4-only
  keys under a legacy schema are rejected as unknown semantic fields.
- Explicit save is the only schema-4 persistence boundary: reading never
  rewrites bytes and never creates a backup.
- Durable sessions store ordinals, not live UUIDs. Live UUIDs remain
  runtime-only; `MatchSpec` remains the only identity matcher and captions are
  never valid in `match`.

## Consequences

- After the first schema-4 save an older binary refuses the file; keep a
  COOPERATOR-owned pre-upgrade copy if a downgrade must survive later saves.
- The dry-run `WorkspacePlan` and the UI work from the same schema, so the
  editor, preview, and future Slice B mutator share one validated model.
- Titles stay opt-in and are never persisted or logged; only the user-authored
  pattern/mode/enabled flags are stored.
