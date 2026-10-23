#pragma once

#include <stdbool.h>
#include "types.h"
#include "macros.h"

// Mario presence while photo mode is active.
typedef enum {
    // Mario is parked far above the level so objects behave as if nobody is there.
    PHOTO_MODE_PRESENCE_ABSENT,
    // Mario is invisible/intangible but follows the camera, so objects react to the observer.
    PHOTO_MODE_PRESENCE_GHOST,
} PhotoModePresence;

// Hooks called from game code.
extern_s bool PhotoMode_IsActive(void);
extern_s bool PhotoMode_IsEnteringLevel(void);
extern_s bool PhotoMode_UpdateMario(void);
extern_s void PhotoMode_ProcessInput(void);
extern_s s32 PhotoMode_OnSetCurrentLevel(s32 levelNum);
extern_s bool PhotoMode_GetRoomProbePos(f32* x, f32* y, f32* z);
extern_s bool PhotoMode_GetFov(f32* fov);
// True if the object's visibility category is hidden by the photo mode filters.
extern_s bool PhotoMode_ShouldHideObject(struct Object* obj);
// Replaces the background (skybox) display list. Returns true if *gfx should be used instead of the
// level's own background.
extern_s bool PhotoMode_RenderSkybox(void** gfx);
// Multiplies the perspective projection by the photo mode stretch (call after loading it).
extern_s void PhotoMode_ApplyProjectionStretch(void);
// Scales an object's draw scale in place (per-category scale). Returns true if it changed.
extern_s bool PhotoMode_ScaleObject(struct Object* obj, f32 scale[3]);

#ifdef __cplusplus
#include <vector>

struct PhotoModeLevel {
    s16 levelId;
    const char* name;
    std::vector<const char*> acts;
};

// UI-facing API.
const std::vector<PhotoModeLevel>& PhotoMode_GetLevels();
void PhotoMode_RequestLevel(s16 levelId, s32 actNum);
void PhotoMode_RequestHere();
void PhotoMode_Exit();
bool PhotoMode_CanEnterHere();
const char* PhotoMode_GetStatusText();

// Free camera pose. Angles are radians: yaw 0 = +Z (increasing toward +X), pitch up is positive,
// roll is the screen rotation. fov is the vertical field of view in degrees.
struct PhotoModePose {
    f32 pos[3];
    f32 yaw;
    f32 pitch;
    f32 roll;
    f32 fov;
};
PhotoModePose PhotoMode_GetPose();

enum class PhotoModeCameraMode {
    Free,
    // Circles the target (or a fixed point) while always looking at it.
    Orbit,
    // Flies freely but always aims at the target, optionally carried along with it.
    LockOn,
};
PhotoModeCameraMode PhotoMode_GetCameraMode();
// Returns false if the mode needs a target and none is under the crosshair.
bool PhotoMode_SetCameraMode(PhotoModeCameraMode mode);
// Picks the visible object nearest the center of the view. Returns false if none.
bool PhotoMode_PickTarget();
void PhotoMode_ClearTarget();
bool PhotoMode_GetTarget(f32 out[3], const char** label);
bool PhotoMode_GetOrbitCenter(f32 out[3]);
// Moves the free camera to the pose and stops any momentum.
void PhotoMode_SetPose(const PhotoModePose& pose);
// Dolly zoom ("vertigo"): locks the focus point (target, or Dolly.Distance ahead) and adjusts
// the FOV as the camera moves so the focus keeps its size on screen.
bool PhotoMode_IsDollyZoom();
void PhotoMode_SetDollyZoom(bool enabled);
#endif
