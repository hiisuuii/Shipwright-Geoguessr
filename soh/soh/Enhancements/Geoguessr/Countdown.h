#pragma once

#include <stdint.h>
#include <ship/window/gui/GuiWindow.h>

void Countdown_Start(int32_t seconds);
void Countdown_Stop();
void Countdown_HoldPlayer(bool hold);

class CountdownWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override{};
    void DrawElement() override{};
    void Draw() override;
    void UpdateElement() override{};
};
