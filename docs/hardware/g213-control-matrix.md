# G213 control-to-host-event matrix (G1 evidence)

Measured by the ORCHESTRATOR with COOPERATOR-assisted physical probing
(2026-09-11, read-only `python-evdev` monitor on both G213 event nodes,
no `EVIOCGRAB`, ordinary typing filtered by a whitelist and never recorded).

Kernel at probe time: `7.2.3-1-cachyos`. Node numbers (`event7`/`event8`) are
host evidence, not identifiers; interfaces are identified by USB ancestry
(`046d:c336`) plus interface number (`if00`, `if01`).

## Results

| Control | Interface | EV_KEY code | Repeat seen | Classification |
|---------|-----------|-------------|-------------|----------------|
| F1 | if00 | 59 | no | host-remappable |
| F2 | if00 | 60 | no | host-remappable |
| F3 | if00 | 61 | no | host-remappable |
| F4 | if00 | 62 | no | host-remappable |
| F5 | if00 | 63 | no | host-remappable |
| F6 | if00 | 64 | no | host-remappable |
| F7 | if00 | 65 | no | host-remappable |
| F8 | if00 | 66 | no | host-remappable |
| F9 | if00 | 67 | no | host-remappable |
| F10 | if00 | 68 | no | host-remappable |
| F11 | if00 | 87 | no | host-remappable |
| F12 | if00 | 88 | no | host-remappable |
| Previous | if01 | 165 | no | host-remappable |
| PlayPause | if01 | 164 | no | host-remappable |
| Next | if01 | 163 | no | host-remappable |
| Mute | if01 | 113 | no | host-remappable |
| VolumeDown | if01 | 114 | no | host-remappable |
| VolumeUp | if01 | 115 | no | host-remappable |
| Game Mode | — | **none observed** (3 deliberate presses, zero evdev events) | — | **firmware-only — not host-remappable** |
| Backlight | — | **none observed** (3 presses, zero evdev events) | — | **firmware-only — not host-remappable** |

## Observations

- Every ordinary key and every catalog control arrived on exactly one node:
  F-block and typing on if00; the media/volume cluster on if01. No duplicated
  press/release across both nodes was observed during this probe.
- Game Mode and Backlight produced no host-visible event in any filtered or
  hunt window. Their firmware effects (Windows-key toggle, lighting mode
  rotation) happen inside the device. The COOPERATOR could not observe the
  Windows-key toggle because the Windows key has no function in their KDE
  session.
- One unexplained single press/release of `code=166` (`KEY_STOP`) on if01 was
  recorded between PlayPause and Previous during the first session. Its
  physical source is unresolved; treat any mapping assumption about 166 as
  unproven.
- Backlight is a firmware lighting control: it may change the device's own
  lighting mode while ContextDeck/OpenRGB also drives the LEDs. Host software
  cannot intercept it; coexistence must be treated as "firmware may fight the
  host" and the UI should re-sync state after a Backlight press.

## Consequences

- M3 remapping covers exactly the eighteen controls above.
- Game Mode and Backlight are permanently out of the remapping catalog for
  this product unless a later logical whole reopens G1 with new evidence
  (for example hidraw vendor-protocol research). They must never be silently
  substituted by PrintScreen or Pause.
- G1 gate: **closed** for routing classification. Repeat behavior for
  remappable keys was not observed in short presses; repeat semantics get
  verified in M2 pass-through testing (holding F-keys during IRL).

Probe date: 2026-09-11
Observer: COOPERATOR
Recording: ORCHESTRATOR (read-only python-evdev, no grabs, no writes)