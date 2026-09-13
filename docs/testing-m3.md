# ContextDeck M3 workspace lighting — later IRL checklist

Numbered procedure for a **later** COOPERATOR physical run after a separate
fresh Worker has accepted the public implementation candidate. This file does
not authorize host mutation, package installs, broker start, ARM, grab,
autostart, suspend, udev/ACL changes, or OpenRGB CLI-per-change.

This is not automated evidence and not M3 acceptance. The keyboard has no
readback: **your eyes** close every lighting claim. Implementation CTest
coverage lives in `CMakeLists.txt`
(`test_profile_persistence`, `test_profile_resolver`,
`test_workspace_receiver`, `test_workspace_lighting`, and the rest of the
registered suite). Receiver tests use `dbus-run-session` and a fake desktop
manager; they must never replace real KWin.

M2 remains parked. Keep the broker inactive. Do not start
`contextdeck-broker.service` from these steps.

## Preconditions

1. Use the already established M1 lighting setup (OpenRGB SDK on loopback,
   KWin context bridge loaded, session app runnable from the tree).
2. At least two existing virtual desktops and two identifiable application
   contexts. Do not create desktops or change host policy for this checklist.
3. Preserve the current configuration file before any schema-3 save.
4. The broker stays inactive. No live G4 grab.

## Steps

1. **Prepare safely.** Confirm the two desktops and two apps. Leave the broker
   stopped. Copy the current profile document aside if you may want to restore
   schema 2 later.

2. **Check compatibility.** Start with a valid schema-2 preset. Confirm the
   previous lighting behavior before enabling roles. Confirm that merely
   loading the file does not rewrite its bytes.

3. **Exercise the default layout.** In Farby, apply **Použiť rozloženie 4
   plochy + aplikácia**. Switch between the two desktops and the two apps.
   Check active versus inactive indicators, the application color, and the
   five-zone preview against the physical keyboard. Colors are a desired
   preview, not physical readback.

4. **Exercise all five physical zones.** Temporarily configure two desktop
   indicators and three app slots. Switch desktop and application contexts so
   both indicator zones and all three app zones visibly change. Record
   observation of each physical zone. Do not infer a control-to-zone map.

5. **Check capacity.** Temporarily reduce indicator capacity to one and select
   the second existing desktop. Confirm the unrepresented-desktop warning and
   the absence of a false active indicator.

6. **Check loss and recovery.** Under the later acceptance grant, disable the
   already installed context bridge and wait for its existing loss timeout:
   app slots must use fallback while desktop indicators remain responsive.
   Re-enable the bridge. Then pause workspace observation in Diagnostics:
   expect the recorded device-default request and an unavailable label. Resume
   and require a fresh snapshot. Real D-Bus owner-loss correctness comes from
   private-bus tests; this pause is labelled simulated loss.

7. **Restore.** Select **Restore device default**, observe the actual keyboard,
   and confirm later context changes do not retake control. Restore the chosen
   configuration. Do not claim successful physical restoration when transport
   was unavailable.

## Fail closed

Stop and note the step number if a step would require starting the broker,
grabbing a device, installing packages, editing udev, or running OpenRGB as a
CLI process per context change. Those mutations are out of this checklist.
