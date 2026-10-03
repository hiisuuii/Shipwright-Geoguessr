#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include <ship/window/gui/GuiWindow.h>

void Overlay_SetState(const nlohmann::json& state);
bool Overlay_AddImage(const std::string& key, const std::string& base64);
void Overlay_CycleMode();

class OverlayWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override{};
    void DrawElement() override{};
    void Draw() override;
    void UpdateElement() override{};
};
