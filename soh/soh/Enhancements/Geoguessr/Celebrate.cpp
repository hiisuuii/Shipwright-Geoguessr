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
#include "src/overlays/actors/ovl_Door_Warp1/z_door_warp1.h"
extern PlayState* gPlayState;
}

#define UPDATES_PER_SECOND 20
#define CELEBRATE_UPDATES (10 * UPDATES_PER_SECOND)
#define RISING_SPARKLE_UPDATES 30
#define BURST_SPARKLES 24
// Stored in the glow's z rotation, which the warp actor never reads, so we can find our glow again
#define GLOW_MARKER 0x6E0

// Sail effects are applied on the network thread, so they only set these and
// everything else happens in the player update on the game thread.
static std::atomic<int32_t> sUpdatesRemaining = 0;
static std::atomic<bool> sRemoveGlow = false;

void Celebrate_Queue() {
    sUpdatesRemaining = CELEBRATE_UPDATES;
}

void Celebrate_Stop() {
    sUpdatesRemaining = 0;
    sRemoveGlow = true;
}

static void KillGlow() {
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ITEMACTION].head; actor != NULL; actor = actor->next) {
        if (actor->id == ACTOR_DOOR_WARP1 && actor->world.rot.z == GLOW_MARKER) {
            Actor_Kill(actor);
        }
    }
}

// Ruto's blue warp does nothing until Ruto activates it, so it works as a harmless glow
static void SpawnGlow(Vec3f* pos) {
    KillGlow();
    f32 y = pos->y;
    Player* player = GET_PLAYER(gPlayState);
    if (player->actor.floorHeight > BGCHECK_Y_MIN) {
        y = player->actor.floorHeight;
    }
    Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_DOOR_WARP1, pos->x, y, pos->z, 0, 0, GLOW_MARKER,
                WARP_BLUE_RUTO);
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

static void SpawnRisingSparkles(Vec3f* base, int32_t update) {
    static Color_RGBA8 primColor = { 255, 255, 255, 255 };
    static Color_RGBA8 envColor = { 100, 200, 255, 0 };

    for (int32_t i = 0; i < 3; i++) {
        float angle = update * 0.6f + (2.0f * M_PI * i) / 3;
        Vec3f pos = { base->x + cosf(angle) * 30.0f, base->y + update * 2.0f, base->z + sinf(angle) * 30.0f };
        Vec3f velocity = { 0.0f, 2.0f, 0.0f };
        Vec3f accel = { 0.0f, 0.0f, 0.0f };
        EffectSsKiraKira_SpawnFocused(gPlayState, &pos, &velocity, &accel, &primColor, &envColor, 1000, 20);
    }
}

static void OnPlayerUpdate() {
    if (sRemoveGlow.exchange(false)) {
        KillGlow();
    }

    int32_t remaining = sUpdatesRemaining.load();
    if (remaining <= 0) {
        return;
    }
    sUpdatesRemaining.compare_exchange_strong(remaining, remaining - 1);

    Player* player = GET_PLAYER(gPlayState);
    Vec3f center = player->actor.world.pos;
    center.y += 40.0f;
    int32_t update = CELEBRATE_UPDATES - remaining;

    if (update == 0) {
        Audio_PlaySfxGeneral(NA_SE_SY_CORRECT_CHIME, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        SpawnGlow(&player->actor.world.pos);
    }
    if (update % UPDATES_PER_SECOND == 0) {
        SpawnBurst(&center);
    }
    if (update < RISING_SPARKLE_UPDATES) {
        SpawnRisingSparkles(&player->actor.world.pos, update);
    }
}

static void RegisterCelebrate() {
    COND_HOOK(OnPlayerUpdate, true, OnPlayerUpdate);
}

static RegisterShipInitFunc initFunc(RegisterCelebrate);
