# AGENTS.md — Ghostship Photo Mode fork

Context for AI agents and future sessions working in this repo.

## What this is

[Ghostship](https://github.com/HarbourMasters/Ghostship) is a native PC port of Super Mario 64.
It has C game code (decomp) plus a C++ port layer built on libultraship.

This fork adds **Photo Mode**: a first-person free-camera utility for capturing photos and video of
running levels, like Pokémon Snap. Enemies behave naturally, as if Mario isn't there.

- Phase 1 is **done** and working on Windows: free camera, level/act select, Mario presence modes, time freeze, FOV, menu UI.
- Planned next: camera angles and roll, effects/filters, screenshot and video capture, more structure and options.

## Where things live

| What | Location |
|---|---|
| **Source of truth (edit here)** | WSL: `/home/jeremy/projects/Ghostship`, branch `photo-mode` |
| Fork on GitHub | `https://github.com/ohsjc/Ghostship` (git remote `fork`). `origin` is upstream HarbourMasters. |
| Windows build checkout (**build-only, never edit**) | `C:\Users\ohsje\Projects\Ghostship-src` (synced from WSL by the build script with `checkout -f`) |
| Windows vcpkg cache | `C:\Users\ohsje\Projects\vcpkg` |
| Game install (where the exe runs) | `C:\Users\ohsje\Projects\Ghostship` (`/mnt/c/Users/ohsje/Projects/Ghostship`). Holds the ROM `Super Mario 64 (USA).z64`, `sm64.o2r`, saves, config. |
| Build wizard copy | `C:\Users\ohsje\Projects\GhostshipBuild` (copied from `tools/windows/`, self-updates after each build) |
| Official 3.0.0 release backup | `C:\Users\ohsje\Projects\GhostshipBuild\Ghostship-backup-20261021-180916` |

`Ghostship-original.exe` in the game folder is the first CI Photo Mode build, **not** the official release.

## Development workflow

1. Edit code in **WSL** (`/home/jeremy/projects/Ghostship`).
2. **Commit** to `photo-mode`. The Windows build only sees committed changes; it fetches from WSL via `\\wsl.localhost\Ubuntu\...`.
3. Build on Windows using one of these, run from Explorer or from WSL:
   - `C:\Users\ohsje\Projects\GhostshipBuild\2-Build-Ghostship.cmd` builds and installs into the game folder.
   - `3-Build-And-Run.cmd` does the same, then launches the game.
   - From WSL: `powershell.exe -ExecutionPolicy Bypass -File 'C:\Users\ohsje\Projects\GhostshipBuild\Build-Ghostship.ps1'`. It fails if `Ghostship.exe` is running.
   - Incremental builds take a few minutes. See `tools/windows/README.md` for the flags (`-Clean`, `-NoDeploy`, `-Run`, `-Source fork`).
4. Optional CI: pushing `photo-mode` to `fork` runs `.github/workflows/photo-mode-windows.yml`, a Windows-only build of about 15 minutes.
   - It produces the artifact `Ghostship-photo-mode-windows`, containing the exe, `ghostship.o2r`, `tcc.dll` and `.tcc/`.
   - The upstream `main.yml` ignores this branch.

There is **no full Linux build in WSL**: dependencies need `sudo apt`, which hasn't been set up.

For a quick compile check before committing, run `tools/wsl/syntax-check.sh`, which takes about 10 seconds.
- It runs `g++`/`gcc -fsyntax-only` against the real headers, with the third-party headers cached in `~/.cache/ghostship-syntax-check`.
- With no arguments it checks the changed `.c`/`.cpp` files. You can also pass files explicitly.
- It doesn't link, so the Windows build is still the final check.

### GitHub accounts

- The Copilot CLI uses `jeremyohsnomad` (default `gh` config).
- The fork belongs to **`ohsjc`**, which uses a separate gh config. Use it like this:
  `GH_CONFIG_DIR=~/.config/gh-ohsjc gh run list -R ohsjc/Ghostship`
- `git push fork photo-mode` already authenticates as ohsjc through a repo-local credential helper.
- Repo-local commit identity: `ohs <ohsjeremyc@gmail.com>`.

## Photo Mode architecture

### Files
- `src/port/mods/PhotoMode.h` declares the C hooks (`extern_s`) and the C++ UI API.
- `src/port/mods/PhotoMode.cpp` holds all the logic. It is registered with `RegisterShipInitFunc`.
- `src/port/ui/GhostshipMenuPhotoMode.cpp` is the "Photo Mode" menu (F1). It is registered in `GhostshipMenu::OnInit` and declared in `GhostshipMenu.h`.
- `src/port/mods/PhotoModeFilters.{h,cpp}` holds the object visibility filters.
  - It maps behavior scripts to categories (Enemies, Wildlife, Coins, Boxes, …) and applies the presets.
  - Unlisted behaviors fall into "Other".
  - The menu page is "Photo Mode → Visibility".
- `src/port/mods/PhotoModeTracks.{h,cpp}` holds camera tracks: keyframed camera paths.
  - Catmull-Rom interpolation on position, yaw, pitch, roll and FOV. Angles are unwrapped to take the shortest turn.
  - Playback advances 1/30 s per game frame inside the free camera update. While a track plays, `UpdateFreeCamera` uses `PhotoMode_SetPose()` and skips manual input; Start still freezes time.
  - Tracks are saved as JSON in `<game folder>/photomode_tracks/<name>.json`. The file also stores the level and area.
  - The menu page is "Photo Mode → Camera Tracks".
- `src/port/ui/PhotoModeOverlay.{h,cpp}` is an always-shown, invisible `GuiWindow` registered in `GhostshipGui.cpp`. It:
  - Polls the track hotkeys with ImGui (skipped when a text field has focus).
  - Draws the path preview projected onto the "Main Game" rect. The projection is roll-aware and matches `mtxf_rotate_xy`.
  - Draws the playback countdown.
- The C++ pose API is `PhotoMode_GetPose()` / `PhotoMode_SetPose()`. Pose angles are in radians; roll is written to `gLakituState.roll` (s16).
- `src/port/mods/BetterLevelSelect.{h,cpp}` exposes its level/act table through `BetterLevelSelect_GetEntries()`.

New `.cpp` files under `src/port/` are picked up automatically by CMake's GLOB_RECURSE.

### State machine (`PhotoMode.cpp`)
- From the menu: `Off → LevelRequested → Warping → Loading → Active`.
- "Enter here" goes `HereRequested → Active`.
- `carryOver` keeps control and hides Mario while switching levels from inside photo mode.
- `ControllingScene()` covers Mario and input; `CameraOwned()` covers the camera, FOV and room probe.

### Level entry
- **In-game:** `warp_to_level_default_spawn(level)` is called. The `lvl_set_current_level` hook sets the act and skips the act selector. `init_level` spawns Mario at the default position and skips the intro cutscene. Photo mode activates on the first normal gameplay frame.
- **Title screen:** a `LevelScriptCallLoop` listener (LOW priority) replaces the intro or level-select loop with `PhotoMode_IntroUpdate`. That function picks save file 1 if none is set and returns the target level.

### Game-code hooks (all `#include "port/mods/PhotoMode.h"`)
| File | Hook |
|---|---|
| `src/game/level_update.c/.h` | Adds `level_is_in_normal_play_mode()` and `warp_to_level_default_spawn()`. Hooks `lvl_set_current_level` (act and act-select skip) and `init_level` (no intro cutscene). |
| `src/game/mario.c` | `execute_mario_action`: when `PhotoMode_UpdateMario()` returns true, it returns early. Mario is invisible and intangible (`oIntangibleTimer = -1`), parked or following the camera. |
| `src/game/game_init.c` | `PhotoMode_ProcessInput()` runs after input is read. It snapshots then zeroes the controllers, so Mario, pause and dialogs see nothing. |
| `src/game/camera.c` | `geo_camera_fov`: FOV override. |
| `src/game/object_helpers.c` | `geo_switch_area`: room visibility is probed from the camera position. |
| `src/game/hud.c` | `render_hud`: hidden when `PhotoMode.HideHud` is on. |
| `src/engine/surface_load.c` | Object collision distance is measured from the camera instead of Mario. Never load all collision: the surface pools are fixed and unchecked, so overflow corrupts memory. |
| `src/game/rendering_graph_node.c` | `geo_process_object`: skips only the draw block when `PhotoMode_ShouldHideObject()` is true. The matrices and `cameraToObject` are still computed, so held objects and sound keep working. Filters apply only while `CameraOwned()`. |

The free camera itself is a `CameraUpdate` listener at HIGH priority. It writes `Camera` and `gLakituState`.

`EntityDistanceRender` and `EntityDistanceLoad` listeners force objects to draw and spawners to load.

### Mario presence (CVar `PhotoMode.Presence`)
- `0` Absent: Mario is parked at (0, 20000, 0). Enemies ignore him and spawners are force-loaded.
- `1` Ghost: an invisible, intangible Mario follows the camera, so enemies react to it.

### CVars
All CVars are `gEnhancements.PhotoMode.*`, via `CVAR_ENHANCEMENT("PhotoMode.X")`:

| CVar | Default |
|---|---|
| `Presence` | 0 |
| `MoveSpeed` | 40 |
| `LookSpeed` | 3 |
| `MouseSensitivity` | 0.15 |
| `MouseLook` | 1 |
| `InvertY` | 0 |
| `Smoothing` | 0.6 |
| `Fov` | 45 |
| `IgnoreDrawDistance` | 1 |
| `HideHud` | 1 |
| `Show.<Category>` | 1 (0 hides the category) |
| `Tracks.Loop` | 0 |
| `Tracks.Easing` | 1 (0 linear, 1 ease whole track, 2 ease each keyframe) |
| `Tracks.Speed` | 1 |
| `Tracks.Countdown` | 3 (seconds) |
| `Tracks.DefaultDuration` | 3 (seconds) |
| `Tracks.ShowPath` | 1 |
| `Tracks.ShowHud` | 1 |

The `<Category>` names are `Enemies`, `Bosses`, `Wildlife`, `Characters`, `Coins`, `StarsKeys`, `PowerUps`, `Boxes`, `Switches`, `Signs`, `Doors`, `Scenery`, `Platforms`, `Hazards`, `NaturalEffects`, `GameEffects` and `Other`.

The presets are Show Everything, Nature Documentary, Creatures Only and Empty Landscape.

### Controls (in photo mode)
- Left stick: move.
- Right stick, C-buttons or mouse: look.
- A / B: up / down.
- Hold R: fast. Hold Z: slow.
- D-pad up/down: FOV. D-pad left/right: roll.
- L: reset FOV and roll, and level the camera.
- Keyboard track hotkeys: K add keyframe, P play/stop, `[` / `]` previous/next keyframe, Delete delete keyframe. None of these keys are in the default N64 keyboard mapping.
- Start: freeze or unfreeze time.
- F1 opens the menu. F2 toggles mouse capture.

## Gotchas learned the hard way

- **C headers in C++:** Most game headers already use `extern_s` (= `extern "C"`), and `sm64.h` pulls in C++ headers (Engine.h, prism, templates).
  - Wrap **only** headers without `extern_s` in `extern "C" {}`: currently `game/interaction.h`, `game/mario.h` and `engine/surface_collision.h`.
  - Wrapping `sm64.h` breaks the build with "templates must have C++ linkage".
- `PLAY_MODE_NORMAL` and the `WARP_TYPE_*` defines are local to `level_update.c`, so expose helpers from there instead.
- `GameFrameUpdate` only fires during gameplay, not on the title or file-select screens.
- The Windows CMake configure needs **Developer Mode** turned on, or the ImGui symlink at `CMakeLists.txt:580` fails ("required privilege is not held").
- The Windows build script runs `git checkout -f` in `Ghostship-src`, so edits made there are lost.
- The Windows build needs `ghostship.o2r` (the `GeneratePortO2R` target) alongside the exe. `sm64.o2r` is extracted from the ROM and is already in the game folder.
- `include/behavior_data.h` declares some behaviors that are never defined, such as `bhvCheepCheep` and `bhvFishCommon`. Using them causes a link error (LNK2019) that the syntax check can't catch. Check the names against the definitions in `data/behavior_data.c`.

## Working with the user

- Give shell instructions as plain, standard steps, one command per line, with no chained one-liners.
- The user runs and tests the game on Windows and reports back. Don't assume runtime behaviour has been verified unless they say so.
- **Don't push to `fork` unless the user asks**, because each push starts a CI build. Committing locally is fine, and the Windows build script needs it.
