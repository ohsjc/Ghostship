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
#endif
