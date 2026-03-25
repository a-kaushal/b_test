#pragma once

#include <vector>
#include "Vector.h"
#include "MemoryRead.h"    // For PlayerInfo, GameEntity

// This allows us to return a pointer to ANY state type
struct ActionState {
    virtual ~ActionState() = default; // Virtual destructor ensures proper cleanup

    // Common variables moved here
    std::vector<PathNode> activePath = {};
    std::mutex pathMutex;
    int activeIndex = 0;
    bool actionChange = 0;
    bool flyingPath = true; // Defines if flying is enabled
    bool inMotion = false;
    bool ignoreUnderWater = true;
};

struct GlobalState : public ActionState {
    bool vendorOpen  = false;
    bool chatOpen    = false;
    bool uiBlocking  = false; // true when any interactive UI frame is open (set by addon, slot 14)
    bool reloaded    = true;
    DWORD bagEmptyTime = -1;
    int mapId = 0;
    int areaId = 0;
    std::string mapName;
    uint32_t mapHash;
    float top, bottom, left, right = 0;
    bool inMotion = false;
};

struct Looting : public ActionState {
    // Looting
    bool enabled = false;
    Vector3 position;
    std::vector<PathNode> path;
    int index;
    int mapId;
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
    int mapId;
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
    int mapId;
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
    bool flyingTarget = false;

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
    int attackerCount = 0; // Track number of active enemies
    bool hasTarget = false; // True when the bot selects a target to attack, but has not reached within range yet
    bool reset = false;
    bool pullExtraMobs = false; // when true, ActionCombat will aggro additional nearby enemies
    int  extraMobLimit = 1;    // maximum simultaneous mobs to pull (including current target)
    Vector3 enemyPosition;
    ULONG_PTR targetGuidLow;
    ULONG_PTR targetGuidHigh;
};

struct StuckState : public ActionState {
    bool isStuck = false;
    Vector3 lastPosition = { 0, 0, 0 };
    DWORD lastCheckTime = 0;
    DWORD stuckStartTime = 0;
    DWORD lastStuckTime = 0;

    int attemptCount = 0;       // Which escape stage we are on
    DWORD lastUnstuckTime = 0;  // When we last completed an unstuck
    float stuckAngle = 0.0f;    // Player yaw (radians) when stuck was detected — points toward the obstacle
};

struct InteractState : public ActionState {
    bool enabled = true;
    std::vector<PathNode> path;
    int index;
    Vector3 location;
    Vector3 inGameLocation;
    int interactId;
    int mapId;
    bool interactActive = false;
    int interactTimes = 0;
    bool resupply = false;
    bool vendorSell = false;
    bool mailing = false;
    bool repair = false;
    bool repairDone = false;
    bool vendorDone = false;
    bool sellComplete = false;
    ULONG_PTR targetGuidLow = 0;
    ULONG_PTR targetGuidHigh = 0;
    bool locationChange = false;
    DWORD locationChangeTime = -1;
};

struct RespawnState : public ActionState {
    bool enabled = true;
    std::vector<float> possibleZLayers = {}; // Stores all potential Z heights found at corpse X,Y
    int currentLayerIndex = -1;              // Which layer we are currently navigating to
    bool isPathingToCorpse = false;
    Vector3 currentTargetPos = { 0,0,0 };
    std::vector<PathNode> path;
    int index;
    uint32_t mapId;
    bool hasPath = false;
    bool isDead = false;
};

struct RecorderState : public ActionState {
    bool enabled = false;
    bool showOnOverlay = true;
    float stepDistance = 5.0f;
    std::vector<Vector3> recordedPath;
};

struct HealState : public ActionState {
    bool isHealing = false;
};

struct FleeState : public ActionState {
    bool fleeActive = false;
    Vector3 destination = { 0, 0, 0 };
};

struct GrindState : public ActionState {
    bool enabled = false;
    int index;
    std::vector<int> mobIds;   // entry IDs to target (empty = any mob within level range)
    bool killAllMobs = false;  // attack any hostile regardless of mob ID (level range still applies)
    int maxLevelMod = 5;       // engage mobs up to this many levels above player
    int minLevelMod = 0;       // do not engage mobs more than this many levels below player
    bool canFly = false;       // allowed to use a flying mount on the route
    bool loop = true;
    bool lootMobs = true;      // enable looting after kills
    int targetLevel = 0;       // stop grinding when player reaches this level (0 = no limit)
    float pullRange = 60.0f;   // yards to scan for eligible mobs (active only once inLoop is true, or when engageAlways=true)
    bool engageAlways = false; // when true, engage mobs within pullRange even before reaching the grind loop
    bool inLoop = false;       // set true once the player first reaches any hotspot
    std::string taskName;      // display name shown in logs
    std::vector<Vector3> hotspots; // the route waypoints
    int hotspotIndex = 0;
    int mapId = 0;
    std::vector<PathNode> path;
    bool hasPath = false;

    // Mobs that have failed interaction 3 times are blacklisted for 5 minutes
    // so the bot does not keep re-selecting unattackable neutral units.
    std::vector<ULONG_PTR> blacklistGuidLow  = {};
    std::vector<ULONG_PTR> blacklistGuidHigh = {};
    std::vector<DWORD>     blacklistTime     = {}; // GetTickCount() when the entry was added
};

// --- WORLD STATE (The Knowledge) ---
struct WorldState {
    GlobalState globalState;
    ProfileSettings settings;

    Looting lootState;
    Gathering gatherState;
    PathFollowing pathFollowState;
    WaypointReturn waypointReturnState;
    Combat combatState;
    StuckState stuckState;
    InteractState interactState;
    RespawnState respawnState;
    RecorderState recorder;
    HealState healState;
    FleeState fleeState;
    GrindState grindState;

    std::vector<GameEntity> entities;
    PlayerInfo player;

    bool overrideAction = false; // True if the current profile action is being overridden with the same action
};