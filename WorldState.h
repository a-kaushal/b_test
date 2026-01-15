#pragma once

#include <vector>
#include "Vector.h"
#include "Pathfinding2.h"  // For PathNode
#include "MemoryRead.h"    // For PlayerInfo, GameEntity

// This allows us to return a pointer to ANY state type
struct ActionState {
    virtual ~ActionState() = default; // Virtual destructor ensures proper cleanup

    // Common variables moved here
    std::vector<PathNode> activePath = {};
    int activeIndex = 0;
    bool actionChange = 0;
    bool flyingPath = true;
    bool inMotion = false;
    bool ignoreUnderWater = true;
    int bagFreeSlots;
};

struct GlobalState : public ActionState {
};

struct Looting : public ActionState {
    // Looting
    bool enabled = false;
    Vector3 position;
    std::vector<PathNode> path;
    int index;
    bool hasLoot = false;
    ULONG_PTR guidLow;
    ULONG_PTR guidHigh;
};

struct Gathering : public ActionState {
    // Gathering (Nodes/Herbs)
    bool enabled = true;
    Vector3 position;
    std::vector<PathNode> path;
    bool hasNode = false;
    bool nodeActive = false;
    int index;
    ULONG_PTR guidLow;
    ULONG_PTR guidHigh;
    std::vector<ULONG_PTR> gatheredNodesGuidLow = {};
    std::vector<ULONG_PTR> gatheredNodesGuidHigh = {};
    std::vector<ULONG_PTR> blacklistNodesGuidLow = {};
    std::vector<ULONG_PTR> blacklistNodesGuidHigh = {};
    std::vector<DWORD> blacklistTime = {};
};

struct PathFollowing : public ActionState {
    // Follow a set path
    bool enabled = true;
    std::vector<PathNode> path;
    std::vector<Vector3> presetPath;
    int index;
    int presetIndex;
    bool looping = true; // If true the path repeats from the beginning when it finishes
    bool startNearest = true; // If true starts at the closest waypoint to player
    bool hasPath = false;
    bool pathIndexChange = false;
};

struct WaypointReturn : public ActionState {
    // Follow a set path
    bool enabled = true;
    std::vector<PathNode> path = {};
    std::vector<PathNode> savedPath = {};
    int index;
    int savedIndex;
    bool hasTarget = false;
    bool hasPath = false;

    int pathfindingAttempts = 0;
    bool waitingForUnstuck = false;
    DWORD lastPathAttemptTime = 0;
};

struct Combat : public ActionState {
    bool enabled = true;
    std::vector<PathNode> path;
    int32_t targetHealth;
    bool inCombat = false; // True when bot is within range of enemy to perform rotation
    int index;
    int entityIndex;
    bool underAttack = false; // True when bot is under attack from a enemy
    bool hasTarget = false; // True when the bot selects a target to attack, but has not reached within range yet
    Vector3 enemyPosition;
    ULONG_PTR targetGuidLow;
    ULONG_PTR targetGuidHigh;
};

struct StuckState : public ActionState {
    bool isStuck = false;
    Vector3 lastPosition = { 0, 0, 0 };
    DWORD lastCheckTime = 0;
    DWORD stuckStartTime = 0;

    // --- UPDATED TRACKING ---
    int attemptCount = 0;           // Tracks which stage (Jump, Strafe L, Strafe R, etc.) we are on
    DWORD lastUnstuckTime = 0;      // Tracks when we last performed an unstuck action
};

struct InteractState : public ActionState {
    bool enabled = true;
    std::vector<PathNode> path;
    int index;
    Vector3 location;
    int interactId;
    bool interactActive = false;
    int interactTimes = 0;
    ULONG_PTR targetGuidLow = 0;
    ULONG_PTR targetGuidHigh = 0;
};

// --- WORLD STATE (The Knowledge) ---
struct WorldState {
    GlobalState globalState;
    Looting lootState;
    Gathering gatherState;
    PathFollowing pathFollowState;
    WaypointReturn waypointReturnState;
    Combat combatState;
    StuckState stuckState;
    InteractState interactState;

    std::vector<GameEntity> entities;
    PlayerInfo player;

    // Danger / Interrupts
    bool isInDanger = false; // e.g., standing in fire
    Vector3 dangerPos;       // Where the fire is    
};