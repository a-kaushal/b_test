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

// Helper: Drops points that are too close to the previous point
inline std::vector<Vector3> FilterPath(const std::vector<Vector3>& rawPath, float minDistance) {
    if (rawPath.empty()) return {};

    std::vector<Vector3> cleanPath;
    cleanPath.reserve(rawPath.size());

    // Always keep the starting point
    cleanPath.push_back(rawPath[0]);

    for (size_t i = 1; i < rawPath.size(); ++i) {
        // Only add the point if it is far enough from the LAST accepted point
        if (rawPath[i].Dist3D(cleanPath.back()) >= minDistance) {
            cleanPath.push_back(rawPath[i]);
        }
    }
    return cleanPath;
}

// Task: Follow Path
class TaskFollowPath : public IProfileTask {
    std::vector<Vector3> path;
    bool looping;
    bool flying;
    bool flyingPath;
    bool started = false;
    int mapId;
public:
    TaskFollowPath(int mapId, std::string pathStr, bool fly, bool flyingPath, bool loop)
        : looping(loop), flying(fly), mapId(mapId), flyingPath(flyingPath) {
        std::vector<Vector3> rawPath = ParsePathStr(pathStr);
        // Clean it up (Minimum 20 yards between points)
        // This removes "stutter steps" or points that are too dense
        path = FilterPath(rawPath, 40.0f);
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
            state->pathFollowState.flyingPath = flyingPath;
            state->pathFollowState.enabled = true;
            state->pathFollowState.hasPath = true;
            state->pathFollowState.pathIndexChange = true;
            state->pathFollowState.mapId = mapId;
            state->waypointReturnState.flyingPath = flyingPath;
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

// Helper: Parse comma-separated integer IDs e.g. "25817, 25818, 25819"
inline std::vector<int> ParseMobIds(const std::string& input) {
    std::vector<int> ids;
    std::stringstream ss(input);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        size_t s = token.find_first_not_of(" \t");
        size_t e = token.find_last_not_of(" \t");
        if (s != std::string::npos)
            ids.push_back(std::stoi(token.substr(s, e - s + 1)));
    }
    return ids;
}

// Task: Grind Mobs
class TaskGrind : public IProfileTask {
    std::vector<Vector3> hotspots;
    int              mapId;
    std::vector<int> mobIds;
    bool             killAllMobs;
    bool             canFly;
    int              maxLevelMod;
    int              minLevelMod;
    bool             loop;
    bool             lootMobs;
    int              targetLevel;
    float            pullRange;
    bool             engageAlways;
    int              extraMobLimit;
    std::string      taskName;
    bool             started = false;
public:
    TaskGrind(int mapId, const std::string& hotspotsStr,
              const std::string& mobIdsStr, bool killAllMobs,
              bool canFly, int maxLevelMod, int minLevelMod, bool loop,
              bool lootMobs, int targetLevel,
              float pullRange, bool engageAlways,
              int extraMobLimit,
              const std::string& taskName)
        : mapId(mapId), killAllMobs(killAllMobs), canFly(canFly),
          maxLevelMod(maxLevelMod), minLevelMod(minLevelMod), loop(loop),
          lootMobs(lootMobs), targetLevel(targetLevel),
          pullRange(pullRange), engageAlways(engageAlways),
          extraMobLimit(extraMobLimit), taskName(taskName) {
        mobIds = ParseMobIds(mobIdsStr);
        std::vector<Vector3> raw = ParsePathStr(hotspotsStr);
        // Preserve hotspot density — use a gentle filter
        hotspots = FilterPath(raw, 10.0f);
    }

    std::string GetName() const override { return taskName.empty() ? "Grind" : taskName; }
    void OnInterrupt() override { }

    bool Execute(WorldState* state) override {
        if (started && !state->grindState.enabled) {
            return true; // Engine disabled us — task complete
        }

        // Stop when player reaches target level
        if (started && targetLevel > 0 && g_GameState->player.level >= targetLevel) {
            state->grindState.enabled = false;
            state->grindState.hasPath = false;
            return true;
        }

        if (!started) {
            std::vector<PathNode> emptyNav = {};
            state->grindState.hotspots              = hotspots;
            state->grindState.hotspotIndex          = FindClosestWaypoint(hotspots, emptyNav, g_GameState->player.position);
            state->grindState.mobIds                = mobIds;
            state->grindState.killAllMobs           = killAllMobs;
            state->grindState.canFly                = canFly;
            state->grindState.maxLevelMod           = maxLevelMod;
            state->grindState.minLevelMod           = minLevelMod;
            state->grindState.loop                  = loop;
            state->grindState.lootMobs              = lootMobs;
            state->grindState.targetLevel           = targetLevel;
            state->grindState.pullRange             = pullRange;
            state->grindState.engageAlways          = engageAlways;
            state->grindState.inLoop                = false;
            state->grindState.taskName              = taskName;
            state->combatState.extraMobLimit        = extraMobLimit;
            state->grindState.mapId                 = mapId;
            state->grindState.path.clear();
            state->grindState.index                 = 0;
            state->grindState.enabled               = true;
            state->grindState.hasPath               = true;
            // Apply looting preference
            state->lootState.enabled                = lootMobs;
            state->combatState.enabled              = true;
            state->waypointReturnState.flyingPath   = canFly;
            started = true;
            return false;
        }
        return false; // Running until engine disables grindState
    }
};

// =============================================================
// 2. HELPER CLASS (The Fix for your Profile)
// =============================================================
class BehaviorProfile : public BotProfile {
public:
    void QueuePath(int mapId, std::string path, bool flying = true, bool flyingPath = true, bool looping = false) {
        AddTask(std::make_shared<TaskFollowPath>(mapId, path, flying, flyingPath, looping));
    }

    void QueueResupply(int mapId, Vector3 loc, int npcId) {
        AddTask(std::make_shared<TaskInteract>(mapId, loc, npcId, false));
    }

    void QueueRepair(int mapId, Vector3 loc, int npcId) {
        AddTask(std::make_shared<TaskInteract>(mapId, loc, npcId, true));
    }

    // mobIds: comma-separated entry IDs e.g. "25817, 25818". Empty = any mob in level range.
    // killAllMobs: attack any hostile regardless of ID or level range.
    // maxLevelMod / minLevelMod: levels above / below the player that are acceptable targets.
    // canFly: allow a flying mount on the grind route.
    // lootMobs: enable looting after kills (default true).
    // targetLevel: stop when player reaches this level (0 = no limit).
    // pullRange: scan radius in yards for eligible mobs (active only once inLoop is true, or when engageAlways=true).
    // engageAlways: when true, engage mobs within pullRange even before reaching the grind loop.
    // taskName: display name for logs.
    void QueueGrind(int mapId, const std::string& hotspots,
                    const std::string& mobIds = "",
                    bool killAllMobs = false, bool canFly = false,
                    int maxLevelMod = 5, int minLevelMod = 0, bool loop = true,
                    bool lootMobs = true, int targetLevel = 0,
                    float pullRange = 60.0f, bool engageAlways = false,
                    const std::string& taskName = "",
                    int extraMobLimit = 1) {
        AddTask(std::make_shared<TaskGrind>(mapId, hotspots, mobIds, killAllMobs, canFly,
                                            maxLevelMod, minLevelMod, loop,
                                            lootMobs, targetLevel, pullRange, engageAlways,
                                            extraMobLimit, taskName));
    }
};

// Helper function to find best mailbox/vendor
void FindNearestTarget(Vector3 target, Vector3 start, int& mapId, Vector3& position, int& objId) {

}

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
    g_GameState->interactState.inGameLocation = { position };
    g_GameState->interactState.mapId = mapId;

    //g_LogFile << "A " << g_GameState->interactState.targetGuidLow << " " << g_GameState->interactState.targetGuidHigh << std::endl;
    for (auto& entity : g_GameState->entities) {
        if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
            if (npc->id == objectId) {
                g_GameState->interactState.targetGuidLow = entity.guidLow;
                g_GameState->interactState.targetGuidHigh = entity.guidHigh;
                g_GameState->interactState.inGameLocation = { npc->position };
                if ((g_GameState->interactState.inGameLocation != g_GameState->interactState.location) && (g_GameState->interactState.inGameLocation.Dist3D(g_GameState->player.position) < 50.0f)) {
                    // && (g_GameState->interactState.locationChangeTime != -1) && (g_GameState->interactState.locationChangeTime - GetTickCount() > 2000)
                    g_GameState->interactState.locationChange = true;
                    g_GameState->interactState.location = { npc->position };
                    //g_GameState->interactState.locationChangeTime = GetTickCount();
                }
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
//inline void MailItems(int mapId, Vector3 position, int mailboxId) { InteractWithObject(mapId, 1, position, mailboxId); g_GameState->interactState.mailing = true; }
//inline void Resupply(int mapId, int numTimes, Vector3 vendorPosition, int vendorId) { InteractWithObject(mapId, numTimes, vendorPosition, vendorId); g_GameState->interactState.vendorSell = true; }
//inline void Repair(int mapId, int numTimes, Vector3 vendorPosition, int vendorId) { InteractWithObject(mapId, numTimes, vendorPosition, vendorId); g_GameState->interactState.repair = true; }
inline void MailItems() {
    int mapId;
    Vector3 position;
    int mailboxId;

    int nearestIndex = -1;
    float nearestDist = 99999.0f;

    for (int i = 0; i < g_ProfileSettings.mailboxes.size(); i++) {
        if (g_ProfileSettings.mailboxes[i].Position.Dist3D(g_GameState->player.position) < nearestDist) {
            nearestDist = g_ProfileSettings.mailboxes[i].Position.Dist3D(g_GameState->player.position);
            nearestIndex = i;
        }
    }
    if (nearestIndex != -1) {
        mapId = g_ProfileSettings.mailboxes[nearestIndex].MapId;
        position = g_ProfileSettings.mailboxes[nearestIndex].Position;
        mailboxId = g_ProfileSettings.mailboxes[nearestIndex].Id;
    }
    else {
        g_LogFile << "No mailbox found in profile" << std::endl;
    }
    InteractWithObject(mapId, 1, position, mailboxId);
    g_GameState->interactState.mailing = true; 
}

inline void Resupply() {
    int mapId;
    Vector3 position;
    int vendorId;

    int nearestIndex = -1;
    float nearestDist = 99999.0f;

    for (int i = 0; i < g_ProfileSettings.vendors.size(); i++) {
        if ((g_ProfileSettings.vendors[i].Type == VendorType::Repair) && (g_ProfileSettings.vendors[i].Position.Dist3D(g_GameState->player.position) < nearestDist)) {
            nearestDist = g_ProfileSettings.vendors[i].Position.Dist3D(g_GameState->player.position);
            nearestIndex = i;
        }
    }
    if (nearestIndex != -1) {
        mapId = g_ProfileSettings.vendors[nearestIndex].MapId;
        position = g_ProfileSettings.vendors[nearestIndex].Position;
        vendorId = g_ProfileSettings.vendors[nearestIndex].Id;
    }
    else {
        g_LogFile << "No resupply vendor found in profile" << std::endl;        
    }
    InteractWithObject(mapId, 1, position, vendorId);
    g_GameState->interactState.vendorSell = true; 
}

inline void Repair() {
    int mapId;
    Vector3 position;
    int vendorId;

    int nearestIndex = -1;
    float nearestDist = 99999.0f;

    for (int i = 0; i < g_ProfileSettings.vendors.size(); i++) {
        if ((g_ProfileSettings.vendors[i].Type == VendorType::Repair) && (g_ProfileSettings.vendors[i].Position.Dist3D(g_GameState->player.position) < nearestDist)) {
            nearestDist = g_ProfileSettings.vendors[i].Position.Dist3D(g_GameState->player.position);
            nearestIndex = i;
        }
    }
    if (nearestIndex != -1) {
        mapId = g_ProfileSettings.vendors[nearestIndex].MapId;
        position = g_ProfileSettings.vendors[nearestIndex].Position;
        vendorId = g_ProfileSettings.vendors[nearestIndex].Id;
    }
    else {
        g_LogFile << "No resupply vendor found in profile" << std::endl;
    }
    InteractWithObject(mapId, 1, position, vendorId);
    g_GameState->interactState.repair = true; }
#endif