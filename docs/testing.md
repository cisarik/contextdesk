# ContextDeck IRL acceptance (M1 lighting correction)

Numbered script for the COOPERATOR. Run it on the machine with the G213
attached after following `docs/operations.md`. This is not an automated suite.
The keyboard has no readback: **your eyes** close every lighting claim.

When a step fails, **stop**, note the step number, and paste the log lines
listed at the end. Do not "fix forward" by installing extra packages, running
`openrgb --list-devices`, grabbing `/dev/input`, or opening the OpenRGB GUI
while ContextDeck owns the device.

Reload the KWin script after this tree's bridge change so heartbeat
`ContextReport` is actually loaded.

## Preconditions

1. `build/contextdeck` exists (`cmake` + `ninja` + `ctest` already green).
2. `openrgb` is installed, udev rules loaded, G213 hidraw reachable by the
   session user.
3. OpenRGB SDK is listening on `127.0.0.1:6742`.
4. The KWin script `contextdeck-bridge` is loaded (`isScriptLoaded` is true).
5. `~/.config/contextdeck/profiles.json` is **absent** or moved aside for
   step 1 (cold start). Restore it after that step if you had one.

## Steps

1. **Cold start is non-destructive**  
   Confirm the keyboard is showing its own firmware effect (Wave family).  
   `./build/contextdeck`  
   **Expect:** tray icon; stderr contains `bus name registered`,
   `status notifier started`, and `cold start: no profile document,
   pass-through, writing nothing`. Lighting connection may become `ready`.
   The keyboard **keeps the same firmware effect**. Overview (**Stav**) shows
   hollow/dashed Hero strips with a `Device default (Wave)` badge — not solid
   black — and a single status sentence (`… · svetlá: firmware Wave`). Lighting
   is `untouched — device default`, never Automatic.  
   **Fail:** the board goes dark, snaps to a flat color, or the process exits.

2. **Keyboard still types normally everywhere**  
   Type in any ordinary text field: a terminal, a browser, and a text input
   inside ContextDeck. The hex color field is **not** on Overview; it lives
   under **Farby → Pokročilé: hex** (optional). You do not need that field to
   complete this step.  
   **Expect:** every key works as before. No stuck modifiers, no missing
   keys, no duplicate characters. This tree does not intercept input.  
   **Fail:** any change in typing — unload the KWin script, quit ContextDeck,
   report immediately.

3. **Restore device default**  
   In Settings → **Farby**, set the global preset to Direct with a saturated
   color (click a zone swatch, or enable **Pokročilé: hex** and enter
   `#ff0000` on all five zones) and **Uložiť**. Confirm the board shows that
   color. Tray, or Overview overflow → **Restore device default**.  
   **Expect:** the previously recorded device mode returns (Wave unless you
   saw a different non-Direct effect at connect). The Hero returns to hollow
   strips / `Device default (Wave)` and stops touching the device.  
   **Fail:** it stays red, goes dark, or the UI says Automatic.

4. **Each device preset**  
   With Follow profile visible (it appears after a preset exists), set the
   global preset in **Farby** in turn to `wave`, `cycle`, `breathing`, `off`,
   then `direct` (use a non-black color). **Uložiť** each time and look at the
   keyboard before changing the next.  
   For **Breathing**: pick **Farba dýchania** (not black; `#7c3aed` is the
   default) and move **Rýchlosť animácie**. The board must pulse that color;
   faster on the slider must visibly speed up the pulse. Wave and Cycle should
   also follow the speed slider.  
   **Expect:** Wave/Cycle/Breathing animate in firmware; Breathing is a visible
   colored pulse, not a dark keyboard; Off is dark (Hero strips may be solid
   black — that means Off); Direct is a static five-zone color. The status
   sentence names that preset, never Automatic.  
   **Fail:** a named preset does nothing, Breathing stays black/invisible, speed
   does not change the animation, or Direct leaves all zones black.

5. **Per-zone gradient**  
   Global preset Direct on **Farby**. Pick Start Color and End Color (the live
   five-band preview under the pickers must follow those colors). Then **Použiť
   gradient** (for example `#ff0000` → `#0000ff`). The five labelled zone
   swatches must change **immediately**, the preset must switch to Direct if it
   was not already, and a hint to **Uložiť** may appear. Then **Uložiť**.  
   **Expect:** five distinct bands, left → numpad, matching the labelled
   swatches: Left Area, Middle Area, Right Area, Arrow and Homekeys, Numpad.
   Not per-key RGB.  
   **Fail:** swatches do not change on **Použiť gradient**, only one color, or
   more than five independently colored keys.

6. **Per-application preset on focus change**  
   On **Aplikácie**, add a profile from the inventory picker. Give it a
   different preset (for example Direct `#0000ff` while global is Direct
   `#ff0000`, or Wave vs Direct). **Uložiť**. Alt-Tab between that application
   and another window.  
   **Expect:** lighting follows the focused application's preset when one
   exists, otherwise the global preset. Tray shows the current application
   and resolved profile. With the settings window focused, Overview must not
   show the raw D-Bus name as the context line.  
   **Fail:** it never changes, or it changes on window title rather than
   identity.

7. **Temporary override and expiry**  
   Set a temporary color from the existing override path (or a Direct
   temporary). Open Settings.  
   **Expect:** the override stays; the banner says it is a temporary
   override, not Automatic. Focus a *different* application.  
   **Expect:** the override expires and the resolved profile returns.
   Opening our own UI must not expire it.

8. **Bridge-loss fallback**  
   Unload the KWin script (`unloadScript "contextdeck-bridge"`). Wait ~15
   seconds (three missed 5 s heartbeats).  
   **Expect:** one warning `bridge lost`; context unidentified; lighting
   falls back to the **global** preset, not forced off; typing still
   normal. Reload the script; the bridge returns.  
   **Fail:** lights turn off, the app crashes, or identity sticks forever.

9. **Restart recovers context (D3)**  
   With the bridge loaded and some application focused, quit ContextDeck
   (tray Quit) and start it again **without** changing focus. Wait up to
   one heartbeat interval (5 s).  
   **Expect:** Overview (**Stav**) shows that application's identity without an
   extra Alt-Tab. **Diagnostika** `bridgeConnected` is true and
   `CurrentIdentity` is not empty. Repeating heartbeats do not spam
   lighting changes or bump `policyRevision` for the same identity.  
   **Fail:** identity stays empty until you click another window.

10. **Displays Off (D2)**  
    Tray → Displays Off.  
    **Expect:** displays blank via DPMS; keyboard lighting unchanged;
    KScreen layout unchanged when you wake the screens. Debounced: a
    second click within 2 s is ignored.  
    **Fail:** outputs rearranged, the machine suspends, or nothing
    happens (the old stack-temporary DPMS bug). Do not repeat.

11. **Suspend**  
    Tray → Suspend… → confirm Yes.  
    **Expect:** sleep through logind. After resume, ContextDeck is still
    running or has exited cleanly; the keyboard types; lighting reconnects
    or reports a lighting error without crashing. Debounced: 5 s.  
    **Fail:** no confirmation dialog, or suspend is triggered twice. Do
    not write `/sys/power/state`.

12. **Five-step zone-map probe**  
    Follow `docs/hardware/g213-zone-map.md` exactly. Fill the **Results**
    table there (not this file). Restore device default when finished.

13. **Quit**  
    Tray → Quit.  
    **Expect:** process exits; bus name
    `io.github.cisarik.ContextDeck` disappears; keyboard still types;
    if lighting intent had been applied, quit restores the recorded
    device mode. OpenRGB (if left running) still owns the SDK port.

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
scope. Game Mode and Backlight remain conditional on G1. Zone-accent writes
stay inert until the probe below marks map entries verified.
