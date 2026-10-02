#include "soh/Enhancements/Geoguessr/PositionWarp.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

#include <atomic>

extern "C" {
#include <z64.h>
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
}

// The warp reuses the void-out respawn, which would otherwise cost a heart
static std::atomic<bool> sSkipVoidDamage = false;

// Loads the scene through the given entrance, then places Link at an exact spot the way a void-out respawn does
void PositionWarp_Start(int32_t entranceIndex, int32_t roomNum, float x, float y, float z, int16_t yaw) {
    RespawnData* respawn = &gSaveContext.respawn[RESPAWN_MODE_DOWN];
    respawn->entranceIndex = entranceIndex;
    respawn->roomIndex = roomNum;
    respawn->pos = { x, y, z };
    respawn->yaw = yaw;
    respawn->playerParams = 0x0DFF;
    respawn->tempSwchFlags = 0;
    respawn->tempCollectFlags = 0;

    sSkipVoidDamage = true;
    gSaveContext.respawnFlag = 1;
    gPlayState->nextEntranceIndex = entranceIndex;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK;
}

static void RegisterPositionWarp() {
    REGISTER_VB_SHOULD(VB_INFLICT_VOID_DAMAGE, {
        if (sSkipVoidDamage.exchange(false)) {
            *should = false;
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterPositionWarp);
