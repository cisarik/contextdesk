# ContextDeck M2 input safety — crash/hang/watchdog (full G4 open)

Procedure and S3 device-free evidence for the input broker. Device-free S3
tests do **not** require a second keyboard or SSH. Do **not** start or enable
`contextdeck-broker.service` from this file's S3 commands. Do **not** grab
the G213 except under a separately authorized live G4 prompt.

One named physical slice is recorded as accepted on candidate `cb72ae0`
(explicit ARM, sampled G213 pass-through, matching-invocation cutoff,
post-death typing; META Worker 16). A second named slice on the same runtime
candidate / docs descendant `9a89095` recorded armed watchdog abort with a
held modifier (META Worker 19). **Full G4 remains open:** production/autostart readiness (G8/M5),
hibernate/hybrid-sleep, and general input-remapper coexistence. Named slices
from Workers 16, 19, 22, 23, and 24 are recorded as accepted and are not
rerun.

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

- Guard/grant/uinput/ACL-guard rule files keep the G213 event, hidraw,
  `/dev/port`, and `/dev/i2c-*` policy.
- The G213 event grant stays in `62-contextdeck-broker.rules` with no `OWNER`
  and no uinput `RUN`.
- The broker uinput ACL is only in `99-contextdeck-broker-uinput.rules`,
  matching the misc `uinput` node, adding `u:contextdeck-broker:rw` via
  `setfacl`, and not setting `OWNER`/`GROUP`/`MODE`.
- `99-contextdeck-input-acl-guard.rules` matches only G213 `event*` by USB
  ancestry, `/dev/port`, and `/dev/i2c-*`; runs on add/change with
  `/usr/bin/setfacl -b %N`; does not match hidraw or `/dev/uinput`; and does
  not set `OWNER`/`GROUP`/`MODE` or a session user.
- Both `99-` filenames sort after `73-seat-late.rules` (the file that queues
  the `uaccess` builtin).
- `udevadm verify --resolve-names=never` accepts all four rule files.

`test_broker_production` checks (no G213, no `/dev/uinput`, no live udev
scan):

- Enumerator filtering by USB ancestry and interface `00`/`01`.
- Rejection of missing/duplicate interfaces, virtual bus, name prefix, wrong
  VID/PID, and nodes without USB ancestry. Interface `02` is ignored.
- Production objects start disarmed; enumerator/`open`/`createVirtual` are
  not called until explicit `arm()`.
- Missing or invalid paths fail closed **without** creating a virtual device
  and without grabbing.
- Measured capability union: `EV_KEY` union, `EV_LED` from if00 only,
  `EV_MSC`, never `EV_REP`. Create happens after measure, grab after create.
- `RealLifecycleSink` construction does not create uinput.
- `EvdevGrabber::create` refuses a null or fd-less handle.
- LED return writes go to if00 only; a runtime LED write failure is
  `led-write-failed` and does not disarm.

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
- Logind: direct session path, exactly-one fallback, and rejection of zero
  candidates, ambiguity, inactive, remote, unseated, non-graphical, UID
  mismatch, and non-session lookup errors that are not fallback-eligible.

`test_broker_forwarding` / `test_broker_acquisition` add fail-closed
`sink-write-failed` (disarm, ungrab-first, ledger clear, best-effort cleanup
writes) and LED-setup abort before grab.

`test_trial_cutoff` checks the helper usage/timeout range, missing-unit
refusal, production unit pins (`TimeoutStopSec=5`, `TimeoutAbortSec=5`, no
`RuntimeMaxSec`), and `systemd-analyze verify` on the unit file. It does not
start the broker.

`test_sleep_hook` checks the systemd-sleep script with a fake `systemctl`
confined to a temporary directory (no host `systemctl`, no `/run`, no
suspend): non-sleep modes are no-ops; inactive/failed units write no marker;
an active unit is stopped and started once, always disarmed / no lease;
failed pre-stop never starts; post-start failure is not retried; duplicate
phases, stale/malformed/wrong-mode markers, concurrent state change, and
reboot-like marker absence fail closed. This is **not** live suspend
evidence.

`test_broker_ipc_client` checks that `BrokerIpcClient` start, `STATUS` probe,
and reconnect after broker loss never emit `LEASE` or `ARM` without an
explicit `arm()` call.

Device-free cutoff rehearsal (systemd `--user` only, unique nonce, no broker
or device names) is `tests/unit/rehearse_trial_cutoff.sh`. Cases: normal
completion before expiry; exited fixture; hung fixture; invoking-shell loss;
timer/setup failure; stale invocation guard. Every path removes fixtures.

`contextdeck-broker watchdog-selftest` is the same coupling in the broker
binary. It does not open devices and does not use `NOTIFY_SOCKET`.

Static unit check (does not start the service):

```sh
systemd-analyze verify packaging/systemd/contextdeck-broker.service
bash -n packaging/systemd/contextdeck-trial-cutoff.sh
bash -n packaging/systemd/contextdeck-sleep.sh
tests/unit/test_trial_cutoff.sh .
tests/unit/test_sleep_hook.sh .
```

Missing `/usr/bin/contextdeck-broker` may warn until the documented `/usr`
install (operations §9). `WatchdogSec=2`, `TimeoutStopSec=5`,
`TimeoutAbortSec=5`, and `Type=notify` are the live settings. After install,
`systemd-analyze verify` of the installed unit should match the repository
file, including `RuntimeDirectoryMode=0755`. Do not treat a cutoff kill as a
watchdog PASS.

## Watchdog semantics

| Condition | Watchdog feed | systemd effect |
|-----------|---------------|----------------|
| Idle loop, wait timeout | `WATCHDOG=1` after the wait | stays running |
| Wait returns, ingest runs and returns | `WATCHDOG=1` after ingest | stays running |
| Wait blocked (no timeout honored) | no further pings | abort after 2 s |
| Ingest/work blocked | no ping for that iteration | abort after 2 s |
| Process crash / `SIGKILL` | none | unit dead (`Restart=no`) |
| `SIGTERM` / `SIGINT` | loop stops, `STOPPING=1` | orderly exit |
| Invocation-bound cutoff expiry | none from the broker; PID1 `SIGKILL` of the matching invocation | controlled recovery, **not** a watchdog PASS |

The feed is the event-loop thread. A second thread that pings while the
loop is stuck is forbidden and is not present.

The trial cutoff is a separate PID1 timer (`OnActiveSec=30s`,
`AccuracySec=1us`). It is armed per invocation after the broker is running
and before `ARM`. Cancel it after a normal disarm. Device-free cutoff
rehearsal does not require a second keyboard or SSH. Any live G4 or
physical-grab procedure does: a second physical keyboard or SSH from another
device must be independently verified before the broker is started or ARM is
attempted and must remain available through the trial. Either route is
sufficient. The cutoff helper is never a substitute for that path.

## Production hang harness (G4 only)

Do **not** run this during implementation or against an unarmed/unstarted
broker. There is no hidden hang command in `contextdeck-broker`. Use an
external signal from a recovery path that does **not** need the G213.

Preconditions: an independently verified second physical keyboard or SSH
from another device is available (either is sufficient; the cutoff helper
is not a substitute) and remains available through the trial; the broker
unit is actually running because a later G4 session started it; an
authenticated lease has armed it if the test is about grabbed-keyboard
recovery; `Restart=no` remains set.

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

## Live suspend / resume (G4, still open)

Do **not** run `systemctl suspend`, `systemctl hibernate`, or any power
action from this file. Device-free evidence is `test_sleep_hook` plus
`test_broker_ipc_client`. Those tests do not prove host sleep.

When a later authorized G4 session runs live suspend, expect:

- An independently verified second keyboard or SSH before any broker start or
  ARM, kept through the trial.
- If the broker was active, the sleep hook stops it on `pre` so
  `WatchdogSec=2` does not abort a frozen process.
- After resume, the unit starts once only if it was active before that sleep,
  and it starts **disarmed** (no lease, no grab). Inactive/failed brokers stay
  down.
- The session app reconnects with `STATUS` only. Re-ARM is an explicit user
  action, not automatic.
- Fail: watchdog abort during freeze because the hook did not stop an active
  unit; silent start of an inactive broker; automatic re-ARM; a restart loop.

Until that live run exists, host suspend/resume remains an open G4 claim.

## Crash / hang recovery (later, when the unit actually runs)

Do **not** execute these against a live seat for S3. They are remaining G4
scripts once grabbing is authorized.

Preconditions for that later run: an independently verified second physical
keyboard or SSH from another device, available before start or ARM and
through the trial. Either route is sufficient. An already-open TTY is extra
recovery only if it does not depend on the G213; it is not a third
substitute for that precondition. The cutoff helper is not a substitute.
Grabbed G213 keys, including Ctrl+Alt+Fn and SysRq, must not be assumed to
work.

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

The named physical slices covered sampled grab, pass-through, matching-invocation
cutoff death, typing after descriptor close, and armed watchdog abort with a
held modifier, one live suspend/resume cycle, LED return and all-control
fidelity, and one bounded input-remapper mapping. Remaining G4 claims:
production/autostart readiness (G8/M5), hibernate/hybrid-sleep, and general
input-remapper coexistence. Autostart (G8) stays forbidden until those pass. Session IPC (S4) is covered by
`test_broker_ipc` / `test_broker_ipc_client` and `docs/operations.md` §8;
production install is `docs/operations.md` §9. The sleep hook is covered by
`test_sleep_hook`; do not invoke `systemctl suspend` from this file. Do not
start the broker unit to exercise S3.

Never paste ordinary typed text, key names, scan codes, raw event
payloads, USB serials, or per-event timing into reports.
