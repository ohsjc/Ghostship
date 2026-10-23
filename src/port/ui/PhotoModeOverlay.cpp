#include "PhotoModeOverlay.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <libultraship/bridge/consolevariablebridge.h>

#include "port/mods/PhotoMode.h"
#include "port/mods/PhotoModeTracks.h"
#include "port/ui/cvar_prefixes.h"

#define TRACK_CVAR(name) CVAR_ENHANCEMENT("PhotoMode.Tracks." name)

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Projects world points with the current free camera, matching the game's view and roll.
struct Projector {
    float pos[3];
    float fwd[3], right[3], up[3];
    float cosRoll, sinRoll;
    float tanHalfFov, aspect;
    ImVec2 min, size;

    Projector(const PhotoModePose& pose, ImVec2 rectMin, ImVec2 rectSize) : min(rectMin), size(rectSize) {
        for (int i = 0; i < 3; i++) {
            pos[i] = pose.pos[i];
        }
        float cp = cosf(pose.pitch);
        fwd[0] = sinf(pose.yaw) * cp;
        fwd[1] = sinf(pose.pitch);
        fwd[2] = cosf(pose.yaw) * cp;
        // Right-handed, Y up: looking down +Z, right is -X.
        float hx = -cosf(pose.yaw), hz = sinf(pose.yaw);
        right[0] = hx;
        right[1] = 0.0f;
        right[2] = hz;
        // up = right x fwd
        up[0] = right[1] * fwd[2] - right[2] * fwd[1];
        up[1] = right[2] * fwd[0] - right[0] * fwd[2];
        up[2] = right[0] * fwd[1] - right[1] * fwd[0];
        cosRoll = cosf(pose.roll);
        sinRoll = sinf(pose.roll);
        tanHalfFov = tanf(pose.fov * 0.5f * kPi / 180.0f);
        aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
    }

    // Returns false when the point is behind (or too close to) the camera.
    bool Project(const float world[3], ImVec2* out) const {
        float d[3] = { world[0] - pos[0], world[1] - pos[1], world[2] - pos[2] };
        float z = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2];
        if (z < 10.0f) {
            return false;
        }
        float x = d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
        float y = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
        float rx = x * cosRoll - y * sinRoll;
        float ry = x * sinRoll + y * cosRoll;
        float ndcX = rx / (z * tanHalfFov * aspect);
        float ndcY = ry / (z * tanHalfFov);
        if (fabsf(ndcX) > 4.0f || fabsf(ndcY) > 4.0f) {
            return false;
        }
        out->x = min.x + (ndcX + 1.0f) * 0.5f * size.x;
        out->y = min.y + (1.0f - ndcY) * 0.5f * size.y;
        return true;
    }
};

void HandleHotkeys() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }
    auto& keys = PhotoModeTracks_Keyframes();
    if (ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        PhotoModeTracks_Stop();
        PhotoModeTracks_AddKeyframe(PhotoMode_GetPose());
    }
    if (ImGui::IsKeyPressed(ImGuiKey_P, false)) {
        PhotoModeTracks_TogglePlay();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false) && !keys.empty()) {
        PhotoModeTracks_JumpTo(std::max(PhotoModeTracks_GetSelected() - 1, 0));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false) && !keys.empty()) {
        PhotoModeTracks_JumpTo(std::min(PhotoModeTracks_GetSelected() + 1, (int)keys.size() - 1));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        PhotoModeTracks_DeleteKeyframe(PhotoModeTracks_GetSelected());
    }
    if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        PhotoModeTracks_Stop();
        bool on = PhotoMode_GetCameraMode() == PhotoModeCameraMode::Orbit;
        PhotoMode_SetCameraMode(on ? PhotoModeCameraMode::Free : PhotoModeCameraMode::Orbit);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_U, false)) {
        PhotoModeTracks_Stop();
        if (PhotoMode_GetCameraMode() == PhotoModeCameraMode::LockOn) {
            PhotoMode_SetCameraMode(PhotoModeCameraMode::Free);
        } else {
            // Pressing U again while aiming at something else switches targets.
            PhotoMode_PickTarget();
            PhotoMode_SetCameraMode(PhotoModeCameraMode::LockOn);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
        PhotoMode_ClearTarget();
    }
}

void DrawPath(ImDrawList* draw, const Projector& proj) {
    auto samples = PhotoModeTracks_SamplePath(24);
    const ImU32 lineColor = IM_COL32(255, 220, 80, 200);
    for (size_t i = 0; i + 1 < samples.size(); i++) {
        ImVec2 a, b;
        if (proj.Project(samples[i].pos, &a) && proj.Project(samples[i + 1].pos, &b)) {
            draw->AddLine(a, b, lineColor, 2.0f);
        }
    }

    const auto& keys = PhotoModeTracks_Keyframes();
    int selected = PhotoModeTracks_GetSelected();
    for (size_t i = 0; i < keys.size(); i++) {
        const PhotoModePose& k = keys[i].pose;
        ImVec2 p;
        if (!proj.Project(k.pos, &p)) {
            continue;
        }
        bool isSel = (int)i == selected;
        ImU32 color = isSel ? IM_COL32(80, 220, 255, 255) : IM_COL32(255, 160, 40, 255);
        draw->AddCircleFilled(p, isSel ? 7.0f : 5.0f, color);

        // Short stick showing where the keyframe looks.
        float cp = cosf(k.pitch);
        float tip[3] = { k.pos[0] + sinf(k.yaw) * cp * 150.0f, k.pos[1] + sinf(k.pitch) * 150.0f,
                         k.pos[2] + cosf(k.yaw) * cp * 150.0f };
        ImVec2 t;
        if (proj.Project(tip, &t)) {
            draw->AddLine(p, t, color, 2.0f);
        }
        draw->AddText(ImVec2(p.x + 8.0f, p.y - 8.0f), color, std::to_string(i + 1).c_str());
    }
}

} // namespace

void PhotoModeOverlay::Draw() {
    if (!PhotoMode_IsActive()) {
        return;
    }
    HandleHotkeys();

    ImGuiWindow* game = ImGui::FindWindowByName("Main Game");
    ImVec2 rectMin, rectSize;
    ImDrawList* draw;
    if (game != nullptr) {
        rectMin = game->InnerRect.Min;
        rectSize = ImVec2(game->InnerRect.GetWidth(), game->InnerRect.GetHeight());
        draw = ImGui::GetForegroundDrawList(game->Viewport);
    } else {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        rectMin = vp->Pos;
        rectSize = vp->Size;
        draw = ImGui::GetForegroundDrawList(vp);
    }
    if (rectSize.x <= 0.0f || rectSize.y <= 0.0f) {
        return;
    }
    draw->PushClipRect(rectMin, ImVec2(rectMin.x + rectSize.x, rectMin.y + rectSize.y), true);

    bool playing = PhotoModeTracks_IsPlaying();
    float countdown = PhotoModeTracks_CountdownRemaining();

    if (!playing && CVarGetInteger(TRACK_CVAR("ShowPath"), 1)) {
        DrawPath(draw, Projector(PhotoMode_GetPose(), rectMin, rectSize));
    }

    if (!playing && CVarGetInteger(TRACK_CVAR("ShowHud"), 1)) {
        Projector proj(PhotoMode_GetPose(), rectMin, rectSize);
        float tp[3];
        const char* label = nullptr;
        ImVec2 p;
        if (PhotoMode_GetTarget(tp, &label) && proj.Project(tp, &p)) {
            const ImU32 col = IM_COL32(120, 255, 120, 230);
            float h = 14.0f, l = 6.0f;
            // Corner brackets around the target.
            for (int sx = -1; sx <= 1; sx += 2) {
                for (int sy = -1; sy <= 1; sy += 2) {
                    ImVec2 c(p.x + sx * h, p.y + sy * h);
                    draw->AddLine(c, ImVec2(c.x - sx * l, c.y), col, 2.0f);
                    draw->AddLine(c, ImVec2(c.x, c.y - sy * l), col, 2.0f);
                }
            }
            draw->AddText(ImVec2(p.x + h + 4.0f, p.y - h), col, label);
        } else if (PhotoMode_GetOrbitCenter(tp) && proj.Project(tp, &p)) {
            draw->AddCircle(p, 8.0f, IM_COL32(120, 255, 120, 200), 0, 2.0f);
        }
        // Small crosshair so you can see what O / U will pick.
        ImVec2 c(rectMin.x + rectSize.x * 0.5f, rectMin.y + rectSize.y * 0.5f);
        const ImU32 xc = IM_COL32(255, 255, 255, 120);
        draw->AddLine(ImVec2(c.x - 6, c.y), ImVec2(c.x + 6, c.y), xc, 1.0f);
        draw->AddLine(ImVec2(c.x, c.y - 6), ImVec2(c.x, c.y + 6), xc, 1.0f);
    }

    if (countdown > 0.0f) {
        std::string text = std::to_string((int)ceilf(countdown));
        float fontSize = ImGui::GetFontSize() * 6.0f;
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
        ImVec2 at(rectMin.x + (rectSize.x - ts.x) * 0.5f, rectMin.y + (rectSize.y - ts.y) * 0.5f);
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(at.x + 3, at.y + 3), IM_COL32(0, 0, 0, 160), text.c_str());
        draw->AddText(ImGui::GetFont(), fontSize, at, IM_COL32(255, 255, 255, 230), text.c_str());
    } else if (!playing && CVarGetInteger(TRACK_CVAR("ShowHud"), 1)) {
        const auto& keys = PhotoModeTracks_Keyframes();
        std::string text = "Track: " + std::to_string(keys.size()) + " keyframes";
        if (PhotoModeTracks_GetSelected() >= 0) {
            text += " (selected " + std::to_string(PhotoModeTracks_GetSelected() + 1) + ")";
        }
        static const char* modeNames[] = { "Free", "Orbit", "Lock-on" };
        text = std::string("Camera: ") + modeNames[(int)PhotoMode_GetCameraMode()] + "   " + text;
        text += "   K add  P play  [ ] select  Del delete  O orbit  U lock-on  Y clear target";
        ImVec2 at(rectMin.x + 10.0f, rectMin.y + rectSize.y - ImGui::GetFontSize() - 10.0f);
        draw->AddText(ImVec2(at.x + 1, at.y + 1), IM_COL32(0, 0, 0, 200), text.c_str());
        draw->AddText(at, IM_COL32(255, 255, 255, 220), text.c_str());
    }

    draw->PopClipRect();
}
