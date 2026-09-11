# ContextDeck M2 input safety — crash/hang/watchdog (not G4)

Procedure and S3/S5 device-free evidence for the input broker. This is **not**
real-keyboard acceptance. Do **not** start or enable
`contextdeck-broker.service`. Do **not** grab the G213. G4 still owns
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

`test_udev_policy` and `test_udev_verify` check (no G213, no `/dev/uinput`,
no live udev mutation):

- Guard/grant/uinput rule files keep the G213 event, hidraw, `/dev/port`, and
  `/dev/i2c-*` policy.
- The G213 event grant stays in `62-contextdeck-broker.rules` with no `OWNER`
  and no uinput `RUN`.
- The broker uinput ACL is only in `99-contextdeck-broker-uinput.rules`,
  matching the misc `uinput` node, adding `u:contextdeck-broker:rw` via
  `setfacl`, and not setting `OWNER`/`GROUP`/`MODE`.
- That `99-` filename sorts after `73-seat-late.rules` (the file that queues
  the `uaccess` builtin).
- `udevadm verify --resolve-names=never` accepts all three rule files.

`test_broker_production` checks (no G213, no `/dev/uinput`, no live udev
scan):

- Enumerator filtering by USB ancestry and interface `00`/`01`.
- Rejection of missing/duplicate interfaces, virtual bus, name prefix, wrong
  VID/PID, and nodes without USB ancestry. Interface `02` is ignored.
- Production objects start disarmed; enumerator/`open`/`createVirtual` are
  not called until explicit `arm()`.
- Missing devices fail closed without opening sources.
- Invalid/non-evdev paths fail closed, ungrab/cleanup, no leftover fds.
- `RealLifecycleSink` construction does not create uinput.
- `EvdevGrabber::create` refuses a null or fd-less handle.

`test_broker_ipc` checks (no G213, no `/dev/uinput`):

- Frame codec bounds and rejection of client-supplied UID tokens.
- `SO_PEERCRED` acceptance of the same-UID peer and rejection of a wrong UID,
  failed creds, and uid 0.
- Lease acquire, duplicate-lease rejection, explicit `ARM`/`DISARM`.
- Unauthorized `ARM`/`DISARM` without a lease or from a second peer.
- Disconnect, malformed length, lease expiry, and `shutdown` all disarm with
  ungrab-first teardown on `FakeGrabber`.
- Listen/accept on a temporary Unix socket with mode `0666` does not arm
  before authentication.

`contextdeck-broker watchdog-selftest` is the same coupling in the broker
binary. It does not open devices and does not use `NOTIFY_SOCKET`.

Static unit check (does not start the service):

```sh
systemd-analyze verify packaging/systemd/contextdeck-broker.service
```

Missing `/usr/bin/contextdeck-broker` may warn until the documented `/usr`
install (operations §9). `WatchdogSec=2` and `Type=notify` are the live
settings. After install, `systemd-analyze verify` of the installed unit
should match the repository file, including `RuntimeDirectoryMode=0755`.

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

## Production hang harness (G4 only)

Do **not** run this during implementation or against an unarmed/unstarted
broker. There is no hidden hang command in `contextdeck-broker`. Use an
external signal from a recovery path that does **not** need the G213
(second physical keyboard, SSH, or a TTY already open).

Preconditions: the broker unit is actually running because a later G4
session started it; an authenticated lease has armed it if the test is
about grabbed-keyboard recovery; `Restart=no` remains set.

1. From the recovery path, read the broker PID (`systemctl show -p MainPID
   --value contextdeck-broker.service`). Do not copy key names or event
   payloads into notes.
2. `kill -STOP <pid>`. The event-loop thread cannot feed `WATCHDOG=1`.
3. Wait for systemd watchdog (`WatchdogSec=2` plus one missed interval).
   `systemctl status` should move off `running`. If the process stays
   stopped because `SIGABRT` is pending, `kill -CONT <pid>` so the abort
   can be delivered, or `kill -KILL <pid>` from the same recovery path.
4. Confirm the unit stays down (`Restart=no`), is **not** enabled, and was
   not autostarted.
5. Confirm typing on a text field from the recovery keyboard. If the G213
   was grabbed, it must type again after descriptor close.
6. Do not `systemctl start` to “try again”. Do not change input-remapper.

Fail: the unit restarts and re-grabs; the G213 stays silent after death;
recovery requires rebooting as the first step.

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

Real grab, pass-through fidelity, input-remapper vs the virtual device, and
kernel ungrab-on-close on this host remain G4. Autostart (G8) stays
forbidden until those pass. Session IPC (S4) is covered by `test_broker_ipc`
and `docs/operations.md` §8; production install is `docs/operations.md` §9.
Do not start the broker unit to exercise either.

Never paste ordinary typed text, key names, scan codes, raw event
payloads, USB serials, or per-event timing into reports.
