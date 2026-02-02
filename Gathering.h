#pragma once
#include <vector>
#include <iostream>
#include <fstream>

#include "Profile.h"
#include "Entity.h"
#include "GoapSystem.h"

#include "Pathfinding2.h"

const int BLACKLIST_TIMEOUT = 300000;

void BlacklistClear(WorldState& ws) {
    DWORD now = GetTickCount();
    if (ws.gatherState.blacklistTime.size() > 0) {
        for (int i = ws.gatherState.blacklistTime.size() - 1; i >= 0; i--) {
            if (now - ws.gatherState.blacklistTime[i] > BLACKLIST_TIMEOUT) {
                ws.gatherState.blacklistTime.erase(ws.gatherState.blacklistTime.begin(), ws.gatherState.blacklistTime.begin() + i);
                ws.gatherState.blacklistNodesGuidHigh.erase(ws.gatherState.blacklistNodesGuidHigh.begin(), ws.gatherState.blacklistNodesGuidHigh.begin() + i);
                ws.gatherState.blacklistNodesGuidLow.erase(ws.gatherState.blacklistNodesGuidLow.begin(), ws.gatherState.blacklistNodesGuidLow.begin() + i);
            }
        }
    }
}

bool BlacklistCheck(WorldState& ws, ULONG_PTR guidLow, ULONG_PTR guidHigh) {
    for (size_t i = 0; i < ws.gatherState.blacklistTime.size(); i++) {
        if ((ws.gatherState.blacklistNodesGuidLow[i] == guidLow) && (ws.gatherState.blacklistNodesGuidHigh[i] == guidHigh)) {
            return true;
        }
    }
    return false;
}

// Helper to find nodes in the entity list
void UpdateGatherTarget(WorldState& ws) {    

    float bestDist = g_ProfileSettings.gatherRange; // Max scan range (yards)
    float secondBestDist = g_ProfileSettings.gatherRange; // Max scan range (yards)
    int bestIndex = -1;
    int secondBestIndex = -1;

    BlacklistClear(ws);

    bool foundExistingNode = false;
    // Loop through all entities updated by the Memory Reader
    for (int i = 0; i < ws.entities.size(); ++i) {
        const auto& entity = ws.entities[i];
        int nearbyEnemyCount = 0;
        //int nearbyStrongEnemyCount = 0;

        // 1. Check basic object type (Must be GameObject)
        if (entity.objType != "Object") continue;

        if (g_GameState->player.bagFreeSlots == 0) {
            ws.gatherState.nodeActive = false;
            return;
        }

        // If we already have a target, check if it's still valid/close
        if (ws.gatherState.hasNode) {
            if ((entity.guidLow == ws.gatherState.guidLow) && (entity.guidHigh == ws.gatherState.guidHigh)) {
                float dist = ws.player.position.Dist3D(ws.gatherState.position);
                if ((dist > g_ProfileSettings.gatherRange) || (std::dynamic_pointer_cast<ObjectInfo>(entity.info)->nodeActive == 0)) { // Lost it or moved too far
                    //ws.gatherState.hasNode = false;
                    ws.gatherState.nodeActive = false;
                }

                for (int j = 0; j < ws.entities.size(); ++j) {
                    if (auto otherPlayer = std::dynamic_pointer_cast<OtherPlayerInfo>(ws.entities[j].info)) {
                        if ((ws.gatherState.position.Dist3D(otherPlayer->position) < 10.0f) && (ws.gatherState.position.Dist3D(otherPlayer->position) < ws.gatherState.position.Dist3D(ws.player.position))) {
                            ws.gatherState.blacklistNodesGuidLow.push_back(ws.gatherState.guidLow);
                            ws.gatherState.blacklistNodesGuidHigh.push_back(ws.gatherState.guidHigh);
                            ws.gatherState.blacklistTime.push_back(GetTickCount());
                            g_LogFile << "Player detected near node, blacklisting for " << BLACKLIST_TIMEOUT / 1000 << " seconds" << std::endl;
                            ws.gatherState.nodeActive = false;
                            break;
                        }
                    }
                }
                foundExistingNode = true;
                return;
            }
        }
        if (foundExistingNode) {
            ws.gatherState.nodeActive;
            return;
        }

        float d = 9999.0f;
        if (auto object = std::dynamic_pointer_cast<ObjectInfo>(entity.info)) {
            //if (object->position.z <= 0.0f) continue;
            if (object->nodeActive == 0) continue;

            if (((object->type == 1) && (g_ProfileSettings.herbalismEnabled == true)) || ((object->type == 2) && (g_ProfileSettings.miningEnabled == true))) {
                // --- CLEANER IGNORE WATER LOGIC ---
                if (g_GameState->globalState.ignoreUnderWater) {
                    // Get the Area ID directly from our new function
                    unsigned char area = globalNavMesh.GetAreaID(object->position);

                    // Check against our flags (Water Surface or Sea Floor)
                    if (area == AREA_UNDERWATER || area == AREA_DEEP_WATER) {
                        continue; // Skip this node
                    }
                }
                // ----------------------------------
               
                // 2. Distance Check
                d = object->distance;
                /*if (d <= 5.0f) {
                    continue;
                }*/
                if (BlacklistCheck(ws, entity.guidLow, entity.guidHigh)) {
                    continue;
                }

                bool ignoredNode = false;
                // If area is ignored dont gather
                for (int j = 0; j < g_ProfileSettings.ignoredAreas.size(); j++) {
                    if (object->position.Dist3D(g_ProfileSettings.ignoredAreas[j].Position) < g_ProfileSettings.ignoredAreas[j].Radius) {
                        ws.gatherState.blacklistNodesGuidLow.push_back(entity.guidLow);
                        ws.gatherState.blacklistNodesGuidHigh.push_back(entity.guidHigh);
                        ws.gatherState.blacklistTime.push_back(GetTickCount());
                        g_LogFile << "Node in ignored area, blacklisting for " << BLACKLIST_TIMEOUT / 1000 << " seconds" << std::endl;
                        ignoredNode = true;
                        break;
                    }
                }
                // Check if player is close to node
                for (int j = 0; j < ws.entities.size(); ++j) {
                    if (auto otherPlayer = std::dynamic_pointer_cast<OtherPlayerInfo>(ws.entities[j].info)) {
                        if (object->position.Dist3D(otherPlayer->position) < 10.0f) {
                            ws.gatherState.blacklistNodesGuidLow.push_back(ws.gatherState.guidLow);
                            ws.gatherState.blacklistNodesGuidHigh.push_back(ws.gatherState.guidHigh);
                            ws.gatherState.blacklistTime.push_back(GetTickCount());
                            g_LogFile << "Player detected near node, blacklisting for " << BLACKLIST_TIMEOUT / 1000 << " seconds" << std::endl;
                            ignoredNode = true;
                            break;
                        }
                    }
                }
                if (ignoredNode) continue;

                std::vector<int> enemyIndex = {};
                int index = 0;
                for (auto& entity : ws.entities) {
                    if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
                        if ((npc->reaction == 0) && (npc->position.Dist3D(object->position)) < (npc->agroRange + 5.0f)) {
                            nearbyEnemyCount++;
                            enemyIndex.push_back(index);
                        }
                    }
                    index++;
                }
            }
        }

        if ((d < bestDist) && (nearbyEnemyCount < 4)) {
            bestDist = d;
            bestIndex = i;
        }
        else if ((d < secondBestDist) && (nearbyEnemyCount < 4)) {
            secondBestDist = d;
            secondBestIndex = i;
        }
    }

    // Found a valid target
    if (bestIndex != -1) {
        const auto& target = ws.entities[bestIndex];
        // Add node to blocklist if in water and try second best
        /*if (ws.player.inWater) {
            ws.gatherState.blacklistNodesGuidLow.push_back(ws.gatherState.guidLow);
            ws.gatherState.blacklistNodesGuidHigh.push_back(ws.gatherState.guidHigh);
            ws.gatherState.blacklistTime.push_back(GetTickCount());
            const auto& target = ws.entities[secondBestIndex];
        }*/
        if (auto objInfo = std::dynamic_pointer_cast<ObjectInfo>(target.info)) {
            ws.gatherState.position = objInfo->position;
            ws.gatherState.guidLow = target.guidLow;
            ws.gatherState.guidHigh = target.guidHigh;
            ws.gatherState.hasNode = true;
            ws.gatherState.nodeActive = true;
            ws.gatherState.mapId = ws.player.mapId;
            g_LogFile << "[AGENT] Found Gather Node: " << objInfo->name << " at " << bestDist << "y" << std::endl;
        }
    }
    else if (ws.gatherState.hasNode) {
        ws.gatherState.nodeActive = false;
    }
}