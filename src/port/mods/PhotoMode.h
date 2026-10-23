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
// Moves the free camera to the pose and stops any momentum.
void PhotoMode_SetPose(const PhotoModePose& pose);
#endif
