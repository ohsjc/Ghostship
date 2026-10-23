#pragma once

#include <string>
#include <vector>

struct Object;

// Object visibility filters for photo mode. Objects are grouped by behavior; hidden
// objects keep running (AI, sounds, collision) and are only skipped when drawing.
enum class PhotoModeCategory {
    Enemies,
    Bosses,
    Wildlife,
    Characters,
    Coins,
    StarsKeys,
    PowerUps,
    Boxes,
    Switches,
    Signs,
    Doors,
    Scenery,
    Platforms,
    Hazards,
    NaturalEffects,
    GameEffects,
    Other,
    Count,
};

struct PhotoModeCategoryInfo {
    PhotoModeCategory category;
    const char* label;
    const char* cvarName; // Full CVar name; 1 (default) shows the category, 0 hides it.
    const char* tooltip;
};

enum class PhotoModeFilterPreset {
    ShowAll,
    NatureDocumentary,
    CreaturesOnly,
    EmptyLandscape,
};

const std::vector<PhotoModeCategoryInfo>& PhotoModeFilters_GetCategories();
bool PhotoModeFilters_IsHidden(const Object* obj);
PhotoModeCategory PhotoModeFilters_GetCategory(const Object* obj);
const std::string& PhotoModeFilters_GetDensityCVar(PhotoModeCategory cat);
// Per-category draw scale multiplier (CVar PhotoMode.Scale.<Category>, default 1).
const std::string& PhotoModeFilters_GetScaleCVar(PhotoModeCategory cat);
float PhotoModeFilters_GetScale(const Object* obj);
void PhotoModeFilters_ApplyPreset(PhotoModeFilterPreset preset);
