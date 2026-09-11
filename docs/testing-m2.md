# ContextDeck M2 input safety — crash/hang/watchdog (not G4)

Procedure and S3 evidence for the input broker watchdog. This is **not**
real-keyboard acceptance. Do **not** start or enable
`contextdeck-broker.service`. Do **not** grab the G213. G4 / S5 still own
pass-through fidelity and kernel close-on-death on this host.

## What S3 proves without a keyboard

Automated (run from the repo with a clean `PATH` if CMake complains about
`CMAKE_ROOT`):

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/contextdeck-broker watchdog-selftest
```

`test_broker_watchdog` checks:

- `sd_notify` payloads are exactly `READY=1` and `WATCHDOG=1` (plus
  `STOPPING=1` on orderly exit).
- `$WATCHDOG_USEC=2000000` (2 s) yields a 1 s loop timeout.
- Watchdog pings happen only after wait returns and after ingest work
  returns.
- A wait that never returns never emits `WATCHDOG=1`.
- Ingest work that never returns never emits `WATCHDOG=1`.
- Sleeping after a ping does not produce extra pings (no detached timer).
- A child `abort()` terminates with `SIGABRT` (the default systemd
  watchdog signal) after a ready line and without a watchdog line.
- Fake-source ingest on the loop still does 1:1 forwarding with SYN pairing
  and leaves the ledger idle.

`contextdeck-broker watchdog-selftest` is the same coupling in the broker
binary. It does not open devices and does not use `NOTIFY_SOCKET`.

Static unit check (does not start the service):

```sh
systemd-analyze verify packaging/systemd/contextdeck-broker.service
```

Missing `/usr/bin/contextdeck-broker` may warn; that binary is not installed
in S3. `WatchdogSec=2` and `Type=notify` are the live settings.

## Watchdog semantics

| Condition | Watchdog feed | systemd effect |
|-----------|---------------|----------------|
| Idle loop, wait timeout | `WATCHDOG=1` after the wait | stays running |
| Wait returns, ingest runs and returns | `WATCHDOG=1` after ingest | stays running |
| Wait blocked (no timeout honored) | no further pings | abort after 2 s |
| Ingest/work blocked | no ping for that iteration | abort after 2 s |
| Process crash / `SIGKILL` | none | unit dead (`Restart=no`) |
| `SIGTERM` / `SIGINT` | loop stops, `STOPPING=1` | orderly exit |

The feed is the event-loop thread. A second thread that pings while the
loop is stuck is forbidden and is not present.

## Crash / hang recovery (later, when the unit actually runs)

Do **not** execute these against a live seat for S3. They are the G4
script once S4 exists and grabbing is authorized.

Preconditions for that later run: a recovery path that does **not** need
the G213 (second keyboard, SSH, or a TTY already open). Grabbed G213 keys,
including Ctrl+Alt+Fn and SysRq, must not be assumed to work.

1. **Orderly stop** — `systemctl stop contextdeck-broker.service`. Expect
   the process to leave, no autostart, typing restored if it had been
   grabbed.
2. **Crash** — induce `SIGABRT`/`SIGKILL` on a deliberately armed broker
   (G4 only). Expect the unit to stay down (`Restart=no`), no automatic
   re-grab, physical keyboard usable after descriptor close.
3. **Hang** — induce a blocked loop on an armed broker (G4 only). Expect
   systemd watchdog abort within about 2 s plus one missed ping interval,
   then the same keyboard outcome as crash.
4. **Held modifiers at death** — G4 must include a physical hold at crash
   time. S3 does not.
5. **TTY** — if the graphical seat is stuck, recover from SSH or another
   keyboard; then stop or kill as in `docs/operations.md` §7.

Fail: the unit restarts and re-grabs by itself; the G213 stays silent after
death; recovery requires rebooting as the first step.

## What this file does not cover

Session IPC and leases (S4). Real grab, pass-through fidelity,
input-remapper vs the virtual device, and kernel ungrab-on-close on this
host (S5 / G4). Autostart (G8) stays forbidden until those pass.

Never paste ordinary typed text, key names, scan codes, raw event
payloads, USB serials, or per-event timing into reports.
