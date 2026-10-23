#include "GhostshipMenu.h"
#include "GhostshipGui.hpp"
#include "port/mods/PhotoMode.h"
#include "port/mods/PhotoModeFilters.h"
#include "port/mods/PhotoModeTracks.h"
#include "port/mods/PhotoModeVisuals.h"

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>

namespace GhostshipGui {

extern std::shared_ptr<GhostshipMenu> mGhostshipMenu;
using namespace UIWidgets;

#define PHOTO_CVAR(name) CVAR_ENHANCEMENT("PhotoMode." name)
#define TRACK_CVAR(name) CVAR_ENHANCEMENT("PhotoMode.Tracks." name)

static const std::unordered_map<int32_t, const char*> presenceOptions = {
    { PHOTO_MODE_PRESENCE_ABSENT, "Absent (enemies ignore you)" },
    { PHOTO_MODE_PRESENCE_GHOST, "Ghost (enemies notice you)" },
};

static int32_t sSelectedLevel = 0;
static int32_t sSelectedAct = 0;

static const std::unordered_map<int32_t, const char*> easingOptions = {
    { (int32_t)PhotoModeTrackEasing::Linear, "Linear (constant pace)" },
    { (int32_t)PhotoModeTrackEasing::EaseStartEnd, "Ease in/out of the whole track" },
    { (int32_t)PhotoModeTrackEasing::EaseEachKeyframe, "Ease at every keyframe" },
};

static void DrawTrackControls(WidgetInfo& info) {
    bool canPlay = PhotoModeTracks_CanPlay();
    bool playing = PhotoModeTracks_IsPlaying();
    if (!PhotoMode_IsActive()) {
        ImGui::TextWrapped("Enter photo mode to record and play camera tracks.");
    }

    ImGui::BeginDisabled(!PhotoMode_IsActive());
    if (ImGui::Button("Add Keyframe Here (K)")) {
        PhotoModeTracks_Stop();
        PhotoModeTracks_AddKeyframe(PhotoMode_GetPose());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canPlay && !playing);
    if (ImGui::Button(playing ? "Stop (P)" : "Play (P)")) {
        PhotoModeTracks_TogglePlay();
    }
    ImGui::EndDisabled();

    float duration = PhotoModeTracks_GetDuration();
    if (playing) {
        float cd = PhotoModeTracks_CountdownRemaining();
        if (cd > 0.0f) {
            ImGui::Text("Starting in %.1fs", cd);
        } else {
            ImGui::Text("Playing %.1f / %.1fs", PhotoModeTracks_GetTime(), duration);
        }
    } else {
        ImGui::Text("Track length: %.1fs", duration);
    }
}

static void DrawKeyframeList(WidgetInfo& info) {
    auto& keys = PhotoModeTracks_Keyframes();
    if (keys.empty()) {
        ImGui::TextWrapped("No keyframes yet. Fly to a spot and press K.");
        return;
    }

    int selected = PhotoModeTracks_GetSelected();
    int deleteIndex = -1;
    int moveIndex = -1;
    int moveDir = 0;
    constexpr float kRadToDeg = 57.2957795f;
    constexpr float kDegToRad = 1.0f / kRadToDeg;

    ImGui::BeginChild("##keyframes", ImVec2(0, 360), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)keys.size(); i++) {
        ImGui::PushID(i);
        PhotoModeKeyframe& k = keys[i];
        bool isSel = i == selected;

        char label[32];
        snprintf(label, sizeof(label), "Keyframe %d", i + 1);
        if (ImGui::Selectable(label, isSel, ImGuiSelectableFlags_AllowOverlap, ImVec2(110, 0))) {
            PhotoModeTracks_JumpTo(i);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click to move the camera here.");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Set")) {
            PhotoModeTracks_Stop();
            k.pose = PhotoMode_GetPose();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Replace with the current camera.");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Up")) {
            moveIndex = i;
            moveDir = -1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Down")) {
            moveIndex = i;
            moveDir = 1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            deleteIndex = i;
        }

        if (isSel) {
            ImGui::Indent();
            ImGui::PushItemWidth(-1);
            ImGui::DragFloat3("##pos", k.pose.pos, 5.0f, -30000.0f, 30000.0f, "%.0f");
            float angles[3] = { k.pose.yaw * kRadToDeg, k.pose.pitch * kRadToDeg, k.pose.roll * kRadToDeg };
            ImGui::TextUnformatted("Yaw / Pitch / Roll");
            if (ImGui::DragFloat3("##ang", angles, 0.5f, -360.0f, 360.0f, "%.1f")) {
                k.pose.yaw = angles[0] * kDegToRad;
                k.pose.pitch = std::clamp(angles[1], -89.0f, 89.0f) * kDegToRad;
                k.pose.roll = angles[2] * kDegToRad;
            }
            ImGui::TextUnformatted("FOV");
            ImGui::SliderFloat("##fov", &k.pose.fov, 1.0f, 170.0f, "%.0f");
            if (i + 1 < (int)keys.size()) {
                ImGui::TextUnformatted("Seconds to next keyframe");
                ImGui::DragFloat("##dur", &k.duration, 0.05f, 0.05f, 120.0f, "%.2f s");
            }
            ImGui::PopItemWidth();
            ImGui::Unindent();
        } else if (i + 1 < (int)keys.size()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%.1fs", k.duration);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (moveIndex >= 0) {
        PhotoModeTracks_MoveKeyframe(moveIndex, moveDir);
    }
    if (deleteIndex >= 0) {
        PhotoModeTracks_DeleteKeyframe(deleteIndex);
    }
    if (ImGui::Button("Clear All Keyframes")) {
        PhotoModeTracks_Clear();
    }
}

static void DrawTrackFiles(WidgetInfo& info) {
    static char name[64] = "my_track";
    static std::string message;
    static std::vector<std::string> files;
    static bool listed = false;
    if (!listed) {
        files = PhotoModeTracks_ListSaved();
        listed = true;
    }

    ImGui::PushItemWidth(-1);
    ImGui::InputText("##trackname", name, sizeof(name));
    ImGui::PopItemWidth();
    if (ImGui::Button("Save")) {
        PhotoModeTracks_Save(name, &message);
        files = PhotoModeTracks_ListSaved();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        PhotoModeTracks_Load(name, &message);
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        files = PhotoModeTracks_ListSaved();
    }
    if (!message.empty()) {
        ImGui::TextWrapped("%s", message.c_str());
    }

    if (!files.empty()) {
        ImGui::BeginChild("##trackfiles", ImVec2(0, 140), ImGuiChildFlags_Borders);
        for (const auto& file : files) {
            if (ImGui::Selectable(file.c_str(), file == name)) {
                snprintf(name, sizeof(name), "%s", file.c_str());
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                PhotoModeTracks_Load(name, &message);
            }
        }
        ImGui::EndChild();
    }
    ImGui::TextDisabled("Folder: %s", PhotoModeTracks_GetDirectory().c_str());
}

static void DrawCameraModeControls(WidgetInfo& info) {
    static const char* names[] = { "Free", "Orbit (O)", "Lock-on (U)" };
    static std::string message;
    PhotoModeCameraMode current = PhotoMode_GetCameraMode();
    ImGui::BeginDisabled(!PhotoMode_IsActive());
    for (int i = 0; i < 3; i++) {
        if (i > 0) {
            ImGui::SameLine();
        }
        bool isCurrent = (int)current == i;
        if (isCurrent) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(names[i])) {
            PhotoModeTracks_Stop();
            message = PhotoMode_SetCameraMode((PhotoModeCameraMode)i) ? "" : "Nothing under the crosshair to lock on to.";
        }
        if (isCurrent) {
            ImGui::PopStyleColor();
        }
    }
    if (ImGui::Button("Pick Target Under Crosshair")) {
        message = PhotoMode_PickTarget() ? "" : "Nothing under the crosshair.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Target (Y)")) {
        PhotoMode_ClearTarget();
    }
    ImGui::EndDisabled();

    float tp[3];
    const char* label = nullptr;
    if (PhotoMode_GetTarget(tp, &label)) {
        ImGui::Text("Target: %s at (%.0f, %.0f, %.0f)", label, tp[0], tp[1], tp[2]);
    } else {
        ImGui::TextDisabled("No target");
    }
    if (!message.empty()) {
        ImGui::TextWrapped("%s", message.c_str());
    }
}

static bool ColorCVar(const char* label, const char* cvar, int def) {
    int packed = CVarGetInteger(cvar, def);
    float col[3] = { ((packed >> 16) & 0xFF) / 255.0f, ((packed >> 8) & 0xFF) / 255.0f, (packed & 0xFF) / 255.0f };
    if (!ImGui::ColorEdit3(label, col)) {
        return false;
    }
    int r = (int)(col[0] * 255.0f + 0.5f), g = (int)(col[1] * 255.0f + 0.5f), b = (int)(col[2] * 255.0f + 0.5f);
    CVarSetInteger(cvar, (r << 16) | (g << 8) | b);
    return true;
}

static void FloatCVar(const char* label, const char* cvar, float def, float min, float max, const char* fmt,
                      ImGuiSliderFlags flags = 0) {
    float v = CVarGetFloat(cvar, def);
    if (ImGui::SliderFloat(label, &v, min, max, fmt, flags)) {
        CVarSetFloat(cvar, v);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        CVarSetFloat(cvar, def);
    }
}

static void DrawSkyboxControls(WidgetInfo& info) {
    static const char* modes[] = { "Level default", "Built-in skybox", "Solid color", "Gradient", "Custom image" };
    static const char* builtins[] = { "Ocean (Bob-omb Battlefield)", "Flaming (Bowser)", "Underwater City",
                                      "Below Clouds", "Snow Mountains", "Desert", "Haunted", "Green",
                                      "Above Clouds", "Purple" };
    static std::vector<std::string> images;
    static bool listed = false;
    if (!listed) {
        images = PhotoModeVisuals_ListSkyboxImages();
        listed = true;
    }

    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    int mode = CVarGetInteger(PHOTO_CVAR("Sky.Mode"), 0);
    if (ImGui::Combo("Sky", &mode, modes, IM_ARRAYSIZE(modes))) {
        CVarSetInteger(PHOTO_CVAR("Sky.Mode"), mode);
    }
    switch ((PhotoModeSkyMode)mode) {
        case PhotoModeSkyMode::BuiltIn: {
            int idx = CVarGetInteger(PHOTO_CVAR("Sky.BuiltIn"), 0);
            if (ImGui::Combo("Skybox", &idx, builtins, IM_ARRAYSIZE(builtins))) {
                CVarSetInteger(PHOTO_CVAR("Sky.BuiltIn"), idx);
            }
            break;
        }
        case PhotoModeSkyMode::Solid:
            ColorCVar("Color", PHOTO_CVAR("Sky.Color"), 0x87CEEB);
            break;
        case PhotoModeSkyMode::Gradient:
            ColorCVar("Top", PHOTO_CVAR("Sky.TopColor"), 0x1E3C78);
            ColorCVar("Bottom", PHOTO_CVAR("Sky.BottomColor"), 0xF0A060);
            break;
        case PhotoModeSkyMode::CustomImage: {
            std::string current = CVarGetString(PHOTO_CVAR("Sky.Image"), "");
            if (ImGui::BeginCombo("Image", current.empty() ? "(none)" : current.c_str())) {
                for (const auto& name : images) {
                    if (ImGui::Selectable(name.c_str(), name == current)) {
                        CVarSetString(PHOTO_CVAR("Sky.Image"), name.c_str());
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Refresh List")) {
                images = PhotoModeVisuals_ListSkyboxImages();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload Image")) {
                PhotoModeVisuals_ReloadSkyboxImage();
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy Folder Path")) {
                ImGui::SetClipboardText(PhotoModeVisuals_GetSkyboxDirectory().c_str());
            }
            ImGui::TextWrapped("Folder: %s", PhotoModeVisuals_GetSkyboxDirectory().c_str());
            if (images.empty()) {
                ImGui::TextWrapped("No images yet: drop .png/.jpg/.bmp/.tga files in the folder and press Refresh.");
            }
            if (!PhotoModeVisuals_GetSkyboxStatus().empty()) {
                ImGui::TextDisabled("%s", PhotoModeVisuals_GetSkyboxStatus().c_str());
            }

            ImGui::SeparatorText("Adjust Image");
            FloatCVar("Rotation", PHOTO_CVAR("Sky.Rotation"), 0.0f, -180.0f, 180.0f, "%.0f deg");
            FloatCVar("Spin", PHOTO_CVAR("Sky.Spin"), 0.0f, -90.0f, 90.0f, "%.1f deg/s");
            bool fill = CVarGetInteger(PHOTO_CVAR("Sky.FillSphere"), 1) != 0;
            if (ImGui::Checkbox("Fill Whole Sky", &fill)) {
                CVarSetInteger(PHOTO_CVAR("Sky.FillSphere"), fill);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("On: the image covers the sky top to bottom (best for 2:1 panoramas).\n"
                                  "Off: the image keeps its proportions as a band around the horizon.");
            }
            int repeats = CVarGetInteger(PHOTO_CVAR("Sky.Repeats"), 1);
            if (ImGui::SliderInt("Repeats Around", &repeats, 1, 16)) {
                CVarSetInteger(PHOTO_CVAR("Sky.Repeats"), repeats);
            }
            ImGui::BeginDisabled(fill);
            FloatCVar("Horizon Height", PHOTO_CVAR("Sky.Horizon"), 0.0f, -90.0f, 90.0f, "%.0f deg");
            FloatCVar("Vertical Scale", PHOTO_CVAR("Sky.VScale"), 1.0f, 0.1f, 5.0f, "%.2f");
            ImGui::EndDisabled();
            bool mirror = CVarGetInteger(PHOTO_CVAR("Sky.Mirror"), 0) != 0;
            if (ImGui::Checkbox("Mirror", &mirror)) {
                CVarSetInteger(PHOTO_CVAR("Sky.Mirror"), mirror);
            }
            ColorCVar("Tint", PHOTO_CVAR("Sky.Tint"), 0xFFFFFF);
            FloatCVar("Brightness", PHOTO_CVAR("Sky.Brightness"), 1.0f, 0.0f, 1.0f, "%.2f");
            ColorCVar("Background", PHOTO_CVAR("Sky.FillColor"), 0x000000);
            if (ImGui::Button("Reset Image Adjustments")) {
                for (const char* name : { PHOTO_CVAR("Sky.Rotation"), PHOTO_CVAR("Sky.Spin"), PHOTO_CVAR("Sky.FillSphere"),
                                          PHOTO_CVAR("Sky.Repeats"), PHOTO_CVAR("Sky.Horizon"), PHOTO_CVAR("Sky.VScale"),
                                          PHOTO_CVAR("Sky.Mirror"), PHOTO_CVAR("Sky.Tint"), PHOTO_CVAR("Sky.Brightness"),
                                          PHOTO_CVAR("Sky.FillColor") }) {
                    CVarClear(name);
                }
            }
            ImGui::TextDisabled("Right-click a slider to reset it.");
            break;
        }
        default:
            ImGui::TextDisabled("The level's own sky.");
            break;
    }
    ImGui::PopItemWidth();
}

static void DrawProjectionControls(WidgetInfo& info) {
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    FloatCVar("Field of View", PHOTO_CVAR("Fov"), 45.0f, 1.0f, 170.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    FloatCVar("Stretch X", PHOTO_CVAR("Stretch.X"), 1.0f, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    FloatCVar("Stretch Y", PHOTO_CVAR("Stretch.Y"), 1.0f, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button("Reset Projection")) {
        CVarSetFloat(PHOTO_CVAR("Fov"), 45.0f);
        CVarClear(PHOTO_CVAR("Stretch.X"));
        CVarClear(PHOTO_CVAR("Stretch.Y"));
    }

    ImGui::SeparatorText("Dolly Zoom");
    bool dolly = PhotoMode_IsDollyZoom();
    ImGui::BeginDisabled(!PhotoMode_IsActive());
    if (ImGui::Checkbox("Dolly Zoom (J)", &dolly)) {
        PhotoMode_SetDollyZoom(dolly);
    }
    ImGui::EndDisabled();
    FloatCVar("Focus Distance", PHOTO_CVAR("Dolly.Distance"), 1000.0f, 100.0f, 10000.0f, "%.0f",
              ImGuiSliderFlags_Logarithmic);
    ImGui::TextWrapped("Locks the target (or the point Focus Distance ahead). Move toward or away from it and the "
                       "FOV changes so it keeps its size while the background warps. L or J turns it off.");
    ImGui::PopItemWidth();
}

static void DrawScaleControls(WidgetInfo& info) {
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    constexpr ImGuiSliderFlags log = ImGuiSliderFlags_Logarithmic;
    FloatCVar("All Objects", PHOTO_CVAR("Scale.All"), 1.0f, 0.05f, 20.0f, "%.2fx", log);
    FloatCVar("Width (X)", PHOTO_CVAR("Scale.X"), 1.0f, 0.05f, 20.0f, "%.2fx", log);
    FloatCVar("Height (Y)", PHOTO_CVAR("Scale.Y"), 1.0f, 0.05f, 20.0f, "%.2fx", log);
    FloatCVar("Depth (Z)", PHOTO_CVAR("Scale.Z"), 1.0f, 0.05f, 20.0f, "%.2fx", log);
    ImGui::SeparatorText("Per Category");
    for (const auto& category : PhotoModeFilters_GetCategories()) {
        FloatCVar(category.label, PhotoModeFilters_GetScaleCVar(category.category).c_str(), 1.0f, 0.05f, 20.0f,
                  "%.2fx", log);
    }
    if (ImGui::Button("Reset All Scales")) {
        for (const char* name :
             { PHOTO_CVAR("Scale.All"), PHOTO_CVAR("Scale.X"), PHOTO_CVAR("Scale.Y"), PHOTO_CVAR("Scale.Z") }) {
            CVarClear(name);
        }
        for (const auto& category : PhotoModeFilters_GetCategories()) {
            CVarClear(PhotoModeFilters_GetScaleCVar(category.category).c_str());
        }
    }
    ImGui::TextWrapped("Only the drawing is scaled; collision and behavior are unchanged. Level geometry is not "
                       "affected. Right-click a slider to reset it.");
    ImGui::PopItemWidth();
}

static void DrawLevelPicker(WidgetInfo& info) {
    const auto& levels = PhotoMode_GetLevels();
    if (levels.empty()) {
        return;
    }

    std::vector<const char*> levelNames;
    levelNames.reserve(levels.size());
    for (const auto& level : levels) {
        levelNames.push_back(level.name);
    }
    if (sSelectedLevel < 0 || sSelectedLevel >= (int32_t)levels.size()) {
        sSelectedLevel = 0;
    }
    if (Combobox("Level", &sSelectedLevel, levelNames, ComboboxOptions().Color(THEME_COLOR))) {
        sSelectedAct = 0;
    }

    const auto& acts = levels[sSelectedLevel].acts;
    if (!acts.empty()) {
        if (sSelectedAct < 0 || sSelectedAct >= (int32_t)acts.size()) {
            sSelectedAct = 0;
        }
        Combobox("Act", &sSelectedAct, acts, ComboboxOptions().Color(THEME_COLOR));
    }
}

void GhostshipMenu::AddMenuPhotoMode() {
    AddMenuEntry("Photo Mode", CVAR_SETTING("Menu.PhotoModeSidebarSection"));

    AddSidebarEntry("Photo Mode", "General", 2);
    WidgetPath path = { "Photo Mode", "General", SECTION_COLUMN_1 };

    AddWidget(path, "Status", WIDGET_TEXT).PreFunc([](WidgetInfo& info) {
        static std::string status;
        status = std::string("Status: ") + PhotoMode_GetStatusText();
        info.name = status;
    });

    AddWidget(path, "Level", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "PhotoModeLevelPicker", WIDGET_CUSTOM).CustomFunction(DrawLevelPicker);
    AddWidget(path, "Go to Level in Photo Mode", WIDGET_BUTTON)
        .Options(ButtonOptions()
                     .Tooltip("Loads the selected level and starts photo mode at Mario's spawn point. "
                              "From the title screen, press Start to begin loading.")
                     .Color(THEME_COLOR))
        .Callback([](WidgetInfo& info) {
            const auto& levels = PhotoMode_GetLevels();
            if (sSelectedLevel >= 0 && sSelectedLevel < (int32_t)levels.size()) {
                PhotoMode_RequestLevel(levels[sSelectedLevel].levelId, sSelectedAct + 1);
            }
        });
    AddWidget(path, "Enter Photo Mode Here", WIDGET_BUTTON)
        .Options(ButtonOptions().Tooltip("Starts photo mode from the current camera position.").Color(THEME_COLOR))
        .Callback([](WidgetInfo& info) { PhotoMode_RequestHere(); })
        .PreFunc([](WidgetInfo& info) { info.options->Disabled(PhotoMode_IsActive()); });
    AddWidget(path, "Exit Photo Mode", WIDGET_BUTTON)
        .Options(ButtonOptions().Tooltip("Returns control to Mario below the camera.").Color(Colors::Orange))
        .Callback([](WidgetInfo& info) { PhotoMode_Exit(); });

    AddWidget(path, "Scene", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Mario Presence", WIDGET_CVAR_COMBOBOX)
        .CVar(PHOTO_CVAR("Presence"))
        .Options(ComboboxOptions()
                     .ComboMap(presenceOptions)
                     .DefaultIndex(PHOTO_MODE_PRESENCE_ABSENT)
                     .Tooltip("Absent: Mario is removed so enemies go about their business.\n"
                              "Ghost: an invisible, intangible Mario follows the camera so enemies react to it."));
    AddWidget(path, "Hide HUD", WIDGET_CVAR_CHECKBOX)
        .CVar(PHOTO_CVAR("HideHud"))
        .Options(CheckboxOptions().DefaultValue(true));
    AddWidget(path, "Ignore Draw Distance", WIDGET_CVAR_CHECKBOX)
        .CVar(PHOTO_CVAR("IgnoreDrawDistance"))
        .Options(CheckboxOptions().DefaultValue(true).Tooltip("Draws objects no matter how far they are from Mario."));

    path.column = SECTION_COLUMN_2;
    AddWidget(path, "Camera", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "Field of View", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(PHOTO_CVAR("Fov"))
        .Options(FloatSliderOptions().Min(1.0f).Max(170.0f).Step(1.0f).DefaultValue(45.0f).Format("%.0f"));
    AddWidget(path, "Move Speed", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(PHOTO_CVAR("MoveSpeed"))
        .Options(FloatSliderOptions().Min(1.0f).Max(200.0f).Step(1.0f).DefaultValue(40.0f).Format("%.0f"));
    AddWidget(path, "Look Speed", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(PHOTO_CVAR("LookSpeed"))
        .Options(FloatSliderOptions().Min(0.5f).Max(10.0f).Step(0.1f).DefaultValue(3.0f).Format("%.1f"));
    AddWidget(path, "Smoothing", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(PHOTO_CVAR("Smoothing"))
        .Options(FloatSliderOptions()
                     .Min(0.0f)
                     .Max(0.95f)
                     .Step(0.05f)
                     .DefaultValue(0.6f)
                     .Format("%.2f")
                     .Tooltip("Higher values give slower, more cinematic camera motion."));
    AddWidget(path, "Mouse Look", WIDGET_CVAR_CHECKBOX)
        .CVar(PHOTO_CVAR("MouseLook"))
        .Options(CheckboxOptions().DefaultValue(true).Tooltip("Captures the mouse while photo mode is active. "
                                                               "Press F2 to release it."));
    AddWidget(path, "Mouse Sensitivity", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(PHOTO_CVAR("MouseSensitivity"))
        .Options(FloatSliderOptions().Min(0.01f).Max(1.0f).Step(0.01f).DefaultValue(0.15f).Format("%.2f"));
    AddWidget(path, "Invert Y", WIDGET_CVAR_CHECKBOX).CVar(PHOTO_CVAR("InvertY"));

    AddWidget(path, "Camera Mode", WIDGET_SEPARATOR_TEXT);
    AddWidget(path, "PhotoModeCameraMode", WIDGET_CUSTOM).CustomFunction(DrawCameraModeControls);
    AddWidget(path, "Orbit Auto-Rotate", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(PHOTO_CVAR("Orbit.AutoSpeed"))
        .Options(FloatSliderOptions()
                     .Min(-90.0f)
                     .Max(90.0f)
                     .Step(1.0f)
                     .DefaultValue(0.0f)
                     .Format("%.0f deg/s")
                     .Tooltip("Slowly circles the orbit target on its own. 0 = off."));
    AddWidget(path, "Lock-on Follows Target", WIDGET_CVAR_CHECKBOX)
        .CVar(PHOTO_CVAR("LockOn.Follow"))
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Carries the camera along as the target moves, like a tracking shot. Off: the camera stays put and "
            "only turns to watch."));
    AddWidget(path, "Lock-on Aim Smoothing", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(PHOTO_CVAR("LockOn.AimSmoothing"))
        .Options(FloatSliderOptions().Min(0.0f).Max(0.95f).Step(0.05f).DefaultValue(0.5f).Format("%.2f"));

    AddWidget(path, "Controls", WIDGET_SEPARATOR_TEXT);
    AddWidget(path,
              "Left stick: move\n"
              "Right stick / C buttons / mouse: look\n"
              "A / B: up / down\n"
              "Hold R: fast, hold Z: slow\n"
              "D-pad up / down: zoom (FOV)\n"
              "D-pad left / right: roll\n"
              "L: reset FOV, roll and level the camera\n"
              "Start: freeze / unfreeze time\n"
              "Keyboard O: orbit, U: lock-on, Y: clear target\n"
              "Orbit: look / left stick X circle, stick Y distance, A/B height",
              WIDGET_TEXT);

    AddSidebarEntry("Photo Mode", "Camera Tracks", 2);
    WidgetPath trackPath = { "Photo Mode", "Camera Tracks", SECTION_COLUMN_1 };
    AddWidget(trackPath, "Playback", WIDGET_SEPARATOR_TEXT);
    AddWidget(trackPath, "PhotoModeTrackControls", WIDGET_CUSTOM).CustomFunction(DrawTrackControls);
    AddWidget(trackPath, "Loop", WIDGET_CVAR_CHECKBOX).CVar(TRACK_CVAR("Loop"));
    AddWidget(trackPath, "Easing", WIDGET_CVAR_COMBOBOX)
        .CVar(TRACK_CVAR("Easing"))
        .Options(ComboboxOptions()
                     .ComboMap(easingOptions)
                     .DefaultIndex((int32_t)PhotoModeTrackEasing::EaseStartEnd));
    AddWidget(trackPath, "Playback Speed", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(TRACK_CVAR("Speed"))
        .Options(FloatSliderOptions().Min(0.05f).Max(4.0f).Step(0.05f).DefaultValue(1.0f).Format("%.2fx"));
    AddWidget(trackPath, "Countdown", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(TRACK_CVAR("Countdown"))
        .Options(FloatSliderOptions()
                     .Min(0.0f)
                     .Max(10.0f)
                     .Step(0.5f)
                     .DefaultValue(3.0f)
                     .Format("%.1fs")
                     .Tooltip("Holds on the first keyframe before moving, so you can start recording in OBS."));
    AddWidget(trackPath, "New Keyframe Duration", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(TRACK_CVAR("DefaultDuration"))
        .Options(FloatSliderOptions().Min(0.25f).Max(30.0f).Step(0.25f).DefaultValue(3.0f).Format("%.2fs"));
    AddWidget(trackPath, "Show Path Preview", WIDGET_CVAR_CHECKBOX)
        .CVar(TRACK_CVAR("ShowPath"))
        .Options(CheckboxOptions().DefaultValue(true).Tooltip("Hidden automatically during playback."));
    AddWidget(trackPath, "Show Track Hint Bar", WIDGET_CVAR_CHECKBOX)
        .CVar(TRACK_CVAR("ShowHud"))
        .Options(CheckboxOptions().DefaultValue(true));

    AddWidget(trackPath, "Save / Load", WIDGET_SEPARATOR_TEXT);
    AddWidget(trackPath, "PhotoModeTrackFiles", WIDGET_CUSTOM).CustomFunction(DrawTrackFiles);

    AddWidget(trackPath, "Hotkeys", WIDGET_SEPARATOR_TEXT);
    AddWidget(trackPath,
              "K: add keyframe after the selected one\n"
              "P: play / stop\n"
              "[ / ]: previous / next keyframe\n"
              "Delete: delete selected keyframe\n"
              "Start still freezes time during playback.",
              WIDGET_TEXT);

    trackPath.column = SECTION_COLUMN_2;
    AddWidget(trackPath, "Keyframes", WIDGET_SEPARATOR_TEXT);
    AddWidget(trackPath, "PhotoModeKeyframeList", WIDGET_CUSTOM).CustomFunction(DrawKeyframeList);

    AddSidebarEntry("Photo Mode", "Visibility", 2);
    WidgetPath visPath = { "Photo Mode", "Visibility", SECTION_COLUMN_1 };

    AddWidget(visPath, "Presets", WIDGET_SEPARATOR_TEXT);
    struct PresetButton {
        const char* name;
        PhotoModeFilterPreset preset;
        const char* tooltip;
    };
    static const PresetButton presets[] = {
        { "Show Everything", PhotoModeFilterPreset::ShowAll, "Shows every object category." },
        { "Nature Documentary", PhotoModeFilterPreset::NatureDocumentary,
          "Hides the 'video game' clutter (coins, stars, power-ups, boxes, switches, signs, sparkles) "
          "so the Mushroom Kingdom's wildlife can be observed in its natural habitat." },
        { "Creatures Only", PhotoModeFilterPreset::CreaturesOnly,
          "Shows enemies, bosses, wildlife and characters with the scenery and platforms they live on." },
        { "Empty Landscape", PhotoModeFilterPreset::EmptyLandscape,
          "Hides everything except trees, scenery, platforms and natural effects." },
    };
    for (const auto& preset : presets) {
        PhotoModeFilterPreset value = preset.preset;
        AddWidget(visPath, preset.name, WIDGET_BUTTON)
            .Options(ButtonOptions().Tooltip(preset.tooltip).Color(THEME_COLOR))
            .Callback([value](WidgetInfo& info) { PhotoModeFilters_ApplyPreset(value); });
    }
    AddWidget(visPath,
              "Filters apply only while photo mode is active.\n"
              "Hidden objects keep moving and making sounds;\n"
              "they just aren't drawn.",
              WIDGET_TEXT);

    visPath.column = SECTION_COLUMN_2;
    AddWidget(visPath, "Show in Photo Mode", WIDGET_SEPARATOR_TEXT);
    for (const auto& category : PhotoModeFilters_GetCategories()) {
        AddWidget(visPath, category.label, WIDGET_CVAR_CHECKBOX)
            .CVar(category.cvarName)
            .Options(CheckboxOptions().DefaultValue(true).Tooltip(category.tooltip));
    }

    AddSidebarEntry("Photo Mode", "Visual FX", 2);
    WidgetPath fxPath = { "Photo Mode", "Visual FX", SECTION_COLUMN_1 };
    AddWidget(fxPath, "Skybox", WIDGET_SEPARATOR_TEXT);
    AddWidget(fxPath, "PhotoModeSkybox", WIDGET_CUSTOM).CustomFunction(DrawSkyboxControls);
    fxPath.column = SECTION_COLUMN_2;
    AddWidget(fxPath, "Projection", WIDGET_SEPARATOR_TEXT);
    AddWidget(fxPath, "PhotoModeProjection", WIDGET_CUSTOM).CustomFunction(DrawProjectionControls);
    AddWidget(fxPath, "Object Scale", WIDGET_SEPARATOR_TEXT);
    AddWidget(fxPath, "PhotoModeScale", WIDGET_CUSTOM).CustomFunction(DrawScaleControls);
}

} // namespace GhostshipGui
