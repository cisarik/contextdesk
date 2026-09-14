# ContextDeck operations (M1)

Host enablement and run/stop for the context + lighting vertical. Commands
marked **COOPERATOR-run** require the human at the keyboard: they install
packages, reload udev, or change compositor state. Workers must not run them.

This file was checked against the target workstation's package metadata and
live D-Bus APIs. OpenRGB itself was **not** installed at documentation time;
file lists come from the Arch `extra` package `openrgb 1.0rc3-3`. After
install, re-read the files on disk if anything disagrees.

## Build (unprivileged)

From a clone of this repository, with the system Qt6/KF6 toolchain:

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The binary is `build/contextdeck`. If CMake reports a missing `CMAKE_ROOT`
(usually a poisoned environment), rerun configure with a clean `PATH`:

```sh
env -i HOME="$HOME" PATH=/usr/bin:/bin:/usr/sbin cmake -S . -B build -G Ninja
```

Do not install `extra-cmake-modules`. Do not use a KF6 umbrella
`find_package`. The project already uses per-component lookups.

## 1. Install OpenRGB — COOPERATOR-run

`openrgb` is not required to *build* ContextDeck. It is required to light the
keyboard.

Available packages (this host):

| Repo | Package | Version |
|------|---------|---------|
| `cachyos-extra-v3` | `openrgb` | `1.0rc3-3.1` |
| `extra` | `openrgb` | `1.0rc3-3` |

Either is protocol 5 / OpenRGB 1.0rc3. Prefer the repo you already trust for
the rest of the system.

```sh
# COOPERATOR-run (privileged)
sudo pacman -S openrgb
```

The `extra` package installs at least:

| Path | Role |
|------|------|
| `/usr/bin/openrgb` | SDK server and GUI |
| `/usr/lib/udev/rules.d/60-openrgb.rules` | hidraw `uaccess` tags for supported VID/PID, including G213 `046d:c336` |
| `/usr/lib/systemd/system/openrgb.service` | **system** unit — do **not** enable it for M1 |
| `/usr/lib/modules-load.d/openrgb.conf` | i2c-related; not required for the G213 HID path |
| `/usr/lib/tmpfiles.d/openrgb.conf` | inspect after install; not used by ContextDeck |

After install, inspect those files (`pacman -Ql openrgb`, `systemctl cat
openrgb.service`) before changing anything.

### Make G213 hidraw reachable — COOPERATOR-run

The packaged udev rules tag matching hidraw nodes with `uaccess` so the active
seat user can open them. They do **not** put the session user in group
`input`. After installing the rules:

```sh
# COOPERATOR-run (privileged)
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Unplug and replug the G213 if nodes stay `root:root 0600`. Confirm the RGB
hidraw is reachable by the session user (`ls -l /dev/hidraw*`, `getfacl`)
**without** copying node numbers into public notes.

Do **not** chmod `0666`, do **not** add the GUI account to `input`, and do
**not** write extra udev rules in M1. Event nodes for the input broker are a
later whole.

## 2. Start the OpenRGB SDK server on loopback — COOPERATOR-run

Do **not** `systemctl enable --now openrgb`. The packaged unit is a system
service; M1 needs a **user** process bound to loopback. Default
`--server-host` is `0.0.0.0` (all interfaces). Always set the host:

```sh
# COOPERATOR-run (session user, after udev is correct)
openrgb --server --server-host 127.0.0.1 --server-port 6742
```

Leave that terminal open (or start it minimized with `--startminimized` as
well if you want the GUI). Confirm it listens only on loopback
(`ss -ltnp | grep 6742`). ContextDeck connects to `127.0.0.1:6742` as client
name `ContextDeck` and will **reject** SDK protocol versions newer than 5.

**Do not use OpenRGB's GUI or CLI to "fix" a dark keyboard while ContextDeck
owns the device.** In particular do **not** run `openrgb --list-devices`:
controller initialization selects Direct and can leave all five zones dark.
That is the defect this product exists to avoid. Quit ContextDeck first if
you need to talk to OpenRGB yourself.

If ContextDeck is **not** running and the keyboard is stuck in Direct/dark,
restore Wave from the OpenRGB GUI by selecting the G213 and the **Wave** mode
(not Direct), then close that GUI. Last resort: unplug and replug the
keyboard so firmware can show its own effect, then start the SDK server
again without listing devices.

Stop: Ctrl+C in that terminal, or close the OpenRGB process. Do not kill
unrelated RGB software.

## 3. Load the KWin bridge — COOPERATOR-run

Do **not** have a Worker load the script. On Plasma 6.7.5,
`org.kde.KWin /Scripting` exposes `loadScript(path, pluginName)`, `start()`,
`isScriptLoaded`, and `unloadScript`.

Use an **absolute** path to `contents/code/main.js` in this checkout (do not
commit that path):

```sh
# COOPERATOR-run (session user)
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript \
  "$PWD/kwin/contextdeck-bridge/contents/code/main.js" \
  "contextdeck-bridge"
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.isScriptLoaded \
  "contextdeck-bridge"
```

`loadScript` returns a script id (integer). `isScriptLoaded` must print
`true`. After pulling a ContextDeck change that edits
`kwin/contextdeck-bridge/contents/code/main.js`, **unload and load again**
so heartbeat `ContextReport` is actually running. Persistent install
(optional):

```sh
# COOPERATOR-run
kpackagetool6 -t KWin/Script --install kwin/contextdeck-bridge
```

Unload:

```sh
# COOPERATOR-run
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript \
  "contextdeck-bridge"
# if installed as a package:
kpackagetool6 -t KWin/Script --remove contextdeck-bridge
```

## 4. Run ContextDeck (unprivileged)

```sh
./build/contextdeck
```

Expect a tray icon (`input-keyboard`), a session-bus name
`io.github.cisarik.ContextDeck`, and log lines on stderr:

- `session bus name registered: "io.github.cisarik.ContextDeck"`
- `status notifier started`
- lighting `ready` once OpenRGB is up and a G213 is enumerated **without**
  changing the keyboard if no profile has lighting intent; otherwise
  `lighting disabled: …` while the rest of the app stays alive. A cold start
  with no `profiles.json` must leave the firmware effect running.

Closing the settings window must not quit the process. Quit from the tray.

Optional user unit (no autostart — there is no `[Install]` section):

```sh
# COOPERATOR-run if you want the unit; adjust ExecStart to the built binary
mkdir -p ~/.config/systemd/user
cp packaging/systemd/contextdeck-session.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user start contextdeck-session.service
```

Do **not** `enable` it. `ExecStart` in the shipped file is `/usr/bin/contextdeck`;
point a drop-in at `build/contextdeck` until there is an install prefix.

## 5. Stop and remove

1. Quit ContextDeck (tray Quit, or `kill -TERM` the process, or
   `systemctl --user stop contextdeck-session.service`).
2. Unload the KWin script (section 3).
3. Stop OpenRGB (section 2).
4. Optional package removal — **COOPERATOR-run**: `sudo pacman -R openrgb`.
   That removes the udev rules and the system unit. User
   `~/.config/contextdeck/profiles.json` is **not** owned by the package and
   is left in place.

Do not `systemctl` mask/unmask sleep targets. Do not write `/sys/power/state`.

## 6. G3 host policy (input broker) — COOPERATOR-run

Authors the system identity, udev guard/grant, and the **system** unit
`contextdeck-broker.service`. **Do not enable. Do not start.** The broker
stays disarmed until an authenticated session lease arms it (S4). Real
pass-through IRL is S5. This install is meant to keep session `uaccess` off
G213 **input** nodes, `/dev/port`, and `/dev/i2c-*`. HID RGB (`hidraw`) must
keep working. The broker's `/dev/uinput` grant is an **additive named-user
ACL** from `99-contextdeck-broker-uinput.rules`, queued after the seat
`uaccess` builtin so it is not overwritten.

`61-contextdeck-input-guard.rules` only removes the `uaccess` **tag**. That
does not delete an already materialized POSIX ACL, so a later add/change
re-probe can restore a session ACL even when `CURRENT_TAGS` lacks `uaccess`.
`99-contextdeck-input-acl-guard.rules` therefore runs on add/change after
`73-seat-late.rules` and uses `/usr/bin/setfacl -b` on G213 `event*` nodes,
`/dev/port`, and `/dev/i2c-*` only. That strips extended ACL entries and
leaves the base owner, group, and mode unchanged. It does not match hidraw
or `/dev/uinput`, and it does not set `OWNER`, `GROUP`, `MODE`, or a session
user. Installing these files is not by itself proof that a given host is
secure; verify the table below after install, and treat G3 as a separate
readback/re-audit claim.

If these udev files are already installed from an earlier G3 run, re-run the
udev `install` / `reload-rules` / `trigger` commands below so the late uinput
rule and the late input ACL guard are present. The documented triggers are
not a substitute for that late `setfacl -b` rule.

Run every command from the repository root. Adjust nothing to a private
home path in public notes.

### Install

```sh
# COOPERATOR-run (privileged). No enable, no start.
sudo install -m 0644 packaging/sysusers.d/contextdeck-broker.conf \
  /usr/lib/sysusers.d/contextdeck-broker.conf
sudo systemd-sysusers contextdeck-broker.conf

sudo install -m 0644 packaging/udev/61-contextdeck-input-guard.rules \
  /etc/udev/rules.d/61-contextdeck-input-guard.rules
sudo install -m 0644 packaging/udev/62-contextdeck-broker.rules \
  /etc/udev/rules.d/62-contextdeck-broker.rules
sudo install -m 0644 packaging/udev/99-contextdeck-broker-uinput.rules \
  /etc/udev/rules.d/99-contextdeck-broker-uinput.rules
sudo install -m 0644 packaging/udev/99-contextdeck-input-acl-guard.rules \
  /etc/udev/rules.d/99-contextdeck-input-acl-guard.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=input
sudo udevadm trigger --subsystem-match=i2c-dev
sudo udevadm trigger --sysname-match=port
sudo udevadm trigger --action=add --sysname-match=uinput --settle

sudo install -m 0644 packaging/systemd/contextdeck-broker.service \
  /etc/systemd/system/contextdeck-broker.service
sudo install -m 0755 packaging/systemd/contextdeck-sleep.sh \
  /usr/lib/systemd/system-sleep/contextdeck-broker
sudo systemctl daemon-reload
```

Do **not** `systemctl enable contextdeck-broker`. Do **not**
`systemctl start contextdeck-broker`. The unit has no `[Install]` section.
The binary now sends `READY=1` and feeds `WATCHDOG=1` from its idle event
loop (S3) and accepts an authenticated session lease on
`/run/contextdeck/broker.sock` (S4). The unit is `Type=notify` with
`NotifyAccess=main`, `WatchdogSec=2`, `TimeoutStopSec=5`, `TimeoutAbortSec=5`,
`Restart=no`, `LimitCORE=0`, and `RuntimeDirectoryMode=0755`. It has no
`[Install]` section and no `RuntimeMaxSec`. Production ARM (S5) enumerates the
G213 by USB ancestry and may construct `RealSink` / `EvdevGrabber` only after
an explicit authenticated `ARM`. Do **not** enable or start the unit from this
G3 section. IRL pass-through remains a G4 claim: one named slice is
recorded as accepted; full G4 is not closed.

`ExecStart` is `/usr/bin/contextdeck-broker`. Install that binary with the
documented `/usr` prefix (section 9) before any later G4 start. Do not start
the unit from a home-directory build (`ProtectHome=yes` would block it).

Do **not** add the session user to group `input`. Do **not** stop, disable,
or reconfigure input-remapper.

### Verify

Resolve G213 event nodes from udev by USB ancestry `046d:c336` and
interface number. Do not treat remembered `eventN` as identity, and do not
copy node numbers into public notes.

| Check | Expected |
|-------|----------|
| `getfacl` on each G213 **event** node | group `contextdeck-broker`, mode `0660`, **no** session-user ACL |
| `getfacl` on each G213 **hidraw** node | session-user ACL **still present** (OpenRGB lighting) |
| `getfacl /dev/port` and `/dev/i2c-*` | **no** session-user ACL; not session-readable |
| `getfacl /dev/uinput` | existing group and mode **unchanged**; session-user ACL **still present**; `user:contextdeck-broker:rw-` present |
| `sudo -u contextdeck-broker test -r /dev/uinput && sudo -u contextdeck-broker test -w /dev/uinput` | both succeed (`test` uses `access(2)`; it does not inject events) |
| input-remapper | still enabled/active; G213 preset untouched |
| `systemctl is-enabled contextdeck-broker` | not enabled (expected fail / `not-found` / `disabled`) |
| `/usr/lib/systemd/system-sleep/contextdeck-broker` | executable sleep hook; installing it is not a broker start |

If hidraw lost the session ACL, rollback immediately — lighting would break
and this install over-reached. If `/dev/uinput` lost the session-user ACL or
never gained `user:contextdeck-broker:rw-`, rollback the uinput rule and
re-check before any broker start. If G213 event nodes, `/dev/port`, or
`/dev/i2c-*` still show a session-user ACL after reload and trigger, the
late ACL guard did not take effect; do not treat G3 as restored.

### Rollback

The unit was never enabled, so there is nothing to disable. After rollback,
packaged OpenRGB may restore session `uaccess` on G213 event nodes,
`/dev/port`, and i2c — that re-opens the measured hole.

```sh
# COOPERATOR-run (privileged)
sudo rm -f /etc/udev/rules.d/61-contextdeck-input-guard.rules \
           /etc/udev/rules.d/62-contextdeck-broker.rules \
           /etc/udev/rules.d/99-contextdeck-broker-uinput.rules \
           /etc/udev/rules.d/99-contextdeck-input-acl-guard.rules \
           /etc/systemd/system/contextdeck-broker.service \
           /usr/lib/systemd/system-sleep/contextdeck-broker \
           /usr/lib/sysusers.d/contextdeck-broker.conf
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=input
sudo udevadm trigger --subsystem-match=i2c-dev
sudo udevadm trigger --sysname-match=port
sudo udevadm trigger --action=add --sysname-match=uinput --settle
sudo systemctl daemon-reload
sudo userdel contextdeck-broker
sudo groupdel contextdeck-broker
```

`userdel` may already remove the matching group; ignore `groupdel` if the
group is gone. Unplug/replug the G213 if event-node ownership stays stale.

## 7. Crash, hang, watchdog, cutoff, and TTY recovery

This is the documented recovery path for the input broker. Do **not** start or
enable `contextdeck-broker.service` from this section. Device-free S3 evidence
does not require a second keyboard or SSH. Named physical slices on candidate
`cb72ae0` / docs descendant `9a89095` are recorded as accepted: explicit ARM,
sampled pass-through, matching-invocation cutoff, typing after descriptor
close (Worker 16); armed watchdog abort with a held modifier (Worker 19).
Full G4 remains open (LED return, all-control fidelity, live host
suspend/resume, production/autostart).

### Independent recovery path (live G4)

Any live G4 or physical-grab procedure requires an independently verified
second physical keyboard or SSH from another device **before** the broker is
started or ARM is attempted. That path must remain available through the
trial. Either route is sufficient; both are not required. The
invocation-bound cutoff helper is supplemental recovery evidence and never a
substitute for that path. Device-free S3 procedures (CTest, watchdog-selftest,
`rehearse_trial_cutoff.sh`) may remain keyboard-free.

### What the broker does

- `Type=notify`, `NotifyAccess=main`, `WatchdogSec=2`, `TimeoutStopSec=5`,
  `TimeoutAbortSec=5`. `Restart=no` (a crash must not re-grab). No
  `RuntimeMaxSec`. Do not send `EXTEND_TIMEOUT_USEC`.
- `READY=1` is sent once the single-thread event loop is running.
- `WATCHDOG=1` is sent from **that same thread** after `epoll_wait` (or the
  test wait) returns, including idle timeout, and after that iteration's
  ingest work returns. Half of 2 s is a 1 s wait. Watchdog pings do not
  extend stop or abort timeouts.
- There is no helper thread and no detached in-process timer. If wait or
  ingest blocks, watchdog notifications stop and systemd can detect the hang.
- Orderly stop (SIGTERM/SIGINT via signalfd) leaves the loop and sends
  `STOPPING=1`. Crash / watchdog abort / SIGKILL do not run userspace
  cleanup; recovery relies on kernel close of any held descriptors.
- Orderly disarm ungrabs physical sources first, then best-effort synthetic
  releases, then destroys the virtual device.

### Invocation-bound trial cutoff

The 2 s watchdog recovers a stuck event loop. It does not bound a live loop
that has stopped forwarding. Before any authenticated `ARM` on a live grab,
arm an invocation-bound transient timer with
`packaging/systemd/contextdeck-trial-cutoff.sh`. Default window is 30 seconds
(allowed 20–45). The timer is PID1-owned (`Persistent=no`,
`AccuracySec=1us`, `RandomizedDelaySec=0`) and re-checks the exact
`InvocationID` immediately before `systemctl kill --kill-whom=main
--signal=SIGKILL` on that unit. It never uses `pkill`, `killall`, a guessed
PID, or restart.

Exact order for a later authorized live trial (do not run from this section):

1. Independently verify a second physical keyboard or SSH from another
   device and keep that path available. Failure here means **do not start**
   and **do not ARM**.
2. Start the broker manually and confirm it is disarmed (`STATUS` /
   `armed=0`). Do not enable the unit.
3. Read and record the current `InvocationID`
   (`systemctl show -p InvocationID --value contextdeck-broker.service`).
4. Arm the transient cutoff with that exact identity and verify the timer is
   loaded with `OnActiveSec=30s` (or the chosen 20–45 value) and
   `AccuracySec=1us`. Setup failure means **do not ARM**.
5. Only after that may an explicit authenticated `ARM` be attempted.
6. On normal completion, `DISARM`, cancel the cutoff, then stop the broker.
7. If the broker hangs or the invoking shell disappears, the watchdog and/or
   cutoff kill only the matching invocation.
8. If timer setup, identity verification, or any prerequisite fails, do not
   ARM.
9. Clean up the timer and all temporary state.
10. Final state must be statically and operationally verifiable: broker
    inactive or safely disarmed, no grabbed physical device, no stale virtual
    device, no active trial timer, and unchanged input-remapper state.

A cutoff kill is a controlled recovery event. Do **not** report it as a
watchdog PASS. Distinguish:

| Event | What it proves | What it does not prove |
|-------|----------------|------------------------|
| Watchdog expiry (`WatchdogSec=2`) | the event-loop thread stopped feeding | physical typing; cutoff path |
| Invocation-bound cutoff expiry | PID1 killed the matching invocation after 30 s | that the watchdog fired; physical typing |
| Physical usability after descriptor close | named-slice typing after matching-invocation cutoff death | LED return; all-control fidelity; live suspend |

The cutoff helper is supplemental recovery evidence. It never replaces the
independent second-keyboard or SSH path required for live G4. Limitations:
PID1/user-manager must be running; a kernel hang, machine power loss, or
session teardown is outside this helper; `TimeoutStopSec`/`TimeoutAbortSec`
bound systemd's stop job, not the 30 s trial window.

### Hang (watchdog)

A hung event loop stops feeding `WATCHDOG=1`. systemd then aborts the
process (default watchdog signal is `SIGABRT`) after `WatchdogSec=2`.
Because `Restart=no`, the unit stays dead. If the broker had been armed,
descriptor close is what must return the physical keyboard. Named-slice
cutoff death showed G213 typing after descriptor close; Worker 19 recorded
armed watchdog abort with a held modifier. LED return, all-control fidelity,
and live host suspend/resume still need G4.

The production hang procedure is external: `SIGSTOP` the broker PID from a
recovery path, observe that watchdog feeding stops, then let systemd abort
(or `SIGCONT`/`SIGKILL` if a stopped process holds the abort pending). There
is no hidden production hang command in the binary. Full steps:
`docs/testing-m2.md`. Do **not** run that procedure except as G4.

S3 proves the feed/hang coupling in CTest (`test_broker_watchdog`) and
`contextdeck-broker watchdog-selftest`. It does **not** start the system
unit. Device-free cutoff rehearsal is `tests/unit/rehearse_trial_cutoff.sh`
on systemd `--user` fixtures that do not name the broker.

### Crash

`SIGTERM` is the orderly stop. `SIGKILL` and `SIGABRT` (watchdog) skip
userspace teardown. The planned armed teardown order remains ungrab
physical first, then balanced synthetic releases, then destroy the virtual
device. A dead process cannot run that sequence. Named-slice cutoff SIGKILL
showed typing after descriptor close; Worker 19 recorded armed watchdog abort
with a held modifier. LED return, all-control fidelity, and live host
suspend/resume still need G4.

Do not enable autostart until full G4 evidence exists (handout §29). The
named slice does not authorize autostart.

### TTY / second-seat recovery

Do not assume Ctrl+Alt+Fn or SysRq still work **from the G213** while it is
grabbed. If the broker is hung and the G213 is silent, recover from a
path that does not need that keyboard:

1. Another physical keyboard on the same seat, or
2. SSH / another machine.

An already-open TTY is extra recovery only if it does not depend on the
G213. It is not a third substitute for the live-G4 precondition.

For a live G4 grab a second physical keyboard or SSH must already have been
verified before start or ARM (see Independent recovery path above). The
cutoff helper does not make that optional. Device-free S3 does not start the
unit. Then, only when recovering a **running** broker (not during S3):

```sh
# Recovery — COOPERATOR-run, and only if the unit was started later.
# Do not run this to "try S3".
systemctl stop contextdeck-broker.service
# if stop cannot complete because the unit was never started, that is success
```

If systemd already watchdog-aborted or cutoff-killed the process,
`systemctl status` should show inactive/failed and the keyboard should type
again after descriptor close. Do not `systemctl enable`. Do not
`systemctl start` to "test recovery". Do not change input-remapper.

If stop is not enough, `kill -TERM` then `kill -KILL` the `contextdeck-broker`
PID from the recovery path. Unplug/replug the G213 only as a last resort.

### Suspend / resume

System sleep is handled by `packaging/systemd/contextdeck-sleep.sh`, installed
as `/usr/lib/systemd/system-sleep/contextdeck-broker`. It is not autostart
and it does not arm. Installing the hook only means: if the broker unit is
**active** when the machine enters `suspend` / `hibernate` / `hybrid-sleep` /
`suspend-then-hibernate`, stop it before the freeze; after resume, start it
once **only if** that same cycle recorded a valid active-before-sleep marker
and the unit is `inactive`. The restarted broker is always disarmed: no
lease, no virtual device, no grab. The session app may reconnect and probe
`STATUS`; it must not restore ARM. The user can click the existing Arm
action afterwards.

The marker is root-owned mode `0600` under `/run/contextdeck-sleep`, outside
the broker `RuntimeDirectory`. Reboot clears `/run`, so a marker cannot start
the unit after boot. A failed pre-sleep stop or post-resume start is a
bounded journal line (`class=error reason=stop-failed|start-failed`) and is
not retried. `WatchdogSec=2` and `Restart=no` stay as they are; do not mask
sleep targets or inhibit user sleep to work around the watchdog.

This section does **not** authorize a live suspend. Host suspend/resume
acceptance remains open G4. Device-free coverage is `test_sleep_hook` (fake
`systemctl`, no `/run`, no `systemctl suspend`).

Quitting the session app, closing the broker socket, or letting the 6 s
lease expire also disarms (ungrab-first) without needing `systemctl stop`.
That is the S4 recovery path when the seat is still usable. If the G213 is
silent, still use the already-verified second keyboard or SSH as above —
do not assume the grabbed keyboard can send the quit chord.

After recovery, confirm typing on a text field. Do not paste key names,
scan codes, or raw event dumps into notes.

More procedure detail: `docs/testing-m2.md`.

## 8. Session IPC (S4)

The broker listens on `/run/contextdeck/broker.sock`. Override with
`CONTEXTDECK_BROKER_SOCKET` only in tests. The session app probes `STATUS` at
startup and after a reconnect and does **not** auto-arm. A broker stop or
sleep-hook restart invalidates any prior lease/ARM intent. Arming is a
deliberate tray or Diagnostics action (`Arm G213 pass-through…`) that sends
authenticated `LEASE` then `ARM`. `DISARM` and `RELEASE` are equally explicit.
Do **not** start the system unit to try this.

### Protocol

Length-prefixed frames: 16-bit little-endian payload size, then 1–256 bytes of
ASCII. No NULs. Client-supplied UID/GID fields do not exist and extra tokens
are malformed.

| Command | Who | Effect |
|---------|-----|--------|
| `STATUS` | any authenticated peer | `OK STATUS lease=none\|self\|other armed=0\|1 ttl=<ms>` |
| `LEASE` `[ms]` | one peer | acquire or renew the single lease (does not arm) |
| `HEARTBEAT` `[ms]` | lease holder | renew TTL (default 6000, clamp 1000–30000) |
| `ARM` `[ms]` | lease holder | `Acquisition::arm()` |
| `DISARM` | lease holder | `Acquisition::disarm()`, lease kept |
| `RELEASE` | lease holder | disarm and drop the lease |

Replies: `OK …` or `ERR MALFORMED|UNAUTH|LEASE_HELD|NO_LEASE|UNAUTHORIZED|UNKNOWN|ARM_FAILED`.

### Permissions

- Directory `/run/contextdeck` is `0755` so the seat user can traverse it
  **without** joining group `contextdeck-broker` (that group owns G213 event
  nodes; adding the session user would be a keylogging hole).
- Socket inode is `0666`. Connectability is not authorization.
- On `accept`, the broker reads `SO_PEERCRED` from the kernel. Failed creds
  close the fd before any command is parsed. Root, other UIDs, remote
  sessions, and sessions without a local seat are rejected via logind
  (`sd_pid_get_session` first; if that reports no session, enumerate
  `sd_uid_get_sessions` and accept exactly one eligible active local seated
  `wayland`/`x11` session; reject root, remote, inactive, unseated,
  non-graphical, UID mismatch, and ambiguity).

### Lifecycle

Broker starts **disarmed**. Creating the socket does not arm. One lease only.
Disconnect, malformed framing, lease expiry, failed authentication, or
`shutdown`/`SIGTERM` disarms first (ungrab physical, then balanced synthetic
releases, then destroy virtual), then drops the lease. A systemd-sleep `post`
start is the same disarmed startup; the session client's retry path probes
`STATUS` only and does not emit `LEASE`/`ARM` until the user does.

S5 production `ARM` enumerates the G213 by USB ancestry (`046d:c336` plus
interface `00`/`01`), opens both sources without grabbing, measures the live
capability union, creates the virtual device, then grabs. Startup, socket
creation, `STATUS`, and `LEASE` do not open event nodes or `/dev/uinput`.
Game Mode and Backlight stay firmware-only and are not in the remap catalog.
Missing or invalid devices fail closed and leave the broker disarmed. A
runtime sink write failure disarms with ungrab-first cleanup. Compositor LED
state returns to if00 only; a runtime LED write failure does not disarm.

### TTY recovery

Same as section 7. The live-G4 precondition remains a second physical
keyboard or SSH, verified before start or ARM. Prefer stopping the session
app (drops the lease) when the seat still types. If the G213 is grabbed and
silent, recover from that already-verified path (an already-open TTY is
extra recovery only if it does not depend on the G213), then `systemctl stop
contextdeck-broker.service` only if that unit was actually started.

## 9. Install the broker binary (S5) — COOPERATOR-run

The G3 unit expects `ExecStart=/usr/bin/contextdeck-broker`. Use the documented
prefix `/usr`. Do **not** invent another prefix. Do **not** enable, start,
restart, arm, or grab.

From the repository root, with a clean `PATH` if CMake complains about
`CMAKE_ROOT`:

```sh
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure

# Privileged. No enable, no start.
sudo cmake --install build --component broker
sudo install -m 0644 packaging/systemd/contextdeck-broker.service \
  /etc/systemd/system/contextdeck-broker.service
sudo systemctl daemon-reload
```

`cmake --install --component broker` installs `contextdeck-broker` to
`/usr/bin/contextdeck-broker` and the sleep hook to
`/usr/lib/systemd/system-sleep/contextdeck-broker`. The hook is not
autostart: it only stops an already-active unit on sleep `pre` and may start
it once, disarmed, on `post`. The unit copy refreshes
`RuntimeDirectoryMode=0755` so the seat user can traverse `/run/contextdeck`
without joining group `contextdeck-broker`.

Verify (still no start):

| Check | Expected |
|-------|----------|
| `/usr/bin/contextdeck-broker` | exists, executable |
| `/usr/lib/systemd/system-sleep/contextdeck-broker` | exists, executable; same script as `packaging/systemd/contextdeck-sleep.sh` |
| `diff packaging/systemd/contextdeck-broker.service /etc/systemd/system/contextdeck-broker.service` | empty |
| unit `RuntimeDirectoryMode` | `0755` |
| unit `TimeoutStopSec` / `TimeoutAbortSec` | `5` / `5` |
| unit `WatchdogSec` | `2` |
| `systemctl is-enabled contextdeck-broker` | not enabled (`static` / no `[Install]`) |
| `systemctl is-active contextdeck-broker` | `inactive` |

Do **not** `systemctl start`. An inactive install of candidate `cb72ae0` is
separately recorded as `deployment-PASS` (META Worker 14). Remaining IRL G4
claims are a separate acceptance. This section does not grant a start.

## 10. M4 workspace sessions — explicit Apply only

M4 adds schema 4 (named sessions, per-application workspace assignments), the
read-only `WorkspacePlan` dry-run, and the bounded Slice B mutation path:
desktop create/conditional rename/`rows`/wrapping plus the separately opted-in
`current` switch, extra-desktop removal, typed launch, and placement. Nothing
runs automatically:

- Desktop mutation happens only when the user presses **Použiť** with
  `workspace_management_enabled`, a valid saved session, and a fresh,
  `Available` observation matching the shown preview. A changed live state
  refuses the Apply as `preview-stale`.
- Default Apply is create + conditional rename + `rows`/wrapping only.
  `removeDesktop`, the `current` switch, launch, and maximize are separate
  opt-ins.
- Launch is a typed `.desktop` id through `KIO::ApplicationLauncherJob` and is
  triggered only by the explicit Apply or an in-transaction `desktopCreated`.
  Plasma login, session-app start, `currentChanged`, and user-created desktops
  never launch anything.
- Placement/maximize is event-driven through the existing KWin bridge on
  `windowAdded`; `kwinrulesrc` is never written.
- No broker start, ARM, grab, udev change, package install, or systemd change.
- KWin observation is read-only through the existing `VirtualDesktopManager`
  snapshot path.

Checkpoint and revert:

- Before the first mutation, Apply writes
  `$XDG_CONFIG_HOME/contextdeck/workspace-checkpoint.json` (fallback
  `$HOME/.config/contextdeck/workspace-checkpoint.json`) atomically with
  user-only permissions. It is never the profile document, never META, never
  logged.
- Revert removes exactly the UUIDs this Apply created and restores names,
  `rows`, and wrapping from the checkpoint. The `current` restore is skipped
  as a bounded residual when the checkpoint UUID no longer exists. The
  checkpoint is deleted after a successful revert and overwritten by the next
  Apply.
- Revert is desktop-configuration-only: launched applications are not killed
  and already-open windows are not moved back.

Persistence behavior:

- The first explicit `Uložiť` after this upgrade writes schema 4 and keeps the
  previous exact bytes in `$XDG_CONFIG_HOME/contextdeck/profiles.json.bak`.
- Reading or opening the UI never rewrites the file and never creates a
  backup. Invalid, unsupported, or migration-fallback documents are refused
  for overwrite and keep their bytes.
- An older binary refuses a schema-4 file. Keep a COOPERATOR-owned pre-upgrade
  copy if a downgrade must survive later saves.

Boundaries for later work, so no operator assumes them here:

- Running the IRL checklist against a real session — including the KWin bridge
  reload, desktop mutation, application launch, and placement — needs its own
  explicit COOPERATOR grant. This document does not grant it.
- Plasma-login autostart, systemd user integration, and production lifecycle
  remain M5/G8. M4 never launches because the session began.

The M4 IRL checklist for later operational acceptance lives in
[docs/testing-m4.md](testing-m4.md); that file does not authorize host
mutation either.

## Logs to keep private

Never paste ordinary typed keystrokes, window captions, USB serial numbers, or
raw HID dumps. Component logs (`contextdeck.*` categories) and OpenRGB
stderr without serials are enough.
