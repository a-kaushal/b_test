#include <windows.h>
#include <thread>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <memory>
#include <cstring>
#include <algorithm>
#include <tlhelp32.h>
#include <cstdint>
#include <cmath>

#include "SimpleKeyboardClient.h"
#include "GameGui.h"
#include "PathFinding2.h"
#include "Movement.h"
#include "Vector.h"
#include "Database.h"
#include "MovementController.h"
#include "GoapSystem.h"
#include "ScreenRenderer.h"
#include "OverlayWindow.h"
#include "Gathering.h"

// Global Atomic Flag to control all threads
#include <atomic>
std::atomic<bool> g_IsRunning(true);

// Global Databases
WoWDataTool creature_db;
WoWDataTool item_db;
WoWDataTool object_db;

struct GuidParts {
    uint32_t type;
    uint32_t realm;
    uint32_t map;
    uint32_t instance;
    uint32_t entry;
    uint32_t low;
};

DWORD GetProcId(const wchar_t* procName) {
    PROCESSENTRY32W procEntry;
    procEntry.dwSize = sizeof(procEntry);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(hSnap, &procEntry)) {
        do {
            if (std::wcscmp(procEntry.szExeFile, procName) == 0) {
                CloseHandle(hSnap);
                return procEntry.th32ProcessID;
            }
        } while (Process32NextW(hSnap, &procEntry));
    }
    CloseHandle(hSnap);
    return 0;
}

// ---------------------------------------------------------
// Heuristic: Brute Force Scan of ASLR Range (Bypasses Enumeration)
// ---------------------------------------------------------
ULONG_PTR FindMainModuleViaDriver(MemoryAnalyzer& analyzer, DWORD pid) {
    std::cout << "Starting Brute Force Header Scan..." << std::endl;
    std::cout << "Scanning range: 0x7FF600000000 - 0x7FF7FFFFFFFF" << std::endl;

    // Standard Windows x64 Load Range for Executables
    ULONG_PTR start = 0x7FF600000000;
    ULONG_PTR end = 0x7FF800000000;

    // Windows allocates memory on 64KB boundaries (0x10000)
    // We only need to check the start of every 64KB chunk.
    ULONG_PTR step = 0x10000;

    // Helper buffer
    int16_t dosMagic = 0;
    int32_t e_lfanew = 0;
    int16_t characteristics = 0;
    int32_t sizeOfImage = 0;

    for (ULONG_PTR addr = start; addr < end; addr += step) {

        // 1. Try to read the first 2 bytes (Fastest check)
        // If this fails, the memory is not valid/committed, so we move on.
        if (!analyzer.ReadInt16(pid, addr, dosMagic, true)) {
            continue;
        }

        // 2. Check for "MZ"
        if (dosMagic == 0x5A4D) {

            // 3. We found a Module! Now verify it's NOT a DLL.
            // Read e_lfanew (Offset to PE Header)
            if (!analyzer.ReadInt32(pid, addr + 0x3C, e_lfanew)) continue;

            // Sanity check offset
            if (e_lfanew > 0x1000 || e_lfanew < 0) continue;

            // Read Characteristics (Offset 0x16 inside File Header)
            // PE Signature (4) + File Header (20) -> Characteristics is at +18 (0x12) of File Header
            // So: addr + e_lfanew + 4 + 0x12 = addr + e_lfanew + 0x16
            if (analyzer.ReadInt16(pid, addr + e_lfanew + 0x16, characteristics)) {

                // Check if DLL flag (0x2000) is NOT set
                if ((characteristics & 0x2000) == 0) {

                    // 4. Double check size (WoW is big)
                    // SizeOfImage is at OptionalHeader + 0x38
                    // OptionalHeader starts at e_lfanew + 4 + 20 = e_lfanew + 0x18
                    // So: addr + e_lfanew + 0x18 + 0x38 = addr + e_lfanew + 0x50
                    if (analyzer.ReadInt32(pid, addr + e_lfanew + 0x50, sizeOfImage)) {

                        if (sizeOfImage > 5 * 1024 * 1024) { // > 5MB
                            std::cout << ">>> FOUND MATCH <<<" << std::endl;
                            std::cout << "Base: 0x" << std::hex << addr << std::dec << std::endl;
                            std::cout << "Size: " << (sizeOfImage / 1024 / 1024) << " MB" << std::endl;
                            return addr;
                        }
                    }
                }
            }
        }

        // Progress indicator every ~1GB
        if ((addr % 0x10000000) == 0) {
            // std::cout << "." << std::flush; // Uncomment for visual progress
        }
    }

    std::cerr << "Brute force scan failed." << std::endl;
    return 0;
}

// Reads big-endian 64-bit
static uint64_t ReadBE64(const uint8_t* p)
{
    return (uint64_t)p[0] << 56 |
        (uint64_t)p[1] << 48 |
        (uint64_t)p[2] << 40 |
        (uint64_t)p[3] << 32 |
        (uint64_t)p[4] << 24 |
        (uint64_t)p[5] << 16 |
        (uint64_t)p[6] << 8 |
        (uint64_t)p[7];
}

// Extract bits from a simulated 128-bit BE value
static uint64_t Extract128Bits(uint64_t hi, uint64_t lo, int start, int bits)
{
    // bit 0 = least significant bit of the 128-bit number
    // but we have big-endian layout, so:
    // full128 = (hi << 64) | lo

    if (start < 64)
    {
        // Entire field is in 'lo'
        uint64_t mask = (bits == 64) ? ~0ULL : ((1ULL << bits) - 1ULL);
        return (lo >> start) & mask;
    }
    else
    {
        // Field is in 'hi'
        int s = start - 64;
        uint64_t mask = (bits == 64) ? ~0ULL : ((1ULL << bits) - 1ULL);
        return (hi >> s) & mask;
    }
}

uint64_t get_bits_u128(uint64_t low, uint64_t high, int start, int len)
{
    // start = bit index from LSB of the 128-bit value (low is LSB 0..63).
    if (len == 0) return 0;
    if (start < 64 && (start + len) <= 64) {
        // entirely inside low
        uint64_t mask = (len == 64) ? ~0ULL : ((1ULL << len) - 1ULL);
        return (low >> start) & mask;
    }
    if (start >= 64 && (start + len) <= 128) {
        // entirely inside high
        int s = start - 64;
        uint64_t mask = (len == 64) ? ~0ULL : ((1ULL << len) - 1ULL);
        return (high >> s) & mask;
    }
    // crosses boundary: take lower part from low and upper part from high
    int lowPart = 64 - start;           // how many bits in low from start to end
    int highPart = len - lowPart;       // remaining bits in high
    uint64_t lowmask = (lowPart == 64) ? ~0ULL : ((1ULL << lowPart) - 1ULL);
    uint64_t lowbits = (low >> start) & lowmask;
    uint64_t highmask = (highPart == 64) ? ~0ULL : ((1ULL << highPart) - 1ULL);
    uint64_t highbits = (high & highmask) << lowPart;
    return lowbits | highbits;
}

void GUIDBreakdown(uint32_t& low_counter, uint32_t& type_field, uint32_t& instance, uint32_t& id, uint32_t& map_id, uint32_t& server_id, ULONG_PTR guidLow, ULONG_PTR guidHigh) {
    low_counter = (uint32_t)get_bits_u128(guidLow, guidHigh, 0, 32);    // bits 0..31
    type_field = (uint32_t)get_bits_u128(guidLow, guidHigh, 18, 3);    // bits 18..20
    instance = (uint32_t)get_bits_u128(guidLow, guidHigh, 40, 6);    // bits 40..45
    id = (uint32_t)get_bits_u128(guidLow, guidHigh, 70, 24);   // bits 70..93
    map_id = (uint32_t)get_bits_u128(guidLow, guidHigh, 93, 13);   // bits 93..105
    server_id = (uint32_t)get_bits_u128(guidLow, guidHigh, 106, 16);  // bits 106..121
}

std::vector<GameEntity> ExtractEntities(MemoryAnalyzer& analyzer, DWORD procId, ULONG_PTR hashArray, int hashArrayMaximum, ULONG_PTR entityArray, PlayerInfo& playerInfo, GoapAgent& agent, bool playerOnly = false) {
    std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);

    std::vector<GameEntity> entityList; // This will hold all our data
    PlayerInfo newPlayer = {};

    if (playerOnly == true) {
        if (playerInfo.playerPtr != 0) {
            int i = 0;
            newPlayer.playerPtr = playerInfo.playerPtr;

            analyzer.ReadFloat(procId, playerInfo.playerPtr + ENTITY_POSITION_X_OFFSET, newPlayer.position.x);
            analyzer.ReadFloat(procId, playerInfo.playerPtr + ENTITY_POSITION_Y_OFFSET, newPlayer.position.y);
            analyzer.ReadFloat(procId, playerInfo.playerPtr + ENTITY_POSITION_Z_OFFSET, newPlayer.position.z);
            analyzer.ReadFloat(procId, playerInfo.playerPtr + ENTITY_ROTATION_OFFSET, newPlayer.rotation);
            analyzer.ReadUInt32(procId, playerInfo.playerPtr + ENTITY_PLAYER_STATE_OFFSET, newPlayer.state);

            (((newPlayer.state & (1 << 24)) >> 24) == 1) ? newPlayer.isFlying = true : newPlayer.isFlying = false;
            (((newPlayer.state & (1 << 23)) >> 23) == 1) ? newPlayer.flyingMounted = true : newPlayer.flyingMounted = false;
            (((newPlayer.state & (1 << 20)) >> 20) == 1) ? newPlayer.inWater = true : newPlayer.inWater = false;

            analyzer.ReadUInt32(procId, playerInfo.playerPtr + ENTITY_PLAYER_MOUNT_STATE, newPlayer.mountState);
            //(newPlayer.mountState == 1) ? newPlayer.groundMounted = true : newPlayer.groundMounted = false;
            (newPlayer.mountState >= 1) ? newPlayer.isMounted = true : newPlayer.isMounted = false;

            analyzer.ReadInt32(procId, playerInfo.playerPtr + ENTITY_PLAYER_HEALTH, newPlayer.health);

            playerInfo = newPlayer;
            return entityList;
        }
        else {
            std::cerr << "Player pointer is invalid, reading all entities!" << std::endl;
		}
	}    

    for (int i = 0; i < hashArrayMaximum; ++i) {
        ULONG_PTR EntryGuidLow, EntryGuidHigh, EntityIndex;

        analyzer.ReadPointer(procId, hashArray + (i * 24), EntryGuidLow);
        analyzer.ReadPointer(procId, hashArray + (i * 24) + 0x8, EntryGuidHigh);
        analyzer.ReadPointer(procId, hashArray + (i * 24) + 0x10, EntityIndex);

        long EntInd = EntityIndex & 0x3FFFFFFF;

        // Valid check
        if (!(EntryGuidHigh == 0 && EntryGuidLow == 0) && !(EntryGuidLow == 1 && EntryGuidHigh == 0x400000000000000)) {

            DWORD_PTR entityBuilderPtr, entity_ptr;
            int32_t objType;
            ULONG_PTR guidLow, guidHigh;

            analyzer.ReadPointer(procId, entityArray + ((int)EntInd * ENTITY_BUILDER_ARRAY_ITEM), entityBuilderPtr);
            analyzer.ReadPointer(procId, entityBuilderPtr + ENTITY_ENTRY_OFFSET, entity_ptr);
            analyzer.ReadInt32(procId, entity_ptr + ENTITY_OBJECT_TYPE_OFFSET, objType);
            analyzer.ReadPointer(procId, entity_ptr + ENTITY_GUID_LOW_OFFSET, guidLow);
            analyzer.ReadPointer(procId, entity_ptr + ENTITY_GUID_HIGH_OFFSET, guidHigh);
            uint32_t low_counter, type_field, instance, id, map_id, server_id;
            GUIDBreakdown(low_counter, type_field, instance, id, map_id, server_id, guidLow, guidHigh);

            // Filter for specific types
            if ((objType == 3) || (objType == 7) || (objType == 33) || (objType == 225) || (objType == 257)) {

                // 3. Store the data in our struct
                GameEntity newEntity;
                newEntity.guidLow = EntryGuidLow;
                newEntity.guidHigh = EntryGuidHigh;
                newEntity.entityIndex = EntInd;
                newEntity.entityPtr = entity_ptr;
                newEntity.type = objType;
                if (objType == 3) {
                    newEntity.objType = "Item";
                    auto itemInfo = std::make_shared<ItemInfo>();
                    newEntity.info = itemInfo;

					analyzer.ReadInt32(procId, entity_ptr + PLAYER_ITEM_QUANTITY, std::dynamic_pointer_cast<ItemInfo>(newEntity.info)->stackCount);
                    analyzer.ReadPointer(procId, entity_ptr + PLAYER_BAG_LOW_GUID, std::dynamic_pointer_cast<ItemInfo>(newEntity.info)->bagGuidLow);
                    analyzer.ReadPointer(procId, entity_ptr + PLAYER_BAG_HIGH_GUID, std::dynamic_pointer_cast<ItemInfo>(newEntity.info)->bagGuidHigh);
                    analyzer.ReadUInt32(procId, entity_ptr + PLAYER_BAG_ITEM_ID, std::dynamic_pointer_cast<ItemInfo>(newEntity.info)->id);

                    id = std::dynamic_pointer_cast<ItemInfo>(newEntity.info)->id;

                    string rawData = item_db.getRawLine(std::dynamic_pointer_cast<ItemInfo>(newEntity.info)->id);
                    if (!rawData.empty()) {
                        std::dynamic_pointer_cast<ItemInfo>(newEntity.info)->name = item_db.getColumn(rawData, ITEM_NAME_COLUMN_INDEX);
                    }
				}
                if (objType == 7) {
                    newEntity.objType = "Bag";
                    auto bagInfo = std::make_shared<BagInfo>();
                    newEntity.info = bagInfo;

                    analyzer.ReadUInt32(procId, entity_ptr + PLAYER_BAG_ITEM_ID, std::dynamic_pointer_cast<BagInfo>(newEntity.info)->id);
                    analyzer.ReadInt32(procId, entity_ptr + PLAYER_BAG_SLOTS, std::dynamic_pointer_cast<BagInfo>(newEntity.info)->bagSlots);

                    id = std::dynamic_pointer_cast<BagInfo>(newEntity.info)->id;
                }
                if (objType == 33) {
                    newEntity.objType = "NPC";
                    auto enemyInfo = std::make_shared<EnemyInfo>();
                    newEntity.info = enemyInfo;

                    analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_X_OFFSET, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->position.x);
                    analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_Y_OFFSET, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->position.y);
                    analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_Z_OFFSET, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->position.z);
                    analyzer.ReadPointer(procId, entity_ptr + ENTITY_ENEMY_IN_COMBAT_GUID_LOW, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->targetGuidLow);
                    analyzer.ReadPointer(procId, entity_ptr + ENTITY_ENEMY_IN_COMBAT_GUID_HIGH, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->targetGuidHigh);
                    analyzer.ReadBool(procId, entity_ptr + ENTITY_ENEMY_ATTACKING, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->inCombat);
                    std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->id = id;
                    string rawData = creature_db.getRawLine(id);

                    if (!rawData.empty()) {
                        std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->name = creature_db.getColumn(rawData, ENEMY_NAME_COLUMN_INDEX);
                        std::cout << "Enemy Name: " << std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->name << std::endl;
                    }
                }
                if (objType == 225) {
                    newEntity.objType = "Player";
					newEntity.info = nullptr;
					newPlayer.playerPtr = entity_ptr;

                    analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_X_OFFSET, newPlayer.position.x);
                    analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_Y_OFFSET, newPlayer.position.y);
                    analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_Z_OFFSET, newPlayer.position.z);
                    analyzer.ReadFloat(procId, entity_ptr + ENTITY_ROTATION_OFFSET, newPlayer.rotation);
                    analyzer.ReadUInt32(procId, entity_ptr + ENTITY_PLAYER_STATE_OFFSET, newPlayer.state);
                    analyzer.ReadPointer(procId, entity_ptr + ENTITY_PLAYER_IN_COMBAT_GUID_LOW, newPlayer.inCombatGuidLow);
                    analyzer.ReadPointer(procId, entity_ptr + ENTITY_PLAYER_IN_COMBAT_GUID_HIGH, newPlayer.inCombatGuidLow);
                    analyzer.ReadPointer(procId, entity_ptr + ENTITY_PLAYER_UNDER_ATTACK_GUID_LOW, newPlayer.underAttackGuidLow);
                    analyzer.ReadPointer(procId, entity_ptr + ENTITY_PLAYER_UNDER_ATTACK_GUID_HIGH, newPlayer.underAttackGuidHigh);

                    (((newPlayer.state& (1 << 24)) >> 24) == 1) ? newPlayer.isFlying = true : newPlayer.isFlying = false;
                    (((newPlayer.state& (1 << 23)) >> 23) == 1) ? newPlayer.flyingMounted = true : newPlayer.flyingMounted = false;
                    (((newPlayer.state& (1 << 20)) >> 20) == 1) ? newPlayer.inWater = true : newPlayer.inWater = false;

                    analyzer.ReadUInt32(procId, entity_ptr + ENTITY_PLAYER_MOUNT_STATE, newPlayer.mountState);
                    //(newPlayer.mountState == 2) ? newPlayer.groundMounted = true : newPlayer.groundMounted = false;
                    (newPlayer.mountState == 1) ? newPlayer.isMounted = true : newPlayer.isMounted = false;

                    analyzer.ReadInt32(procId, entity_ptr + ENTITY_PLAYER_HEALTH, newPlayer.health);
                    std::cout << "Player Health: " << std::dec << newPlayer.health << std::fixed << std::setprecision(2) << "  Player Rotation: " << newPlayer.rotation << "  Player Pos: (" 
                        << newPlayer.position.x << ", " << newPlayer.position.y << ", " << newPlayer.position.z << ")" << "  Player Flying: " << std::dec << newPlayer.isFlying << "  Player Water: " 
                        << newPlayer.inWater << "  Player Ground Mount: " << newPlayer.groundMounted << "  Player Flying Mount: " << newPlayer.flyingMounted << std::endl;

                    newPlayer.playerGuidLow = guidLow;
                    newPlayer.playerGuidHigh = guidHigh;

                    playerInfo = newPlayer;
                }
                if (objType == 257) {
                    newEntity.objType = "Object";
                    auto objInfo = std::make_shared<ObjectInfo>();
                    newEntity.info = objInfo;

                    analyzer.ReadFloat(procId, entity_ptr + OBJECT_POSITION_X_OFFSET, std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->position.x);
                    analyzer.ReadFloat(procId, entity_ptr + OBJECT_POSITION_Y_OFFSET, std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->position.y);
                    analyzer.ReadFloat(procId, entity_ptr + OBJECT_POSITION_Z_OFFSET, std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->position.z);
                    analyzer.ReadInt32(procId, entity_ptr + OBJECT_COLLECTED_OFFSET, std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->nodeActive);
                    std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->id = id;
                    object_db.getGatherInfo(std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->id, std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->skillLevel, std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->type);
                    string rawData = object_db.getRawLine(id);


                    if (!rawData.empty()) {
                        std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->name = object_db.getColumn(rawData, OBJ_NAME_COLUMN_INDEX);
                    }
                }
                newEntity.id = id;
                newEntity.mapId = map_id;

                // Add to our list
                entityList.push_back(newEntity);
            }
        }
    }
    ULONG_PTR guidLowBag1, guidHighBag1, guidLowBag2, guidHighBag2, guidLowBag3, guidHighBag3, guidLowBag4, guidHighBag4;
    if (newPlayer.position.x != 0) {

        analyzer.ReadPointer(procId, newPlayer.playerPtr + ENTITY_PLAYER_BAG_OFFSET, guidLowBag1);
        analyzer.ReadPointer(procId, newPlayer.playerPtr + ENTITY_PLAYER_BAG_OFFSET + 0x8, guidHighBag1);
        analyzer.ReadPointer(procId, newPlayer.playerPtr + ENTITY_PLAYER_BAG_OFFSET + 0x10, guidLowBag2);
        analyzer.ReadPointer(procId, newPlayer.playerPtr + ENTITY_PLAYER_BAG_OFFSET + 0x18, guidHighBag2);
        analyzer.ReadPointer(procId, newPlayer.playerPtr + ENTITY_PLAYER_BAG_OFFSET + 0x20, guidLowBag3);
        analyzer.ReadPointer(procId, newPlayer.playerPtr + ENTITY_PLAYER_BAG_OFFSET + 0x28, guidHighBag3);
        analyzer.ReadPointer(procId, newPlayer.playerPtr + ENTITY_PLAYER_BAG_OFFSET + 0x30, guidLowBag4);
        analyzer.ReadPointer(procId, newPlayer.playerPtr + ENTITY_PLAYER_BAG_OFFSET + 0x38, guidHighBag4);

        agent.state.globalState.bagFreeSlots = 0;
        for (auto& entity : entityList) {
            if (entity.entityPtr != newPlayer.playerPtr && entity.info) {
                // Use std::dynamic_pointer_cast for shared_ptr
                if (auto enemy = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
                    enemy->distance = enemy->position.Dist3D(newPlayer.position);
                }
                else if (auto object = std::dynamic_pointer_cast<ObjectInfo>(entity.info)) {
                    object->distance = object->position.Dist3D(newPlayer.position);
                }

                if (auto bag = std::dynamic_pointer_cast<BagInfo>(entity.info)) {

                    bag->freeSlots = bag->bagSlots;
                    if ((entity.guidLow == guidLowBag1) && (entity.guidHigh == guidHighBag1))
						bag->id = 1;
                    else if ((entity.guidLow == guidLowBag2) && (entity.guidHigh == guidHighBag2))
                        bag->id = 2;
                    else if ((entity.guidLow == guidLowBag3) && (entity.guidHigh == guidHighBag3))
                        bag->id = 3;
                    else if ((entity.guidLow == guidLowBag4) && (entity.guidHigh == guidHighBag4))
                        bag->id = 4;
                    else
						bag->id = 0;

                    for (auto& itemList : entityList) {
                        if (auto item = std::dynamic_pointer_cast<ItemInfo>(itemList.info)) {
                            if ((item->bagGuidLow == entity.guidLow) && (item->bagGuidHigh == entity.guidHigh)) {
                                bag->freeSlots = bag->freeSlots - 1;
                                item->bagID = bag->id;
                            }
                        }
                    }
					agent.state.globalState.bagFreeSlots += bag->freeSlots;
                }
            }
        }
    }
    return entityList;
}

inline std::vector<Vector3> ParsePathString(const std::string& input) {
    std::vector<Vector3> path;
    size_t currentPos = 0;

    while (true) {
        // 1. Find the start of a tuple '('
        size_t start = input.find('(', currentPos);
        if (start == std::string::npos) break;

        // 2. Find the end of the tuple ')'
        size_t end = input.find(')', start);
        if (end == std::string::npos) break;

        // 3. Extract the "x, y, z" string inside
        std::string content = input.substr(start + 1, end - start - 1);

        // 4. Parse the numbers
        float x, y, z;
        char comma; // Dummy variable to eat the ',' characters
        std::stringstream ss(content);

        // This reads: Float -> Char(,) -> Float -> Char(,) -> Float
        if (ss >> x >> comma >> y >> comma >> z) {
            path.push_back(Vector3(x, y, z));
        }

        // 5. Advance search position
        currentPos = end + 1;
    }

    return path;
}

void MainThread(HMODULE hModule) {
    // Load the database files
    creature_db.loadDatabase("Z:\\WowDB\\creatures.tsv");
    item_db.loadDatabase("Z:\\WowDB\\items.tsv");
    object_db.loadDatabase("Z:\\WowDB\\objects.tsv");
    object_db.loadLocks("Z:\\WowDB\\Lock.csv");

    // Create log file early
    std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
    if (!logFile.is_open()) {
        logFile.open("SMM_Debug.log", std::ios::app);  // fallback to current dir
    }

    auto log = [&logFile](const std::string& msg) {
        logFile << msg << std::endl;
        logFile.flush();
        std::cout << msg << std::endl;
        };

    // 1. ALLOCATE CONSOLE
    // This creates a popup cmd window so std::cout works
    //AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);

    log("DLL Loaded! Console Attached.");

    // Launch GUI in background thread
    std::thread guiThread(StartGuiThread, hModule);
	log("GUI Thread Started.");

    try {
        MemoryAnalyzer analyzer;
        ULONG_PTR objMan_Entry = 0;
        ULONG_PTR objMan_Base = 0;
        ULONG_PTR entityArray = 0;
        ULONG_PTR hashArray = 0;
        int32_t hashArrayMaximum = 0;
        int32_t hashArraySize = 0;
        int32_t entityArraySize = 0;

        WORD nextKey = 0;
        int keyDuration = 0;

        std::vector<Vector3> path = {};

        SimpleKeyboardClient kbd;
        if (!kbd.Connect()) {
            logFile << "[ERROR] Failed to connect to driver!" << std::endl;
            logFile << "Make sure SimpleKeyboard.sys is loaded:" << std::endl;
            logFile << "  sc query SimpleKeyboard" << std::endl;
        }
        else {
            logFile << "[SUCCESS] Connected to driver\n" << std::endl;
        }
        SimpleMouseClient mouse; // Create Mouse
        mouse.Connect();         // Connect Mouse
        MovementController pilot(kbd);

        // Find the Game Window
        HWND hGameWindow = FindWindowA(NULL, "World of Warcraft");
        if (!hGameWindow) {
            logFile << "Waiting for game window..." << std::endl;
            while (!hGameWindow) {
                hGameWindow = FindWindowA(NULL, "World of Warcraft");
                Sleep(1000);
            }
        }
        logFile << hGameWindow << std::endl;
        mouse.SetLockWindow(hGameWindow); // Update lock window
        logFile << "Found Window: " << std::hex << hGameWindow << std::dec << std::endl;

        if (!analyzer.Connect()) {
            logFile << "Failed to connect to driver!" << std::endl;
            // Don't return, let user see error
            Sleep(5000);
        }
        else {
            // Hardcoded logic for DLL mode (since we can't easily pass args)
            // We act as if "-i" was passed.

            DWORD procId = GetProcId(L"WowClassic.exe");
            if (procId == 0) {
                logFile << "WowClassic.exe not found." << std::endl;
            }
            else {
                logFile << "Attached to PID: " << procId << std::endl;

                // Driver Scan
                baseAddress = FindMainModuleViaDriver(analyzer, procId);

                // Create Camera Helper
                Camera cam(analyzer, procId);
                GoapAgent agent(pilot, mouse, kbd, cam, analyzer, procId, baseAddress, hGameWindow);

                if (baseAddress != 0) {
                    // Read Offsets
                    if (analyzer.ReadPointer(procId, baseAddress + OBJECT_MANAGER_ENTRY_OFFSET, objMan_Entry))
                        logFile << "Object Manager Entry: 0x" << std::hex << objMan_Entry << std::endl;

                    analyzer.ReadPointer(procId, objMan_Entry + OBJECT_MANAGER_FIRST_OBJECT_OFFSET, objMan_Base);
                    analyzer.ReadPointer(procId, objMan_Base + ENTITY_ARRAY_OFFSET, entityArray);
                    analyzer.ReadInt32(procId, objMan_Base + ENTITY_ARRAY_SIZE_OFFSET, entityArraySize);
                    analyzer.ReadInt32(procId, objMan_Base + HASH_ARRAY_MAXIMUM_OFFSET, hashArrayMaximum);
                    analyzer.ReadPointer(procId, objMan_Base + HASH_ARRAY_OFFSET, hashArray);
                    analyzer.ReadInt32(procId, objMan_Base + HASH_ARRAY_SIZE_OFFSET, hashArraySize);

                    // Update Camera
                    if (!cam.Update(baseAddress)) {
                        std::cout << "Camera update failed." << std::endl;
                        Sleep(1000);
                    }
                    
                    // Setup Overlay
                    OverlayWindow overlay;
                    if (!overlay.Setup(hGameWindow)) {
                        std::cout << "Failed to create overlay." << std::endl;
                        return;
                    }

                    // --- HOTKEY STATE VARIABLES ---
                    bool isPaused = false;
                    bool lastF3State = false;

                    agent.state.entities = ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, agent.state.player, agent);

                    std::string temp = "(-358.46, 6509.65, 116.46), (-395.20, 6475.67, 116.46), (-438.96, 6449.58, 116.46), (-482.82, 6425.01, 116.46), (-508.78, 6381.19, 116.46), (-529.89, 6335.85, 116.46), (-547.69, 6288.27, 116.46), (-568.29, 6241.66, 116.46), (-591.41, 6196.38, 116.46), (-630.93, 6164.87, 116.46), (-679.63, 6150.37, 116.46), (-728.66, 6137.20, 116.46), (-777.38, 6122.58, 116.46), (-826.50, 6112.96, 116.46), (-876.56, 6103.19, 116.46), (-926.79, 6102.21, 116.46), (-974.92, 6115.97, 116.46), (-1021.64, 6135.66, 116.46), (-1070.59, 6147.56, 116.46), (-1108.42, 6114.36, 116.46), (-1116.81, 6064.22, 116.46), (-1112.63, 6023.29, 145.06), (-1083.49, 5987.18, 164.99), (-1046.92, 5952.05, 164.99), (-1006.52, 5922.57, 164.99), (-965.26, 5892.46, 164.99), (-924.46, 5861.86, 164.99), (-915.96, 5812.52, 164.99), (-916.19, 5761.44, 164.99), (-908.62, 5711.04, 164.99), (-899.11, 5661.90, 164.99), (-855.82, 5636.18, 164.99), (-806.45, 5628.13, 164.99), (-756.05, 5619.92, 164.96), (-721.15, 5614.23, 129.60), (-682.91, 5608.00, 97.27), (-632.02, 5605.64, 97.27), (-583.31, 5593.95, 97.27), (-533.39, 5584.45, 97.27), (-483.33, 5592.52, 97.27), (-436.47, 5611.26, 97.27), (-389.20, 5630.16, 97.27), (-342.72, 5648.75, 97.27), (-295.78, 5667.52, 97.27), (-251.82, 5692.99, 97.27), (-213.12, 5725.18, 97.27), (-174.67, 5757.16, 97.27), (-125.34, 5769.89, 97.27), (-74.43, 5771.82, 97.27), (-23.49, 5774.45, 97.27), (6.49, 5734.40, 97.27), (21.24, 5686.60, 97.27), (36.31, 5637.81, 97.27), (45.13, 5588.17, 97.27), (53.44, 5538.02, 97.27), (56.05, 5487.54, 97.27), (41.91, 5438.62, 97.27), (19.79, 5392.02, 97.27), (-16.80, 5356.57, 97.27), (-51.41, 5319.75, 97.27), (-67.72, 5271.60, 97.30), (-54.05, 5223.70, 107.79), (-25.12, 5182.87, 107.79), (12.66, 5148.95, 107.79), (49.14, 5113.51, 107.79), (99.49, 5106.84, 107.79), (150.01, 5100.33, 107.79), (200.39, 5094.32, 107.79), (251.19, 5090.80, 107.79), (302.10, 5089.47, 107.79), (351.49, 5080.94, 107.79), (400.78, 5072.43, 107.79), (450.40, 5063.53, 107.79), (497.86, 5047.46, 107.79), (546.30, 5031.06, 107.79), (593.44, 5014.39, 107.79), (643.58, 5005.99, 107.79), (693.62, 5015.84, 107.79), (742.61, 5026.80, 107.79), (791.43, 5037.73, 107.79), (841.04, 5041.72, 98.02), (866.62, 5066.11, 62.65), (894.69, 5093.29, 31.05), (928.30, 5131.29, 31.05), (948.19, 5177.60, 31.05), (959.67, 5226.44, 31.05), (960.40, 5276.46, 31.05), (949.54, 5325.44, 31.05), (915.60, 5363.38, 31.05), (870.56, 5386.79, 31.05), (820.35, 5393.04, 31.05), (770.97, 5405.88, 31.05), (726.76, 5429.27, 31.05), (677.55, 5442.15, 31.05), (627.63, 5438.95, 31.05), (577.35, 5430.87, 31.05), (527.26, 5425.94, 31.05), (495.91, 5466.19, 31.05), (463.93, 5505.60, 31.05), (433.48, 5538.86, 53.98), (398.98, 5569.29, 75.79), (364.08, 5605.63, 75.79), (354.80, 5655.29, 75.79), (346.35, 5705.41, 75.79), (335.31, 5754.00, 69.02), (326.02, 5803.92, 69.02), (354.10, 5845.89, 69.02), (392.51, 5879.17, 69.02), (432.32, 5909.69, 73.53), (466.07, 5947.63, 73.53), (496.15, 5988.32, 73.53), (533.36, 6022.97, 73.53), (572.06, 6055.46, 73.53), (610.36, 6087.62, 73.53), (649.48, 6120.47, 73.53), (688.10, 6152.90, 73.53), (726.40, 6185.06, 73.53), (761.76, 6220.58, 73.53), (758.20, 6270.69, 73.53), (742.82, 6319.40, 73.53), (738.70, 6369.91, 73.53), (739.20, 6419.92, 73.53), (739.70, 6469.94, 73.53), (740.20, 6519.95, 73.53), (740.70, 6570.99, 73.53), (749.73, 6620.66, 73.53), (770.46, 6666.99, 73.53), (776.16, 6717.08, 73.53), (788.34, 6766.65, 73.53), (800.30, 6815.34, 73.53), (813.85, 6864.03, 73.53), (844.40, 6904.35, 73.53), (878.85, 6941.42, 73.53), (913.24, 6978.05, 73.53), (927.41, 7026.19, 73.53), (923.96, 7077.05, 73.53), (882.31, 7105.08, 73.53), (832.27, 7114.13, 73.53), (783.90, 7100.69, 73.53), (733.14, 7095.72, 73.53), (682.66, 7089.52, 73.53), (634.71, 7074.09, 73.53), (584.68, 7066.22, 73.53), (536.03, 7050.79, 73.53), (492.98, 7024.58, 73.53), (450.26, 6998.57, 73.53), (402.57, 6980.30, 73.53), (352.86, 6968.60, 73.53), (302.46, 6962.74, 73.53), (252.19, 6962.96, 73.53), (202.11, 6963.19, 73.53), (151.04, 6963.41, 73.53), (100.86, 6963.64, 73.53), (49.79, 6963.87, 73.53), (-0.26, 6972.52, 73.53), (-50.24, 6976.64, 73.53), (-100.09, 6980.74, 73.53), (-150.18, 6988.99, 73.53), (-198.67, 7004.94, 73.53), (-247.72, 7018.33, 73.53), (-298.02, 7014.87, 73.53), (-320.93, 6970.22, 73.53), (-334.14, 6921.07, 73.53)";
                    std::vector<Vector3> myPath = ParsePathString(temp);
                    agent.state.pathFollowState.presetPath = myPath;
                    agent.state.pathFollowState.presetIndex = FindClosestWaypoint(myPath, agent.state.player.position);
                    agent.state.pathFollowState.looping = true;

                    //std::vector<Vector3> path = { Vector3{7.07241, 7449.82, 17.3746}, Vector3{15.2708, 7443.25, 113.991} };
                    //pilot.SteerTowards(agent.state.player.position, agent.state.player.rotation, path[0], false);
                    path = CalculatePath(agent.state.pathFollowState.presetPath, agent.state.player.position, agent.state.pathFollowState.presetIndex, true, 530, agent.state.pathFollowState.looping);
                    //pilot.SteerTowards(agent.state.player.position, agent.state.player.rotation, path[2], true, agent.state.player);

                    for (const auto& point : path) {
                        logFile << "Point: " << point.x << ", " << point.y << ", " << point.z << std::endl;
                    }
                    logFile << agent.state.player.isMounted << std::endl;
                    
                    agent.state.pathFollowState.path = path;
                    agent.state.pathFollowState.index = 0;
                    agent.state.pathFollowState.hasPath = true;
					agent.state.pathFollowState.flyingPath = true;

                    // Move to first waypoint in path
                    agent.state.waypointReturnState.enabled = true;
                    agent.state.waypointReturnState.savedPath = path;
                    agent.state.waypointReturnState.savedIndex = 0;

                    logFile << "Underwater Check: " << globalNavMesh.IsUnderwater(Vector3(414.4220886f, 6918.97f, -5.0f)) << std::endl;

                    while (!(GetAsyncKeyState(VK_F4) & 0x8000)) {
                        if (agent.state.pathFollowState.pathIndexChange == true) {
                            if (agent.state.pathFollowState.path[agent.state.pathFollowState.index - 1].Dist3D(agent.state.pathFollowState.presetPath[agent.state.pathFollowState.presetIndex]) < 5.0) {                                
                                if ((agent.state.pathFollowState.presetIndex >= agent.state.pathFollowState.presetPath.size() - 1) && (agent.state.pathFollowState.looping == 1)) {
                                    agent.state.pathFollowState.presetIndex = 0;
                                }
                                else if (agent.state.pathFollowState.presetIndex < agent.state.pathFollowState.presetPath.size() - 1) {
                                    agent.state.pathFollowState.presetIndex++;
                                }
                                //path = CalculatePath(agent.state.presetPath, agent.state.player.position, agent.state.presetPathIndex, true, 530);
                            }
                            agent.state.pathFollowState.pathIndexChange = false;
                        }

                        if (agent.state.gatherState.enabled == true) {
                            UpdateGatherTarget(agent.state);
                        }

                        // Sync Overlay Position
                        overlay.UpdatePosition();

                        // 1. Get Window Metrics
                        RECT clientRect;
                        GetClientRect(hGameWindow, &clientRect);
                        int width = clientRect.right - clientRect.left;
                        int height = clientRect.bottom - clientRect.top;

                        // 2. Update Camera with ACTUAL window size
                        cam.UpdateScreenSize(width, height);

                        overlay.DrawFrame(-100, -100, RGB(0, 0, 0));
                        for (size_t i = agent.state.globalState.activeIndex; i < agent.state.globalState.activeIndex + 16; ++i) {
                            int screenPosx, screenPosy;
                            if (i >= agent.state.globalState.activePath.size()) break;
                            if (cam.WorldToScreen(agent.state.globalState.activePath[i], screenPosx, screenPosy, &mouse)) {
                                // Draw a line using your overlay's draw list
                                overlay.DrawFrame(screenPosx, screenPosy, RGB(0, 255, 0), true);
                            }
                        }
                        // 1. Extract Data
                        agent.state.entities = ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, agent.state.player, agent);

                        /*for (size_t i = 0; i < agent.state.entities.size(); ++i) {
							if (auto object = std::dynamic_pointer_cast<ObjectInfo>(agent.state.entities[i].info)) {
                                logFile << "Object ID: " << object->id << "  Name: " << object->name << "  Skill Level: " << object->skillLevel << "  Type: " << object->type << std::endl;
							}
                            if (auto item = std::dynamic_pointer_cast<ItemInfo>(agent.state.entities[i].info)) {
								logFile << "Item ID: " << item->id << "  Name: " << item->name << "  Stack Count: " << item->stackCount << "  Bag GUID: (" << std::hex << item->bagGuidHigh << ", " << item->bagGuidLow << std::dec << ")" << std::endl;
                            }
                            if (auto bag = std::dynamic_pointer_cast<BagInfo>(agent.state.entities[i].info)) {
								logFile << "Bag ID: " << bag->id << "  Slots: " << bag->bagSlots << "  Free Slots: " << bag->freeSlots << std::endl;
                            }
						}*/
                        //logFile << playerInfo.rotation << std::endl;

                        //while (playerInfo.position.Dist3D(tempPos) > 5) {
                        //    std::cout << std::fixed << std::setprecision(2) << "Player Pos: (" << playerInfo.position.x << ", " << playerInfo.position.y << ", " << playerInfo.position.z << ")" << "  Player Rotation: " << playerInfo.rotation << std::endl;
                        //    if (playerInfo.position.Dist3D(tempPos) > 20) {
                        //        keyDuration = Char_Rotate_To(playerInfo.rotation, CalculateAngle(playerInfo.position, tempPos), nextKey);
                        //        if (keyDuration > 0) {
                        //            kbd.HoldKey(nextKey, keyDuration);
                        //            Sleep(5000);
                        //        }
                        //    }
                        //    //kbd.HoldKey('W', 500);
                        //    std::cout << std::fixed << std::setprecision(2) << "Player Pos: (" << playerInfo.position.x << ", " << playerInfo.position.y << ", " << playerInfo.position.z << ")" << "  Player Rotation: " << playerInfo.rotation << std::endl;
                        //    std::vector<GameEntity> currentEntities = ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, playerInfo);
                        //}
                        //Sleep(10000000);

                        // 2. Update GUI
                        UpdateGuiData(agent.state.entities);

                        // 3. Logic (Rotation/Console Print)
                        //logFile << nextKey << std::endl;

                        Sleep(10); // Prevent high CPU usage
                        agent.Tick();
                    }
                    pilot.Stop();
                    g_IsRunning = false;
                    RaiseException(0xDEADBEEF, 0, 0, nullptr); // Forcibly exit all threads (including GUI)

                    //mouse.PressButton(MOUSE_LEFT);
                    //Sleep(5);
                    //mouse.Move(0, 20); // Relative move
                    //Sleep(1000);
                    //mouse.Move(0, 20); // Relative move
                    //Sleep(1000);
                    //mouse.Move(0, 20); // Relative move
                    //Sleep(1000);
                    //mouse.Move(0, -60); // Relative move
                    //Sleep(1000);
                    //mouse.Move(0, -20); // Relative move
                    //Sleep(1000);
                    //mouse.Move(0, -20); // Relative move
                    //Sleep(1000);
                    //mouse.Move(0, -20); // Relative move
                    //mouse.ReleaseButton(MOUSE_LEFT);
                    //Sleep(5000);

                    while(!(GetAsyncKeyState(VK_F4) & 0x8000)) {
                        // Sync Overlay Position
                        overlay.UpdatePosition();

                        // HANDLE F3 (PAUSE TOGGLE)
                        bool currentF3State = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;

                        // If key was just pressed down (Edge Detection)
                        if (currentF3State && !lastF3State) {
                            isPaused = !isPaused; // Toggle State

                            if (isPaused) {
                                std::cout << ">>> PAUSED <<<" << std::endl;
                                pilot.Stop(); // CRITICAL: Stop moving immediately when paused
                            }
                            else {
                                std::cout << ">>> RESUMED <<<" << std::endl;
                            }
                        }
                        lastF3State = currentF3State;

                        // IF PAUSED: Draw Status and Skip Logic
                        if (isPaused) {
                            // Optional: Draw a yellow square to indicate PAUSED
                            overlay.DrawFrame(100, 100, RGB(255, 255, 0));
                            Sleep(100); // Sleep to save CPU
                            continue;   // Skip the rest of the loop
                        }

                        // 1. Get Window Metrics
                        RECT clientRect;
                        GetClientRect(hGameWindow, &clientRect);
                        int width = clientRect.right - clientRect.left;
                        int height = clientRect.bottom - clientRect.top;

                        // 2. Update Camera with ACTUAL window size
                        cam.UpdateScreenSize(width, height);

                        std::vector<GameEntity> currentEntities = ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, agent.state.player, agent);
                        SortEntitiesByDistance(currentEntities);
						float min_distance = 99999.0f;
						Vector3 closest_enemy_pos = {};
                        for (auto& entity : currentEntities) {                            
                            // Use std::dynamic_pointer_cast for shared_ptr
                            if (auto object = std::dynamic_pointer_cast<ObjectInfo>(entity.info)) {
                                if (min_distance > object->distance) {
                                    if (object->name == "Felweed") {
                                        min_distance = object->distance;
                                        closest_enemy_pos = object->position;
                                        agent.state.lootState.guidLow = entity.guidLow;
                                        agent.state.lootState.guidHigh = entity.guidHigh;
                                        agent.state.lootState.hasLoot = true;
                                        agent.state.lootState.position = object->position;
                                    }
                                }
                            }
						}
						logFile << "Closest Enemy Distance: " << std::fixed << std::setprecision(2) << min_distance << std::endl;

                        // 4. Calculate Screen Position
                        int sx, sy;
                        if (cam.WorldToScreen(closest_enemy_pos, sx, sy, &mouse)) {
                            // 5. Convert Game Coordinates -> Monitor Coordinates
                            // // Draw RED dot if on screen
                            overlay.DrawFrame(sx, sy, RGB(255, 0, 0));
                        }
                        else {
                            // Draw nothing (Clear screen) if off screen
                            overlay.DrawFrame(-100, -100, RGB(0, 0, 0));
                        }

                        // Fast update to reduce flickering
                        Sleep(10);
                        agent.Tick();
                    }
                    g_IsRunning = false;
                    RaiseException(0xDEADBEEF, 0, 0, nullptr); // Forcibly exit all threads (including GUI)

                    //////////////////////////////////////////////////////////////
                    //////////////////////////////////////////////////////////////
                    logFile << "Agent Started. Press END to stop." << std::endl;
                    //////////////////////////////////////////////////////////////
                    //////////////////////////////////////////////////////////////
                    // LOOP FOREVER (Or until uninject)
                    // We use GetAsyncKeyState(VK_END) to allow unloading the DLL safely
                    while (!GetAsyncKeyState(VK_END)) {

                        agent.state.lootState.hasLoot = true;

                        // --- 1. SENSOR UPDATE (Update World State) ---
						// Only update player every tick
                        static DWORD lastTick = 0;
                        if (GetTickCount() - lastTick < 100) {
                            std::vector<GameEntity> currentEntities = ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, agent.state.player, agent, true);
                        }
                        // --- 2. ENTITY UPDATE (GUI) ---
                        // Update all entities every 100ms
                        else {
                            std::vector<GameEntity> currentEntities = ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, agent.state.player, agent);
                            UpdateGuiData(currentEntities);
                            lastTick = GetTickCount();
                        }
                        
                        // --- 3. BRAIN UPDATE ---
                        agent.Tick();
                        
                        // 60 Hz Loop
                        Sleep(16);
                    }



                    g_IsRunning = false;
					RaiseException(0xDEADBEEF, 0, 0, nullptr); // Forcibly exit all threads (including GUI)

                    Vector3 lastPoint = {};

     //               for (auto& point : path) {
     //                   logFile << std::fixed << std::setprecision(2) << "Moving to Point: (" << point.x << ", " << point.y << ", " << point.z << ")" << std::endl;
     //                   while (agent.state.player.position.Dist3D(point) > 3.0f) {
     //                       if (lastPoint.x != 0.0f && lastPoint.y != 0.0f && lastPoint.z != 0.0f) {
     //                           if ((kbd.IsHolding('W')) && (agent.state.player.position.Dist3D(lastPoint) > 10)) {
     //                               kbd.StopHold('W');
     //                               Sleep(50000);
     //                           }
     //                       }
     //                       lastPoint = agent.state.player.position;
     //                       keyDuration = Char_Rotate_To(agent.state.player.rotation, CalculateAngle(agent.state.player.position, point), nextKey);
     //                       if (nextKey == 0) {
     //                           if (!kbd.IsHolding('W')) {
     //                               kbd.StartHold('W');
					//			}
					//		}
     //                       else if (keyDuration > 0) {
     //                           kbd.StopHold('W');
     //                           kbd.HoldKey(nextKey, keyDuration);
     //                           nextKey = 0;
     //                       }
     //                       if (GetAsyncKeyState(VK_END) & 1) {
     //                           g_IsRunning = false;
     //                           break;
     //                       }
     //                       Sleep(100); // Small delay to allow position update
     //                       currentEntities = ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, agent.state.player);
     //                       //logFile << std::fixed << std::setprecision(2) << "Player Pos: (" << playerInfo.position.x << ", " << playerInfo.position.y << ", " << playerInfo.position.z << ")" << "  Player Rotation: " << playerInfo.rotation << std::endl;
     //                   }
					//}                    
                }
            }
        }
    }
    catch (const std::exception& ex) {
        std::string msg = std::string("EXCEPTION: ") + ex.what();
        logFile << msg << std::endl;
        logFile.flush();
    }
    catch (...) {
        logFile << "UNKNOWN EXCEPTION CAUGHT" << std::endl;
        logFile.flush();
    }

    log("Unloading...");
    
    // 1. Wait for GUI thread to close naturally
    if (guiThread.joinable()) {
        guiThread.join();
    }

    // 2. Clean up Console Streams
    if (fDummy) fclose(fDummy);
    fclose(stdout);
    fclose(stderr);
    fclose(stdin);

    // 3. Detach Console Window
    //FreeConsole();

    // 4. Unload DLL safely
    // This removes the DLL from Explorer's memory, allowing you to re-inject.
    FreeLibraryAndExitThread(hModule, 0);
}

// --- DLL ENTRY POINT ---
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        // Disable thread library calls optimization
        DisableThreadLibraryCalls(hModule);
        // Spawn the main logic in a new thread so we don't freeze the loader
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, nullptr);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        std::cout << "Unloading thread..." << std::endl;
        FreeConsole();
        std::cout.flush();
        break;
    case DLL_PROCESS_DETACH:
        std::cout << "Unloading..." << std::endl;
        FreeConsole();
        std::cout.flush();
        break;
    }
    return TRUE;
}