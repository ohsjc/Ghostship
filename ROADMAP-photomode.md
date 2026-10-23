# Photo Mode roadmap

Decisions: hotkeys while flying + menu list to edit keyframes; OBS recording now, built-in export later;
OK to fork libultraship for render work.

## Phase 2 — Camera tracks + camera modes (DONE)
- Keyframe {pos, yaw, pitch, roll, fov, time}; hotkey add/delete/jump; menu list to edit/reorder/retime.
- Catmull-Rom position, slerp-ish angles (unwrap yaw), eased timing; play/stop/loop/scrub, speed.
- Save/load JSON per level (+act) next to the game folder.
- Path preview overlay (ImGui projected lines/markers), auto-hidden during playback; countdown before play.
- Camera roll (dutch angle) via gLakituState roll.
- Orbit mode (around a point/object), lock-on follow of a chosen object (nearest-in-view picker).

## Phase 3 — Cheap surreal (DONE)
- Per-category object scale multiplier at draw time (geo_process_object hook already exists).
- Skybox override (GraphNodeBackground / geo_skybox_main param) incl. solid color.
- Projection stretch X/Y + extreme FOV range; dolly zoom tied to tracks (FOV keyframes).

## Phase 4 — Flora & Fauna (implemented; Windows testing pending)
Goal: change creature/scenery density and add selected creatures or objects on the fly. Keep the
catalog limited to behaviors that work without level-specific paths, dialog, special collision or
cutscene setup.

### Implemented
- Menu page **Photo Mode → Flora & Fauna**.
- Reversible, render-only density sliders for every visibility category. Each
  `PhotoMode.Density.<Category>` CVar is 0–100% (default 100). Objects still run their behaviors,
  collide and make sounds; a stable hash of pool slot and behavior keeps the hidden subset stable.
- **Multiply Enemies / Wildlife / Trees** duplicates active objects from a curated supported list,
  preserving behavior parameters. Copies are offset nearby; ground copies need a nearby floor and
  water schools need water.
- **Spawn at Camera** menu picker and **G** hotkey, facing camera yaw. Ground actors use the floor
  beneath the camera; flyers use camera height.
- Curated cross-level actor models: Goomba, Bob-omb, Small Bully, Spindrift, Scuttlebug, Fly Guy,
  Snufit, Swoop, Piranha Plant, Small Penguin, Butterfly and Bubbly Tree. These use a 128 KiB
  runtime geo-layout pool and a free model slot; the geo traversal globals are saved/restored around
  model parsing. Cross-level models are explicitly marked experimental.
- Fish School uses the level's existing fish model and `bhvFishSpawner` (small five-fish blue school).
  It is unavailable unless that model is already loaded and the camera is over water.
- **Remove Spawned** tracks created objects by pointer, behavior and graph-node UID. Removing a fish
  spawner also signals its fish children to delete themselves.
- Requests from the UI and hotkey are queued, then processed on the game input thread. Spawn attempts
  stop while preserving 32 free slots in the 1,024-slot object pool.
- Per-frame UI snapshots use atomics so the menu does not scan or read the live object pool directly.

### Limits and validation
- Multiplication only handles the curated independent actors; it does not clone arbitrary behaviors,
  bosses or scripted/path-dependent objects.
- Runtime geo allocation, custom model-slot use and cross-level actor behavior have only been
  syntax-checked in WSL so far. A Windows build and in-game verification are still required.
- Validate spawn placement, behavior/model compatibility, fish-school removal, level-transition
  cleanup and object-pool safety in the Windows build. Adjust the catalog only for cases that work
  reliably.
- Review fixes (2026-10-23): C linkage for `random_*`/`mark_obj_for_deletion` (would have been
  LNK2019), `parentObj = self` for spawns, canonical model slot preference (required for Bob-omb's
  respawner), direct fish-child deletion.

### Later (optional)
- Save/load a level's spawn set as a preset (JSON in `photomode_spawns/`).

## Phase 5 — Render-level (fork libultraship) (deferred)
- Fog color/range override and global light tint in the Fast3D interpreter.
- Later: post-process shaders, deterministic frame-sequence export (ffmpeg).

## Deferred / not chosen yet
- Slow-mo/fast-forward, doc overlays (letterbox/captions/camcorder), repeatable takes (fixed RNG seed + reload).
