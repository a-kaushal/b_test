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
#include "PathFinding.h"
#include "Movement.h"
#include "Vector.h"
#include "Database.h"
#include "MovementController.h"
#include "GoapSystem.h"
#include "ScreenRenderer.h"
#include "OverlayWindow.h"

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

std::vector<GameEntity> ExtractEntities(MemoryAnalyzer& analyzer, DWORD procId, ULONG_PTR hashArray, int hashArrayMaximum, ULONG_PTR entityArray, PlayerInfo& playerInfo, bool playerOnly = false) {

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

            analyzer.ReadUInt32(procId, playerInfo.playerPtr + 0xA40, newPlayer.mountState);
            (newPlayer.mountState == 1) ? newPlayer.groundMounted = true : newPlayer.groundMounted = false;
            (newPlayer.mountState == 3) ? newPlayer.flyingMounted = true : newPlayer.flyingMounted = false;

            analyzer.ReadInt32(procId, playerInfo.playerPtr + 0x119F8, newPlayer.health);

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

            uint32_t low_counter = (uint32_t)get_bits_u128(guidLow, guidHigh, 0, 32);    // bits 0..31
            uint32_t type_field = (uint32_t)get_bits_u128(guidLow, guidHigh, 18, 3);    // bits 18..20
            uint32_t instance = (uint32_t)get_bits_u128(guidLow, guidHigh, 40, 6);    // bits 40..45
            uint32_t id = (uint32_t)get_bits_u128(guidLow, guidHigh, 70, 24);   // bits 70..93
            uint32_t map_id = (uint32_t)get_bits_u128(guidLow, guidHigh, 93, 13);   // bits 93..105
            uint32_t server_id = (uint32_t)get_bits_u128(guidLow, guidHigh, 106, 16);  // bits 106..121

            // Filter for specific types
            if ((objType == 33) || (objType == 225) || (objType == 257)) {

                // 3. Store the data in our struct
                GameEntity newEntity;
                newEntity.guidLow = EntryGuidLow;
                newEntity.guidHigh = EntryGuidHigh;
                newEntity.entityIndex = EntInd;
                newEntity.entityPtr = entity_ptr;
                newEntity.type = objType;
                if (objType == 33) {
                    newEntity.objType = "NPC";
                    auto enemyInfo = std::make_shared<EnemyInfo>();
                    newEntity.info = enemyInfo;

                    analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_X_OFFSET, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->position.x);
                    analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_Y_OFFSET, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->position.y);
                    analyzer.ReadFloat(procId, entity_ptr + ENTITY_POSITION_Z_OFFSET, std::dynamic_pointer_cast<EnemyInfo>(newEntity.info)->position.z);
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

                    (((newPlayer.state & (1 << 24)) >> 24) == 1) ? newPlayer.isFlying = true : newPlayer.isFlying = false;
                    (((newPlayer.state & (1 << 23)) >> 23) == 1) ? newPlayer.flyingMounted = true : newPlayer.flyingMounted = false;
                    (((newPlayer.state & (1 << 20)) >> 20) == 1) ? newPlayer.inWater = true : newPlayer.inWater = false;

					analyzer.ReadUInt32(procId, entity_ptr + ENTITY_PLAYER_MOUNT_STATE, newPlayer.mountState);
                    (newPlayer.mountState == 1) ? newPlayer.groundMounted = true : newPlayer.groundMounted = false;
					(newPlayer.mountState == 3) ? newPlayer.flyingMounted = true : newPlayer.flyingMounted = false;

                    analyzer.ReadInt32(procId, entity_ptr + ENTITY_PLAYER_HEALTH, newPlayer.health);
                    std::cout << "Player Health: " << std::dec << newPlayer.health << std::fixed << std::setprecision(2) << "  Player Rotation: " << newPlayer.rotation << "  Player Pos: (" 
                        << newPlayer.position.x << ", " << newPlayer.position.y << ", " << newPlayer.position.z << ")" << "  Player Flying: " << std::dec << newPlayer.isFlying << "  Player Water: " 
                        << newPlayer.inWater << "  Player Ground Mount: " << newPlayer.groundMounted << "  Player Flying Mount: " << newPlayer.flyingMounted << std::endl;

                    playerInfo = newPlayer;
                }
                if (objType == 257) {
                    newEntity.objType = "Object";
                    auto objInfo = std::make_shared<ObjectInfo>();
                    newEntity.info = objInfo;

                    analyzer.ReadFloat(procId, entity_ptr + 0xF0, std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->position.x);
                    analyzer.ReadFloat(procId, entity_ptr + 0xF4, std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->position.y);
                    analyzer.ReadFloat(procId, entity_ptr + 0xF8, std::dynamic_pointer_cast<ObjectInfo>(newEntity.info)->position.z);
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
            if (newPlayer.position.x != 0) {
                for (auto& entity : entityList) {
                    if (entity.entityPtr != newPlayer.playerPtr && entity.info) {
                        // Use std::dynamic_pointer_cast for shared_ptr
                        if (auto enemy = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
                            enemy->distance = enemy->position.Dist3D(newPlayer.position);
                        }
                        else if (auto object = std::dynamic_pointer_cast<ObjectInfo>(entity.info)) {
                            object->distance = object->position.Dist3D(newPlayer.position);
                        }
                    }
                }
            }
        }
    }
    return entityList;
}

void MainThread(HMODULE hModule) {
    // Load the database files
    creature_db.loadDatabase("C:\\Driver\\creatures.tsv");
    item_db.loadDatabase("C:\\Driver\\items.tsv");
    object_db.loadDatabase("C:\\Driver\\objects.tsv");

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

                    mouse.PressButton(MOUSE_LEFT);
                    Sleep(5);
                    mouse.Move(0, 20); // Relative move
                    Sleep(1000);
                    mouse.Move(0, 20); // Relative move
                    Sleep(1000);
                    mouse.Move(0, 20); // Relative move
                    Sleep(1000);
                    mouse.Move(0, -60); // Relative move
                    Sleep(1000);
                    mouse.Move(0, -20); // Relative move
                    Sleep(1000);
                    mouse.Move(0, -20); // Relative move
                    Sleep(1000);
                    mouse.Move(0, -20); // Relative move
                    mouse.ReleaseButton(MOUSE_LEFT);
                    Sleep(5000);

                    // --- HOTKEY STATE VARIABLES ---
                    bool isPaused = false;
                    bool lastF3State = false;

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

                        std::vector<GameEntity> currentEntities = ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, agent.state.player);
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
                                        agent.state.lootGuidLow = entity.guidLow;
                                        agent.state.lootGuidHigh = entity.guidHigh;
                                        agent.state.hasLootTarget = true;
                                        agent.state.lootPos = object->position;
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

                        agent.state.hasLootTarget = true;

                        // --- 1. SENSOR UPDATE (Update World State) ---
						// Only update player every tick
                        static DWORD lastTick = 0;
                        if (GetTickCount() - lastTick < 100) {
                            std::vector<GameEntity> currentEntities = ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, agent.state.player, true);
                        }
                        // --- 2. ENTITY UPDATE (GUI) ---
                        // Update all entities every 100ms
                        else {
                            std::vector<GameEntity> currentEntities = ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, agent.state.player);
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

                    while (g_IsRunning) {
                        if (GetAsyncKeyState(VK_END) & 1) {
							g_IsRunning = false;
                            break;
                        }
                        // 1. Extract Data
                        std::vector<GameEntity> currentEntities = ExtractEntities(analyzer, procId, hashArray, hashArrayMaximum, entityArray, agent.state.player);
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
                        UpdateGuiData(currentEntities);

                        // 3. Logic (Rotation/Console Print)
                        keyDuration = Char_Rotate_To(agent.state.player.rotation, 0.6f, nextKey);
                        //logFile << nextKey << std::endl;

                        Sleep(20); // Prevent high CPU usage
                    }
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