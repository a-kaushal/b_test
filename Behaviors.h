#pragma once
#include <vector>
#include <iostream>
#include <memory>
#include <sstream>

#include "ProfileInterface.h"
#include "Vector.h"

// --- COMPILE-TIME GUARD ---
// The Main EXE needs ProfileLoader for interrupts.
// The Profile DLL (ActiveProfile.dll) DOES NOT, so we skip it to prevent linking errors.
#ifndef COMPILING_PROFILE
#include "ProfileLoader.h"
#include "dllmain.h" // Access to g_GameState
#endif

// =============================================================
// 1. TASK DEFINITIONS
// =============================================================

// Helper: Parse "(x,y,z), (x,y,z)" string
inline std::vector<Vector3> ParsePathStr(const std::string& input) {
    std::vector<Vector3> path;
    size_t currentPos = 0;
    while (true) {
        size_t start = input.find('(', currentPos);
        if (start == std::string::npos) break;
        size_t end = input.find(')', start);
        if (end == std::string::npos) break;
        std::string content = input.substr(start + 1, end - start - 1);
        float x, y, z;
        char comma;
        std::stringstream ss(content);
        if (ss >> x >> comma >> y >> comma >> z) path.push_back(Vector3(x, y, z));
        currentPos = end + 1;
    }
    return path;
}

// Task: Follow Path
class TaskFollowPath : public IProfileTask {
    std::vector<Vector3> path;
    bool looping;
    bool flying;
    bool started = false;
public:
    TaskFollowPath(int mapId, std::string pathStr, bool fly, bool loop)
        : looping(loop), flying(fly) {
        path = ParsePathStr(pathStr);
    }
    std::string GetName() const override { return "Follow Path"; }
    void OnInterrupt() override { }
    bool Execute(WorldState* state) override {
        if (started && !state->pathFollowState.enabled) {
            return true;
        }

        if (!started) {
            std::vector<PathNode> empty = {};

            state->pathFollowState.presetPath = path;
            state->pathFollowState.presetIndex = FindClosestWaypoint(state->pathFollowState.presetPath, empty, g_GameState->player.position);
            state->pathFollowState.looping = looping;
            state->pathFollowState.flyingPath = flying;
            state->pathFollowState.enabled = true;
            state->pathFollowState.hasPath = true;
            state->pathFollowState.pathIndexChange = true;
            started = true;
            return false;
        }
        return false; // Done when engine disables itself
    }
};

// Task: Interact
class TaskInteract : public IProfileTask {
    Vector3 loc;
    int npcId;
    bool isRepair;
    bool started = false;
public:
    TaskInteract(int mapId, Vector3 location, int id, bool repair)
        : loc(location), npcId(id), isRepair(repair) {
    }
    std::string GetName() const override { return isRepair ? "Repairing" : "Selling"; }
    void OnInterrupt() override { }
    bool Execute(WorldState* state) override {
        // If interaction is active, but the ID doesn't match ours, 
        // it means dllmain/Repair triggered a high-priority override.
        if (state->interactState.interactActive && state->interactState.interactId != npcId) {
            started = false; // Reset ourselves so we retry later
            return false;    // Yield immediately
        }

        if (!started) {
            // NOTE: In the DLL, we rely on the generic interact state. 
            // We cannot call global functions like Repair() here because they are in the Main EXE.
            // We set the state manually.
            state->interactState.location = loc;
            state->interactState.interactId = npcId;
            state->interactState.interactTimes = 1;
            state->interactState.interactActive = true;
            state->interactState.repair = isRepair;
            state->interactState.vendorSell = !isRepair;
            // GUID lookup happens in the main loop Tick
            started = true;
            return false;
        }
        return started && !state->interactState.interactActive;
    }
};

// =============================================================
// 2. HELPER CLASS (The Fix for your Profile)
// =============================================================
class BehaviorProfile : public BotProfile {
public:
    void QueuePath(int mapId, std::string path, bool flying = true, bool looping = false) {
        AddTask(std::make_shared<TaskFollowPath>(mapId, path, flying, looping));
    }

    void QueueResupply(int mapId, Vector3 loc, int npcId) {
        AddTask(std::make_shared<TaskInteract>(mapId, loc, npcId, false));
    }

    void QueueRepair(int mapId, Vector3 loc, int npcId) {
        AddTask(std::make_shared<TaskInteract>(mapId, loc, npcId, true));
    }
};

// =============================================================
// 3. GLOBAL INTERRUPT LOGIC (Main EXE Only)
// =============================================================
#ifndef COMPILING_PROFILE
inline void InteractWithObject(int mapId, int numTimes, Vector3 position, int objectId) {
    // --- DETECT OVERWRITE ---
    // If we are about to start a NEW interaction, but one is ALREADY active...
    if (g_GameState->interactState.interactActive) {
        // Check if the IDs are different
        if (g_GameState->interactState.interactId != objectId) {
        // if (true) {
            // Notify the Active Profile Task (if it exists)
            if (auto* profile = g_ProfileLoader.GetActiveProfile()) {
                if (!profile->taskQueue.empty()) {
                    g_LogFile << "[System] High-Priority Interrupt! Resetting Profile Task." << std::endl;
                    profile->taskQueue.front()->OnInterrupt();
                    // Reset flags & GUIDs
                    g_GameState->interactState.path.clear();
                    g_GameState->interactState.index = 0;

                    g_GameState->interactState.repair = false;
                    g_GameState->interactState.mailing = false;
                    g_GameState->interactState.vendorSell = false;
                    g_GameState->interactState.targetGuidLow = 0;
                    g_GameState->interactState.targetGuidHigh = 0;
                }
            }
        }
    }

    g_GameState->interactState.location = { position };
    g_GameState->interactState.interactId = objectId;
    g_GameState->interactState.interactTimes = numTimes;
    g_GameState->interactState.interactActive = true;

    //g_LogFile << "A " << g_GameState->interactState.targetGuidLow << " " << g_GameState->interactState.targetGuidHigh << std::endl;
    for (auto& entity : g_GameState->entities) {
        if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
            if (npc->id == objectId) {
                g_GameState->interactState.targetGuidLow = entity.guidLow;
                g_GameState->interactState.targetGuidHigh = entity.guidHigh;
                break;
            }
        }
        else if (auto object = std::dynamic_pointer_cast<ObjectInfo>(entity.info)) {
            if (object->id == objectId) {
                g_GameState->interactState.targetGuidLow = entity.guidLow;
                g_GameState->interactState.targetGuidHigh = entity.guidHigh;
                break;
            }
        }
    }
}

// Wrappers
inline void MailItems(int mapId, Vector3 position, int mailboxId) { InteractWithObject(mapId, 1, position, mailboxId); g_GameState->interactState.mailing = true; }
inline void Resupply(int mapId, int numTimes, Vector3 vendorPosition, int vendorId) { InteractWithObject(mapId, numTimes, vendorPosition, vendorId); g_GameState->interactState.vendorSell = true; }
inline void Repair(int mapId, int numTimes, Vector3 vendorPosition, int vendorId) { InteractWithObject(mapId, numTimes, vendorPosition, vendorId); g_GameState->interactState.repair = true; }
#endif