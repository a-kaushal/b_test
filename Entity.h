#pragma once
#include <windows.h>
#include <vector>
#include <mutex>
#include <memory>
#include <string>
#include <algorithm>

#include "Vector.h"

struct EntityInfo {
    virtual ~EntityInfo() = default;
};

struct GameEntity {
    std::shared_ptr<EntityInfo> info;
    ULONG_PTR guidLow;
    ULONG_PTR guidHigh;
    long entityIndex;
    DWORD_PTR entityPtr;
    uint32_t lowCounter;
    uint32_t type;
    uint32_t instance;
    uint32_t entry;
    uint32_t mapId;
    uint32_t id;
    std::string objType;
    int32_t rotation;

    // --- THREAD-SAFE CACHE ---
    // The GUI thread will ONLY read these. They are copies.
    std::string name_safe;
    std::string reaction_safe;
    float dist_safe;
    int nodeActive_safe;

    GameEntity() : info(nullptr), guidLow(0), guidHigh(0), entityIndex(0), entityPtr(0),
        lowCounter(0), type(0), instance(0), entry(0), mapId(0), id(0), rotation(0) {
    }
};

struct PlayerInfo : EntityInfo {
    ULONG_PTR playerPtr;
    ULONG_PTR playerGuidLow;
    ULONG_PTR playerGuidHigh;
    Vector3 position;
    uint32_t state;
    uint32_t mountState;
    int32_t health;
    int32_t maxHealth;
    int32_t level;
    int32_t mapId;
    int32_t areaId;
    bool isFlying;
    bool onGround;
    bool inWater;
    bool inAir;
	bool groundMounted;
	bool flyingMounted;
    bool isMounted;
    bool areaMountable; // True if an area is flyable
    bool isIndoor; // True if player is indoors
    bool needRepair; // True if player equipment needs repairs
    int bagFreeSlots;
    ULONG_PTR inCombatGuidLow;
    ULONG_PTR inCombatGuidHigh;
    ULONG_PTR targetGuidLow;
    ULONG_PTR targetGuidHigh;
    float rotation;
    float vertRotation;
    float distance;

    int32_t corpseMapId;
    int32_t corpseAreaId; // Local Area ID (lookup from WorldMapArea.csv)
    std::string corpseMapName;
    uint32_t corpseMapHash; // Map name hash
    bool isDead; // Player is dead
    bool isGhost; // Player is dead and a ghost
    float corpseX; // Player corpse position x
    float corpseY; // Player corpse position y
    bool isDeadBody; // Player is dead but not a ghost yet
    bool canRespawn; // Player ghost in reange of body and able to respawn
};

struct OtherPlayerInfo : EntityInfo {
    Vector3 position;
};

struct EnemyInfo : EntityInfo {
    ULONG_PTR enemyPtr;
    Vector3 position;
    float distance;
    int32_t health;
    int32_t maxHealth;
    int32_t level;
    uint32_t id;
    float agroRange = 0;
    bool inCombat = false;
    std::string name;
    int reaction;  // 0 = Hostile, 1 = Neutral, 2 = Friendly
    int rank;  // (0=Normal, 1=Elite, 2=Rare Elite, 3=Boss)
    int npcFlag;
    ULONG_PTR targetGuidLow; // GUID for the unit being targetted by the entity
    ULONG_PTR targetGuidHigh;
};

struct ObjectInfo : EntityInfo {
    ULONG_PTR objectPtr;
    Vector3 position;
    float distance;
    uint32_t id;
    int type; // 1 for Herb and 2 for Ore
    int nodeActive; // 1 if not collected, 0 if collected (only for herb and ore)
    int skillLevel; // Gathering skill level required
    std::string name;
};

struct ItemInfo : EntityInfo {
    ULONG_PTR itemPtr;
    uint32_t id;
    std::string name;
    int stackCount;
    int bagID;
    ULONG_PTR bagGuidLow;
    ULONG_PTR bagGuidHigh;
};

struct BagInfo : EntityInfo {
    ULONG_PTR bagPtr;
    uint32_t id;
    int bagId;
    bool equippedBag;
    int bagSlots;
    int slotCount;
    int freeSlots;
};

struct CorpseInfo : EntityInfo {
    ULONG_PTR objectPtr;
    Vector3 position;
    float distance;
    uint32_t id;
};

 /* Sorts a vector of GameEntities by distance (Ascending).
 * Handles dynamic casting to find the distance variable in different entity types.
 */
inline void SortEntitiesByDistance(std::vector<GameEntity>& entities) {
    std::sort(entities.begin(), entities.end(), [](const GameEntity& a, const GameEntity& b) {
        float distA = 999999.0f; // Default high value (pushes unknown items to the end)
        float distB = 999999.0f;

        // 1. Extract Distance for Entity A
        if (a.info) {
            // Check if it's an Enemy
            if (auto enemy = std::dynamic_pointer_cast<EnemyInfo>(a.info)) {
                distA = enemy->distance;
            }
            // Check if it's an Object
            else if (auto object = std::dynamic_pointer_cast<ObjectInfo>(a.info)) {
                distA = object->distance;
            }
        }

        // 2. Extract Distance for Entity B
        if (b.info) {
            // Check if it's an Enemy
            if (auto enemy = std::dynamic_pointer_cast<EnemyInfo>(b.info)) {
                distB = enemy->distance;
            }
            // Check if it's an Object
            else if (auto object = std::dynamic_pointer_cast<ObjectInfo>(b.info)) {
                distB = object->distance;
            }
        }

        // 3. Compare: Closest (lowest distance) goes first
        return distA < distB;
        });
}