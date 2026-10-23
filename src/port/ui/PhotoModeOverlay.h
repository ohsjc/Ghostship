#pragma once

#include <ship/window/gui/GuiWindow.h>

// Always-on, invisible window that handles Photo Mode hotkeys and draws the camera track preview,
// keyframe markers and playback countdown on top of the game view.
class PhotoModeOverlay : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void UpdateElement() override {
    }
    void DrawElement() override {
    }
    void Draw() override;
};
