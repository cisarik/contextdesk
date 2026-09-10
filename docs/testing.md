# ContextDeck IRL acceptance (M1)

Numbered script for the COOPERATOR. Run it on the machine with the G213
attached after following `docs/operations.md`. This is not an automated suite.

When a step fails, **stop**, note the step number, and paste the log lines
listed at the end. Do not "fix forward" by installing extra packages or
grabbing `/dev/input`.

## Preconditions

1. `build/contextdeck` exists (`cmake` + `ninja` + `ctest` already green).
2. `openrgb` is installed, udev rules loaded, G213 hidraw reachable by the
   session user.
3. OpenRGB SDK is listening on `127.0.0.1:6742`.
4. The KWin script `contextdeck-bridge` is loaded (`isScriptLoaded` is true).

## Steps

1. **Start ContextDeck**  
   `./build/contextdeck`  
   **Expect:** tray icon appears; stderr contains `bus name registered` and
   `status notifier started`. Lighting connection becomes `ready` (or a
   lighting-disabled warning if OpenRGB cannot see the G213 — then stop and
   fix OpenRGB, not ContextDeck).  
   **Fail:** process exits, or the bus name is reported taken (another copy is
   running).

2. **Keyboard still types normally everywhere**  
   Type in a terminal, in a browser, and in the ContextDeck settings window.  
   **Expect:** every key works as before M1. No stuck modifiers, no missing
   keys, no duplicate characters. M1 does not intercept input.  
   **Fail:** any change in typing behavior — unload the KWin script, quit
   ContextDeck, and report immediately. Do not continue.

3. **G2 five-zone check**  
   In Settings → Profiles, set the global base color to a saturated color
   (for example `#ff0000`) and Save.  
   **Expect:** all five physical zones (left, middle, right, arrows/home,
   numpad) show that one color. No per-key pattern.  
   **Fail:** only some zones change, or OpenRGB GUI shows a different device.
   Capture OpenRGB stderr and ContextDeck `contextdeck.rgb` lines.

4. **Focus-change color check**  
   Add a profile from the inventory picker for a second application. Give it a
   different color (for example `#0000ff`). Save. Alt-Tab between that
   application and another window.  
   **Expect:** the keyboard color follows the focused application's profile
   when one exists, otherwise the global color. The tray Overview line shows
   the current application and resolved profile.  
   **Fail:** color never changes, or it changes on title text rather than app
   identity. Check `BridgeConnected` on Diagnostics and whether
   `isScriptLoaded` is still true.

5. **Bridge-loss fallback**  
   Unload the KWin script (`unloadScript "contextdeck-bridge"`). Wait ~15
   seconds (three missed 5 s heartbeats).  
   **Expect:** one warning `bridge lost`; context becomes unidentified;
   lighting falls back to the **global** profile color, **not** lights-off;
   typing is still normal. Reload the script and confirm the bridge returns.  
   **Fail:** lights turn off, the app crashes, or identity sticks to the last
   application forever.

6. **Lights off and Automatic**  
   Tray or Overview: Lights off.  
   **Expect:** all five zones go dark; the UI still says `lights_off`, never
   "Automatic". Restore automatic.  
   **Expect:** profile colors return. Opening Settings must **not** by itself
   flip a `temporary_color` override (if you set a temporary color first,
   opening our window must keep it until you focus a *different*
   application).

7. **Displays Off**  
   Tray → Displays Off.  
   **Expect:** displays blank via DPMS; keyboard lighting unchanged; KScreen
   display layout unchanged when you wake the screens (move mouse / press a
   key). Debounced: a second click within 2 s is ignored.  
   **Fail:** outputs rearranged, or the machine suspends. Do not repeat.

8. **Suspend**  
   Tray → Suspend… → confirm Yes in the dialog.  
   **Expect:** the machine sleeps through logind (`CanSuspend` was `yes` on
   this host). After resume, ContextDeck is still running or has exited
   cleanly; the keyboard types; lighting reconnects or reports a lighting
   error without crashing. Debounced: 5 s.  
   **Fail:** no confirmation dialog, or suspend is triggered twice. Do not
   write `/sys/power/state`.

9. **Quit**  
   Tray → Quit.  
   **Expect:** process exits; bus name
   `io.github.cisarik.ContextDeck` disappears; keyboard still types;
   OpenRGB (if left running) still owns lighting.

## Logs to paste back (redact serials and captions)

If any step fails, paste **only**:

1. ContextDeck stderr from start until the failure (categories
   `contextdeck.context`, `contextdeck.rgb`, `contextdeck.app`,
   `contextdeck.ui`, `contextdeck.actions`).
2. OpenRGB stdout/stderr from the SDK server terminal, with USB serials
   removed if present.
3. KWin script errors, if any:

   ```sh
   journalctl --user -n 80 --no-pager | grep -i -e kwin -e contextdeck
   ```

4. The step number, the expected result, and what you actually saw on the
   keyboard and in the tray.

Do not paste window titles, typed text, `lsusb -v` serials, or hidraw dumps.

## What this script does not cover

Automated CTest (resolver, persistence, protocol frames) is a developer gate,
already run at build time. GUI tests, QML tests, and input grabbing are out of
scope for M1. Game Mode and Backlight remain conditional on G1.
