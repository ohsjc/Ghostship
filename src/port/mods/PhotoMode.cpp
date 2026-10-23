#include "PhotoMode.h"
#include "BetterLevelSelect.h"
#include "PhotoModeFilters.h"
#include "PhotoModeTracks.h"

#include <cmath>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

#include "port/ShipCompat.h"
#include "port/ShipInit.hpp"
#include "port/events/Events.h"
#include "port/ui/cvar_prefixes.h"

#include "sm64.h"
#include "game/area.h"
#include "game/camera.h"
#include "game/game_init.h"
#include "game/ingame_menu.h"
#include "game/level_update.h"
#include "game/object_list_processor.h"
#include "game/save_file.h"
#include "engine/math_util.h"
#include "menu/title_screen.h"

// These headers declare their functions without extern_s.
extern "C" {
#include "game/interaction.h"
#include "game/mario.h"
#include "engine/surface_collision.h"
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
    f32 roll = 0;  // radians, screen rotation

    PhotoModeCameraMode camMode = PhotoModeCameraMode::Free;
    struct Object* target = nullptr;
    const BehaviorScript* targetBehavior = nullptr;
    Vec3f lastTargetPos = { 0, 0, 0 };
    Vec3f orbitCenter = { 0, 0, 0 }; // used when orbiting without a target
    f32 orbitDist = 800.0f;
    f32 orbitDistVel = 0;

    // Dolly zoom: FOV follows the distance to a focus point so its apparent size stays fixed.
    bool dolly = false;
    Vec3f dollyFocus = { 0, 0, 0 };
    f32 dollyDist0 = 1000.0f;
    f32 dollyTan0 = 0.0f; // tan(fov0 / 2)
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
constexpr f32 kMinFov = 1.0f;
constexpr f32 kMaxFov = 170.0f;
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

    sPhoto.roll = 0.0f;
    sPhoto.camMode = PhotoModeCameraMode::Free;
    sPhoto.target = nullptr;
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

void WriteCamera(struct Camera* c);

bool TargetValid() {
    struct Object* t = sPhoto.target;
    return t != nullptr && (t->activeFlags & ACTIVE_FLAG_ACTIVE) && t->behavior == sPhoto.targetBehavior;
}

void TargetPoint(struct Object* obj, Vec3f out) {
    out[0] = obj->oPosX;
    out[1] = obj->oPosY + (obj->hitboxHeight > 0.0f ? obj->hitboxHeight * 0.5f : 50.0f);
    out[2] = obj->oPosZ;
}

void OrbitCenter(Vec3f out) {
    if (TargetValid()) {
        TargetPoint(sPhoto.target, out);
    } else {
        vec3f_copy(out, sPhoto.orbitCenter);
    }
}

void AimAt(const Vec3f point, f32 smoothing) {
    f32 dx = point[0] - sPhoto.pos[0];
    f32 dy = point[1] - sPhoto.pos[1];
    f32 dz = point[2] - sPhoto.pos[2];
    f32 yaw = atan2f(dx, dz);
    f32 pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
    f32 k = 1.0f - smoothing;
    sPhoto.yaw += remainderf(yaw - sPhoto.yaw, 2.0f * kPi) * k;
    sPhoto.pitch += (pitch - sPhoto.pitch) * k;
    sPhoto.pitch = sPhoto.pitch > kMaxPitch ? kMaxPitch : (sPhoto.pitch < -kMaxPitch ? -kMaxPitch : sPhoto.pitch);
    sPhoto.yawVel = 0.0f;
    sPhoto.pitchVel = 0.0f;
}

// Orbit: the look controls (and left stick X) circle the camera around the center; left stick Y
// changes the distance and A/B raise/lower the camera.
void UpdateOrbit(const InputSnapshot& in, f32 smoothing) {
    Vec3f center;
    OrbitCenter(center);

    f32 lookSpeed = DegToRad(CVarGetFloat(PHOTO_CVAR("LookSpeed"), 3.0f));
    sPhoto.yaw -= (in.stickX / 64.0f) * lookSpeed;
    sPhoto.yaw += DegToRad(CVarGetFloat(PHOTO_CVAR("Orbit.AutoSpeed"), 0.0f)) / 30.0f;
    if (in.buttonDown & A_BUTTON) {
        sPhoto.pitch -= lookSpeed * 0.5f;
    }
    if (in.buttonDown & B_BUTTON) {
        sPhoto.pitch += lookSpeed * 0.5f;
    }
    sPhoto.pitch = sPhoto.pitch > kMaxPitch ? kMaxPitch : (sPhoto.pitch < -kMaxPitch ? -kMaxPitch : sPhoto.pitch);

    f32 speed = CVarGetFloat(PHOTO_CVAR("MoveSpeed"), 40.0f);
    if (in.buttonDown & R_TRIG) {
        speed *= 4.0f;
    }
    if (in.buttonDown & Z_TRIG) {
        speed *= 0.25f;
    }
    sPhoto.orbitDistVel = ApproachSmoothed(sPhoto.orbitDistVel, -(in.stickY / 64.0f) * speed, smoothing);
    sPhoto.orbitDist += sPhoto.orbitDistVel;
    sPhoto.orbitDist = sPhoto.orbitDist < 50.0f ? 50.0f : (sPhoto.orbitDist > 20000.0f ? 20000.0f : sPhoto.orbitDist);

    f32 cosPitch = cosf(sPhoto.pitch);
    sPhoto.pos[0] = center[0] - sinf(sPhoto.yaw) * cosPitch * sPhoto.orbitDist;
    sPhoto.pos[1] = center[1] - sinf(sPhoto.pitch) * sPhoto.orbitDist;
    sPhoto.pos[2] = center[2] - cosf(sPhoto.yaw) * cosPitch * sPhoto.orbitDist;
    vec3f_set(sPhoto.vel, 0, 0, 0);
}

void UpdateFreeCamera(struct Camera* c) {
    const InputSnapshot& in = sPhoto.input;

    if ((in.buttonPressed & START_BUTTON) && sPhoto.state == State::Active) {
        SetFrozen(!sPhoto.frozen);
    }
    if (sPhoto.frozen) {
        // Dialogs and cutscenes may clear time stop; keep the scene paused.
        SetFrozen(true);
    }

    // A playing camera track drives the camera; manual controls are ignored.
    PhotoModePose trackPose;
    if (PhotoModeTracks_Advance(&trackPose)) {
        PhotoMode_SetPose(trackPose);
        WriteCamera(c);
        return;
    }

    f32 smoothing = CVarGetFloat(PHOTO_CVAR("Smoothing"), 0.6f);
    smoothing = smoothing < 0.0f ? 0.0f : (smoothing > 0.95f ? 0.95f : smoothing);

    bool hasTarget = TargetValid();
    if (!hasTarget && sPhoto.target != nullptr) {
        // The target despawned: keep orbiting where it was, or drop lock-on.
        vec3f_copy(sPhoto.orbitCenter, sPhoto.lastTargetPos);
        sPhoto.target = nullptr;
        if (sPhoto.camMode == PhotoModeCameraMode::LockOn) {
            sPhoto.camMode = PhotoModeCameraMode::Free;
        }
    }
    if (hasTarget) {
        Vec3f tp;
        TargetPoint(sPhoto.target, tp);
        if (sPhoto.camMode == PhotoModeCameraMode::LockOn && CVarGetInteger(PHOTO_CVAR("LockOn.Follow"), 1)) {
            for (int i = 0; i < 3; i++) {
                sPhoto.pos[i] += tp[i] - sPhoto.lastTargetPos[i];
            }
        }
        vec3f_copy(sPhoto.lastTargetPos, tp);
    }

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

    if (sPhoto.camMode == PhotoModeCameraMode::Orbit) {
        UpdateOrbit(in, smoothing);
    } else {
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

    }
    if (sPhoto.camMode == PhotoModeCameraMode::LockOn) {
        Vec3f tp;
        TargetPoint(sPhoto.target, tp);
        f32 aim = CVarGetFloat(PHOTO_CVAR("LockOn.AimSmoothing"), 0.5f);
        AimAt(tp, aim < 0.0f ? 0.0f : (aim > 0.95f ? 0.95f : aim));
    }

    // Keep the camera inside the range the engine can handle.
    for (int i = 0; i < 3; i++) {
        sPhoto.pos[i] = sPhoto.pos[i] > 30000.0f ? 30000.0f : (sPhoto.pos[i] < -30000.0f ? -30000.0f : sPhoto.pos[i]);
    }

    // Roll on D-pad left/right.
    f32 rollSpeed = DegToRad(1.5f);
    if (in.buttonDown & L_JPAD) {
        sPhoto.roll -= rollSpeed;
    }
    if (in.buttonDown & R_JPAD) {
        sPhoto.roll += rollSpeed;
    }
    sPhoto.roll = remainderf(sPhoto.roll, 2.0f * kPi);

    // FOV on the D-pad, L resets FOV and levels the camera.
    f32 fov = CVarGetFloat(PHOTO_CVAR("Fov"), 45.0f);
    if (sPhoto.dolly) {
        if (TargetValid()) {
            TargetPoint(sPhoto.target, sPhoto.dollyFocus);
        }
        f32 d[3] = { sPhoto.dollyFocus[0] - sPhoto.pos[0], sPhoto.dollyFocus[1] - sPhoto.pos[1],
                     sPhoto.dollyFocus[2] - sPhoto.pos[2] };
        f32 dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dist < 1.0f) {
            dist = 1.0f;
        }
        fov = 2.0f * atanf(sPhoto.dollyTan0 * sPhoto.dollyDist0 / dist) * 180.0f / kPi;
    } else {
        f32 fovStep = fov > 20.0f ? 1.0f : 0.25f;
        if (in.buttonDown & U_JPAD) {
            fov -= fovStep;
        }
        if (in.buttonDown & D_JPAD) {
            fov += fovStep;
        }
    }
    if (in.buttonPressed & L_TRIG) {
        sPhoto.dolly = false;
        fov = 45.0f;
        sPhoto.pitch = 0.0f;
        sPhoto.roll = 0.0f;
    }
    fov = fov < kMinFov ? kMinFov : (fov > kMaxFov ? kMaxFov : fov);
    CVarSetFloat(PHOTO_CVAR("Fov"), fov);

    WriteCamera(c);
}

void WriteCamera(struct Camera* c) {
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
    gLakituState.roll = (s16)(s32)(sPhoto.roll * (65536.0f / (2.0f * kPi)));
    gLakituState.keyDanceRoll = 0;
    vec3s_set(gLakituState.shakeMagnitude, 0, 0, 0);
}

void Deactivate() {
    sPhoto.dolly = false;
    sPhoto.camMode = PhotoModeCameraMode::Free;
    sPhoto.target = nullptr;
    PhotoModeTracks_Stop();
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

extern "C" bool PhotoMode_ShouldHideObject(struct Object* obj) {
    return CameraOwned() && PhotoModeFilters_IsHidden(obj);
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

PhotoModePose PhotoMode_GetPose() {
    PhotoModePose p;
    for (int i = 0; i < 3; i++) {
        p.pos[i] = sPhoto.pos[i];
    }
    p.yaw = sPhoto.yaw;
    p.pitch = sPhoto.pitch;
    p.roll = sPhoto.roll;
    p.fov = CVarGetFloat(PHOTO_CVAR("Fov"), 45.0f);
    return p;
}

void PhotoMode_SetPose(const PhotoModePose& p) {
    for (int i = 0; i < 3; i++) {
        sPhoto.pos[i] = p.pos[i];
        sPhoto.vel[i] = 0.0f;
    }
    sPhoto.yaw = p.yaw;
    sPhoto.pitch = p.pitch > kMaxPitch ? kMaxPitch : (p.pitch < -kMaxPitch ? -kMaxPitch : p.pitch);
    sPhoto.roll = p.roll;
    sPhoto.yawVel = 0.0f;
    sPhoto.pitchVel = 0.0f;
    f32 fov = p.fov < kMinFov ? kMinFov : (p.fov > kMaxFov ? kMaxFov : p.fov);
    CVarSetFloat(PHOTO_CVAR("Fov"), fov);
}

PhotoModeCameraMode PhotoMode_GetCameraMode() {
    return sPhoto.camMode;
}

bool PhotoMode_PickTarget() {
    f32 fwd[3] = { sinf(sPhoto.yaw) * cosf(sPhoto.pitch), sinf(sPhoto.pitch), cosf(sPhoto.yaw) * cosf(sPhoto.pitch) };
    const f32 maxAngleCos = cosf(DegToRad(15.0f));
    struct Object* best = nullptr;
    f32 bestScore = 0.0f;
    for (int i = 0; i < OBJECT_POOL_CAPACITY; i++) {
        struct Object* obj = &gObjectPool[i];
        if (!(obj->activeFlags & ACTIVE_FLAG_ACTIVE) || obj == gMarioObject || obj->header.gfx.sharedChild == nullptr ||
            (obj->header.gfx.node.flags & GRAPH_RENDER_INVISIBLE) || PhotoModeFilters_IsHidden(obj)) {
            continue;
        }
        Vec3f tp;
        TargetPoint(obj, tp);
        f32 d[3] = { tp[0] - sPhoto.pos[0], tp[1] - sPhoto.pos[1], tp[2] - sPhoto.pos[2] };
        f32 dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dist < 1.0f || dist > 10000.0f) {
            continue;
        }
        f32 cosAngle = (d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]) / dist;
        if (cosAngle < maxAngleCos) {
            continue;
        }
        // Prefer objects closest to the crosshair, then nearer ones.
        f32 score = cosAngle - dist * 0.000002f;
        if (best == nullptr || score > bestScore) {
            best = obj;
            bestScore = score;
        }
    }
    if (best == nullptr) {
        return false;
    }
    sPhoto.target = best;
    sPhoto.targetBehavior = best->behavior;
    TargetPoint(best, sPhoto.lastTargetPos);
    return true;
}

void PhotoMode_ClearTarget() {
    if (TargetValid()) {
        TargetPoint(sPhoto.target, sPhoto.orbitCenter);
    }
    sPhoto.target = nullptr;
    if (sPhoto.camMode == PhotoModeCameraMode::LockOn) {
        sPhoto.camMode = PhotoModeCameraMode::Free;
    }
}

bool PhotoMode_GetTarget(f32 out[3], const char** label) {
    if (!TargetValid()) {
        return false;
    }
    TargetPoint(sPhoto.target, out);
    if (label != nullptr) {
        PhotoModeCategory cat = PhotoModeFilters_GetCategory(sPhoto.target);
        *label = PhotoModeFilters_GetCategories()[(size_t)cat].label;
    }
    return true;
}

bool PhotoMode_GetOrbitCenter(f32 out[3]) {
    if (sPhoto.camMode != PhotoModeCameraMode::Orbit) {
        return false;
    }
    OrbitCenter(out);
    return true;
}

bool PhotoMode_SetCameraMode(PhotoModeCameraMode mode) {
    if (!PhotoMode_IsActive()) {
        return false;
    }
    if (mode == PhotoModeCameraMode::LockOn) {
        if (!TargetValid() && !PhotoMode_PickTarget()) {
            return false;
        }
    } else if (mode == PhotoModeCameraMode::Orbit) {
        if (!TargetValid() && !PhotoMode_PickTarget()) {
            // Nothing under the crosshair: orbit a point in front of the camera.
            f32 cosPitch = cosf(sPhoto.pitch);
            sPhoto.orbitCenter[0] = sPhoto.pos[0] + sinf(sPhoto.yaw) * cosPitch * 800.0f;
            sPhoto.orbitCenter[1] = sPhoto.pos[1] + sinf(sPhoto.pitch) * 800.0f;
            sPhoto.orbitCenter[2] = sPhoto.pos[2] + cosf(sPhoto.yaw) * cosPitch * 800.0f;
        }
        Vec3f center;
        OrbitCenter(center);
        f32 dx = center[0] - sPhoto.pos[0], dy = center[1] - sPhoto.pos[1], dz = center[2] - sPhoto.pos[2];
        f32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
        sPhoto.orbitDist = dist < 100.0f ? 100.0f : dist;
        sPhoto.orbitDistVel = 0.0f;
        AimAt(center, 0.0f);
    }
    sPhoto.camMode = mode;
    return true;
}

bool PhotoMode_IsDollyZoom() {
    return sPhoto.dolly;
}

void PhotoMode_SetDollyZoom(bool enabled) {
    sPhoto.dolly = false;
    if (!enabled || sPhoto.state != State::Active) {
        return;
    }
    if (TargetValid()) {
        TargetPoint(sPhoto.target, sPhoto.dollyFocus);
    } else {
        f32 dist = CVarGetFloat(PHOTO_CVAR("Dolly.Distance"), 1000.0f);
        f32 cosPitch = cosf(sPhoto.pitch);
        sPhoto.dollyFocus[0] = sPhoto.pos[0] + sinf(sPhoto.yaw) * cosPitch * dist;
        sPhoto.dollyFocus[1] = sPhoto.pos[1] + sinf(sPhoto.pitch) * dist;
        sPhoto.dollyFocus[2] = sPhoto.pos[2] + cosf(sPhoto.yaw) * cosPitch * dist;
    }
    f32 d[3] = { sPhoto.dollyFocus[0] - sPhoto.pos[0], sPhoto.dollyFocus[1] - sPhoto.pos[1],
                 sPhoto.dollyFocus[2] - sPhoto.pos[2] };
    f32 dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    sPhoto.dollyDist0 = dist < 1.0f ? 1.0f : dist;
    sPhoto.dollyTan0 = tanf(CVarGetFloat(PHOTO_CVAR("Fov"), 45.0f) * kPi / 360.0f);
    sPhoto.dolly = true;
}
