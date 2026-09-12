# ADR 0001 — Broker suspend/resume via systemd-sleep

Status: accepted for the M2 tree. Live host suspend/resume acceptance remains
open.

## Context

An armed or even merely running broker is a `Type=notify` unit with
`WatchdogSec=2` and `Restart=no`. During system suspend the process is frozen
and cannot feed the watchdog, so systemd can abort an otherwise healthy
broker. Stopping the unit only after resume is too late: the grab may already
have been torn down by a watchdog abort, and an automatic restart would
re-grab without a fresh user `ARM`.

## Decision

Install a narrowly scoped executable at
`/usr/lib/systemd/system-sleep/contextdeck-broker` (source:
`packaging/systemd/contextdeck-sleep.sh`). systemd runs it as root only for
`pre`/`post` of `suspend`, `hibernate`, `hybrid-sleep`, and
`suspend-then-hibernate`.

- **pre, unit `ActiveState=active`:** write a root-owned mode `0600` marker
  under `/run/contextdeck-sleep` (not `RuntimeDirectory=contextdeck`, which
  vanishes when the unit stops), then `systemctl stop contextdeck-broker.service`
  and wait. Orderly SIGTERM keeps the existing ungrab-first teardown.
- **pre, not active:** do not create a marker. A valid marker from an earlier
  pre in the same cycle is kept (duplicate-pre). Malformed markers are
  removed.
- **post:** consume at most one valid marker. `systemctl start` the unit once
  only when the marker is valid and the unit is `inactive`. Start remains the
  existing disarmed path: no `LEASE`, no `ARM`, no event-node or uinput open.
- Failed stop removes the marker. Failed start is logged and not retried.
  Stale, symlink, wrong-mode, or wrong-boot-id markers are removed and never
  start the unit. `/run` is tmpfs, so reboot cannot replay a marker.

The broker binary does not call `systemctl`, a shell, or sleep D-Bus APIs.
`WatchdogSec=2`, `Restart=no`, `Type=notify`, and the absent `[Install]`
section stay unchanged. The session client reconnects with `STATUS` only;
explicit ARM is still a user action.

## Consequences

- Sleep no longer depends on disabling or inflating the watchdog, inhibiting
  user sleep, or adding a standing helper daemon.
- Post-resume presence of the broker means “it was active before this sleep”,
  not “the user still wants it armed”.
- Transparent re-ARM after resume is out of scope until a later acceptance
  covers re-enumeration, policy freshness, lease semantics, and physical
  recovery.
- Installing the hook is not autostart and not a grant to start the unit on a
  live seat. Host suspend/resume evidence remains a separate G4 procedure.
