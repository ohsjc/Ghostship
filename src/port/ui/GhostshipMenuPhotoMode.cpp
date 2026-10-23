#include "GhostshipMenu.h"
#include "GhostshipGui.hpp"
#include "port/mods/PhotoMode.h"
#include "port/mods/PhotoModeFilters.h"

namespace GhostshipGui {

extern std::shared_ptr<GhostshipMenu> mGhostshipMenu;
using namespace UIWidgets;

#define PHOTO_CVAR(name) CVAR_ENHANCEMENT("PhotoMode." name)

static const std::unordered_map<int32_t, const char*> presenceOptions = {
    { PHOTO_MODE_PRESENCE_ABSENT, "Absent (enemies ignore you)" },
    { PHOTO_MODE_PRESENCE_GHOST, "Ghost (enemies notice you)" },
};

static int32_t sSelectedLevel = 0;
static int32_t sSelectedAct = 0;

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
        .Options(FloatSliderOptions().Min(5.0f).Max(120.0f).Step(1.0f).DefaultValue(45.0f).Format("%.0f"));
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

    AddWidget(path, "Controls", WIDGET_SEPARATOR_TEXT);
    AddWidget(path,
              "Left stick: move\n"
              "Right stick / C buttons / mouse: look\n"
              "A / B: up / down\n"
              "Hold R: fast, hold Z: slow\n"
              "D-pad up / down: zoom (FOV)\n"
              "L: reset FOV and level the camera\n"
              "Start: freeze / unfreeze time",
              WIDGET_TEXT);

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
}

} // namespace GhostshipGui
