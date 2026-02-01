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
#include "Logger.h"
#include "Behaviors.h"
#include "LuaAnchor.h"
#include "WebServer.h"
#include "ProfileLoader.h"
#include "Mailing.h"
#include "Profile.h"

#include <DbgHelp.h> // Required for MiniDump

#pragma comment(lib, "Dbghelp.lib")

// Global atomic flag to ensure only ONE thread writes the crash dump
std::atomic<bool> g_HasCrashed = false;
ProfileLoader g_ProfileLoader;

std::atomic<bool> g_IsRunning(true);
std::atomic<bool> g_IsPaused(false);

// --- STATUS FILE HELPERS ---
const std::string STATUS_FILE = "C:\\SMM\\bot_status.txt";

void UpdateStatus(const std::string& status) {
    static std::string lastStatus = "";
    static DWORD lastWriteTime = 0;
    DWORD currentTime = GetTickCount();

    // Write if status changed OR it's been > 2 seconds (Heartbeat)
    if (status != lastStatus || (currentTime - lastWriteTime) > 2000) {
        std::ofstream statusFile(STATUS_FILE, std::ios::trunc);
        if (statusFile.is_open()) {
            statusFile << status;
            statusFile.close();
            lastStatus = status;
            lastWriteTime = currentTime;
        }
    }
}

std::string ReadStatus() {
    std::ifstream statusFile(STATUS_FILE);
    std::string line;
    if (statusFile.is_open()) {
        std::getline(statusFile, line);
        statusFile.close();
    }
    // Trim whitespace just in case
    line.erase(line.find_last_not_of(" \n\r\t") + 1);
    return line;
}

int GetProcessCount(const wchar_t* procName) {
    int count = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (wcscmp(pe.szExeFile, procName) == 0) {
                    count++;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return count;
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* pExceptionInfo) {
    DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
    // --- FIX START: FILTER EXCEPTIONS ---
    // Ignore RPC errors (0x6BA), C++ Exceptions (0xE06D7363), and Debugger signals
    if (code == 0x000006BA || code == 0xE06D7363 || code == 0x40010006) {
        // Return EXCEPTION_CONTINUE_SEARCH to let Windows handle it normally
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Only handle FATAL memory errors
    if (code != EXCEPTION_ACCESS_VIOLATION &&         // 0xC0000005
        code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED &&    // 0xC000008C
        code != EXCEPTION_STACK_OVERFLOW &&           // 0xC00000FD
        code != EXCEPTION_ILLEGAL_INSTRUCTION) {      // 0xC000001D

        return EXCEPTION_CONTINUE_SEARCH;
    }
    // --- FIX END ---
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
    HANDLE hFile = CreateFileA("C:\\SMM\\SMM_Crash.dmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
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

extern "C" void CleanupFMapCache(int mapId, float x, float y);

// Global Databases
WoWDataTool creature_db;
WoWDataTool item_db;
WoWDataTool object_db;
WoWDataTool worldmap_db;

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
ProfileSettings g_ProfileSettings;

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

    // [LOGGING] Start of frame (Helpful to see if we crash immediately upon entering)
    // Uncomment the next line only if you need extreme spam debugging, otherwise keep it off to save disk I/O
    // if (g_LogFile.is_open()) g_LogFile << "[Extract] Frame Start. HashMax: " << hashArrayMaximum << " EntSize: " << entityArraySize << std::endl;

    // --- SANITY CHECK 1: INVALID SIZES ---
    // If these values are garbage (negative or unreasonably huge), we MUST abort.
    // WoW Classic Object Manager rarely exceeds 10,000 objects.
    if (entityArraySize < 0 || entityArraySize > 100000) {
        if (g_LogFile.is_open()) g_LogFile << "[CRITICAL] Garbage EntityArraySize detected: " << entityArraySize << ". Skipping frame." << std::endl;
        return;
    }
    if (hashArrayMaximum < 0 || hashArrayMaximum > 100000) {
        if (g_LogFile.is_open()) g_LogFile << "[CRITICAL] Garbage HashArrayMaximum detected: " << hashArrayMaximum << ". Skipping frame." << std::endl;
        return;
    }

    if (!playerOnly) {
        entityList.clear(); 
        entityList.reserve(500); 
    }
    PlayerInfo newPlayer = {};
    ULONG_PTR zoneNameLoc = 0;
    string zoneName;

    try {
        analyzer.ReadPointer(procId, baseAddress + ZONE_TEXT, zoneNameLoc);
        analyzer.ReadString(procId, zoneNameLoc, zoneName);
        zoneName.erase(std::remove(zoneName.begin(), zoneName.end(), ' '), zoneName.end());
        int mapId;
        int areaId;
        float top;
        float bottom;
        float left;
        float right;
        uint32_t hash;

        if (worldmap_db.getZoneInfoByName(zoneName, mapId, top, bottom, left, right, areaId, hash)) {
            /*g_LogFile << "Found Zone: " << zoneName << endl;
            g_LogFile << "Map ID: " << mapId << endl;
            g_LogFile << "World X Limits (Top/Bottom): " << top << " / " << bottom << endl;
            g_LogFile << "World Y Limits (Left/Right): " << left << " / " << right << endl;*/

            // Example: Convert Normalized (0.5, 0.5) to World Coords
            g_GameState->globalState.top = top;
            g_GameState->globalState.bottom = bottom;
            g_GameState->globalState.left = left;
            g_GameState->globalState.right = right;
            g_GameState->globalState.mapId = mapId;
            g_GameState->globalState.mapName = zoneName;
            g_GameState->globalState.areaId = areaId;
            g_GameState->globalState.mapHash = hash;
            /*g_LogFile << "Center of Map: " << worldX << ", " << worldY << endl;*/
        }

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
                //(((newPlayer.state & (1 << 23)) >> 23) == 1) ? newPlayer.flyingMounted = true : newPlayer.flyingMounted = false;
                (((newPlayer.state & (1 << 20)) >> 20) == 1) ? newPlayer.inWater = true : newPlayer.inWater = false;

                analyzer.ReadUInt32(procId, playerInfo.playerPtr + ENTITY_PLAYER_MOUNT_STATE, newPlayer.mountState);
                //(newPlayer.mountState == 2) ? newPlayer.groundMounted = true : newPlayer.groundMounted = false;
                //(newPlayer.mountState >= 1) ? newPlayer.isMounted = true : newPlayer.isMounted = false;

                analyzer.ReadInt32(procId, playerInfo.playerPtr + ENTITY_PLAYER_HEALTH, newPlayer.health);

                playerInfo = newPlayer;
                return;
            }
        }
        // --- STEP 1: BULK READ HASH ARRAY ---
        std::vector<HashNode> buffer(hashArrayMaximum);
        if (!analyzer.ReadBuffer(procId, hashArray, buffer.data(), hashArrayMaximum * sizeof(HashNode))) {
            if (g_LogFile.is_open()) g_LogFile << "[ERROR] Failed to bulk read HashArray at: " << std::hex << hashArray << std::dec << std::endl;
            return; // Abort safely
        }
        
        // --- STEP 2: BULK READ ENTITY PTR ARRAY ---
        std::vector<DWORD_PTR> ptrBuffer(entityArraySize);
        if (!analyzer.ReadBuffer(procId, entityArray, ptrBuffer.data(), entityArraySize * sizeof(DWORD_PTR))) {
            if (g_LogFile.is_open()) g_LogFile << "[ERROR] Failed to bulk read EntityArray at: " << std::hex << entityArray << std::dec << std::endl;
            return; // Abort safely
        }

        // 1. Read the WHOLE array in one shot
        for (int i = 0; i < hashArrayMaximum; ++i) {
            ULONG_PTR EntryGuidLow = buffer[i].GuidLow;
            ULONG_PTR EntryGuidHigh = buffer[i].GuidHigh;
            ULONG_PTR EntityIndex = buffer[i].EntityIndex;

            //analyzer.ReadPointer(procId, hashArray + (i * 24), EntryGuidLow);
            //analyzer.ReadPointer(procId, hashArray + (i * 24) + 0x8, EntryGuidHigh);
            //analyzer.ReadPointer(procId, hashArray + (i * 24) + 0x10, EntityIndex);

            long EntInd = EntityIndex & 0x3FFFFFFF;

            // --- SANITY CHECK 2: INDEX BOUNDS ---
            if (EntInd < 0 || EntInd >= entityArraySize) {
                // This is normal for empty hash slots, but if it happens for VALID guids, it's a problem.
                // We just continue silently to avoid spam, or log if it looks suspicious.
                continue;
            }

            // Valid check
            if (!(EntryGuidHigh == 0 && EntryGuidLow == 0) && !(EntryGuidLow == 1 && EntryGuidHigh == 0x400000000000000)) {

                DWORD_PTR entityBuilderPtr = ptrBuffer[EntInd];

                // --- SANITY CHECK 3: NULL OR INVALID POINTERS ---
                if (entityBuilderPtr == 0) continue;
                // Pointers in 64-bit windows are generally 8-byte aligned. 
                // If it's odd, it's definitely garbage.
                if (entityBuilderPtr % 8 != 0) {
                    if (g_LogFile.is_open()) g_LogFile << "[WARN] Unaligned EntityPointer at Index " << i << ": " << std::hex << entityBuilderPtr << std::endl;
                    continue;
                }

                DWORD_PTR entity_ptr = 0;
                int32_t objType = 0;
                ULONG_PTR guidLow = 0, guidHigh = 0;

                // READ ENTITY BASE
                if (!analyzer.ReadPointer(procId, entityBuilderPtr + ENTITY_ENTRY_OFFSET, entity_ptr)) {
                    if (g_LogFile.is_open()) g_LogFile << "[WARN] Read fail: EntityBase at " << std::hex << entityBuilderPtr << std::endl;
                    continue;
                }
                // READ TYPE
                if (!analyzer.ReadInt32(procId, entity_ptr + ENTITY_OBJECT_TYPE_OFFSET, objType)) {
                    // Fail silently or log
                    continue;
                }
                // [DEBUG] Log suspicious types if you suspect corruption
                if (objType < 0 || objType > 10000) {
                    if (g_LogFile.is_open()) g_LogFile << "[WARN] Garbage ObjType: " << objType << " at ptr " << std::hex << entity_ptr << std::endl;
                    continue;
                }
                // READ GUIDs
                analyzer.ReadPointer(procId, entity_ptr + ENTITY_GUID_LOW_OFFSET, guidLow);
                analyzer.ReadPointer(procId, entity_ptr + ENTITY_GUID_HIGH_OFFSET, guidHigh);

                uint32_t low_counter, type_field, instance, id, map_id, server_id;
                GUIDBreakdown(low_counter, type_field, instance, id, map_id, server_id, guidLow, guidHigh);

                // Filter for specific types
                if ((objType == 3) || (objType == 7) || (objType == 33) || (objType == 97) || (objType == 225) || (objType == 257) || (objType == 1025)) {
                    // --- SAFETY BLOCK FOR LOGGING ---
                    // Log before processing complex entities if you suspect a specific one crashes it
                    //if (g_LogFile.is_open()) g_LogFile << "Processing Type " << objType << " ID: " << id << std::endl; 

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
                        ULONG_PTR targetGuidLow;
                        ULONG_PTR targetGuidHigh;
                        ULONG_PTR rangedTargetGuidLow;
                        ULONG_PTR rangedTargetGuidHigh;

                        // Use robust reads
                        if (!analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_X_OFFSET, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->position.x)) continue;
                        analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_Y_OFFSET, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->position.y);
                        analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_Z_OFFSET, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->position.z);
                        // Fix NaN positions immediately
                        auto& pos = std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->position;
                        if (std::isnan(pos.x) || std::isnan(pos.y) || std::isnan(pos.z)) {
                            if (g_LogFile.is_open()) g_LogFile << "[WARN] NaN Position for NPC " << id << ". Skipping." << std::endl;
                            continue;
                        }
                        analyzer.ReadPointer(procId, entity_ptr + ENTITY_ENEMY_IN_COMBAT_GUID_LOW, targetGuidLow);
                        analyzer.ReadPointer(procId, entity_ptr + ENTITY_ENEMY_IN_COMBAT_GUID_HIGH, targetGuidHigh);
                        analyzer.ReadPointer(procId, entity_ptr + ENTITY_ENEMY_RANGED_COMBAT_GUID_LOW, rangedTargetGuidLow);
                        analyzer.ReadPointer(procId, entity_ptr + ENTITY_ENEMY_RANGED_COMBAT_GUID_HIGH, rangedTargetGuidHigh);
                        std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->targetGuidLow = rangedTargetGuidLow;  // This seems to have the correct value for both ranged and melee attacks
                        std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->targetGuidHigh = rangedTargetGuidHigh;

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
                        analyzer.ReadPointer(procId, entity_ptr + ENTITY_PLAYER_IN_COMBAT_GUID_HIGH, newPlayer.inCombatGuidHigh);
                        analyzer.ReadPointer(procId, entity_ptr + ENTITY_PLAYER_TARGET_GUID_LOW, newPlayer.targetGuidLow);
                        analyzer.ReadPointer(procId, entity_ptr + ENTITY_PLAYER_TARGET_GUID_HIGH, newPlayer.targetGuidHigh);

                        (((newPlayer.state & (1 << 11)) >> 11) == 1) ? newPlayer.inAir = true : newPlayer.inAir = false;
                        (((newPlayer.state & (1 << 24)) >> 24) == 1) ? newPlayer.isFlying = true : newPlayer.isFlying = false;
                        (((newPlayer.state & (1 << 26)) >> 26) == 1) ? newPlayer.isDead = true : newPlayer.isDead = false;
                        (((newPlayer.state & (1 << 20)) >> 20) == 1) ? newPlayer.inWater = true : newPlayer.inWater = false;

                        /*if (newPlayer.isFlying == false) {
                            newPlayer.onGround = true;
                        }*/

                        analyzer.ReadUInt32(procId, entity_ptr + ENTITY_PLAYER_MOUNT_STATE, newPlayer.mountState);

                        analyzer.ReadInt32(procId, entity_ptr + ENTITY_PLAYER_HEALTH, newPlayer.health);
                        analyzer.ReadInt32(procId, entity_ptr + ENTITY_PLAYER_MAX_HEALTH, newPlayer.maxHealth);
                        analyzer.ReadInt32(procId, entity_ptr + ENTITY_PLAYER_LEVEL, newPlayer.level);
                        std::cout << "Player Health: " << std::dec << newPlayer.health << std::fixed << std::setprecision(2) << "  Player Rotation: " << newPlayer.rotation << "  Player Pos: ("
                            << newPlayer.position.x << ", " << newPlayer.position.y << ", " << newPlayer.position.z << ")" << "  Player Flying: " << std::dec << newPlayer.isFlying << "  Player Water: "
                            << newPlayer.inWater << "  Player Ground Mount: " << newPlayer.groundMounted << "  Player Flying Mount: " << newPlayer.flyingMounted << std::endl;

                        newPlayer.playerGuidLow = guidLow;
                        newPlayer.playerGuidHigh = guidHigh;
                        newPlayer.mapId = mapId;
                        newPlayer.areaId = areaId;

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
                    if (objType == 1025) {
                        newEntity.objType = "Player Corpse";
                        auto corpseInfo = std::make_shared<CorpseInfo>();
                        newEntity.info = corpseInfo;

                        analyzer.ReadFloat(procId, entity_ptr + OBJECT_POSITION_X_OFFSET, std::dynamic_pointer_cast<CorpseInfo>(newEntity.info)->position.x);
                        analyzer.ReadFloat(procId, entity_ptr + OBJECT_POSITION_Y_OFFSET, std::dynamic_pointer_cast<CorpseInfo>(newEntity.info)->position.y);
                        analyzer.ReadFloat(procId, entity_ptr + OBJECT_POSITION_Z_OFFSET, std::dynamic_pointer_cast<CorpseInfo>(newEntity.info)->position.z);
                        std::dynamic_pointer_cast<CorpseInfo>(newEntity.info)->id = id;
                    }
                    if (objType == 97) {
                        newEntity.objType = "Other Player";
                        auto otherPlayerInfo = std::make_shared<OtherPlayerInfo>();
                        newEntity.info = otherPlayerInfo;

                        analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_X_OFFSET, std::dynamic_pointer_cast<OtherPlayerInfo>(newEntity.info)->position.x);
                        analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_Y_OFFSET, std::dynamic_pointer_cast<OtherPlayerInfo>(newEntity.info)->position.y);
                        analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_Z_OFFSET, std::dynamic_pointer_cast<OtherPlayerInfo>(newEntity.info)->position.z);
                    }
                    newEntity.id = id;
                    newEntity.mapId = map_id;

                    // Add to our list
                    entityList.push_back(newEntity);
                }
                else {
                    //g_LogFile << objType << " " << entity_ptr << std::endl;
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

            g_GameState->player.bagFreeSlots = 0;
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
                    else if (auto corpse = std::dynamic_pointer_cast<CorpseInfo>(entity.info)) {
                        corpse->distance = corpse->position.Dist3D(newPlayer.position);
                    }
                    else if (auto otherPlayer = std::dynamic_pointer_cast<OtherPlayerInfo>(entity.info)) {
                        otherPlayer->distance = otherPlayer->position.Dist3D(newPlayer.position);
                    }
                    if (auto bag = std::dynamic_pointer_cast<BagInfo>(entity.info)) {
                        bag->freeSlots = bag->bagSlots;
                        bag->equippedBag = true;
                        if ((entity.guidLow == guidLowBag1) && (entity.guidHigh == guidHighBag1))
                            bag->bagId = 1;
                        else if ((entity.guidLow == guidLowBag2) && (entity.guidHigh == guidHighBag2))
                            bag->bagId = 2;
                        else if ((entity.guidLow == guidLowBag3) && (entity.guidHigh == guidHighBag3))
                            bag->bagId = 3;
                        else if ((entity.guidLow == guidLowBag4) && (entity.guidHigh == guidHighBag4))
                            bag->bagId = 4;
                        else {
                            //bag->bagId = 0;
                            bag->equippedBag = false;
                            continue;
                        }

                        for (auto& itemList : entityList) {
                            if (auto item = std::dynamic_pointer_cast<ItemInfo>(itemList.info)) {
                                if ((item->bagGuidLow == entity.guidLow) && (item->bagGuidHigh == entity.guidHigh) && bag->equippedBag) {
                                    bag->freeSlots = bag->freeSlots - 1;
                                    item->bagID = bag->bagId;
                                }
                            }
                        }
                        g_GameState->player.bagFreeSlots += bag->freeSlots;
                    }
                }
            }

            // Calculate Free slots in main bag. Don't know why its not included above
            int freeSlots = 16;
            ULONG_PTR guidLowBag, guidHighBag;
            for (int i = 0; i < 16; i++) {
                analyzer.ReadPointer(procId, newPlayer.playerPtr + ENTITY_PLAYER_BAG_OFFSET + 0x50 + (i * 0x10), guidLowBag);
                analyzer.ReadPointer(procId, newPlayer.playerPtr + ENTITY_PLAYER_BAG_OFFSET + 0x58 + (i * 0x10), guidHighBag);
                if (guidLowBag != 0 && guidHighBag != 0) {
                    freeSlots--;
                }
            }
            g_GameState->player.bagFreeSlots += freeSlots;
        }
    }
    catch (const std::exception& e) {
        if (g_LogFile.is_open()) g_LogFile << "[EXCEPTION] In ExtractEntities: " << e.what() << std::endl;
    }
    catch (...) {
        if (g_LogFile.is_open()) g_LogFile << "[CRITICAL] Unknown Exception in ExtractEntities!" << std::endl;
    }

    return;
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
    worldmap_db.loadWorldMapArea("Z:\\WowDB\\WorldMapArea.csv");

    AddVectoredExceptionHandler(1, CrashHandler);

    g_GameState = &g_GameStateInstance;
    g_LogFile.open("C:\\SMM\\SMM_Debug.log", std::ios::out | std::ios::trunc);

    // 1. ALLOCATE CONSOLE
    // This creates a popup cmd window so std::cout works
    //AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);

    g_LogFile << "DLL Loaded! Console Attached." << std::endl;

    WebServer::Start(5678); // Starts web server on http://localhost:5678
    g_LogFile << "Web Server started at http://localhost:8080" << std::endl;
    // Launch GUI in background thread
    std::thread guiThread(StartGuiThread, hModule);

    try {
        MemoryAnalyzer analyzer;
        SimpleKeyboardClient kbd;
        SimpleMouseClient mouse;

        // Connect Drivers once (assuming driver persistence)
        if (!kbd.Connect()) g_LogFile << "Keyboard Driver Init Failed" << std::endl;
        if (!mouse.Connect()) g_LogFile << "Mouse Driver Init Failed" << std::endl;
        if (!analyzer.Connect()) g_LogFile << "Memory Driver Init Failed" << std::endl;

        ConsoleInput console(kbd);
        bool wasPaused = false;

        // --- OUTER LOOP: RE-INITIALIZATION LOOP ---
        // This loop allows the bot to "Restart" finding the process if "GAME_REBOOTED" is received
        while (g_IsRunning) {
            // 1. CHECK PROCESS COUNT
            int procCount = GetProcessCount(L"WowClassic.exe");

            if (procCount == 0) {
                g_LogFile << "[System] WowClassic.exe not found." << std::endl;
                UpdateStatus("NOT_IN_GAME");
                Sleep(1000);
                continue;
            }

            if (procCount > 1) {
                g_LogFile << "[System] Multiple WowClassic.exe processes detected!" << std::endl;
                UpdateStatus("NOT_IN_GAME"); // Signal Python to kill them
                Sleep(1000);
                continue;
            }

            // 2. CHECK WINDOW
            HWND hGameWindow = FindWindowA(NULL, "World of Warcraft");
            if (!hGameWindow) {
                g_LogFile << "[System] Window not found." << std::endl;
                UpdateStatus("NOT_IN_GAME");
                Sleep(1000);
                continue;
            }
            
            if (!g_IsRunning) break; 

            // 3. ATTACH
            MovementController pilot(kbd, mouse, hGameWindow);
            mouse.SetLockWindow(hGameWindow); // Update lock window
            // Bring Window to Foreground & Center Cursor ---
            if (IsIconic(hGameWindow)) {
                ShowWindow(hGameWindow, SW_RESTORE);
            }
            SetForegroundWindow(hGameWindow);

            DWORD procId = GetProcId(L"WowClassic.exe");
            if (procId != 0) {
                baseAddress = FindMainModuleViaDriver(analyzer, procId);

                // --- INITIALIZE SYSTEMS ---
                Camera cam(analyzer, mouse, procId);
                GoapAgent agent(g_GameStateInstance, pilot, mouse, kbd, cam, analyzer, procId, baseAddress, hGameWindow);
                InteractionController interact(pilot, mouse, kbd, cam, analyzer, procId, baseAddress, hGameWindow);

                OverlayWindow overlay;
                if (!overlay.Setup(hGameWindow)) {
                    std::cout << "Failed to create overlay." << std::endl;
                    return;
                }

                if (baseAddress != 0) {
                    ULONG_PTR objMan_Direct = 0;
                    ULONG_PTR objMan_Entry = 0;
                    ULONG_PTR objMan_Base = 0;
                    ULONG_PTR entityArray = 0;
                    ULONG_PTR hashArray = 0;
                    ULONG_PTR luaEntry = 0;
                    int32_t hashArrayMaximum = 0;
                    int32_t hashArraySize = 0;
                    int32_t entityArraySize = 0;

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
                        g_LogFile << "Camera update failed." << std::endl;
                        Sleep(1000);
                    }
                    console.SendDataRobust(std::wstring(L"/console autointeract 0"));

                    bool isPaused = false;
                    bool lastF3State = false;
                    bool needsReboot = false; // Flag to break the inner loop
                    std::vector<GameEntity> persistentEntityList;
                    persistentEntityList.reserve(3000);
                    std::string searchPattern = "##MAGSTR##table:";

                    RECT rect;
                    GetClientRect(hGameWindow, &rect);
                    POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
                    ClientToScreen(hGameWindow, &center);
                    SetCursorPos(center.x, center.y);

                    g_LogFile << g_GameState->player.position.x << " " << g_GameState->player.position.y << " " << g_GameState->player.position.z << " " << std::endl;

                    // --- INNER LOOP: GAME LOOP ---
                    // Exits if F4 is pressed (g_IsRunning = false) OR Reboot needed
                    while (g_IsRunning && !needsReboot) {
                        // --- PAUSE LOGIC ---
                        if (g_IsPaused) {
                            if (!wasPaused) {
                                std::cout << ">>> PAUSED <<<" << std::endl;
                                pilot.Stop();
                                wasPaused = true;
                            }
                            Sleep(100);
                            continue; // Skip logic while paused
                        }
                        else {
                            if (wasPaused) {
                                std::cout << ">>> RESUMED <<<" << std::endl;
                                wasPaused = false;
                            }
                        }

                        UpdateStatus("IN_GAME");

                        // 2. Refresh Pointers & Entities
                        analyzer.ReadPointer(procId, baseAddress + OBJECT_MANAGER_OFFSET, objMan_Direct);
                        if (objMan_Direct == 0x0) {
                            g_LogFile << "Not in game detected. Entering Wait Mode..." << std::endl;
                            UpdateStatus("NOT_IN_GAME");
                            DWORD lastCheckTime = 0;

                            // WAIT LOOP
                            while (g_IsRunning) {

                                // Check status file every 1000ms
                                if (GetTickCount() - lastCheckTime > 1000) {
                                    std::string cmd = ReadStatus();

                                    if (cmd == "GAME_REBOOTED") {
                                        std::cout << "Received GAME_REBOOTED." << std::endl;
                                        needsReboot = true;
                                        break;
                                    }
                                    else if (cmd == "GAME_OK") {
                                        std::cout << "Received GAME_OK." << std::endl;
                                        break;
                                    }
                                    lastCheckTime = GetTickCount();
                                }

                                // Sleep shortly to keep loop responsive
                                Sleep(10);
                            }

                            if (needsReboot) break; // Break Inner Loop
                            continue; // Continue Inner Loop (Check mem again)
                        }

                        analyzer.ReadPointer(procId, objMan_Entry + OBJECT_MANAGER_FIRST_OBJECT_OFFSET, objMan_Base);
                        analyzer.ReadPointer(procId, objMan_Base + ENTITY_ARRAY_OFFSET, entityArray);
                        analyzer.ReadInt32(procId, objMan_Base + ENTITY_ARRAY_SIZE_OFFSET, entityArraySize);
                        analyzer.ReadInt32(procId, objMan_Base + HASH_ARRAY_MAXIMUM_OFFSET, hashArrayMaximum);
                        analyzer.ReadPointer(procId, objMan_Base + HASH_ARRAY_OFFSET, hashArray);
                        analyzer.ReadInt32(procId, objMan_Base + HASH_ARRAY_SIZE_OFFSET, hashArraySize);

                        // --- [RECOMMENDED] Sanity Check to prevent Driver Crash on bad reads ---
                        if (hashArrayMaximum > 100000 || hashArrayMaximum < 0) {
                            g_LogFile << "[WARNING] Garbage Hash Size: " << hashArrayMaximum << ". Skipping frame." << std::endl;
                            continue;
                        }

                        // LOCK before writing
                        std::lock_guard<std::mutex> lock(g_EntityMutex);
                        ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, entityArraySize, g_GameState->player, persistentEntityList, agent);
                        g_GameState->entities = persistentEntityList;
                        // Read Lua data
                        LuaAnchor::ReadLuaData(analyzer, procId, searchPattern, luaEntry, worldmap_db);

                        // Update UI / Overlay
                        RECT clientRect;
                        GetClientRect(hGameWindow, &clientRect);
                        int width = clientRect.right - clientRect.left;
                        int height = clientRect.bottom - clientRect.top;
                        cam.UpdateScreenSize(width, height);

                        // Draw Path (Optional visualization)
                        overlay.DrawFrame(-100, -100, RGB(0, 0, 0));
                        for (size_t i = g_GameState->globalState.activeIndex; i < min(g_GameState->globalState.activeIndex + 6, g_GameState->globalState.activePath.size()); ++i) {
                            int screenPosx, screenPosy;
                            if (i >= g_GameState->globalState.activePath.size()) break;
                            if (cam.WorldToScreen(g_GameState->globalState.activePath[i].pos, screenPosx, screenPosy)) {
                                overlay.DrawFrame(screenPosx, screenPosy, RGB(0, 255, 0), true);
                            }
                        }
                        try {
                            UpdateGuiData(g_GameState->entities);
                        }
                        catch (...) {
                            g_LogFile << "[WARNING] GUI Update failed. Skipping frame." << std::endl;
                        }
                        //pilot.Calibrate(g_GameState->player.rotation, g_GameState->player.vertRotation);

                        //if (g_GameState->pathFollowState.pathIndexChange == true) {
                        //    if (g_GameState->pathFollowState.path[g_GameState->pathFollowState.index - 1].pos.Dist3D(g_GameState->pathFollowState.presetPath[g_GameState->pathFollowState.presetIndex]) < 5.0) {
                        //        if ((g_GameState->pathFollowState.presetIndex >= g_GameState->pathFollowState.presetPath.size() - 1) && (g_GameState->pathFollowState.looping == 1)) {
                        //            g_GameState->pathFollowState.presetIndex = 0;
                        //        }
                        //        else if (g_GameState->pathFollowState.presetIndex < g_GameState->pathFollowState.presetPath.size() - 1) {
                        //            g_GameState->pathFollowState.presetIndex++;
                        //        }
                        //        //path = CalculatePath(agent.state.presetPath, agent.state.player.position, agent.state.presetPathIndex, true, 530);
                        //    }
                        //    g_GameState->pathFollowState.pathIndexChange = false;
                        //}
                        
                        // 3. Execution Logic
                        if (!isPaused) {
                            try {
                                auto* activeProfile = g_ProfileLoader.GetActiveProfile();
                                if (activeProfile) {
                                    // Enforce Focus
                                    if (GetForegroundWindow() != hGameWindow) SetForegroundWindow(hGameWindow);

                                    // This runs your profile script (once) to populate the queue
                                    if (auto* profile = g_ProfileLoader.GetActiveProfile()) {
                                        profile->Tick();
                                    }

                                    if (UnderAttackCheck() == true) {
                                        //g_LogFile << "Under Attack Detected!" << std::endl;
                                    }
                                    if (g_GameState->gatherState.enabled == true) {
                                        UpdateGatherTarget(g_GameStateInstance);
                                    }
                                    if ((g_GameState->player.bagFreeSlots <= 2)) {
                                        // If 30 minutes since last resupply mail items
                                        if (((g_GameState->globalState.bagEmptyTime != -1) && (GetTickCount() - g_GameState->globalState.bagEmptyTime < 1800000) && g_ProfileSettings.mailingEnabled) || g_GameState->interactState.mailing) {
                                            MailItems();
                                        }
                                        else {
                                            Repair();
                                        }
                                    }
                                    // if (g_GameState->player.needRepair && !g_GameState->interactState.interactActive) {
                                    if (g_GameState->player.needRepair) {
                                        Repair();
                                    }
                                    agent.Tick();
                                }
                                else {
                                    Sleep(100); // Sleep longer when idle to save CPU
                                }
                            }
                            catch (...) {
                                g_LogFile << "Agent Tick Fail" << std::endl;
                            }
                        }
                        Sleep(10); // Prevent high CPU usage
                    }
                    pilot.Stop();
                }
            }
            // Safety sleep between re-init attempts
            Sleep(2000);
        }
        RaiseException(0xDEADBEEF, 0, 0, nullptr); // Forcibly exit all threads (including GUI)
    }

    catch (const std::exception& ex) {
        std::string msg = std::string("EXCEPTION: ") + ex.what();
        g_LogFile << msg << std::endl;
    }
    catch (...) {
        g_LogFile << "UNKNOWN EXCEPTION CAUGHT" << std::endl;
    }

    if (fDummy) fclose(fDummy);
    fclose(stdout);
    fclose(stderr);
    fclose(stdin);
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