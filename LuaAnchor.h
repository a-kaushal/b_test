#pragma once
#include <vector>
#include <string>
#include <iostream>
#include "MemoryRead.h" // Ensures we have access to MemoryAnalyzer

ULONG_PTR LUA_ADDON_ENTRY = 0x0;

class LuaAnchor {
public:
    // Scans the target process for the specific string pattern.
    // Returns the address if found, or 0 if not found.
    static ULONG_PTR Find(MemoryAnalyzer& analyzer, DWORD pid, const std::string& magicString) {
        std::cout << "[LuaAnchor] Starting scan for pattern: " << magicString << std::endl;

        // 1. Get all memory regions from the kernel driver
        //    This is much faster than blindly scanning 0x0000 to 0x7FFF...
        auto regions = analyzer.EnumerateMemoryRegions(pid);

        if (regions.empty()) {
            std::cerr << "[LuaAnchor] Failed to enumerate regions or process is empty." << std::endl;
            return 0;
        }

        // 2. Convert the search string to a byte vector
        std::vector<uint8_t> pattern(magicString.begin(), magicString.end());

        // 3. Iterate over every memory region
        for (const auto& region : regions) {

            // --- OPTIMIZATION FILTERS ---

            // Lua variables are stored on the Heap, which is always MEM_COMMIT.
            if (region.State != MEM_COMMIT) continue;

            // Lua variables are Read/Write. They are never in Read-Only (Const) or Execute (Code) pages.
            // We check for PAGE_READWRITE or PAGE_WRITECOPY.
            if ((region.Protect & PAGE_READWRITE) == 0 && (region.Protect & PAGE_WRITECOPY) == 0) {
                continue;
            }

            // Optional: Lua memory is usually MEM_PRIVATE (Private Heap). 
            // However, some custom allocators might use MEM_MAPPED. 
            // Scanning only PRIVATE is faster, but scanning both is safer.
            if (region.Type == MEM_IMAGE) continue; // Skip DLLs/Exes

            // 4. Scan this specific region
            ULONG_PTR result = ScanRegion(analyzer, pid, region.BaseAddress, region.RegionSize, pattern);
            if (result != 0) {
                std::cout << "[LuaAnchor] Found pattern at: 0x" << std::hex << result << std::dec << std::endl;
                return result;
            }
        }

        std::cout << "[LuaAnchor] Pattern not found." << std::endl;
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
            std::cerr << "[Error] Failed to convert hex string: " << hexString << std::endl;
            return 0;
        }
    }

    static void ReadLuaData(MemoryAnalyzer& analyzer, DWORD pid, const std::string& magicString, ULONG_PTR& entryPtr) {
        std::string rawMemoryString;
        bool firstRead = false;
        if (entryPtr == 0) {
            // 2. Find the address
            firstRead = true;
            entryPtr = LuaAnchor::Find(analyzer, pid, magicString);
            g_LogFile << ">>> Lua Base Address: 0x" << std::hex << entryPtr << std::dec << std::endl;
        }

        if ((entryPtr != 0) && (analyzer.ReadString(pid, entryPtr, rawMemoryString, 60))) {
            // 3. Read the full string from memory
            // We read ~60 bytes to ensure we get the whole "##...##" block
            //g_LogFile << "[Found String] " << rawMemoryString << std::endl;

            // 4. Extract the actual pointer from the text
            uintptr_t tableAddress = LuaAnchor::ExtractAddressFromLuaString(rawMemoryString);

            if (tableAddress != 0) {
                //g_LogFile << ">>> Lua Table Address: 0x" << std::hex << tableAddress << std::dec << std::endl;

                // 5. Now you can read the array data!
                // Remember to find the OFFSET_ARRAY_PTR (usually 0x10 or 0x18) manually first.
                uintptr_t arrayPtr = 0;
                // Example offset 0x18 (Common in x64 Lua)
                if (analyzer.ReadPointer(pid, tableAddress + 0x20, arrayPtr)) {

                    // Read the first 3 values (Double precision)
                    double val1, val2, val3, val4, val5, val6, val7, val8, val9, val10, val11;
                    analyzer.ReadDouble(pid, arrayPtr + 0x00, val1);
                    analyzer.ReadDouble(pid, arrayPtr + 0x18, val2);
                    analyzer.ReadDouble(pid, arrayPtr + 0x30, val3);
                    analyzer.ReadDouble(pid, arrayPtr + 0x48, val4);
                    analyzer.ReadDouble(pid, arrayPtr + 0x60, val5);
                    analyzer.ReadDouble(pid, arrayPtr + 0x78, val6);
                    analyzer.ReadDouble(pid, arrayPtr + 0x90, val7);
                    analyzer.ReadDouble(pid, arrayPtr + 0xA8, val8);
                    analyzer.ReadDouble(pid, arrayPtr + 0xC0, val9);
                    analyzer.ReadDouble(pid, arrayPtr + 0xD8, val10);
                    analyzer.ReadDouble(pid, arrayPtr + 0xF0, val11);

                    ((val1 > 0.5) ? g_GameState->player.needRepair = true : g_GameState->player.needRepair = false);
                    ((val2 > 0.5) ? g_GameState->player.isIndoor = true : g_GameState->player.isIndoor = false);
                    ((val3 > 0.5) ? g_GameState->player.areaMountable = true : g_GameState->player.areaMountable = false);
                    (((val6 > 0.5) && (val4 > 0.5)) ? g_GameState->player.flyingMounted = true : g_GameState->player.flyingMounted = false);
                    (((val6 < 0.5) && (val4 > 0.5)) ? g_GameState->player.groundMounted = true : g_GameState->player.groundMounted = false);
                    ((val7 > 0.5) ? g_GameState->player.isGhost = true : g_GameState->player.isGhost = false);
                    g_GameState->player.corpsePositionX = float(val8);
                    g_GameState->player.corpsePositionY = float(val9);
                    ((val10 > 0.5) ? g_GameState->player.canRespawn = true : g_GameState->player.canRespawn = false);
                    ((val11 > 0.5) ? g_GameState->player.isDeadBody = true : g_GameState->player.isDeadBody = false);

                    if (firstRead == true) {
                        g_LogFile << "Data: [" << g_GameState->player.needRepair << ", " << g_GameState->player.isIndoor << ", " << g_GameState->player.areaMountable << ", " << val4 << ", " << val5 << ", " << val6 << "]" << std::endl;
                    }
                }
            }
        }
        else {
            entryPtr = 0;
            g_LogFile << "Anchor not found. Check if addon is loaded." << std::endl;
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
};