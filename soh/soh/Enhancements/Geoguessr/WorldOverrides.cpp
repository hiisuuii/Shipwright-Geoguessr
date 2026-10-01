#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

#include <vector>

extern "C" {
#include <z64.h>
#include "functions.h"
#include "src/overlays/actors/ovl_En_Door/z_en_door.h"
#include "src/overlays/actors/ovl_Door_Shutter/z_door_shutter.h"
extern PlayState* gPlayState;
}

// Setting a locked or switch-barred door's flag before it initializes makes it spawn already opened,
// the same as if the key had been used or the puzzle solved, without spending any keys
static void UnlockDoor(Actor* actor) {
    s32 switchFlag = actor->params & 0x3F;
    if (actor->id == ACTOR_EN_DOOR) {
        if (((actor->params >> 7) & 7) == DOOR_LOCKED) {
            Flags_SetSwitch(gPlayState, switchFlag);
        }
    } else if (actor->id == ACTOR_DOOR_SHUTTER) {
        switch ((actor->params >> 6) & 0xF) {
            case SHUTTER_KEY_LOCKED:
            case SHUTTER_BOSS:
            case SHUTTER_FRONT_SWITCH:
            case SHUTTER_FRONT_SWITCH_BACK_CLEAR:
                Flags_SetSwitch(gPlayState, switchFlag);
                break;
        }
    }
}

// Gates that a finished save can't open because they use temporary flags, which reset on every scene load
struct SceneOverride {
    s16 sceneNum;
    std::vector<s32> switchFlags;
    std::vector<s32> clearedRooms;
    std::vector<s16> removedActors;
};

static const std::vector<SceneOverride> sOverrides = {
    // Dampe's race: the doors he opens as he passes, the exit he opens at the end, and Dampe himself
    { SCENE_WINDMILL_AND_DAMPES_GRAVE, { 0x35, 0x36, 0x37 }, { 4 }, { ACTOR_EN_PO_RELAY } },
};

static const SceneOverride* FindOverride(s16 sceneNum) {
    for (const SceneOverride& entry : sOverrides) {
        if (entry.sceneNum == sceneNum) {
            return &entry;
        }
    }
    return nullptr;
}

static void RegisterWorldOverrides() {
    COND_HOOK(ShouldActorInit, true, [](void* refActor, bool* result) {
        Actor* actor = (Actor*)refActor;
        UnlockDoor(actor);

        const SceneOverride* entry = FindOverride(gPlayState->sceneNum);
        if (entry == nullptr) {
            return;
        }

        // The player spawns right after the scene's flags are loaded and before every other actor
        if (actor->id == ACTOR_PLAYER) {
            for (s32 flag : entry->switchFlags) {
                Flags_SetSwitch(gPlayState, flag);
            }
            for (s32 room : entry->clearedRooms) {
                Flags_SetTempClear(gPlayState, room);
            }
            return;
        }

        for (s16 id : entry->removedActors) {
            if (actor->id == id) {
                *result = false;
                actor->destroy = NULL;
                return;
            }
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterWorldOverrides);
