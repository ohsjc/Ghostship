# Building Ghostship.exe locally on Windows

These scripts set up Visual Studio 2022 Build Tools, CMake, Git and Python, then build Ghostship.
The finished exe is copied into the game folder (`%USERPROFILE%\Projects\Ghostship`).

## One-time setup

1. Double-click **`1-Setup-Build-Tools.cmd`** and approve the administrator prompt.
   - It skips anything that is already installed.
   - Expect roughly 8 GB of downloads and 15-40 minutes.
   - At the end it asks whether to add a Windows Defender exclusion. Answering `y` makes builds much faster.
2. Wait until it prints **DONE**, then close the window.

## Every build

1. Make sure the changes are committed in the WSL repo (`/home/jeremy/projects/Ghostship`, branch `photo-mode`).
2. Double-click one of these:
   - **`2-Build-Ghostship.cmd`** builds and installs.
   - **`3-Build-And-Run.cmd`** builds, installs and launches the game.

The first build takes about 30-60 minutes because it compiles all the dependencies.
Later builds only recompile changed files and usually take a few minutes.

## Troubleshooting: CMake symbolic-link permission

If CMake reports `file failed to create symbolic link` at `CMakeLists.txt:580`,
enable **Developer Mode** in Windows Settings (`System` → `For developers` → `Developer Mode`),
then rerun `2-Build-Ghostship.cmd`. This lets CMake create the ImGui link without running
every build as administrator. The partially completed configure can be retried; a clean build
is not normally needed.

## What the build does

| Step | Action |
|---|---|
| Source | Syncs `%USERPROFILE%\Projects\Ghostship-src` to the WSL repo's `photo-mode` branch. Any local edits in that folder are overwritten. |
| Dependencies | Puts vcpkg libraries in `%USERPROFILE%\Projects\vcpkg`, so they survive a clean rebuild. |
| Output | Installs `Ghostship.exe`, its DLLs and `ghostship.o2r` into the game folder. |
| Backup | Saves the originals as `Ghostship-original.exe` and `ghostship-original.o2r` the first time it runs. |

## Options

Pass these flags in PowerShell, or after the `.cmd` name in a terminal (for example `2-Build-Ghostship.cmd -Clean`):

| Flag | Effect |
|---|---|
| `-Source fork` | Builds from github.com/ohsjc/Ghostship instead of WSL. |
| `-Branch <name>` | Builds a different branch. |
| `-Clean` | Deletes the build folder first. Use this if a build gets into a bad state. |
| `-NoDeploy` | Builds only and does not copy into the game folder. |
| `-Run` | Launches the game after installing. |

To restore the official release, copy `Ghostship-original.exe` and `ghostship-original.o2r` back over `Ghostship.exe` and `ghostship.o2r`.
