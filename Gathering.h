#pragma once
#include <vector>
#include <iostream>
#include <fstream>

#include "Profile.h"
#include "Entity.h"
#include "GoapSystem.h"


// Helper to find nodes in the entity list
void UpdateGatherTarget(WorldState& ws) {
    std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
    // If we already have a target, check if it's still valid/close
    if (ws.gatherState.hasNode) {
        float dist = ws.player.position.Dist3D(ws.gatherState.position);
        if (dist > GATHERING_RANGE) { // Lost it or moved too far
            ws.gatherState.hasNode = false;
        }
        return;
    }

    float bestDist = GATHERING_RANGE; // Max scan range (yards)
    int bestIndex = -1;

    // Loop through all entities updated by the Memory Reader
    for (size_t i = 0; i < ws.entities.size(); ++i) {
        const auto& entity = ws.entities[i];

        // 1. Check basic object type (Must be GameObject)
        if (entity.objType != "Object") continue;

        float d = 9999.0f;
        if (auto object = std::dynamic_pointer_cast<ObjectInfo>(entity.info)) {
            if (object->position.z <= 0.0f) continue;
            if (object->nodeActive == 0) continue; // NOT WORKING. NEED TO INVESTIGATE
            if (((object->type == 1) && (HERBALISM_ENABLED == true)) || ((object->type == 2) && (MINING_ENABLED == true))) {
                // 2. Distance Check
                d = object->distance;
                if (d <= 5.0f) {
                    continue;
                }
                if (d < bestDist) {
                    bestDist = d;
                    bestIndex = i;
                }
            }
        }
    }

    // Found a valid target
    if (bestIndex != -1) {
        const auto& target = ws.entities[bestIndex];
        if (auto objInfo = std::dynamic_pointer_cast<ObjectInfo>(target.info)) {
            ws.gatherState.position = objInfo->position;
            ws.gatherState.guidLow = target.guidLow;
            ws.gatherState.guidHigh = target.guidHigh;
            ws.gatherState.hasNode = true;
            logFile << "[AGENT] Found Gather Node: " << objInfo->name << " at " << bestDist << "y" << std::endl;
        }
    }
}