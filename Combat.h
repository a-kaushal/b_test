#pragma once
#include <windows.h>
#include <vector>
#include <mutex>
#include <memory>
#include <string>
#include <algorithm>

#include "GoapSystem.h"
#include "Entity.h"

bool UnderAttackCheck() {
    bool targetFound = false;
    std::vector<Vector3> enemyIndices = {};
	if ((g_GameState->player.targetGuidLow != 0) && (g_GameState->player.targetGuidHigh != 0)) {
        int count = 0;
        bool npcCombat = 0;
        for (auto& entity : g_GameState->entities) {
            // Use std::dynamic_pointer_cast for shared_ptr
            if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
                if (npc->inCombat && (npc->targetGuidLow == g_GameState->player.playerGuidLow) && (npc->targetGuidHigh == g_GameState->player.playerGuidHigh) && (!g_GameState->player.isFlying)) {
                    if (npc->health > 0) {
                        g_GameState->combatState.hasTarget = true;
                        g_GameState->combatState.underAttack = true;
                        g_GameState->combatState.enemyPosition = npc->position;
                        g_GameState->combatState.targetGuidLow = entity.guidLow;
                        g_GameState->combatState.targetGuidHigh = entity.guidHigh;
                        g_GameState->combatState.entityIndex = count;
                        return true;
                    }
                    else {
                        g_GameState->combatState.reset = true;
                        targetFound = true;
                    }
                }
            }
			count++;
        }
	}
	else {
        g_GameState->combatState.underAttack = false;
		return false;
	}
    if (!targetFound) {
        g_GameState->combatState.reset = true;
    }
    return false;
}

void TargetEnemy(ULONG_PTR targetGuidLow = 0, ULONG_PTR targetGuidHigh = 0, uint32_t targetId = 0) {
	int minDistance = 99999;
    if (targetId != 0) {
        for (auto& entity : g_GameState->entities) {
            if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
				int distance = std::round(g_GameState->player.position.Dist2D(npc->position));
                if ((npc->id == targetId) && (distance < minDistance)) {
                    if (npc->health > 0) {
                        g_GameState->combatState.hasTarget = true;
                        g_GameState->combatState.underAttack = false;
                        g_GameState->combatState.enemyPosition = npc->position;
                        g_GameState->combatState.targetGuidLow = entity.guidLow;
                        g_GameState->combatState.targetGuidHigh = entity.guidHigh;
                    }
					minDistance = distance;
                }
            }
        }
    }
    else if (targetGuidHigh != 0 && targetGuidLow != 0) {
        for (auto& entity : g_GameState->entities) {
            if ((entity.guidLow == targetGuidLow) && (entity.guidHigh == targetGuidHigh)) {
                if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
                    if (npc->health > 0) {
                        g_GameState->combatState.hasTarget = true;
                        g_GameState->combatState.underAttack = false;
                        g_GameState->combatState.enemyPosition = npc->position;
                        g_GameState->combatState.targetGuidLow = entity.guidLow;
                        g_GameState->combatState.targetGuidHigh = entity.guidHigh;
                    }
                }
            }
        }
    }
    else {
        g_GameState->combatState.hasTarget = false;
	}
}