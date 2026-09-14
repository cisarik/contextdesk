# ADR 0004 — Typed application launch (in-session, not autostart)

Status: accepted for the M4 tree. Launch itself is not implemented in Slice A.

## Context

M4 assigns applications to desktops and wants to launch an assigned
application when the user applies a session. Candidate routes observed on the
target platform were `systemd-run --user`, `kstart`, and direct shell/QProcess
of an `Exec=` line. The project's action model is typed; shell strings and
executable configuration are forbidden.

## Decision

- **Launch is typed.** The product launches a `.desktop` application id
  through `KIO::ApplicationLauncherJob` (`KF6::Service` + `KF6::KIOGui`) in a
  later separately authorized slice. `KF6KIO` is not linked in Slice A.
- **Rejected routes:** `systemd-run --user` for GUI launch (session/Wayland
  inheritance), `kstart` argv wrapping, and shell/`QProcess` execution of
  `Exec=` lines.
- **Trigger set is narrow:** only an explicit user `applyWorkspaceSession` and
  a `desktopCreated` event that occurs during that in-flight transaction (for
  profiles assigned to the new ordinal). Plasma login, session-app start,
  `currentChanged`, user-created desktops outside a transaction, and broker
  events are not triggers.
- **Duplicate skip:** do not launch when the bridge inventory already matches
  the profile. Debounce: one launch attempt per profile per transaction, with
  a bounded retry only on the matching in-transaction `desktopCreated`.
  Bounded failure class, no retry storm, no kill-on-revert.
- **Non-autostart posture:** no units, no `[Install]` sections, no login
  hooks, no `graphical-session.target` binding. M4 never launches because the
  session began; M5/G8 owns login lifecycle.
- **Slice A dry-runs only:** `WorkspacePlan` reports `would_launch`,
  `already_running`, `missing_desktop_file`, or `disabled` in memory. It never
  contacts KIO and never starts anything.

## Consequences

- Launch inherits the user's session, environment, and Wayland connection
  through the KDE launcher path rather than a transient unit.
- Desktop files must be provided as ids; a bare application name or a shell
  command is refused by schema validation.
- Without M5 autostart, launching is tied to an explicit in-session action,
  which keeps the failure surface observable and revertable.
