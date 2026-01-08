//#pragma once
//#include <vector>
//#include <iostream>
//#include <fstream>
//
//#include "Entity.h"
//#include "GoapSystem.h"
//
//
//// Helper to find nodes in the entity list
//void FindRepairVendor(int32_t id, WorldState& ws) {
//    for (auto& entity : ws.entities) {
//        if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
//            if (entity.id == id) {
//                ws.repairState.repairNeeded = true;
//                ws.repairState.npcLocation = npc->position;
//                ws.repairState.npcGuidLow = entity.guidLow;
//                ws.repairState.npcGuidHigh = entity.guidHigh;
//            }
//        }
//    }
//}