#pragma once
#include <vector>
#include <iostream>
#include <memory>

#include "Profile.h"

inline void InteractWithObject(int mapId, int numTimes, Vector3 position, int objectId) {
    g_GameState->interactState.location = { position };
    g_GameState->interactState.interactId = objectId;
    g_GameState->interactState.interactTimes = numTimes;
    g_GameState->interactState.interactActive = true;
    //g_LogFile << "A " << g_GameState->interactState.targetGuidLow << " " << g_GameState->interactState.targetGuidHigh << std::endl;
    for (auto& entity : g_GameState->entities) {
        if (auto object = std::dynamic_pointer_cast<ObjectInfo>(entity.info)) {
            if (object->id == objectId) {
                g_GameState->interactState.targetGuidLow = entity.guidLow;
                g_GameState->interactState.targetGuidHigh = entity.guidHigh;
            }
        }
    }
}

inline void MailItems(int mapId, Vector3 position, int mailboxId) {
    InteractWithObject(mapId, 1, position, mailboxId);
    g_GameState->interactState.mailing = true;
}

inline void FollowPath(int mapId, std::string myPath, bool looping, bool flyingPath) {
    std::vector<PathNode> empty = {};
    std::vector<PathNode> path = {};
    std::vector<Vector3> myPathParsed = ParsePathString(myPath);

    // --- Pre-validate path for No-Fly Zones ---
    //if (globalNavMesh.LoadMap("C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/mmaps/", g_GameState->player.mapId)) {
    //    for (auto& pt : myPathParsed) {
    //        // If this point is in a No-Fly Zone (CanFlyAt returns false), fix it
    //        if (!CanFlyAt(g_GameState->player.mapId, pt.x, pt.y, pt.z)) {
    //            Vector3 fixedPt = globalNavMesh.FindNearestFlyablePoint(pt, g_GameState->player.mapId);
    //            if (fixedPt.x != 0.0f || fixedPt.y != 0.0f || fixedPt.z != 0.0f) {
    //                pt = fixedPt;
    //            }
    //        }
    //    }
    //}

    g_GameState->pathFollowState.presetPath = myPathParsed;
    g_GameState->pathFollowState.presetIndex = FindClosestWaypoint(myPathParsed, empty, g_GameState->player.position);
    g_GameState->pathFollowState.looping = looping;

    path = CalculatePath(g_GameState->pathFollowState.presetPath, g_GameState->player.position, g_GameState->pathFollowState.presetIndex, flyingPath, 530, g_GameState->player.isFlying
        , g_GameState->globalState.ignoreUnderWater, g_GameState->pathFollowState.looping);

    /*for (const auto& point : path) {
        g_LogFile << "Point: " << point.pos.x << ", " << point.pos.y << ", " << point.pos.z << std::endl;
    }*/

    g_GameState->pathFollowState.path = path;
    g_GameState->pathFollowState.index = 0;
    g_GameState->pathFollowState.hasPath = true;
    g_GameState->pathFollowState.flyingPath = flyingPath;
}

inline void Resupply(int mapId, int numTimes, Vector3 vendorPosition, int vendorId) {
    g_GameState->interactState.location = { vendorPosition };
    g_GameState->interactState.interactId = vendorId;
    g_GameState->interactState.interactTimes = numTimes;
    g_GameState->interactState.interactActive = true;
    for (auto& entity : g_GameState->entities) {
        if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
            if (npc->id == vendorId) {
                g_GameState->interactState.targetGuidLow = entity.guidLow;
                g_GameState->interactState.targetGuidHigh = entity.guidHigh;
            }
        }
    }
}

inline void Repair(int mapId, int numTimes, Vector3 vendorPosition, int vendorId) {
    g_GameState->interactState.location = { vendorPosition };
    g_GameState->interactState.interactId = vendorId;
    g_GameState->interactState.interactTimes = numTimes;
    g_GameState->interactState.interactActive = true;
    for (auto& entity : g_GameState->entities) {
        if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
            if (npc->id == vendorId) {
                g_GameState->interactState.targetGuidLow = entity.guidLow;
                g_GameState->interactState.targetGuidHigh = entity.guidHigh;
            }
        }
    }
}