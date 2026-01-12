#pragma once
#include <windows.h>
#include <vector>
#include <mutex>
#include <memory>
#include <string>
#include <algorithm>

#include "GoapSystem.h"
#include "Entity.h"

bool UnderAttackCheck(WorldState& ws) {
	if ((ws.player.underAttackGuidLow != 0) && (ws.player.underAttackGuidHigh != 0)) {
        int count = 0;
        for (auto& entity : ws.entities) {
            // Use std::dynamic_pointer_cast for shared_ptr
            if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
                if ((entity.guidLow == ws.player.underAttackGuidLow) && (entity.guidHigh == ws.player.underAttackGuidHigh)) {
                    if ((npc->health > 0) && (npc->reaction == 0)) {
                        ws.combatState.hasTarget = true;
                        ws.combatState.underAttack = true;
                        ws.combatState.enemyPosition = npc->position;
                        ws.combatState.targetGuidLow = entity.guidLow;
                        ws.combatState.targetGuidHigh = entity.guidHigh;
						ws.combatState.entityIndex = count;
                        return true;
                    }
                }
            }
			count++;
        }
	}
	else {
		ws.combatState.underAttack = false;
		return false;
	}
    return false;
}

void TargetEnemy(WorldState& ws, ULONG_PTR targetGuidLow = 0, ULONG_PTR targetGuidHigh = 0, uint32_t targetId = 0) {
	int minDistance = 99999;
    if (targetId != 0) {
        for (auto& entity : ws.entities) {
            if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
				int distance = std::round(ws.player.position.Dist2D(npc->position));
                if ((npc->id == targetId) && (distance < minDistance)) {
                    if (npc->health > 0) {
                        ws.combatState.hasTarget = true;
                        ws.combatState.underAttack = false;
                        ws.combatState.enemyPosition = npc->position;
                        ws.combatState.targetGuidLow = entity.guidLow;
                        ws.combatState.targetGuidHigh = entity.guidHigh;
                    }
					minDistance = distance;
                }
            }
        }
    }
    else if (targetGuidHigh != 0 && targetGuidLow != 0) {
        for (auto& entity : ws.entities) {
            if ((entity.guidLow == targetGuidLow) && (entity.guidHigh == targetGuidHigh)) {
                if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
                    if (npc->health > 0) {
                        ws.combatState.hasTarget = true;
                        ws.combatState.underAttack = false;
                        ws.combatState.enemyPosition = npc->position;
                        ws.combatState.targetGuidLow = entity.guidLow;
                        ws.combatState.targetGuidHigh = entity.guidHigh;
                    }
                }
            }
        }
    }
    else {
        ws.combatState.hasTarget = false;
	}
}