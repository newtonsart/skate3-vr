# Windows installer and portable package

The player package includes the OpenXR-enabled application, its runtime DLL,
the English launcher, documentation and dependency license notices. It contains no ISO, extracted game
files, title update, saved games, settings or generated PPC sources.
Packages are built locally; there is no GitHub Actions release workflow yet.

## Player setup

Run `Skate3-VR-Setup-0.2.3-alpha.exe`. The Inno Setup wizard installs per user,
without requesting administrator privileges. Choose:

- Application location.
- A separate game-files folder, or an existing installation.
- A separate save-profiles folder.
- Optional desktop shortcut and Start menu location.
- Your own Skate 3 ISO, when the game files are not installed already.

The existing native installer extracts the ISO with progress. It then offers
the existing TU3 acquisition wizard: download explicitly or select a local TU
package. Setup mode exits before gameplay or OpenXR headset initialization.
The ISO is opened for reading only. Required executables and both pinned TU3
payload hashes are checked before the launcher enables its play buttons.
An interrupted extraction leaves an in-progress marker; it cannot be mistaken
for a complete installation, even when its executable files were copied first.
The USA/Europe ISO used for development is the tested input; broader disc
version coverage remains pending.

If setup is cancelled, the application can still be opened. Use **Install /
finish setup** to finish preparation before playing. Setup does not merge game
files into an unrelated nonempty folder. Uninstall removes application files
and shortcuts, while leaving extracted game data and all saves in place.

## Save profiles

The launcher reads the game/save paths from `install.ini` beside
`Skate3-VR.cmd`. Without that file, source checkouts retain `game/`,
`out/build/relwithdebinfo/skate3.exe` and `out/runtime-test/`.

**Main save** uses the selected save root. Additional profiles use
`save-profiles/<GUID>/` underneath it. Each runs the same game with a separate
`user_data_root`, keeping progress independent. Only the main graphics settings
are copied when a profile is created. No existing saved-game data is copied,
moved or deleted. The game's own profile/slot selection remains available
inside the game.

`launcher-profiles.json` stores profile names, identifiers and the last selected
profile. Updates are atomic and keep a `.bak` of the previous metadata. Invalid
metadata is reported rather than overwritten. Preserve the entire save root
when backing up progress or moving to another PC. Game/setup logs are also
written there rather than into the installed application folder.

## Generate packages

Use a configured, working OpenXR build and an x64 Visual Studio developer
shell with CMake on PATH. Install the official **Inno Setup** compiler; the
local build was checked with 7.1.1. From the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File cmake/PackageWindows.ps1 -Compiler 'C:/path/to/Inno Setup/ISCC.exe'
```

The default build directory is `out/build/relwithdebinfo`. Override it with
`-BuildDirectory` if needed; use `-Version` for the package version.
Close any game using that build first: the script relinks the executable
before copying it, because a played executable can contain an icon extracted
from the user's game art. The clean payload is packaged before any game runs.

Outputs in `out/packages/`:

- `Skate3-VR-Setup-0.2.3-alpha.exe`: installer.
- `Skate3-VR-Windows-0.2.3-alpha.zip`: portable application.
- `SHA256SUMS-0.2.3-alpha.txt`: package checksums.

The portable ZIP uses `game/` and `out/runtime-test/` beside the launcher by
default; use **Install / finish setup** on first launch. For selectable folders,
shortcuts and uninstall support, use the installer instead.

Packaging copies only an explicit file allowlist into a new staging folder.
It never archives the build directory, workspace, game folder or user-data
root. PDBs, title updates, local configuration and logs are excluded. These
preview packages are unsigned; signing and a public release are separate work.

To reuse a source-checkout save in an installed copy, select its existing
`out/runtime-test/` directory on Setup's **Save profiles folder** page. Select
the existing `game/` folder to reuse extracted game files. There is no automatic
migration or deletion of either location.

## Checks

```powershell
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File cmake/test_vr_launcher.ps1
```

The launcher tests use temporary fixtures for installed/development layouts,
paths with spaces/Unicode, missing/corrupt TU3, setup without a headset,
independent progress, atomic metadata, invalid profile IDs and all mode buttons.
`skate3-setup-test` checks extraction-path rejection, including traversal,
Windows drive/stream paths and reserved device names. SDK patch tests check
the setup-progress handoff and repeat application.

Installer/package validation uses separate directories under ignored `out/`.
No new PCVR headset validation is implied by successful installation.

Local checks on 2026-11-03:

- Compiled the native setup mode, extraction-path test and Inno Setup installer.
  Launcher/profile tests, stereo/editor math, movie-skip, SDK patch/idempotency
  and Python ISO-extraction regressions passed.
- Extracted the user's real USA/Europe ISO into a new ignored test directory
  and staged a local TU3 package successfully. The ISO was opened read-only.
- Ran the generated installer with a fresh application/game/save layout.
  The preselected ISO followed the graphical extraction/progress path, TU3
  passed both pinned SHA256 checks, setup returned success and did not start
  gameplay or require Quest Link. The download option was not retested; TU3
  was supplied from a local package.
- Checked the created desktop shortcut target. Opened the installed launcher,
  created a second named save profile and inspected the rendered form.
- Uninstalled the test application and verified that game data plus both
  profiles' progress sentinels remained. Repeated the final package installation
  and uninstall after the interruption-marker and UI fixes.
- Verified that the ZIP contains only the allowlisted application/runtime,
  launcher, documentation and dependency notices. No retail files, TU payloads,
  local profiles, logs, PDBs or generated source were packaged.

Still pending: a separate clean Windows PC, full gameplay/save cycles through
the installed launcher's profiles, headset checks and signing/public release.
