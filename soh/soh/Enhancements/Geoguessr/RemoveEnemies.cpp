#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include <z64.h>
}

static bool ShouldRemove(Actor* actor) {
    switch (actor->id) {
        // Story, scenery or hazard actors that happen to use the enemy or boss category
        case ACTOR_EN_ZL3:
        case ACTOR_EN_ENCOUNT2:
        case ACTOR_EN_FIRE_ROCK:
        case ACTOR_EN_GANON_ORGAN:
        case ACTOR_EN_GANON_MANT:
            return false;
        // Defeated dungeon bosses spawn their blue warp from their own init, so they must be allowed to run it.
        // Ganondorf and Ganon aren't beaten in the shared save and fall through to removal.
        case ACTOR_BOSS_GOMA:
        case ACTOR_BOSS_DODONGO:
        case ACTOR_BOSS_VA:
        case ACTOR_BOSS_GANONDROF:
        case ACTOR_BOSS_FD:
        case ACTOR_BOSS_FD2:
        case ACTOR_BOSS_MO:
        case ACTOR_BOSS_TW:
        case ACTOR_BOSS_SST:
            return false;
        // Flying pots and Leevers start out as props and only become enemies once they attack
        case ACTOR_EN_TUBO_TRAP:
        case ACTOR_EN_REEBA:
        // Guards that catch Link and throw him out
        case ACTOR_EN_GE2:
        case ACTOR_EN_HEISHI1:
        case ACTOR_EN_HEISHI3:
            return true;
        // Regular Skullwalltulas share this actor with Gold Skulltulas, which have a nonzero type
        case ACTOR_EN_SW:
            return (actor->params & 0xE000) == 0;
    }

    return actor->category == ACTORCAT_ENEMY || actor->category == ACTORCAT_BOSS;
}

static void RegisterRemoveEnemies() {
    COND_HOOK(ShouldActorInit, true, [](void* refActor, bool* result) {
        Actor* actor = (Actor*)refActor;
        if (ShouldRemove(actor)) {
            *result = false;
            // Init never ran, so there is nothing for destroy to clean up
            actor->destroy = NULL;
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterRemoveEnemies);
