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
pass-through IRL is S5. This install also closes the measured OpenRGB `uaccess` keylogging
hole on G213 **input** nodes, `/dev/port`, and `/dev/i2c-*`. HID RGB
(`hidraw`) must keep working.

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
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=input
sudo udevadm trigger --subsystem-match=i2c-dev
sudo udevadm trigger --sysname-match=port

sudo install -m 0644 packaging/systemd/contextdeck-broker.service \
  /etc/systemd/system/contextdeck-broker.service
sudo systemctl daemon-reload
```

Do **not** `systemctl enable contextdeck-broker`. Do **not**
`systemctl start contextdeck-broker`. The unit has no `[Install]` section.
The binary now sends `READY=1` and feeds `WATCHDOG=1` from its idle event
loop (S3) and accepts an authenticated session lease on
`/run/contextdeck/broker.sock` (S4). It still does not open G213 event nodes
or `/dev/uinput` in this slice (ARM fail-closes without a device enumerator).
Starting the unit remains forbidden until S5 and G4.

`ExecStart` is `/usr/bin/contextdeck-broker`. Until there is an install
prefix, leave that path as documentation; do not start the unit from a
home-directory build (`ProtectHome=yes` would block it).

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
| `getfacl /dev/port` and `/dev/i2c-*` | **no** session-user ACL; no longer user-readable |
| input-remapper | still enabled/active; G213 preset untouched |
| `systemctl is-enabled contextdeck-broker` | not enabled (expected fail / `not-found` / `disabled`) |

If hidraw lost the session ACL, rollback immediately — lighting would break
and this install over-reached.

### Rollback

The unit was never enabled, so there is nothing to disable. After rollback,
packaged OpenRGB may restore session `uaccess` on G213 event nodes,
`/dev/port`, and i2c — that re-opens the measured hole.

```sh
# COOPERATOR-run (privileged)
sudo rm -f /etc/udev/rules.d/61-contextdeck-input-guard.rules \
           /etc/udev/rules.d/62-contextdeck-broker.rules \
           /etc/systemd/system/contextdeck-broker.service \
           /usr/lib/sysusers.d/contextdeck-broker.conf
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=input
sudo udevadm trigger --subsystem-match=i2c-dev
sudo udevadm trigger --sysname-match=port
sudo systemctl daemon-reload
sudo userdel contextdeck-broker
sudo groupdel contextdeck-broker
```

`userdel` may already remove the matching group; ignore `groupdel` if the
group is gone. Unplug/replug the G213 if event-node ownership stays stale.

## 7. Crash, hang, watchdog, and TTY recovery

This is the documented recovery path for the input broker. It does **not**
require running the unit or grabbing the G213. Do **not** start or enable
`contextdeck-broker.service` from this section. Real-keyboard proof of
FD-close ungrab on this kernel is G4 / S5, not S3.

### What the broker does

- `Type=notify` plus `WatchdogSec=2`. `Restart=no` (a crash must not re-grab).
- `READY=1` is sent once the single-thread event loop is running.
- `WATCHDOG=1` is sent from **that same thread** after `epoll_wait` (or the
  test wait) returns, including idle timeout, and after that iteration's
  ingest work returns. Half of 2 s is a 1 s wait.
- There is no helper thread and no detached timer. If wait or ingest blocks,
  watchdog notifications stop and systemd can detect the hang.
- Orderly stop (SIGTERM/SIGINT via signalfd) leaves the loop and sends
  `STOPPING=1`. Crash / watchdog abort / SIGKILL do not run userspace
  cleanup; recovery relies on kernel close of any held descriptors.

### Hang (watchdog)

A hung event loop stops feeding `WATCHDOG=1`. systemd then aborts the
process (default watchdog signal is `SIGABRT`) after `WatchdogSec=2`.
Because `Restart=no`, the unit stays dead. If the broker had been armed
(later S4/S5), descriptor close is what must return the physical keyboard;
that close-on-death behavior is kernel-side and still needs G4 on this
host.

S3 proves the feed/hang coupling in CTest (`test_broker_watchdog`) and
`contextdeck-broker watchdog-selftest`. It does **not** start the system
unit.

### Crash

`SIGTERM` is the orderly stop. `SIGKILL` and `SIGABRT` (watchdog) skip
userspace teardown. The planned armed teardown order remains ungrab
physical first, then balanced synthetic releases, then destroy the virtual
device. A dead process cannot run that sequence; G4 must show that closing
the evdev and uinput descriptors is enough.

Do not enable autostart until that G4 evidence exists (handout §29).

### TTY / second-seat recovery

Do not assume Ctrl+Alt+Fn or SysRq still work **from the G213** while it is
grabbed. If the broker is hung and the G213 is silent, recover from a
path that does not need that keyboard:

1. Another physical keyboard on the same seat, or
2. SSH / another machine, or
3. A TTY already reachable without the G213.

Then, only when recovering a **running** broker (not during S3):

```sh
# Recovery — COOPERATOR-run, and only if the unit was started later.
# Do not run this to "try S3".
systemctl stop contextdeck-broker.service
# if stop cannot complete because the unit was never started, that is success
```

If systemd already watchdog-aborted the process, `systemctl status` should
show inactive/failed and the keyboard should type again after descriptor
close. Do not `systemctl enable`. Do not `systemctl start` to "test
recovery". Do not change input-remapper.

If stop is not enough, `kill -TERM` then `kill -KILL` the `contextdeck-broker`
PID from the recovery path. Unplug/replug the G213 only as a last resort.

Quitting the session app, closing the broker socket, or letting the 6 s
lease expire also disarms (ungrab-first) without needing `systemctl stop`.
That is the S4 recovery path when the seat is still usable. If the G213 is
silent, still use a second keyboard/SSH/TTY as above — do not assume the
grabbed keyboard can send the quit chord.

After recovery, confirm typing on a text field. Do not paste key names,
scan codes, or raw event dumps into notes.

More procedure detail: `docs/testing-m2.md`.

## 8. Session IPC (S4)

The broker listens on `/run/contextdeck/broker.sock`. Override with
`CONTEXTDECK_BROKER_SOCKET` only in tests. The session app probes `STATUS` at
startup and does **not** auto-arm. Do **not** start the system unit to try
this.

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
  (`sd_pid_get_session`, active, `wayland`/`x11`, `sd_session_get_uid`
  matching the kernel UID, `sd_session_is_remote==0`).

### Lifecycle

Broker starts **disarmed**. Creating the socket does not arm. One lease only.
Disconnect, malformed framing, lease expiry, failed authentication, or
`shutdown`/`SIGTERM` disarms first (ungrab physical, then balanced synthetic
releases, then destroy virtual), then drops the lease.

S4 production `ARM` still fail-closes: there is no device enumerator and
`RealSink` is not constructed. Tests drive the same `Acquisition` path with
`FakeGrabber`. Real grab remains S5/G4.

### TTY recovery

Same as section 7. Prefer stopping the session app (drops the lease) when the
seat still types. If the G213 is grabbed and silent, recover from another
keyboard, SSH, or an already-open TTY, then `systemctl stop
contextdeck-broker.service` only if that unit was actually started.

## Logs to keep private

Never paste ordinary typed keystrokes, window captions, USB serial numbers, or
raw HID dumps. Component logs (`contextdeck.*` categories) and OpenRGB
stderr without serials are enough.
