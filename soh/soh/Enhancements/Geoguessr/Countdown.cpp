#include "soh/Enhancements/Geoguessr/Countdown.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

#include <atomic>
#include <string>

#include <SDL2/SDL_timer.h>
#include <imgui.h>

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

// A hold whose release never arrives (lost connection, server crash) must not freeze Link for good
#define MAX_HOLD_MS 30000
#define GO_DISPLAY_MS 1000

// Effects arrive on the Sail network thread; the window reads these on the game thread every frame
static std::atomic<int64_t> sCountdownStartMs = 0;
static std::atomic<int32_t> sCountdownSeconds = 0;
static std::atomic<int64_t> sHoldStartMs = 0;
// A held player can't press Start, so an open pause menu would trap them and block the warp
static std::atomic<bool> sClosePausePending = false;

static int64_t sShownStartMs = 0;
static int32_t sShownNumber = 0;

static void PlaySfx(u16 sfxId) {
    Audio_PlaySfxGeneral(sfxId, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                         &gSfxDefaultReverb);
}

void Countdown_Start(int32_t seconds) {
    sCountdownSeconds = seconds;
    sCountdownStartMs = (int64_t)SDL_GetTicks64();
}

void Countdown_Stop() {
    sCountdownSeconds = 0;
}

void Countdown_HoldPlayer(bool hold) {
    sHoldStartMs = (int64_t)SDL_GetTicks64();
    sClosePausePending = hold;
    GameInteractor::State::HoldPlayerActive = hold;
}

static void FinishClosingPause() {
    if (sClosePausePending && gPlayState != NULL && gPlayState->pauseCtx.state == 0) {
        sClosePausePending = false;
    }
}

static void RegisterCountdown() {
    COND_HOOK(OnGameFrameUpdate, true, FinishClosingPause);
    // Closes the pause menu as if Start were pressed, once it has finished opening
    REGISTER_VB_SHOULD(VB_CLOSE_PAUSE_MENU, {
        if (sClosePausePending) {
            *should = true;
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterCountdown);

void CountdownWindow::Draw() {
    int64_t now = (int64_t)SDL_GetTicks64();
    if (GameInteractor::State::HoldPlayerActive && now - sHoldStartMs > MAX_HOLD_MS) {
        GameInteractor::State::HoldPlayerActive = false;
    }

    int32_t seconds = sCountdownSeconds;
    int64_t start = sCountdownStartMs;
    if (seconds <= 0) {
        return;
    }
    int64_t elapsedMs = now - start;
    if (elapsedMs >= seconds * 1000 + GO_DISPLAY_MS) {
        sCountdownSeconds = 0;
        return;
    }

    int32_t number = seconds - (int32_t)(elapsedMs / 1000);
    if (start != sShownStartMs || number != sShownNumber) {
        sShownStartMs = start;
        sShownNumber = number;
        if (number > 0) {
            PlaySfx(NA_SE_SY_WARNING_COUNT_N);
        } else {
            PlaySfx(NA_SE_SY_START_SHOT);
            GameInteractor::State::HoldPlayerActive = false;
        }
    }

    std::string text = number > 0 ? std::to_string(number) : "GO!";
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.4f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::Begin("GeoguessrCountdown", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(6.0f);
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", text.c_str());
    ImGui::End();
    ImGui::PopStyleColor(2);
}
