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
`true`. Persistent install (optional):

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
- lighting `ready` once OpenRGB is up and a G213 is enumerated; otherwise
  `lighting disabled: …` while the rest of the app stays alive

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

## Logs to keep private

Never paste ordinary typed keystrokes, window captions, USB serial numbers, or
raw HID dumps. Component logs (`contextdeck.*` categories) and OpenRGB
stderr without serials are enough.
