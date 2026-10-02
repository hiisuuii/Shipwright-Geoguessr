#include "soh/Enhancements/Geoguessr/Capture.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Notification/Notification.h"
#include "soh/ShipInit.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>

#include <fast/Fast3dWindow.h>
#include <nlohmann/json.hpp>
#include <png.h>
#include <ship/Context.h>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>
#endif

extern "C" {
#include <z64.h>
#include "macros.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
}

// Vertical field of view of each clue, tightest first. 0 means the camera's own view.
static const float sClueFovs[] = { 6.0f, 12.0f, 25.0f, 0.0f };
// Game updates to wait after changing the view before reading the frame, so it has been drawn
#define SETTLE_UPDATES 3

static std::filesystem::path sCaptureDir;
static bool sCapturing = false;
static int32_t sClueIndex = 0;
static int32_t sSettleUpdates = 0;
static float sCameraFov = 0.0f;
static ActorFunc sNaviDraw = NULL;

static FILE* OpenFile(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
    std::wstring wideMode(mode, mode + strlen(mode));
    return _wfopen(path.c_str(), wideMode.c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

static void Notify(const std::string& message) {
    Notification::Emit({ .message = message, .remainingTime = 5.0f });
}

static bool InGameplay() {
    return gPlayState != NULL && GameInteractor::IsSaveLoaded(true);
}

static std::filesystem::path NewCaptureDir() {
    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", std::localtime(&now));
    std::filesystem::path dir = std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("captures")) / stamp;
    std::filesystem::create_directories(dir);
    return dir;
}

static bool ReadGameFrame(uint32_t& width, uint32_t& height, std::vector<uint8_t>& rgb) {
#ifdef _WIN32
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    if (window->GetWindowBackend() != Fast::FAST3D_DXGI_DX11) {
        return false;
    }
    // With DirectX 11 this is the shader resource view of the game's own framebuffer, drawn before any menus
    auto view = reinterpret_cast<ID3D11ShaderResourceView*>(window->GetGfxFrameBuffer());
    if (view == nullptr) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(resource.As(&texture))) {
        return false;
    }
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || desc.SampleDesc.Count != 1) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    texture->GetDevice(&device);
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) {
        return false;
    }
    context->CopyResource(staging.Get(), texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }
    width = desc.Width;
    height = desc.Height;
    rgb.resize((size_t)width * height * 3);
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* src = (const uint8_t*)mapped.pData + (size_t)y * mapped.RowPitch;
        uint8_t* dst = rgb.data() + (size_t)y * width * 3;
        for (uint32_t x = 0; x < width; x++) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    context->Unmap(staging.Get(), 0);
    return true;
#else
    return false;
#endif
}

static bool WritePng(const std::filesystem::path& path, uint32_t width, uint32_t height,
                     const std::vector<uint8_t>& rgb) {
    FILE* file = OpenFile(path, "wb");
    if (file == nullptr) {
        return false;
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(file);
        return false;
    }
    png_init_io(png, file);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (uint32_t y = 0; y < height; y++) {
        png_write_row(png, rgb.data() + (size_t)y * width * 3);
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(file);
    return true;
}

void Capture_MarkTarget() {
    if (!InGameplay()) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json target = {
        { "sceneNum", gPlayState->sceneNum },
        { "roomNum", gPlayState->roomCtx.curRoom.num },
        { "x", player->actor.world.pos.x },
        { "y", player->actor.world.pos.y },
        { "z", player->actor.world.pos.z },
        { "linkAge", gSaveContext.linkAge },
        { "dayTime", gSaveContext.dayTime },
    };

    sCaptureDir = NewCaptureDir();
    FILE* file = OpenFile(sCaptureDir / "target.json", "w");
    if (file == nullptr) {
        Notify("Couldn't write the target file");
        return;
    }
    fputs(target.dump(2).c_str(), file);
    fclose(file);
    Notify("Target marked. Frame the view and press F3.");
}

static void SetHidden(bool hidden) {
    GameInteractor::State::NoUIActive = hidden;
    Actor* navi = GET_PLAYER(gPlayState)->naviActor;
    if (navi == NULL) {
        return;
    }
    if (hidden) {
        sNaviDraw = navi->draw;
        navi->draw = NULL;
    } else if (sNaviDraw != NULL) {
        navi->draw = sNaviDraw;
        sNaviDraw = NULL;
    }
}

static void FinishCapture(const std::string& message) {
    sCapturing = false;
    if (gPlayState != NULL) {
        SetHidden(false);
    }
    Notify(message);
}

void Capture_TakeClues() {
    if (sCapturing || !InGameplay()) {
        return;
    }
    if (sCaptureDir.empty()) {
        sCaptureDir = NewCaptureDir();
    }
    sCapturing = true;
    sClueIndex = 0;
    sSettleUpdates = SETTLE_UPDATES;
    sCameraFov = gPlayState->view.fovy;
    SetHidden(true);
}

static void OnPlayDrawBegin() {
    if (!sCapturing) {
        return;
    }
    if (!InGameplay()) {
        FinishCapture("Clue capture cancelled");
        return;
    }

    // The previous update's frame has been drawn by now, so it shows the current clue's view
    if (sSettleUpdates-- <= 0) {
        uint32_t width, height;
        std::vector<uint8_t> rgb;
        if (!ReadGameFrame(width, height, rgb)) {
            FinishCapture("Clue capture needs the DirectX 11 renderer");
            return;
        }
        std::filesystem::path path = sCaptureDir / ("clue" + std::to_string(sClueIndex + 1) + ".png");
        if (!WritePng(path, width, height, rgb)) {
            FinishCapture("Couldn't write " + path.filename().string());
            return;
        }
        sClueIndex++;
        sSettleUpdates = SETTLE_UPDATES;
        if (sClueIndex == ARRAY_COUNT(sClueFovs)) {
            SPDLOG_INFO("[Geoguessr] Saved clues to {}", sCaptureDir.string());
            FinishCapture("Saved " + std::to_string(sClueIndex) + " clues to captures/" +
                          sCaptureDir.filename().string());
            sCaptureDir.clear();
            return;
        }
    }

    float fov = sClueFovs[sClueIndex];
    gPlayState->view.fovy = fov > 0.0f ? fov : sCameraFov;
}

static void RegisterCapture() {
    COND_HOOK(OnPlayDrawBegin, true, OnPlayDrawBegin);
}

static RegisterShipInitFunc initFunc(RegisterCapture);
