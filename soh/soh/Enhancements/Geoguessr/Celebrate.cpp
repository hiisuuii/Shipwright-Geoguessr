#include "soh/Enhancements/Geoguessr/Celebrate.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

#include <atomic>
#include <cmath>

extern "C" {
#include <z64.h>
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

#define CELEBRATE_FRAMES 30
#define BURST_SPARKLES 24

// Sail effects are applied on the network thread, so the effect only sets this
// and the sparkles are spawned from the player update on the game thread.
static std::atomic<int32_t> sFramesRemaining = 0;

void Celebrate_Queue() {
    sFramesRemaining = CELEBRATE_FRAMES;
}

static void SpawnBurst(Vec3f* center) {
    static Color_RGBA8 primColor = { 255, 255, 220, 255 };
    static Color_RGBA8 envColor = { 255, 200, 0, 0 };
    Vec3f zero = { 0.0f, 0.0f, 0.0f };

    EffectSsBlast_SpawnWhiteShockwave(gPlayState, center, &zero, &zero);

    for (int32_t i = 0; i < BURST_SPARKLES; i++) {
        float angle = (2.0f * M_PI * i) / BURST_SPARKLES;
        Vec3f velocity = { cosf(angle) * 6.0f, 4.0f + Rand_ZeroOne() * 4.0f, sinf(angle) * 6.0f };
        Vec3f accel = { 0.0f, -0.3f, 0.0f };
        EffectSsKiraKira_SpawnDispersed(gPlayState, center, &velocity, &accel, &primColor, &envColor, 1500, 30);
    }
}

static void SpawnRisingSparkles(Vec3f* base, int32_t frame) {
    static Color_RGBA8 primColor = { 255, 255, 255, 255 };
    static Color_RGBA8 envColor = { 100, 200, 255, 0 };

    for (int32_t i = 0; i < 3; i++) {
        float angle = frame * 0.6f + (2.0f * M_PI * i) / 3;
        Vec3f pos = { base->x + cosf(angle) * 30.0f, base->y + frame * 2.0f, base->z + sinf(angle) * 30.0f };
        Vec3f velocity = { 0.0f, 2.0f, 0.0f };
        Vec3f accel = { 0.0f, 0.0f, 0.0f };
        EffectSsKiraKira_SpawnFocused(gPlayState, &pos, &velocity, &accel, &primColor, &envColor, 1000, 20);
    }
}

static void OnPlayerUpdate() {
    int32_t framesRemaining = sFramesRemaining.load();
    if (framesRemaining <= 0) {
        return;
    }
    sFramesRemaining.compare_exchange_strong(framesRemaining, framesRemaining - 1);

    Player* player = GET_PLAYER(gPlayState);
    Vec3f center = player->actor.world.pos;
    center.y += 40.0f;
    int32_t frame = CELEBRATE_FRAMES - framesRemaining;

    if (frame == 0) {
        Audio_PlaySfxGeneral(NA_SE_SY_CORRECT_CHIME, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        SpawnBurst(&center);
    }
    SpawnRisingSparkles(&player->actor.world.pos, frame);
}

static void RegisterCelebrate() {
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdate);
}

static RegisterShipInitFunc initFunc(RegisterCelebrate);
