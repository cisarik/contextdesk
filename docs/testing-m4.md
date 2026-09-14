# ContextDeck M4 workspace sessions — later IRL checklist

Numbered procedure for a **later** COOPERATOR run after the public Slice B
implementation candidate was separately code-accepted (`db9ddc1`) and after an
explicit mutation grant. This file records intent only: it does **not** authorize host
mutation, package installs, desktop changes, application launches, KWin bridge
reload, broker start, ARM, grab, autostart, suspend, udev/ACL changes, or
OpenRGB CLI-per-change. Running these steps against a real session requires its
own grant.

The Slice B code path is in the tree: explicit Apply, checkpoint revert, typed
launch, and bridge placement. The steps below still require the separately
granted IRL run; automated tests alone do not establish live behavior.

QML runtime behavior has so far been validated only by build-time compilation;
these steps are its first runtime validation.

Automated evidence lives in the registered CTest suite owned by
`CMakeLists.txt` (`test_profile_persistence`, `test_profile_resolver`,
`test_workspace_receiver`, `test_workspace_plan`, `test_workspace_lighting`,
`test_workspace_mutator`, `test_placement_resolver`,
`test_application_launcher`, and the rest). Receiver, mutator, and lighting
tests use `dbus-run-session` with a fake desktop manager on a private bus; they
must never replace or mutate real KWin. This checklist covers none of the M3
five-zone lighting run, raw input, broker, udev, suspend, autostart, or
input-remapper.

## Preconditions

1. The already established M1 lighting setup (OpenRGB SDK on loopback, KWin
   context bridge loaded, session app runnable from the tree).
2. A second physical keyboard or an equivalent independent recovery route is
   available for the whole session. M4 never grabs input, but the desktop
   under test is the user's live session.
3. Copy `$XDG_CONFIG_HOME/contextdeck/profiles.json` aside as a named
   checkpoint before any schema-4 save.
4. Note the current live desktop count and layout without writing them into
   META, prompts, or reports.

## Steps

1. **Checkpoint.** Copy the profile document aside. Confirm the session app
   starts with the broker inactive and no live G4 grab. Reload the edited
   `kwin/contextdeck-bridge` only under the granted mutation class; the code
   change alone does not reload anything.

2. **Edit and save a session.** Create a two-desktop named session
   and one assigned application; save with **Uložiť**. Confirm an unrelated
   application's lighting behavior is unchanged and that merely loading the
   document did not rewrite it.

3. **Preview.** Select the session and confirm the dry-run preview shows
   create/rename or "no difference" against the live desktops. Confirm the
   preview does not remove anything by default and reports extra live
   desktops explicitly. Change the live layout and confirm Apply refuses with
   `preview-stale` until the preview is re-read.

4. **Apply.** Apply the session under the checkpoint discipline: confirm
   create/rename intent first, then apply. Verify the live desktop layout
   matches the session, then invert via the checkpoint and confirm the
   previous layout returns. Confirm the checkpoint file is written before any
   change and deleted after a successful revert.

5. **Launch once.** Apply a session with an assigned application. The assigned
   app starts once; a second Apply does not duplicate it because it is already
   running. Failed launch leaves a bounded error class and no retry storm.
   Confirm nothing launches on session-app start or with no explicit Apply.

6. **Placement and maximize.** The launched or newly added window lands on the
   assigned desktop and maximizes through the existing KWin bridge. No
   `kwinrulesrc` entry is created; quitting the session app stops placement.

7. **Title fallback (opt-in).** Enable the global flag and one profile flag,
   set a pattern, and focus an unmatched window whose caption matches. The
   profile's assignment applies for that focus event. Disable either flag and
   confirm no match occurs. Confirm the caption never appears in logs,
   diagnostics, the profile document, or `workspace-checkpoint.json`. Never
   paste captured captions into META, prompts, or reports.

8. **Quit.** Quit the session app. The keyboard still types normally and
   lighting restores per the existing M1 behavior. Restore the copied profile
   document if a downgrade is needed, and confirm the restored layout.

## Fail closed

Stop and note the step number if a step would require starting the broker,
grabbing a device, installing packages, editing udev, writing `kwinrulesrc`,
running OpenRGB as a CLI process per context change, enabling autostart, or
changing host policy outside the grant. Those are out of this checklist.
Real D-Bus owner-loss and invalidation correctness comes from the private-bus
tests, not from a destructive live experiment.
