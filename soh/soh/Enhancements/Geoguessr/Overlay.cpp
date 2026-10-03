#include "soh/Enhancements/Geoguessr/Overlay.h"
#include "soh/cvar_prefixes.h"

#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include <fast/Fast3dGui.h>
#include <SDL2/SDL_timer.h>
#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/gui/resource/GuiTexture.h>
#include <spdlog/spdlog.h>
#include <stb_image.h>

#define CVAR_OVERLAY_MODE CVAR_REMOTE_SAIL("GeoguessrOverlay")

struct OverlayState {
    bool visible = false;
    bool running = false;
    std::string round;
    double elapsed = 0.0;
    double timeLimit = 0.0;
    std::string status;
    std::vector<std::string> hints;
    std::string clue;
    std::string clueInfo;
    int64_t receivedMs = 0;
};

// Sail delivers state and images on its network thread; the window reads them on the game thread
static std::mutex sMutex;
static OverlayState sState;
static std::vector<std::pair<std::string, std::shared_ptr<Ship::GuiTexture>>> sPendingImages;
static std::set<std::string> sLoadedImages;

static std::vector<uint8_t> DecodeBase64(const std::string& input) {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> output;
    output.reserve(input.size() * 3 / 4);
    uint32_t buffer = 0;
    int32_t bits = 0;
    for (char c : input) {
        size_t value = alphabet.find(c);
        if (value == std::string::npos) {
            continue;
        }
        buffer = (buffer << 6) | (uint32_t)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back((uint8_t)(buffer >> bits));
        }
    }
    return output;
}

static std::string StringField(const nlohmann::json& json, const char* key) {
    auto it = json.find(key);
    return it != json.end() && it->is_string() ? it->get<std::string>() : "";
}

static double NumberField(const nlohmann::json& json, const char* key) {
    auto it = json.find(key);
    return it != json.end() && it->is_number() ? it->get<double>() : 0.0;
}

// Converted here, on the network thread, so the draw code never touches JSON and a bad packet can't crash a frame
void Overlay_SetState(const nlohmann::json& json) {
    OverlayState state;
    if (json.is_object()) {
        auto visible = json.find("visible");
        state.visible = visible != json.end() && visible->is_boolean() && visible->get<bool>();
        state.running = StringField(json, "phase") == "running";
        state.round = StringField(json, "round");
        state.elapsed = NumberField(json, "elapsed");
        state.timeLimit = NumberField(json, "timeLimit");
        state.status = StringField(json, "status");
        state.clue = StringField(json, "clue");
        state.clueInfo = StringField(json, "clueInfo");
        auto hints = json.find("hints");
        if (hints != json.end() && hints->is_array()) {
            for (const auto& hint : *hints) {
                if (hint.is_string()) {
                    state.hints.push_back(hint.get<std::string>());
                }
            }
        }
    }
    state.receivedMs = (int64_t)SDL_GetTicks64();

    std::lock_guard<std::mutex> lock(sMutex);
    sState = std::move(state);
}

// Decodes on the calling (network) thread; the upload to the GPU waits for the game thread
bool Overlay_AddImage(const std::string& key, const std::string& base64) {
    std::vector<uint8_t> data = DecodeBase64(base64);
    auto texture = std::make_shared<Ship::GuiTexture>();
    texture->Data = stbi_load_from_memory(data.data(), (int)data.size(), &texture->Metadata.Width,
                                          &texture->Metadata.Height, nullptr, 4);
    if (texture->Data == nullptr) {
        SPDLOG_ERROR("[Geoguessr] Couldn't decode overlay image: {}", stbi_failure_reason());
        return false;
    }
    texture->DataSize = (size_t)texture->Metadata.Width * texture->Metadata.Height * 4;

    std::lock_guard<std::mutex> lock(sMutex);
    sPendingImages.push_back({ key, texture });
    return true;
}

void Overlay_CycleMode() {
    int32_t mode = (CVarGetInteger(CVAR_OVERLAY_MODE, OVERLAY_SMALL) + 1) % OVERLAY_MODE_COUNT;
    CVarSetInteger(CVAR_OVERLAY_MODE, mode);
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

static std::string FormatTime(double seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
    char text[32];
    snprintf(text, sizeof(text), "%d:%04.1f", (int)seconds / 60, seconds - ((int)seconds / 60) * 60);
    return text;
}

static std::string TextureName(const std::string& key) {
    return "GeoguessrClue_" + key;
}

void OverlayWindow::Draw() {
    auto gui = std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui());
    OverlayState state;
    std::vector<std::pair<std::string, std::shared_ptr<Ship::GuiTexture>>> pending;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        state = sState;
        pending.swap(sPendingImages);
    }
    if (!pending.empty()) {
        // A 1440p clue takes about 14 MB of GPU memory, so only the one on screen and the new ones are kept
        std::set<std::string> keep = { state.clue };
        for (auto& [key, texture] : pending) {
            keep.insert(key);
        }
        for (auto it = sLoadedImages.begin(); it != sLoadedImages.end();) {
            if (keep.count(*it) == 0) {
                gui->UnloadTexture(TextureName(*it));
                it = sLoadedImages.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& [key, texture] : pending) {
            if (sLoadedImages.insert(key).second) {
                gui->LoadTextureFromResource(TextureName(key), texture);
            }
        }
    }

    int32_t mode = CVarGetInteger(CVAR_OVERLAY_MODE, OVERLAY_SMALL);
    if (mode == OVERLAY_HIDDEN || !state.visible) {
        return;
    }

    double elapsed = state.elapsed;
    if (state.running) {
        elapsed += ((int64_t)SDL_GetTicks64() - state.receivedMs) / 1000.0;
    }
    const std::string& clue = state.clue;
    bool hasImage = !clue.empty() && sLoadedImages.count(clue) > 0;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float imageWidth = mode == OVERLAY_LARGE ? viewport->Size.x * 0.7f : viewport->Size.x * 0.24f;
    if (mode == OVERLAY_LARGE) {
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    } else {
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - 20, viewport->Pos.y + 20), ImGuiCond_Always,
                                ImVec2(1.0f, 0.0f));
    }
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::Begin("GeoguessrOverlay", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(mode == OVERLAY_LARGE ? 1.6f : 1.2f);

    ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.7f, 1.0f), "%s", state.round.c_str());
    std::string timer = FormatTime(elapsed);
    if (state.timeLimit > 0) {
        timer += " / " + FormatTime(state.timeLimit);
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", timer.c_str());
    ImGui::TextWrapped("%s", state.status.c_str());
    for (const std::string& hint : state.hints) {
        ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.55f, 1.0f), "%s", hint.c_str());
    }

    if (hasImage) {
        ImVec2 size = gui->GetTextureSize(TextureName(clue));
        float height = size.x > 0 ? imageWidth * size.y / size.x : 0;
        if (mode == OVERLAY_LARGE && height > viewport->Size.y * 0.75f) {
            height = viewport->Size.y * 0.75f;
            imageWidth = height * size.x / size.y;
        }
        ImGui::Image(gui->GetTextureByName(TextureName(clue)), ImVec2(imageWidth, height));
    }
    if (!state.clueInfo.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.7f, 1.0f), "%s", state.clueInfo.c_str());
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
}
