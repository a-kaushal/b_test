#include <winsock2.h>               // MUST be included before windows.h
#include <ws2tcpip.h>
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

#include "dllmain.h"
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
#include "Combat.h"
#include "Repair.h"
#include "Logger.h"
#include "Behaviors.h"

#include <DbgHelp.h> // Required for MiniDump

#pragma comment(lib, "Dbghelp.lib")

// Global atomic flag to ensure only ONE thread writes the crash dump
std::atomic<bool> g_HasCrashed = false;

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* pExceptionInfo) {
    // If another thread is already handling a crash, sleep and let it finish
    if (g_HasCrashed.exchange(true)) {
        Sleep(10000);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // 1. Log the crash details
    if (g_LogFile.is_open()) {
        g_LogFile << "\n[CRITICAL] === CRASH DETECTED ===" << std::endl;
        g_LogFile << "Exception Code: 0x" << std::hex << pExceptionInfo->ExceptionRecord->ExceptionCode << std::endl;
        g_LogFile << "Fault Address: 0x" << std::hex << pExceptionInfo->ExceptionRecord->ExceptionAddress << std::endl;
        g_LogFile.flush();
    }

    // 2. Create Dump
    HANDLE hFile = CreateFileA("C:\\Driver\\SMM_Crash.dmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
        dumpInfo.ThreadId = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = pExceptionInfo;
        dumpInfo.ClientPointers = TRUE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &dumpInfo, NULL, NULL);
        CloseHandle(hFile);
    }

    if (g_LogFile.is_open()) {
        g_LogFile << "Dump saved. Terminating." << std::endl;
        g_LogFile.close();
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

std::mutex g_EntityMutex;       // Create the mutex
std::ofstream g_LogFile;        // Create the logger
std::atomic<bool> g_IsRunning(true);

extern "C" void CleanupFMapCache(int mapId, float x, float y);

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
struct WorldState;

WorldState g_GameStateInstance;
WorldState* g_GameState = nullptr;

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

void ExtractEntities(MemoryAnalyzer& analyzer, DWORD procId, ULONG_PTR hashArray, int hashArrayMaximum, ULONG_PTR entityArray, int entityArraySize, PlayerInfo& playerInfo, 
    std::vector<GameEntity>& entityList, GoapAgent& agent, bool playerOnly = false) {
    if (!playerOnly) {
        entityList.clear(); // Reuse memory capacity
        entityList.reserve(500); // Prevent re-allocations during push_back
    }
    PlayerInfo newPlayer = {};

    try {
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
                return;
            }
            else {
                std::cerr << "Player pointer is invalid, reading all entities!" << std::endl;
            }
        }
        // 1. Bulk Read Hash Array
        std::vector<HashNode> buffer(hashArrayMaximum);
        analyzer.ReadBuffer(procId, hashArray, buffer.data(), hashArrayMaximum * sizeof(HashNode));

        // 2. [NEW] Bulk Read Entity Pointers (Saves 2000+ driver calls/sec)
        std::vector<DWORD_PTR> ptrBuffer(entityArraySize);
        analyzer.ReadBuffer(procId, entityArray, ptrBuffer.data(), entityArraySize * sizeof(DWORD_PTR));

        // 1. Read the WHOLE array in one shot
        for (int i = 0; i < hashArrayMaximum; ++i) {
            ULONG_PTR EntryGuidLow = buffer[i].GuidLow;
            ULONG_PTR EntryGuidHigh = buffer[i].GuidHigh;
            ULONG_PTR EntityIndex = buffer[i].EntityIndex;

            //analyzer.ReadPointer(procId, hashArray + (i * 24), EntryGuidLow);
            //analyzer.ReadPointer(procId, hashArray + (i * 24) + 0x8, EntryGuidHigh);
            //analyzer.ReadPointer(procId, hashArray + (i * 24) + 0x10, EntityIndex);

            long EntInd = EntityIndex & 0x3FFFFFFF;

            if (EntInd < 0 || EntInd >= entityArraySize) {
                continue; // Skip invalid indices prevents reading out of bounds
            }

            // Valid check
            if (!(EntryGuidHigh == 0 && EntryGuidLow == 0) && !(EntryGuidLow == 1 && EntryGuidHigh == 0x400000000000000)) {

                DWORD_PTR entityBuilderPtr = ptrBuffer[EntInd];
                DWORD_PTR entity_ptr = 0;
                int32_t objType = 0;
                ULONG_PTR guidLow = 0, guidHigh = 0;

                //if (!analyzer.ReadPointer(procId, entityArray + ((int)EntInd * ENTITY_BUILDER_ARRAY_ITEM), entityBuilderPtr)) continue;
                if (entityBuilderPtr == 0) continue;
                if (!analyzer.ReadPointer(procId, entityBuilderPtr + ENTITY_ENTRY_OFFSET, entity_ptr)) continue;
                if (!analyzer.ReadInt32(procId, entity_ptr + ENTITY_OBJECT_TYPE_OFFSET, objType)) continue;
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
                        analyzer.ReadInt32(procId, entity_ptr + ENTITY_ENEMY_HEALTH, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->health);
                        analyzer.ReadInt32(procId, entity_ptr + ENTITY_ENEMY_MAX_HEALTH, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->maxHealth);
                        std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->id = id;

                        creature_db.getCreatureReaction(std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->id, true, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->reaction);
                        if (id > 0 && id < 200000) {
                            string rawData = creature_db.getRawLine(id);

                            if (!rawData.empty()) {
                                auto enemyInfo = std::dynamic_pointer_cast<EnemyInfo>(newEntity.info);
                                enemyInfo->name = creature_db.getColumn(rawData, ENEMY_NAME_COLUMN_INDEX);

                                // SAFE PARSING REPLACEMENT
                                try {
                                    std::string flagStr = creature_db.getColumn(rawData, NPC_FLAG_COLUMN_INDEX);
                                    if (!flagStr.empty()) {
                                        enemyInfo->npcFlag = std::stoi(flagStr);
                                    }
                                    else {
                                        enemyInfo->npcFlag = 0; // Default value
                                    }
                                }
                                catch (...) {
                                    enemyInfo->npcFlag = 0; // Fallback on error
                                }
                            }
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
                        analyzer.ReadFloat(procId, entity_ptr + ENTITY_VERTICAL_ROTATION_OFFSET, newPlayer.vertRotation);
                        analyzer.ReadUInt32(procId, entity_ptr + ENTITY_PLAYER_STATE_OFFSET, newPlayer.state);
                        analyzer.ReadPointer(procId, entity_ptr + ENTITY_PLAYER_IN_COMBAT_GUID_LOW, newPlayer.inCombatGuidLow);
                        analyzer.ReadPointer(procId, entity_ptr + ENTITY_PLAYER_IN_COMBAT_GUID_HIGH, newPlayer.inCombatGuidLow);
                        analyzer.ReadPointer(procId, entity_ptr + ENTITY_PLAYER_TARGET_GUID_LOW, newPlayer.targetGuidLow);
                        analyzer.ReadPointer(procId, entity_ptr + ENTITY_PLAYER_TARGET_GUID_HIGH, newPlayer.targetGuidHigh);

                        (((newPlayer.state & (1 << 11)) >> 11) == 1) ? newPlayer.inAir = true : newPlayer.inAir = false;
                        (((newPlayer.state & (1 << 24)) >> 24) == 1) ? newPlayer.isFlying = true : newPlayer.isFlying = false;
                        (((newPlayer.state & (1 << 26)) >> 26) == 1) ? newPlayer.isDead = true : newPlayer.isDead = false;
                        (((newPlayer.state & (1 << 23)) >> 23) == 1) ? newPlayer.flyingMounted = true : newPlayer.flyingMounted = false;
                        (((newPlayer.state & (1 << 20)) >> 20) == 1) ? newPlayer.inWater = true : newPlayer.inWater = false;

                        if (newPlayer.isFlying == false) {
                            newPlayer.onGround = true;
                        }

                        analyzer.ReadUInt32(procId, entity_ptr + ENTITY_PLAYER_MOUNT_STATE, newPlayer.mountState);
                        //(newPlayer.mountState == 2) ? newPlayer.groundMounted = true : newPlayer.groundMounted = false;
                        (newPlayer.mountState == 1) ? newPlayer.isMounted = true : newPlayer.isMounted = false;

                        analyzer.ReadInt32(procId, entity_ptr + ENTITY_PLAYER_HEALTH, newPlayer.health);
                        analyzer.ReadInt32(procId, entity_ptr + ENTITY_PLAYER_MAX_HEALTH, newPlayer.maxHealth);
                        analyzer.ReadInt32(procId, entity_ptr + ENTITY_PLAYER_LEVEL, newPlayer.level);
                        std::cout << "Player Health: " << std::dec << newPlayer.health << std::fixed << std::setprecision(2) << "  Player Rotation: " << newPlayer.rotation << "  Player Pos: ("
                            << newPlayer.position.x << ", " << newPlayer.position.y << ", " << newPlayer.position.z << ")" << "  Player Flying: " << std::dec << newPlayer.isFlying << "  Player Water: "
                            << newPlayer.inWater << "  Player Ground Mount: " << newPlayer.groundMounted << "  Player Flying Mount: " << newPlayer.flyingMounted << std::endl;

                        newPlayer.playerGuidLow = guidLow;
                        newPlayer.playerGuidHigh = guidHigh;
                        newPlayer.mapId = map_id;

                        playerInfo = newPlayer;
                    }
                    if (objType == 257) {
                        if (id == 0)
                            continue;
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

            g_GameState->globalState.bagFreeSlots = 0;
            for (auto& entity : entityList) {
                if (entity.entityPtr != newPlayer.playerPtr && entity.info) {
                    // Use std::dynamic_pointer_cast for shared_ptr
                    if (auto enemy = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
                        playerInfo.mapId = entity.mapId;
                        enemy->distance = enemy->position.Dist3D(newPlayer.position);

                        string rawdata = creature_db.getRawLine(enemy->id);
                        // Fast Lookup
                        CreatureTemplateEntry* c = creature_db.getCreatureTemplate(enemy->id);
                        if (c == nullptr) {
                            g_LogFile << "Creature Template Entry with ID " << enemy->id << " not found." << std::endl;
                        }
                        else {
                            // 1. Check Reaction
                            if (enemy->reaction == REACTION_HOSTILE) {
                                // 3. Calculate Range
                                float range = 20.0f - (float)(newPlayer.level - enemy->level);
                                // 2. Check Passive Flag (0x0200 = UNIT_FLAG_PASSIVE)
                                if (c->UnitFlags & 512); //g_LogFile << "Creature Template Entry with ID " << enemy->id << " passive flag." << std::endl;
                                else {
                                    // 4. Elite/Boss Bonus
                                    if (c->Rank >= 1) range += 10.0f; // Elite
                                    if (c->Rank == 3) range = 100.0f; // Boss
                                    // 5. Clamp
                                    if (range < 5.0f) range = 5.0f;
                                    if (range > 45.0f && c->Rank < 3) range = 45.0f;
                                }
                                enemy->agroRange = range;
                            }
                            enemy->rank = c->Rank;
                        }

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
                        g_GameState->globalState.bagFreeSlots += bag->freeSlots;
                    }
                }
                // If in repair mode update npc Guid when in range
                if ((g_GameState->interactState.interactActive == true) && (g_GameState->interactState.targetGuidLow == 0) && (g_GameState->interactState.targetGuidHigh == 0)) {
                    if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
                        if (npc->id == g_GameState->interactState.interactId) {
                            g_GameState->interactState.targetGuidLow = entity.guidLow;
                            g_GameState->interactState.targetGuidHigh = entity.guidHigh;
                        }
                    }
                }
            }
        }
    }
    catch (...) {
        g_LogFile << "Memory Read Failed" << std::endl;
    }

    return;
}

std::vector<Vector3> ParsePathString(const std::string& input) {
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
    creature_db.parseCreatureTemplates();
    creature_db.loadFactions("Z:\\WowDB\\FactionTemplate.csv");
    creature_db.loadCreatureSpawnLocations("Z:\\WowDB\\creature_spawns.tsv");
    item_db.loadDatabase("Z:\\WowDB\\items.tsv");
    object_db.loadDatabase("Z:\\WowDB\\objects.tsv");
    object_db.loadLocks("Z:\\WowDB\\Lock.csv");

    AddVectoredExceptionHandler(1, CrashHandler);

    g_GameState = &g_GameStateInstance;

    // Create log file early
    //
    //if (!g_LogFile.is_open()) {
    //    g_LogFile.open("SMM_Debug.log", std::ios::app);  // fallback to current dir
    //}

    g_LogFile.open("C:\\Driver\\SMM_Debug.log", std::ios::out | std::ios::app);

    auto log = [](const std::string& msg) {
        // Write to global file
        if (g_LogFile.is_open()) {
            g_LogFile << msg << std::endl;
            // g_LogFile.flush(); // Only flush if debugging a crash, otherwise remove for speed
        }
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

        std::vector<PathNode> path = {};

        SimpleKeyboardClient kbd;
        if (!kbd.Connect()) {
            g_LogFile << "[ERROR] Failed to connect to driver!" << std::endl;
            g_LogFile << "Make sure SimpleKeyboard.sys is loaded:" << std::endl;
            g_LogFile << "  sc query SimpleKeyboard" << std::endl;
        }
        else {
            g_LogFile << "[SUCCESS] Connected to driver\n" << std::endl;
        }
        ConsoleInput console(kbd);
        SimpleMouseClient mouse; // Create Mouse
        mouse.Connect();         // Connect Mouse

        // Find the Game Window
        HWND hGameWindow = FindWindowA(NULL, "World of Warcraft");
        MovementController pilot(kbd, mouse, hGameWindow);

        if (!hGameWindow) {
            g_LogFile << "Waiting for game window..." << std::endl;
            while (!hGameWindow) {
                hGameWindow = FindWindowA(NULL, "World of Warcraft");
                Sleep(1000);
            }
        }
        g_LogFile << hGameWindow << std::endl;
        mouse.SetLockWindow(hGameWindow); // Update lock window
        g_LogFile << "Found Window: " << std::hex << hGameWindow << std::dec << std::endl;

        // Bring Window to Foreground & Center Cursor ---
        if (IsIconic(hGameWindow)) {
            ShowWindow(hGameWindow, SW_RESTORE);
        }
        SetForegroundWindow(hGameWindow);

        if (!analyzer.Connect()) {
            g_LogFile << "Failed to connect to driver!" << std::endl;
            // Don't return, let user see error
            Sleep(5000);
        }
        else {
            // Hardcoded logic for DLL mode (since we can't easily pass args)
            // We act as if "-i" was passed.

            DWORD procId = GetProcId(L"WowClassic.exe");
            if (procId == 0) {
                g_LogFile << "WowClassic.exe not found." << std::endl;
            }
            else {
                g_LogFile << "Attached to PID: " << procId << std::endl;

                // Driver Scan
                baseAddress = FindMainModuleViaDriver(analyzer, procId);

                // Create Camera Helper
                Camera cam(analyzer, mouse, procId);
                GoapAgent agent(g_GameStateInstance, pilot, mouse, kbd, cam, analyzer, procId, baseAddress, hGameWindow);
                InteractionController interact(pilot, mouse, kbd, cam, analyzer, procId, baseAddress, hGameWindow);

                if (baseAddress != 0) {
                    // Read Offsets
                    if (analyzer.ReadPointer(procId, baseAddress + OBJECT_MANAGER_ENTRY_OFFSET, objMan_Entry))
                        g_LogFile << "Object Manager Entry: 0x" << std::hex << objMan_Entry << std::endl;

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

                    std::vector<GameEntity> persistentEntityList;
                    persistentEntityList.reserve(3000);

                    ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, entityArraySize, g_GameState->player, persistentEntityList, agent);
                    g_GameState->entities = persistentEntityList;

                    std::string temp = "(-358.46, 6509.65, 116.46), (-395.20, 6475.67, 116.46), (-438.96, 6449.58, 116.46), (-482.82, 6425.01, 116.46), (-508.78, 6381.19, 116.46), (-529.89, 6335.85, 116.46), (-547.69, 6288.27, 116.46), (-568.29, 6241.66, 116.46), (-591.41, 6196.38, 116.46), (-630.93, 6164.87, 116.46), (-679.63, 6150.37, 116.46), (-728.66, 6137.20, 116.46), (-777.38, 6122.58, 116.46), (-826.50, 6112.96, 116.46), (-876.56, 6103.19, 116.46), (-926.79, 6102.21, 116.46), (-974.92, 6115.97, 116.46), (-1021.64, 6135.66, 116.46), (-1070.59, 6147.56, 116.46), (-1108.42, 6114.36, 116.46), (-1116.81, 6064.22, 116.46), (-1112.63, 6023.29, 145.06), (-1083.49, 5987.18, 164.99), (-1046.92, 5952.05, 164.99), (-1006.52, 5922.57, 164.99), (-965.26, 5892.46, 164.99), (-924.46, 5861.86, 164.99), (-915.96, 5812.52, 164.99), (-916.19, 5761.44, 164.99), (-908.62, 5711.04, 164.99), (-899.11, 5661.90, 164.99), (-855.82, 5636.18, 164.99), (-806.45, 5628.13, 164.99), (-756.05, 5619.92, 164.96), (-721.15, 5614.23, 129.60), (-682.91, 5608.00, 97.27), (-632.02, 5605.64, 97.27), (-583.31, 5593.95, 97.27), (-533.39, 5584.45, 97.27), (-483.33, 5592.52, 97.27), (-436.47, 5611.26, 97.27), (-389.20, 5630.16, 97.27), (-342.72, 5648.75, 97.27), (-295.78, 5667.52, 97.27), (-251.82, 5692.99, 97.27), (-213.12, 5725.18, 97.27), (-174.67, 5757.16, 97.27), (-125.34, 5769.89, 97.27), (-74.43, 5771.82, 97.27), (-23.49, 5774.45, 97.27), (6.49, 5734.40, 97.27), (21.24, 5686.60, 97.27), (36.31, 5637.81, 97.27), (45.13, 5588.17, 97.27), (53.44, 5538.02, 97.27), (56.05, 5487.54, 97.27), (41.91, 5438.62, 97.27), (19.79, 5392.02, 97.27), (-16.80, 5356.57, 97.27), (-51.41, 5319.75, 97.27), (-67.72, 5271.60, 97.30), (-54.05, 5223.70, 107.79), (-25.12, 5182.87, 107.79), (12.66, 5148.95, 107.79), (49.14, 5113.51, 107.79), (99.49, 5106.84, 107.79), (150.01, 5100.33, 107.79), (200.39, 5094.32, 107.79), (251.19, 5090.80, 107.79), (302.10, 5089.47, 107.79), (351.49, 5080.94, 107.79), (400.78, 5072.43, 107.79), (450.40, 5063.53, 107.79), (497.86, 5047.46, 107.79), (546.30, 5031.06, 107.79), (593.44, 5014.39, 107.79), (643.58, 5005.99, 107.79), (693.62, 5015.84, 107.79), (742.61, 5026.80, 107.79), (791.43, 5037.73, 107.79), (841.04, 5041.72, 98.02), (866.62, 5066.11, 62.65), (894.69, 5093.29, 31.05), (928.30, 5131.29, 31.05), (948.19, 5177.60, 31.05), (959.67, 5226.44, 31.05), (960.40, 5276.46, 31.05), (949.54, 5325.44, 31.05), (915.60, 5363.38, 31.05), (870.56, 5386.79, 31.05), (820.35, 5393.04, 31.05), (770.97, 5405.88, 31.05), (726.76, 5429.27, 31.05), (677.55, 5442.15, 31.05), (627.63, 5438.95, 31.05), (577.35, 5430.87, 31.05), (527.26, 5425.94, 31.05), (495.91, 5466.19, 31.05), (463.93, 5505.60, 31.05), (433.48, 5538.86, 53.98), (398.98, 5569.29, 75.79), (364.08, 5605.63, 75.79), (354.80, 5655.29, 75.79), (346.35, 5705.41, 75.79), (335.31, 5754.00, 69.02), (326.02, 5803.92, 69.02), (354.10, 5845.89, 69.02), (392.51, 5879.17, 69.02), (432.32, 5909.69, 73.53), (466.07, 5947.63, 73.53), (496.15, 5988.32, 73.53), (533.36, 6022.97, 73.53), (572.06, 6055.46, 73.53), (610.36, 6087.62, 73.53), (649.48, 6120.47, 73.53), (688.10, 6152.90, 73.53), (726.40, 6185.06, 73.53), (761.76, 6220.58, 73.53), (758.20, 6270.69, 73.53), (742.82, 6319.40, 73.53), (738.70, 6369.91, 73.53), (739.20, 6419.92, 73.53), (739.70, 6469.94, 73.53), (740.20, 6519.95, 73.53), (740.70, 6570.99, 73.53), (749.73, 6620.66, 73.53), (770.46, 6666.99, 73.53), (776.16, 6717.08, 73.53), (788.34, 6766.65, 73.53), (800.30, 6815.34, 73.53), (813.85, 6864.03, 73.53), (844.40, 6904.35, 73.53), (878.85, 6941.42, 73.53), (913.24, 6978.05, 73.53), (927.41, 7026.19, 73.53), (923.96, 7077.05, 73.53), (882.31, 7105.08, 73.53), (832.27, 7114.13, 73.53), (783.90, 7100.69, 73.53), (733.14, 7095.72, 73.53), (682.66, 7089.52, 73.53), (634.71, 7074.09, 73.53), (584.68, 7066.22, 73.53), (536.03, 7050.79, 73.53), (492.98, 7024.58, 73.53), (450.26, 6998.57, 73.53), (402.57, 6980.30, 73.53), (352.86, 6968.60, 73.53), (302.46, 6962.74, 73.53), (252.19, 6962.96, 73.53), (202.11, 6963.19, 73.53), (151.04, 6963.41, 73.53), (100.86, 6963.64, 73.53), (49.79, 6963.87, 73.53), (-0.26, 6972.52, 73.53), (-50.24, 6976.64, 73.53), (-100.09, 6980.74, 73.53), (-150.18, 6988.99, 73.53), (-198.67, 7004.94, 73.53), (-247.72, 7018.33, 73.53), (-298.02, 7014.87, 73.53), (-320.93, 6970.22, 73.53), (-334.14, 6921.07, 73.53)";
                    
                    //InteractWithObject(530, 1, Vector3{ 258.91f, 7870.72f, 23.01f }, 182567);
                    FollowPath(530, temp, true, true);

                    g_GameState->globalState.flyingPath = true;
                    
                    // Force disable Click-to-Move to prevent accidental movement on mouse clicks
                    console.SendDataRobust(std::wstring(L"/console autointeract 0"));

                    RECT rect;
                    GetClientRect(hGameWindow, &rect);
                    POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
                    ClientToScreen(hGameWindow, &center);
                    SetCursorPos(center.x, center.y);

                    static DWORD lastTrim = 0;
                    static DWORD overlayUpdate = 0;
                    while (!(GetAsyncKeyState(VK_F4) & 0x8000)) {
                        if (GetTickCount() - lastTrim > 30000) { // Every 30 seconds
                            //SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
                            lastTrim = GetTickCount();
                        }

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

                        if (g_GameState->pathFollowState.pathIndexChange == true) {
                            if (g_GameState->pathFollowState.path[g_GameState->pathFollowState.index - 1].pos.Dist3D(g_GameState->pathFollowState.presetPath[g_GameState->pathFollowState.presetIndex]) < 5.0) {
                                if ((g_GameState->pathFollowState.presetIndex >= g_GameState->pathFollowState.presetPath.size() - 1) && (g_GameState->pathFollowState.looping == 1)) {
                                    g_GameState->pathFollowState.presetIndex = 0;
                                }
                                else if (g_GameState->pathFollowState.presetIndex < g_GameState->pathFollowState.presetPath.size() - 1) {
                                    g_GameState->pathFollowState.presetIndex++;
                                }
                                //path = CalculatePath(agent.state.presetPath, agent.state.player.position, agent.state.presetPathIndex, true, 530);
                            }
                            g_GameState->pathFollowState.pathIndexChange = false;
                        }
                        if (!isPaused) {
                            if (UnderAttackCheck() == true) {
                                //g_LogFile << "Under Attack Detected!" << std::endl;
                            }
                            if (g_GameState->gatherState.enabled == true) {
                                UpdateGatherTarget(g_GameStateInstance);
                            }
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

                        //overlay.DrawFrame(-100, -100, RGB(0, 0, 0));
                        //for (size_t i = g_GameState->globalState.activeIndex; i < min(g_GameState->globalState.activeIndex + 6, g_GameState->globalState.activePath.size()); ++i) {
                        //    int screenPosx, screenPosy;
                        //    if (i >= g_GameState->globalState.activePath.size()) break;
                        //    if (cam.WorldToScreen(g_GameState->globalState.activePath[i].pos, screenPosx, screenPosy)) {
                        //        // Draw a line using your overlay's draw list
                        //        overlay.DrawFrame(screenPosx, screenPosy, RGB(0, 255, 0), true);
                        //    }
                        //}
                        
						uint32_t repairId = 0;
                        uint32_t mapId = 0;
						Vector3 repairPos = {};

                        // --- FIX START: REFRESH POINTERS HERE ---
                        // Re-read the Object Manager Base itself, because it can move too!
                        analyzer.ReadPointer(procId, objMan_Entry + OBJECT_MANAGER_FIRST_OBJECT_OFFSET, objMan_Base);
                        // The ObjectManager may reallocate these arrays at ANY time.
                        // We must re-read the pointers every frame to ensure they are valid.
                        analyzer.ReadPointer(procId, objMan_Base + ENTITY_ARRAY_OFFSET, entityArray);
                        analyzer.ReadInt32(procId, objMan_Base + ENTITY_ARRAY_SIZE_OFFSET, entityArraySize);
                        analyzer.ReadInt32(procId, objMan_Base + HASH_ARRAY_MAXIMUM_OFFSET, hashArrayMaximum);
                        analyzer.ReadPointer(procId, objMan_Base + HASH_ARRAY_OFFSET, hashArray);
                        analyzer.ReadInt32(procId, objMan_Base + HASH_ARRAY_SIZE_OFFSET, hashArraySize);

                        // --- [RECOMMENDED] Sanity Check to prevent Driver Crash on bad reads ---
                        if (hashArrayMaximum > 100000 || hashArrayMaximum < 0) {
                            g_LogFile << "[WARNING] Garbage Hash Size: " << hashArrayMaximum << ". Skipping frame." << std::endl;
                            continue; // Skip the rest of this loop iteration
                        }

                        // 1. Extract Data
                        // LOCK before writing
                        std::lock_guard<std::mutex> lock(g_EntityMutex);

                        // Now it is safe to update the vector
                        ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, entityArraySize, g_GameState->player, persistentEntityList, agent);
                        g_GameState->entities = persistentEntityList;

                        // UpdateGuiData usually reads entities too, so keep it inside the lock
                        try {
                            UpdateGuiData(g_GameState->entities);
                        }
                        catch (...) {
                            g_LogFile << "[WARNING] GUI Update failed. Skipping frame." << std::endl;
                        }

                        // 3. Logic (Rotation/Console Print)
                        //g_LogFile << nextKey << std::endl;

                        /*if (pilot.Calibrate(agent.state.player.rotation, agent.state.player.vertRotation) == true) {
                            break;
						}*/

                        Sleep(50); // Prevent high CPU usage
                        try {
                            if (!isPaused) agent.Tick();
                        }
                        catch (...) {
                            g_LogFile << "Agent Tick Fail" << std::endl;
                        }

                        // --- MEMORY CLEANUP ---
                        //static DWORD lastCleanup = 0;
                        //if (GetTickCount() - lastCleanup > 30000) { // Run every 30 seconds
                        //    if (g_GameState->player.playerPtr != 0) {
                        //        CleanupFMapCache(g_GameState->player.mapId, g_GameState->player.position.x, g_GameState->player.position.y);
                        //        // Optional: Log to confirm it's running
                        //        // g_LogFile << "[System] Pruning FMap Cache..." << std::endl; 
                        //    }
                        //    lastCleanup = GetTickCount();
                        //}
                    }
                    pilot.Stop();

                    if (g_LogFile.is_open()) {
                        g_LogFile.close();
                    }
                    //FreeLibraryAndExitThread(hModule, 0);
                    
                    g_IsRunning = false;
                    RaiseException(0xDEADBEEF, 0, 0, nullptr); // Forcibly exit all threads (including GUI)

                    Vector3 lastPoint = {};                  
                }
            }
        }
    }
    catch (const std::exception& ex) {
        std::string msg = std::string("EXCEPTION: ") + ex.what();
        g_LogFile << msg << std::endl;
    }
    catch (...) {
        g_LogFile << "UNKNOWN EXCEPTION CAUGHT" << std::endl;
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