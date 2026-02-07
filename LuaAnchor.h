#pragma once
#include <vector>
#include <string>
#include <iostream>
#include "MemoryRead.h" // Ensures we have access to MemoryAnalyzer
#include "Database.h"

ULONG_PTR LUA_ADDON_ENTRY = 0x0;
ULONG_PTR oldPtr = 0;

class LuaAnchor {
public:

    // Scans the target process for the specific string pattern.
    // Returns the address if found, or 0 if not found.
    static ULONG_PTR Find(MemoryAnalyzer& analyzer, DWORD pid, const std::string& magicString) {
        // 1. Get all memory regions from the kernel driver
        //    This is much faster than blindly scanning 0x0000 to 0x7FFF...
        auto regions = analyzer.EnumerateMemoryRegions(pid);

        if (regions.empty()) {
            g_LogFile << "[LuaAnchor] Failed to enumerate regions or process is empty." << std::endl;
            return 0;
        }

        // 2. Convert the search string to a byte vector
        std::vector<uint8_t> pattern(magicString.begin(), magicString.end());

        // 3. Iterate over every memory region
        for (const auto& region : regions) {

            // Lua variables are stored on the Heap, which is always MEM_COMMIT.
            if (region.State != MEM_COMMIT) continue;

            // Lua variables are Read/Write. They are never in Read-Only (Const) or Execute (Code) pages.
            // We check for PAGE_READWRITE or PAGE_WRITECOPY.
            if ((region.Protect & PAGE_READWRITE) == 0 && (region.Protect & PAGE_WRITECOPY) == 0) {
                continue;
            }
            // Reading a Guard Page triggers an exception (Access Violation) in the target process.
            if (region.Protect & PAGE_GUARD) {
                continue;
            }
            // Skip No Access explicitly (though implicit in R/W check above)
            if (region.Type == MEM_IMAGE) continue; // Skip DLLs/Exes

            // 4. Scan this specific region
            ULONG_PTR result = ScanRegion(analyzer, pid, region.BaseAddress, region.RegionSize, pattern);
            if (result != 0) {
                if (Validator(analyzer, pid, result)) {
                    // std::cout << "[LuaAnchor] Found pattern at: 0x" << std::hex << result << std::dec << std::endl;
                    return result;
                }
            }
        }
        g_LogFile << "[LuaAnchor] Pattern not found." << std::endl;
        return 0;
    }

    static uintptr_t ExtractAddressFromLuaString(const std::string& fullString) {
        // The format we expect: "##MAGSTR##table: 0000000064228860##MAGSTR##"

        // 1. Find the start of the address
        // Lua's tostring() always outputs "table: " followed by the address.
        std::string key = "table: ";
        size_t startPos = fullString.find(key);

        if (startPos == std::string::npos) {
            std::cerr << "[Error] 'table: ' not found in string." << std::endl;
            return 0;
        }

        // Move index past "table: "
        startPos += key.length();

        // 2. Find the end of the address
        // The address ends where the next "##" starts, or at the end of the hex string.
        // We can just search for the suffix "##".
        size_t endPos = fullString.find("##", startPos);

        // If no suffix found, just take a reasonable length (16 chars for 64-bit hex)
        if (endPos == std::string::npos) {
            endPos = startPos + 16;
        }

        // 3. Extract the substring containing ONLY the hex digits
        // e.g., "0000000064228860"
        std::string hexString = fullString.substr(startPos, endPos - startPos);

        // 4. Convert hex string to uintptr_t
        try {
            // std::stoull converts string to unsigned long long. 
            // Base 16 tells it to treat it as Hex.
            uintptr_t address = std::stoull(hexString, nullptr, 16);
            return address;
        }
        catch (...) {
            g_LogFile << "[Error] Failed to convert hex string: " << hexString << std::endl;
            return 0;
        }
    }

    static void ReadLuaData(MemoryAnalyzer& analyzer, DWORD pid, const std::string& magicString, ULONG_PTR& entryPtr, WoWDataTool& worldMap) {
        try {
            std::string rawMemoryString = "";
            bool firstRead = false;

            if (entryPtr != 0 && g_GameState->globalState.reloaded) {
                oldPtr = entryPtr;
                g_GameState->globalState.reloaded = false;
                entryPtr = 0;
                return;
            }

            if (entryPtr == 0) {
                entryPtr = LuaAnchor::Find(analyzer, pid, magicString);
                if ((entryPtr != 0) && (oldPtr != 0 && entryPtr == oldPtr)) {
                    firstRead = true;
                    oldPtr = 0;
                    g_LogFile << ">>> Lua Base Address Found: 0x" << std::hex << entryPtr << std::dec << std::endl;
                }
                else {
                    return; // Still looking
                }
            }

            // Read the string anchor
            if (!analyzer.ReadString(pid, entryPtr, rawMemoryString, 60)) {
                // g_LogFile << "[LuaAnchor] ReadString failed. Resetting EntryPtr." << std::endl;
                entryPtr = 0;
                return;
            }

            // Validation: Does the string still look like our anchor?
            if (rawMemoryString.find("##MAGSTR##") == std::string::npos) {
                // g_LogFile << "[LuaAnchor] Magic header missing. Resetting EntryPtr." << std::endl;
                entryPtr = 0;
                return;
            }

            uintptr_t tableAddress = LuaAnchor::ExtractAddressFromLuaString(rawMemoryString);
            if (tableAddress == 0) return;

            uintptr_t arrayPtr = 0;
            // 0x20 is a common offset for Lua tables in WoW, but ensure this is correct for your version
            if (!analyzer.ReadPointer(pid, tableAddress + 0x20, arrayPtr) || arrayPtr == 0) {
                return; // Read failed or null pointer
            }

            double values[25];

            // Read Verification Value (Index 0)
            if (!analyzer.ReadDouble(pid, arrayPtr + 0x00, values[0])) return;

            analyzer.ReadDouble(pid, arrayPtr + 0x18, values[1]);
            analyzer.ReadDouble(pid, arrayPtr + 0x30, values[2]);
            analyzer.ReadDouble(pid, arrayPtr + 0x48, values[3]);
            analyzer.ReadDouble(pid, arrayPtr + 0x60, values[4]);
            analyzer.ReadDouble(pid, arrayPtr + 0x78, values[5]);
            analyzer.ReadDouble(pid, arrayPtr + 0x90, values[6]);
            analyzer.ReadDouble(pid, arrayPtr + 0xA8, values[7]);
            analyzer.ReadDouble(pid, arrayPtr + 0xC0, values[8]);
            analyzer.ReadDouble(pid, arrayPtr + 0xD8, values[9]);
            analyzer.ReadDouble(pid, arrayPtr + 0xF0, values[10]);
            analyzer.ReadDouble(pid, arrayPtr + 0x108, values[11]);
            analyzer.ReadDouble(pid, arrayPtr + 0x120, values[12]);
            analyzer.ReadDouble(pid, arrayPtr + 0x138, values[13]);
            analyzer.ReadDouble(pid, arrayPtr + 0x150, values[14]);
            analyzer.ReadDouble(pid, arrayPtr + 0x168, values[15]);
            analyzer.ReadDouble(pid, arrayPtr + 0x180, values[16]);
            analyzer.ReadDouble(pid, arrayPtr + 0x198, values[17]);

            double prof1Id, prof1Level, prof1Max, prof2Id, prof2Level, prof2Max;
            analyzer.ReadDouble(pid, arrayPtr + 0x1C8, prof1Id);
            analyzer.ReadDouble(pid, arrayPtr + 0x1E0, prof1Level);
            analyzer.ReadDouble(pid, arrayPtr + 0x1F8, prof1Max);
            analyzer.ReadDouble(pid, arrayPtr + 0x210, prof2Id);
            analyzer.ReadDouble(pid, arrayPtr + 0x228, prof2Level);
            analyzer.ReadDouble(pid, arrayPtr + 0x240, prof2Max);

            //g_LogFile << prof1Id << " " << prof1Level << " " << prof1Max << " " << prof2Id << " " << prof2Level << " " << prof2Max << std::endl;

            ((values[1] > 0.5) ? g_GameState->player.needRepair = true : g_GameState->player.needRepair = false);
            ((values[2] > 0.5) ? g_GameState->player.isIndoor = true : g_GameState->player.isIndoor = false);
            ((values[3] > 0.5) ? g_GameState->player.areaMountable = true : g_GameState->player.areaMountable = false);
            (((values[6] > 0.5) && (values[4] > 0.5)) ? g_GameState->player.flyingMounted = true : g_GameState->player.flyingMounted = false);
            (((values[6] < 0.5) && (values[4] > 0.5)) ? g_GameState->player.groundMounted = true : g_GameState->player.groundMounted = false);
            g_GameState->player.isMounted = g_GameState->player.flyingMounted || g_GameState->player.groundMounted;
            ((values[7] > 0.5) ? g_GameState->player.isGhost = true : g_GameState->player.isGhost = false);
            ((values[10] > 0.5) ? g_GameState->player.canRespawn = true : g_GameState->player.canRespawn = false);
            ((values[11] > 0.5) ? g_GameState->player.isDeadBody = true : g_GameState->player.isDeadBody = false);
            ((values[12] > 0.5) ? g_GameState->globalState.vendorOpen = true : g_GameState->globalState.vendorOpen = false);
            ((values[13] > 0.5) ? g_GameState->globalState.chatOpen = true : g_GameState->globalState.chatOpen = false);
            ((values[16] > 0.5) ? g_GameState->player.onGround = true : g_GameState->player.onGround = false);
            ((values[17] > 0.5) ? g_GameState->interactState.sellComplete = true : g_GameState->interactState.sellComplete = false);
            //((values[14 > 0.5) ? g_LogFile << "Mail Window Open" << std::endl : g_LogFile << "Mail Window Closed" << std::endl);

            g_GameState->player.isDead = g_GameState->player.isGhost || g_GameState->player.isDeadBody;
            if (g_GameState->player.isDead) {
                float top, bottom, left, right;
                worldMap.reverseHash(values[15], g_GameState->player.corpseMapId, top, bottom, left, right,
                    g_GameState->player.corpseAreaId, g_GameState->player.corpseMapName);
                worldMap.convertNormToWorld(values[8], values[9], top, bottom, left, right, g_GameState->player.corpseX, g_GameState->player.corpseY);
                g_GameState->player.corpseMapHash = values[15];
                g_GameState->respawnState.isDead = g_GameState->player.isDead;
                //g_LogFile << g_GameState->player.corpseX << " " << g_GameState->player.corpseY << std::endl;
            }
            else {
                g_GameState->player.corpseMapId = -1;
                g_GameState->player.corpseMapName = -1;
                g_GameState->player.corpseMapHash = -1; // Map name hash
                g_GameState->player.corpseX = -1;
                g_GameState->player.corpseY = -1;
                g_GameState->player.corpseMapHash = -1;
            }

            if (firstRead == true) {
                g_LogFile << "Data: [" << g_GameState->player.needRepair << ", " << g_GameState->player.isIndoor << ", " << g_GameState->player.areaMountable << ", " << values[4] << ", " << values[5]
                    << ", " << values[6] << " " << values[7] << " " << values[8] << " " << values[9] << " " << values[10] << " " << values[11] << " " << values[12] << " " << values[13] << " "
                    << values[14] << " " << values[15] << " " << values[16] << "]" << std::endl;
            }
        }
        catch (...) {
            // If ANYTHING goes wrong (bad cast, weird memory), reset.
            entryPtr = 0;
        }
    }

private:
    // Helper: Scans a single memory region efficiently
    static ULONG_PTR ScanRegion(MemoryAnalyzer& analyzer, DWORD pid, ULONG_PTR base, SIZE_T size, const std::vector<uint8_t>& pattern) {
        // Read in 64KB chunks to keep memory usage low but speed high
        const size_t CHUNK_SIZE = 0x10000;
        std::vector<uint8_t> buffer(CHUNK_SIZE);

        size_t patternLen = pattern.size();
        if (patternLen == 0 || size < patternLen) return 0;

        // We loop through the region
        for (size_t offset = 0; offset < size; ) {

            // Calculate how much to read
            size_t bytesToRead = (std::min)(CHUNK_SIZE, size - offset);

            // Read the chunk (Quiet mode = true, so we don't spam console on bad reads)
            if (analyzer.ReadMemory(pid, base + offset, buffer.data(), bytesToRead, true)) {

                // Scan the buffer
                // We stop 'patternLen' bytes before the end to prevent overflow in the inner loop
                // (The boundary crossing is handled by the offset overlap below)
                size_t scanLimit = (bytesToRead >= patternLen) ? (bytesToRead - patternLen + 1) : 0;

                for (size_t i = 0; i < scanLimit; ++i) {
                    bool match = true;
                    for (size_t p = 0; p < patternLen; ++p) {
                        if (buffer[i + p] != pattern[p]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        return base + offset + i;
                    }
                }
            }

            // Move to next chunk
            // CRITICAL: We overlap the chunks by (patternLen - 1). 
            // If the pattern is split exactly between two chunks (e.g. half in chunk A, half in chunk B),
            // this overlap ensures it appears fully in chunk B's start.
            if (bytesToRead < CHUNK_SIZE) break; // End of region reached

            offset += (CHUNK_SIZE - patternLen);
        }
        return 0;
    }

    static bool Validator(MemoryAnalyzer& analyzer, DWORD pid, ULONG_PTR entryPtr) {
        std::string rawMemoryString;
        if ((entryPtr != 0) && (analyzer.ReadString(pid, entryPtr, rawMemoryString, 60))) {
            uintptr_t tableAddress = LuaAnchor::ExtractAddressFromLuaString(rawMemoryString);
            // g_LogFile << "tbl" << tableAddress << std::endl;
            if (tableAddress != 0) {
                uintptr_t arrayPtr = 0;
                if (analyzer.ReadPointer(pid, tableAddress + 0x20, arrayPtr)) {
                    double val1;
                    analyzer.ReadDouble(pid, arrayPtr + 0x00, val1);
                    // g_LogFile << val1 << std::endl;
                    if (val1 == 54381046.0) {
                        // g_LogFile << "Found" << std::endl;
                        return true;
                    }
                }
            }
        }
        return false;
    }
};