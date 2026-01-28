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
    int attackerCount = 0;
    int closestIndex = -1;
    float closestDist = 99999.0f;
    int index = 0;

    // Scan ALL entities to find everyone attacking us
    for (auto& entity : g_GameState->entities) {
        if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
            // Check if this NPC is attacking the player
            if (npc->inCombat && (npc->targetGuidLow == g_GameState->player.playerGuidLow) &&
                (npc->targetGuidHigh == g_GameState->player.playerGuidHigh) &&
                (!g_GameState->player.isFlying)) {

                if (npc->health > 0) {
                    attackerCount++;

                    // Check if this is the closest attacker found so far
                    float dist = g_GameState->player.position.Dist3D(npc->position);
                    if (dist < closestDist) {
                        closestDist = dist;
                        closestIndex = index;
                    }
                }
            }
        }
        index++;
    }
    // Update the total count in the state
    g_GameState->combatState.attackerCount = attackerCount;

    // If we found at least one attacker, target the closest one
    if (closestIndex != -1) {
        const auto& entity = g_GameState->entities[closestIndex];
        if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
            g_GameState->combatState.hasTarget = true;
            g_GameState->combatState.underAttack = true;
            g_GameState->combatState.enemyPosition = npc->position;
            g_GameState->combatState.targetGuidLow = entity.guidLow;
            g_GameState->combatState.targetGuidHigh = entity.guidHigh;
            g_GameState->combatState.entityIndex = closestIndex;
            return true;
        }
    }

    // No attackers found
    g_GameState->combatState.underAttack = false;
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