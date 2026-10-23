#pragma once

#include <string>
#include <vector>

// Photo Mode visual effects: skybox override (built-in, solid, gradient or custom image files),
// projection stretch and per-category object scale. The C hooks are declared in PhotoMode.h.

enum class PhotoModeSkyMode {
    Default,
    BuiltIn,
    Solid,
    Gradient,
    CustomImage,
};

// Custom skybox images live in <game folder>/photomode_skyboxes/.
std::string PhotoModeVisuals_GetSkyboxDirectory();
std::vector<std::string> PhotoModeVisuals_ListSkyboxImages();
// Forces the selected image to be reloaded from disk (after editing the file).
void PhotoModeVisuals_ReloadSkyboxImage();
// Human-readable status of the custom image ("Loaded foo.png (4096x2048)" or an error).
const std::string& PhotoModeVisuals_GetSkyboxStatus();

void PhotoModeVisuals_GetStretch(float* x, float* y);
