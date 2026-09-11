# G213 control-to-zone map

Evidence owner for which catalog controls sit in which of the five physical
RGB zones. **Nothing in the hypothesis table is measured.** Do not copy these
rows into product copy as facts.

Hardware truth: the G213 has five zones, not per-key RGB. OpenRGB names them,
in order:

1. Left Area
2. Middle Area
3. Right Area
4. Arrow and Homekeys
5. Numpad

Until a row in **Results** is filled, every `verified` flag in the running
program is `false` and zone-accent writes to hardware are inert. The Controls
page may show an unverified preview label only.

## Hypothesis (unverified)

| Control | Hypothesized zone | Why this guess |
|---------|-------------------|----------------|
| F1 | Left Area | Left function-key block over the left letter field |
| F2 | Left Area | Same left function-key block |
| F3 | Left Area | Same left function-key block |
| F4 | Left Area | End of the left function-key block |
| F5 | Middle Area | Center function-key block |
| F6 | Middle Area | Center function-key block |
| F7 | Middle Area | Center function-key block |
| F8 | Middle Area | End of the center function-key block |
| F9 | Right Area | Right function-key block over the right letter field |
| F10 | Right Area | Right function-key block |
| F11 | Right Area | Right function-key block |
| F12 | Right Area | End of the right function-key block |
| Previous | Numpad | Dedicated media keys sit on the far-right deck |
| PlayPause | Numpad | Far-right media cluster |
| Next | Numpad | Far-right media cluster |
| Mute | Numpad | Far-right media cluster |
| VolumeDown | Numpad | Far-right media cluster |
| VolumeUp | Numpad | Far-right media cluster |
| GameMode | Arrow and Homekeys | Guessed nearer the navigation cluster than the letter field |
| Backlight | Arrow and Homekeys | Guessed beside Game Mode |

## IRL probe (five steps)

Start ContextDeck with OpenRGB on loopback. Use Settings → Farby → global
Direct. After each step, look at the **physical keys**, not the OpenRGB GUI.
Do not run `openrgb --list-devices`. Write what you see into **Results**.
A failure stops the run; restore device default before quitting.

Use a dim base `#202020` on every zone you are not isolating.

1. **Left Area.** Set zone 1 to `#ff0000` and the other four to `#202020`.
   List every catalog control (F1–F12, media, volume, Game Mode, Backlight)
   that is clearly red. Those belong in Left Area.
2. **Middle Area.** Zone 2 `#00ff00`, others `#202020`. List the green
   catalog controls.
3. **Right Area.** Zone 3 `#0000ff`, others `#202020`. List the blue
   catalog controls.
4. **Arrow and Homekeys.** Zone 4 `#ffff00`, others `#202020`. List the
   yellow catalog controls.
5. **Numpad.** Zone 5 `#ff00ff`, others `#202020`. List the magenta
   catalog controls.

A control must appear in **exactly one** zone. If a key looks split or
ambiguous, write `ambiguous` and do not mark it verified. Controls that never
light in any step stay `unseen`.

When all five steps are done: tray → Restore device default.

## Results

Fill this table during the probe. Leave cells empty until seen. This section
is the only place a later change may set `verified: true` in code.

| Control | Observed zone | Notes |
|---------|---------------|-------|
| F1 | | |
| F2 | | |
| F3 | | |
| F4 | | |
| F5 | | |
| F6 | | |
| F7 | | |
| F8 | | |
| F9 | | |
| F10 | | |
| F11 | | |
| F12 | | |
| Previous | | |
| PlayPause | | |
| Next | | |
| Mute | | |
| VolumeDown | | |
| VolumeUp | | |
| GameMode | | |
| Backlight | | |

Date of probe:  
Observer: COOPERATOR  
Nothing above is measured until this table has entries.
