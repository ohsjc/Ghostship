#include "PhotoMode.h"
#include "BetterLevelSelect.h"

#include <cmath>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

#include "port/ShipCompat.h"
#include "port/ShipInit.hpp"
#include "port/events/Events.h"
#include "port/ui/cvar_prefixes.h"

extern "C" {
#include "sm64.h"
#include "game/area.h"
#include "game/camera.h"
#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/level_update.h"
#include "game/interaction.h"
#include "game/mario.h"
#include "game/object_list_processor.h"
#include "game/save_file.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "menu/title_screen.h"
}

#define PHOTO_CVAR(name) CVAR_ENHANCEMENT("PhotoMode." name)

namespace {

enum class State {
    Off,
    // A level was chosen; waiting for a safe moment to start the warp.
    LevelRequested,
    // Warp issued; waiting for the target level to be set up.
    Warping,
    // Target level is loading; activate on its first normal gameplay frame.
    Loading,
    // "Enter here" was chosen; activate on the next normal gameplay frame.
    HereRequested,
    Active,
};

struct InputSnapshot {
    f32 stickX, stickY;
    f32 stick2X, stick2Y;
    u16 buttonDown;
    u16 buttonPressed;
};

struct PhotoModeState {
    State state = State::Off;
    s16 targetLevel = 0;
    s32 targetAct = 1;

    Vec3f pos = { 0, 0, 0 };
    f32 yaw = 0;   // radians, SM64 convention (0 = +Z, increasing toward +X)
    f32 pitch = 0; // radians, positive looks up
    Vec3f vel = { 0, 0, 0 };
    f32 yawVel = 0;
    f32 pitchVel = 0;

    bool frozen = false;
    // Switching levels from inside photo mode: keep Mario hidden and the controller captured
    // until the new level is ready.
    bool carryOver = false;
    Vec3f savedMarioPos = { 0, 0, 0 };

    bool savedMouseCaptured = false;
    bool savedAutoCapture = false;

    InputSnapshot input = {};
};

PhotoModeState sPhoto;
std::vector<PhotoModeLevel> sLevels;

constexpr f32 kPi = 3.14159265358979323846f;
constexpr f32 kMaxPitch = 89.0f * kPi / 180.0f;
constexpr f32 kEyeHeight = 160.0f;
// Far above the middle of the level: out of range of every object, but objects that turn
// toward Mario face the level centre instead of wandering off an edge.
constexpr f32 kParkedMarioY = 20000.0f;

f32 DegToRad(f32 deg) {
    return deg * kPi / 180.0f;
}

f32 ApproachSmoothed(f32 current, f32 target, f32 smoothing) {
    return current + (target - current) * (1.0f - smoothing);
}

bool ControllingScene() {
    return sPhoto.state == State::Active ||
           (sPhoto.carryOver && (sPhoto.state == State::LevelRequested || sPhoto.state == State::Warping ||
                                 sPhoto.state == State::Loading));
}

// The free camera still belongs to the level it was flying around in.
bool CameraOwned() {
    return sPhoto.state == State::Active ||
           (sPhoto.carryOver && (sPhoto.state == State::LevelRequested || sPhoto.state == State::Warping));
}

bool InNormalGameplay() {
    return gCurrentArea != NULL && gMarioObject != NULL && gMarioState != NULL && gMarioState->marioObj != NULL &&
           level_is_in_normal_play_mode() && gCurrDemoInput == NULL && gCurrCreditsEntry == NULL;
}

PhotoModePresence GetPresence() {
    return (PhotoModePresence)CVarGetInteger(PHOTO_CVAR("Presence"), PHOTO_MODE_PRESENCE_ABSENT);
}

void SetFrozen(bool frozen) {
    if (frozen) {
        gTimeStopState |= TIME_STOP_ENABLED | TIME_STOP_ALL_OBJECTS;
    } else if (sPhoto.frozen) {
        gTimeStopState &= ~(TIME_STOP_ENABLED | TIME_STOP_ALL_OBJECTS | TIME_STOP_ACTIVE);
    }
    sPhoto.frozen = frozen;
}

void SetupMouseCapture(bool saveCurrent) {
    auto window = ShipCompat::GetWindow();
    if (window == nullptr) {
        return;
    }
    if (saveCurrent) {
        sPhoto.savedMouseCaptured = window->IsMouseCaptured();
        sPhoto.savedAutoCapture = window->ShouldAutoCaptureMouse();
    }

    if (CVarGetInteger(PHOTO_CVAR("MouseLook"), 1)) {
        // Auto-capture makes the mouse lock again whenever the menu is closed.
        window->SetAutoCaptureMouse(true);
        if (!window->GetGui()->GetMenuOrMenubarVisible()) {
            window->SetMouseCapture(true);
        }
    }
    // Discard movement that accumulated before photo mode started.
    window->GetMouseDelta();
}

void RestoreMouseCapture() {
    auto window = ShipCompat::GetWindow();
    if (window == nullptr) {
        return;
    }
    window->SetAutoCaptureMouse(sPhoto.savedAutoCapture);
    window->SetMouseCapture(sPhoto.savedMouseCaptured);
}

void Activate(bool fromSpawn) {
    struct MarioState* m = gMarioState;
    bool carryOver = sPhoto.carryOver;
    sPhoto.carryOver = false;

    vec3f_copy(sPhoto.savedMarioPos, m->pos);

    if (fromSpawn) {
        sPhoto.pos[0] = m->pos[0];
        sPhoto.pos[1] = m->pos[1] + kEyeHeight;
        sPhoto.pos[2] = m->pos[2];
        sPhoto.yaw = (f32)m->faceAngle[1] * (2.0f * kPi / 65536.0f);
        sPhoto.pitch = 0.0f;
    } else {
        vec3f_copy(sPhoto.pos, gLakituState.pos);
        f32 dx = gLakituState.focus[0] - gLakituState.pos[0];
        f32 dy = gLakituState.focus[1] - gLakituState.pos[1];
        f32 dz = gLakituState.focus[2] - gLakituState.pos[2];
        sPhoto.yaw = atan2f(dx, dz);
        sPhoto.pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
    }

    vec3f_set(sPhoto.vel, 0, 0, 0);
    sPhoto.yawVel = 0;
    sPhoto.pitchVel = 0;
    sPhoto.frozen = false;
    sPhoto.input = {};

    if (m->heldObj != NULL) {
        mario_drop_held_object(m);
    }

    SetupMouseCapture(!carryOver);
    sPhoto.state = State::Active;
}

void UpdateFreeCamera(struct Camera* c) {
    const InputSnapshot& in = sPhoto.input;

    f32 smoothing = CVarGetFloat(PHOTO_CVAR("Smoothing"), 0.6f);
    smoothing = smoothing < 0.0f ? 0.0f : (smoothing > 0.95f ? 0.95f : smoothing);

    // Look: right stick, C-buttons and (when captured) mouse.
    f32 lookSpeed = DegToRad(CVarGetFloat(PHOTO_CVAR("LookSpeed"), 3.0f));
    f32 lookX = in.stick2X / 64.0f;
    f32 lookY = in.stick2Y / 64.0f;
    if (in.buttonDown & L_CBUTTONS) {
        lookX -= 1.0f;
    }
    if (in.buttonDown & R_CBUTTONS) {
        lookX += 1.0f;
    }
    if (in.buttonDown & U_CBUTTONS) {
        lookY += 1.0f;
    }
    if (in.buttonDown & D_CBUTTONS) {
        lookY -= 1.0f;
    }
    if (CVarGetInteger(PHOTO_CVAR("InvertY"), 0)) {
        lookY = -lookY;
    }

    sPhoto.yawVel = ApproachSmoothed(sPhoto.yawVel, -lookX * lookSpeed, smoothing);
    sPhoto.pitchVel = ApproachSmoothed(sPhoto.pitchVel, lookY * lookSpeed, smoothing);
    sPhoto.yaw += sPhoto.yawVel;
    sPhoto.pitch += sPhoto.pitchVel;

    auto window = ShipCompat::GetWindow();
    if (window != nullptr) {
        Ship::Coords delta = window->GetMouseDelta();
        if (window->IsMouseCaptured() && CVarGetInteger(PHOTO_CVAR("MouseLook"), 1)) {
            f32 sensitivity = DegToRad(CVarGetFloat(PHOTO_CVAR("MouseSensitivity"), 0.15f));
            f32 mouseY = CVarGetInteger(PHOTO_CVAR("InvertY"), 0) ? (f32)delta.y : -(f32)delta.y;
            sPhoto.yaw -= delta.x * sensitivity;
            sPhoto.pitch += mouseY * sensitivity;
        }
    }

    sPhoto.pitch = sPhoto.pitch > kMaxPitch ? kMaxPitch : (sPhoto.pitch < -kMaxPitch ? -kMaxPitch : sPhoto.pitch);
    sPhoto.yaw = fmodf(sPhoto.yaw, 2.0f * kPi);

    // Movement: left stick flies horizontally like a drone, A/B rise and descend.
    f32 speed = CVarGetFloat(PHOTO_CVAR("MoveSpeed"), 40.0f);
    if (in.buttonDown & R_TRIG) {
        speed *= 4.0f;
    }
    if (in.buttonDown & Z_TRIG) {
        speed *= 0.25f;
    }

    f32 fwdX = sinf(sPhoto.yaw);
    f32 fwdZ = cosf(sPhoto.yaw);
    // Right of the view direction (SM64 is right-handed with Y up).
    f32 rightX = -fwdZ;
    f32 rightZ = fwdX;

    f32 moveF = in.stickY / 64.0f;
    f32 moveR = in.stickX / 64.0f;
    f32 moveU = 0.0f;
    if (in.buttonDown & A_BUTTON) {
        moveU += 1.0f;
    }
    if (in.buttonDown & B_BUTTON) {
        moveU -= 1.0f;
    }

    Vec3f target = {
        (fwdX * moveF + rightX * moveR) * speed,
        moveU * speed,
        (fwdZ * moveF + rightZ * moveR) * speed,
    };
    for (int i = 0; i < 3; i++) {
        sPhoto.vel[i] = ApproachSmoothed(sPhoto.vel[i], target[i], smoothing);
        sPhoto.pos[i] += sPhoto.vel[i];
    }

    // Keep the camera inside the range the engine can handle.
    for (int i = 0; i < 3; i++) {
        sPhoto.pos[i] = sPhoto.pos[i] > 30000.0f ? 30000.0f : (sPhoto.pos[i] < -30000.0f ? -30000.0f : sPhoto.pos[i]);
    }

    // FOV on the D-pad, L resets FOV and levels the camera.
    f32 fov = CVarGetFloat(PHOTO_CVAR("Fov"), 45.0f);
    if (in.buttonDown & U_JPAD) {
        fov -= 1.0f;
    }
    if (in.buttonDown & D_JPAD) {
        fov += 1.0f;
    }
    if (in.buttonPressed & L_TRIG) {
        fov = 45.0f;
        sPhoto.pitch = 0.0f;
    }
    fov = fov < 5.0f ? 5.0f : (fov > 120.0f ? 120.0f : fov);
    CVarSetFloat(PHOTO_CVAR("Fov"), fov);

    if ((in.buttonPressed & START_BUTTON) && sPhoto.state == State::Active) {
        SetFrozen(!sPhoto.frozen);
    }
    if (sPhoto.frozen) {
        // Dialogs and cutscenes may clear time stop; keep the scene paused.
        SetFrozen(true);
    }

    f32 cosPitch = cosf(sPhoto.pitch);
    Vec3f focus = {
        sPhoto.pos[0] + sinf(sPhoto.yaw) * cosPitch * 100.0f,
        sPhoto.pos[1] + sinf(sPhoto.pitch) * 100.0f,
        sPhoto.pos[2] + cosf(sPhoto.yaw) * cosPitch * 100.0f,
    };

    vec3f_copy(c->pos, sPhoto.pos);
    vec3f_copy(c->focus, focus);
    c->yaw = calculate_yaw(focus, sPhoto.pos);

    vec3f_copy(gLakituState.pos, sPhoto.pos);
    vec3f_copy(gLakituState.curPos, sPhoto.pos);
    vec3f_copy(gLakituState.goalPos, sPhoto.pos);
    vec3f_copy(gLakituState.focus, focus);
    vec3f_copy(gLakituState.curFocus, focus);
    vec3f_copy(gLakituState.goalFocus, focus);
    gLakituState.yaw = c->yaw;
    gLakituState.nextYaw = c->yaw;
    gLakituState.roll = 0;
    gLakituState.keyDanceRoll = 0;
    vec3s_set(gLakituState.shakeMagnitude, 0, 0, 0);
}

void Deactivate() {
    bool wasActive = sPhoto.state == State::Active;
    if (ControllingScene()) {
        SetFrozen(false);
        RestoreMouseCapture();
    }
    sPhoto.carryOver = false;
    sPhoto.state = State::Off;

    if (!wasActive) {
        return;
    }

    if (!InNormalGameplay()) {
        return;
    }

    // Drop Mario below the camera if there is ground there, otherwise put him back where he was.
    struct MarioState* m = gMarioState;
    struct Surface* floor = NULL;
    f32 floorY = find_floor(sPhoto.pos[0], sPhoto.pos[1], sPhoto.pos[2], &floor);
    if (floor != NULL) {
        vec3f_set(m->pos, sPhoto.pos[0], floorY, sPhoto.pos[2]);
    } else {
        vec3f_copy(m->pos, sPhoto.savedMarioPos);
    }
    vec3f_set(m->vel, 0, 0, 0);
    m->forwardVel = 0.0f;
    m->faceAngle[1] = (s16)(sPhoto.yaw * (65536.0f / (2.0f * kPi)));
    m->marioObj->oIntangibleTimer = 0;
    m->marioObj->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
    set_mario_action(m, ACT_FREEFALL, 0);
    reset_camera(gCurrentArea->camera);
}

s32 PhotoMode_IntroUpdate(UNUSED s16 arg, UNUSED s32 unused) {
    if (gCurrSaveFileNum < 1 || gCurrSaveFileNum > NUM_SAVE_FILES) {
        gCurrSaveFileNum = 1;
    }
    CALL_EVENT(OnGameFileLoad, gCurrSaveFileNum);
    sPhoto.state = State::Warping;
    return sPhoto.targetLevel;
}

void Init() {
    // Runs after BetterLevelSelect (normal priority) has swapped in its own intro handler.
    REGISTER_LISTENER(LevelScriptCallLoop, EVENT_PRIORITY_LOW, [](IEvent* event) {
        if (sPhoto.state != State::LevelRequested) {
            return;
        }
        LevelScriptCallLoop* ev = (LevelScriptCallLoop*)event;
        if (*ev->func != lvl_intro_update && *ev->func != BetterLevelSelect_UpdateMenu) {
            return;
        }
        if (*ev->arg == LVL_INTRO_REGULAR || *ev->arg == LVL_INTRO_GAME_OVER || *ev->arg == LVL_INTRO_LEVEL_SELECT) {
            *ev->func = PhotoMode_IntroUpdate;
        }
    });

    REGISTER_LISTENER(GameFrameUpdate, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        switch (sPhoto.state) {
            case State::LevelRequested:
                if (InNormalGameplay()) {
                    sPhoto.state = State::Warping;
                    warp_to_level_default_spawn(sPhoto.targetLevel);
                }
                break;
            case State::Loading:
                if (InNormalGameplay()) {
                    Activate(true);
                }
                break;
            case State::HereRequested:
                if (PhotoMode_CanEnterHere()) {
                    Activate(false);
                }
                break;
            default:
                break;
        }
    });

    REGISTER_LISTENER(CameraUpdate, EVENT_PRIORITY_HIGH, [](IEvent* event) {
        // Also keeps the camera in place while switching levels from inside photo mode.
        if (!CameraOwned()) {
            return;
        }
        CameraUpdate* ev = (CameraUpdate*)event;
        UpdateFreeCamera(ev->c);
    });

    REGISTER_LISTENER(EntityDistanceRender, EVENT_PRIORITY_HIGH, [](IEvent* event) {
        if (sPhoto.state == State::Active && CVarGetInteger(PHOTO_CVAR("IgnoreDrawDistance"), 1)) {
            *((EntityDistanceRender*)event)->visible = true;
        }
    });

    // Spawners (goomba triplets, chain chomps, pokeys...) normally wait for Mario to come close.
    REGISTER_LISTENER(EntityDistanceLoad, EVENT_PRIORITY_HIGH, [](IEvent* event) {
        if (sPhoto.state == State::Active && GetPresence() == PHOTO_MODE_PRESENCE_ABSENT) {
            *((EntityDistanceLoad*)event)->visible = true;
        }
    });

    REGISTER_LISTENER(GameReset, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        if (ControllingScene()) {
            RestoreMouseCapture();
        }
        sPhoto.frozen = false;
        sPhoto.carryOver = false;
        sPhoto.state = State::Off;
    });
}

} // namespace

// ---- Hooks called from game code ----

extern "C" bool PhotoMode_IsActive(void) {
    return ControllingScene();
}

extern "C" bool PhotoMode_IsEnteringLevel(void) {
    return sPhoto.state == State::Warping || sPhoto.state == State::Loading;
}

extern "C" bool PhotoMode_UpdateMario(void) {
    if (!ControllingScene()) {
        return false;
    }

    struct MarioState* m = gMarioState;
    // While a new level is loading, Mario stays at his spawn point so the camera can start there.
    if (CameraOwned()) {
        if (GetPresence() == PHOTO_MODE_PRESENCE_GHOST) {
            vec3f_set(m->pos, sPhoto.pos[0], sPhoto.pos[1] - kEyeHeight, sPhoto.pos[2]);
        } else {
            vec3f_set(m->pos, 0.0f, kParkedMarioY, 0.0f);
        }
    }
    vec3f_set(m->vel, 0, 0, 0);
    m->forwardVel = 0.0f;
    m->particleFlags = 0;
    m->marioObj->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
    m->marioObj->oIntangibleTimer = -1;
    m->marioObj->oInteractStatus = 0;
    return true;
}

extern "C" void PhotoMode_ProcessInput(void) {
    if (!ControllingScene()) {
        return;
    }

    // Take the controller for the camera so Mario, the pause menu and dialogs never see it.
    sPhoto.input.stickX = gPlayer1Controller->stickX;
    sPhoto.input.stickY = gPlayer1Controller->stickY;
    sPhoto.input.stick2X = gPlayer1Controller->stick2X;
    sPhoto.input.stick2Y = gPlayer1Controller->stick2Y;
    sPhoto.input.buttonDown = gPlayer1Controller->buttonDown;
    sPhoto.input.buttonPressed = gPlayer1Controller->buttonPressed;

    struct Controller* controllers[] = { gPlayer1Controller, gPlayer3Controller };
    for (struct Controller* controller : controllers) {
        controller->rawStickX = 0;
        controller->rawStickY = 0;
        controller->stickX = 0;
        controller->stickY = 0;
        controller->stickMag = 0;
        controller->rawStick2X = 0;
        controller->rawStick2Y = 0;
        controller->stick2X = 0;
        controller->stick2Y = 0;
        controller->stick2Mag = 0;
        controller->buttonDown = 0;
        controller->buttonPressed = 0;
    }
}

extern "C" s32 PhotoMode_OnSetCurrentLevel(s32 levelNum) {
    if (sPhoto.state != State::Warping || levelNum != sPhoto.targetLevel) {
        return 0;
    }
    sPhoto.state = State::Loading;
    return sPhoto.targetAct;
}

extern "C" bool PhotoMode_GetRoomProbePos(f32* x, f32* y, f32* z) {
    if (!CameraOwned()) {
        return false;
    }
    *x = sPhoto.pos[0];
    *y = sPhoto.pos[1];
    *z = sPhoto.pos[2];
    return true;
}

extern "C" bool PhotoMode_GetFov(f32* fov) {
    if (!CameraOwned()) {
        return false;
    }
    *fov = CVarGetFloat(PHOTO_CVAR("Fov"), 45.0f);
    return true;
}

// ---- UI API ----

const std::vector<PhotoModeLevel>& PhotoMode_GetLevels() {
    if (sLevels.empty()) {
        size_t count = 0;
        const LevelSelectEntry* entries = BetterLevelSelect_GetEntries(&count);
        for (size_t i = 0; i < count; i++) {
            if (entries[i].levelId == LEVEL_ENDING) {
                continue;
            }
            sLevels.push_back({ (s16)entries[i].levelId, entries[i].englishName, entries[i].actsEn });
        }
    }
    return sLevels;
}

void PhotoMode_RequestLevel(s16 levelId, s32 actNum) {
    if (sPhoto.state == State::Warping || sPhoto.state == State::Loading) {
        return;
    }
    if (sPhoto.state == State::Active) {
        SetFrozen(false);
        sPhoto.carryOver = true;
    }
    sPhoto.targetLevel = levelId;
    sPhoto.targetAct = actNum < 1 ? 1 : actNum;
    sPhoto.state = State::LevelRequested;
}

void PhotoMode_RequestHere() {
    if (sPhoto.state == State::Off) {
        sPhoto.state = State::HereRequested;
    }
}

void PhotoMode_Exit() {
    Deactivate();
}

bool PhotoMode_CanEnterHere() {
    if (!InNormalGameplay() || (gTimeStopState & TIME_STOP_ENABLED) || get_dialog_id() >= 0) {
        return false;
    }
    u32 action = gMarioState->action;
    return (action & ACT_GROUP_MASK) != ACT_GROUP_CUTSCENE && !(action & ACT_FLAG_RIDING_SHELL) &&
           action != ACT_IN_CANNON;
}

const char* PhotoMode_GetStatusText() {
    switch (sPhoto.state) {
        case State::Off:
            return "Off";
        case State::LevelRequested:
            return "Waiting to load level (press Start on the title screen if needed)";
        case State::Warping:
        case State::Loading:
            return "Loading level...";
        case State::HereRequested:
            return "Waiting for Mario to be free to enter photo mode...";
        case State::Active:
            return sPhoto.frozen ? "Active (time frozen)" : "Active";
    }
    return "";
}

static RegisterShipInitFunc initFunc(Init);
