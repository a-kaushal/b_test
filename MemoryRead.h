#pragma once
#include <windows.h>
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

struct HashNode {
    ULONG_PTR GuidLow;
    ULONG_PTR GuidHigh;
    ULONG_PTR EntityIndex;
};

// IOCTL code - must match driver
#define IOCTL_READ_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x8B3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ENUMERATE_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x8B4, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_REGION_NAME CTL_CODE(FILE_DEVICE_UNKNOWN, 0x8B5, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define MAX_REGIONS_PER_CALL 5000  // Process in chunks

// Memory Offsets
inline ULONG_PTR baseAddress;

#define OBJECT_MANAGER_ENTRY_OFFSET 0x396CD50
#define OBJECT_MANAGER_FIRST_OBJECT_OFFSET 0x8

#define OBJECT_MANAGER_OFFSET 0x3DDE298
#define ENTITY_ARRAY_OFFSET 0x8
#define ENTITY_ARRAY_SIZE_OFFSET 0x10
#define HASH_ARRAY_MAXIMUM_OFFSET 0x40
#define HASH_ARRAY_OFFSET 0x48
#define HASH_ARRAY_SIZE_OFFSET 0x50


#define ENTITY_BUILDER_ARRAY_ITEM 0x8
#define ENTITY_ENTRY_OFFSET 0x28
#define ENTITY_OBJECT_TYPE_OFFSET 0xC
#define ENTITY_GUID_LOW_OFFSET 0x18
#define ENTITY_GUID_HIGH_OFFSET 0x20

#define ENTITY_POSITION_X_OFFSET 0x130
#define ENTITY_POSITION_Y_OFFSET 0x134
#define ENTITY_POSITION_Z_OFFSET 0x138
#define ENTITY_ROTATION_OFFSET 0x140
#define ENTITY_VERTICAL_ROTATION_OFFSET 0x144
#define ENTITY_ENEMY_IN_COMBAT_GUID_LOW 0x670 // GUID of enemy target (Has a value if enemy is performing a melee attack only. 0 if enemy is not close to player and doing a ranged attack)
#define ENTITY_ENEMY_IN_COMBAT_GUID_HIGH 0x678 // Attacking Enemy
#define ENTITY_ENEMY_RANGED_COMBAT_GUID_LOW 0x11A80 // GUID of ranged enemy attack target (Has a value if enemy is performing a ranged or melee attack)
#define ENTITY_ENEMY_RANGED_COMBAT_GUID_HIGH 0x11A88
#define ENTITY_ENEMY_ATTACKING 0x11AB0
#define ENTITY_LEVEL 0x11C20
#define ENTITY_ENEMY_HEALTH 0x119F8
#define ENTITY_ENEMY_MAX_HEALTH 0x11B38

#define OBJECT_POSITION_X_OFFSET 0xF0
#define OBJECT_POSITION_Y_OFFSET 0xF4
#define OBJECT_POSITION_Z_OFFSET 0xF8
#define OBJECT_COLLECTED_OFFSET 0xCC

#define ENTITY_PLAYER_STATE_OFFSET 0x2A8
//#define ENTITY_PLAYER_MOUNT_STATE 0xA40
#define ENTITY_PLAYER_MOUNT_STATE 0x8D0
#define ENTITY_PLAYER_HEALTH 0x119F8
#define ENTITY_PLAYER_MAX_HEALTH 0x11B38
#define ENTITY_PLAYER_LEVEL 0x11C20
#define ENTITY_PLAYER_EQUIPEMENT_OFFSET 0x14520 // Start of equipment array
#define ENTITY_PLAYER_BAG_OFFSET 0x14700 // Offset to equipped bags guid
#define ENTITY_PLAYER_BAG_GUID_OFFSET 0x14750 
#define ENTITY_PLAYER_MAIN_BAG_OFFSET 0x14750 // Offset to main bag items array
#define ENTITY_PLAYER_IN_COMBAT_GUID_LOW 0x670 // Attacking Enemy
#define ENTITY_PLAYER_IN_COMBAT_GUID_HIGH 0x678 // Attacking Enemy
#define ENTITY_PLAYER_TARGET_GUID_LOW 0x11BF0 // Under attack from enemy
#define ENTITY_PLAYER_TARGET_GUID_HIGH 0x11BF8 // Under attack from enemy


#define MOUSE_OVER_GUID_OFFSET 0x3F2D038
#define ZONE_TEXT 0x3F2C2A8
#define TARGET_GUID_OFFSET 0x3BD5928


#define CAMERA_MANAGER 0x3DEFB68
#define CAMERA_OFFSET 0x488
#define CAMERA_POSITION_X 0x10
#define CAMERA_POSITION_Y 0x14
#define CAMERA_POSITION_Z 0x18
#define CAMERA_PROJECTION_MATRIX_FORWARD_X 0x1C
#define CAMERA_PROJECTION_MATRIX_FORWARD_Y 0x20
#define CAMERA_PROJECTION_MATRIX_FORWARD_Z 0x24
#define CAMERA_PROJECTION_MATRIX_RIGHT_X 0x28
#define CAMERA_PROJECTION_MATRIX_RIGHT_Y 0x2C
#define CAMERA_PROJECTION_MATRIX_RIGHT_Z 0x30
#define CAMERA_PROJECTION_MATRIX_UP_X 0x34
#define CAMERA_PROJECTION_MATRIX_UP_Y 0x38
#define CAMERA_PROJECTION_MATRIX_UP_Z 0x3C


#define PLAYER_BAG_ARRAY 0x8  // Offset to the bag item array in the entity array
#define PLAYER_BAG_ARRAY_ITEM 0x50 // Offset between array entries
#define PLAYER_BAG_ITEM 0x28 // Entry to player bag structure
#define PLAYER_BAG_ITEM_LOW_GUID 0x18 // Low part of item GUID
#define PLAYER_BAG_ITEM_HIGH_GUID 0x20 // High part of item GUID


#define PLAYER_ITEM_TYPE 0xC // Item type in item structure
#define PLAYER_BAG_ITEM_ID 0xC8 // Item ID in item structure
#define PLAYER_ITEM_QUANTITY 0x1A8 // Quantity of item in item structure
#define PLAYER_BAG_LOW_GUID 0x178 // Low part of bag GUID in item structure
#define PLAYER_BAG_HIGH_GUID 0x180 // High part of bag GUID in item structure
#define PLAYER_BAG_SLOTS 0x478 // Offset from bag entry
#define PLAYER_BAG_EQUIPPED


#define LUA_ADDON_ARRAY_START = 0x0

// Helper function to get minimum of two values
template<typename T>
T MinValue(T a, T b) {
    return (a < b) ? a : b;
};

// Memory operation structure - must match driver
struct MemoryOperation {
    ULONG_PTR ProcessId;
    ULONG_PTR Address;
    PVOID Buffer;
    SIZE_T Size;
    SIZE_T BytesProcessed;
    ULONG OperationType;
};

class MemoryAnalyzer {
private:
    HANDLE driverHandle;
    std::string devicePath;

public:
    MemoryAnalyzer(const std::string& device = "\\\\.\\{8A9B7C6D-5E4F-3A2B-1C0D-9E8F7A6B5C4D}")
        : driverHandle(INVALID_HANDLE_VALUE), devicePath(device) {
    }

    ~MemoryAnalyzer() {
        Close();
    }

    struct MemoryRegionInfo {
        ULONG_PTR BaseAddress;
        SIZE_T RegionSize;
        ULONG State;
        ULONG Type;
        ULONG Protect;
    };

    struct MemoryEnumerateRequest {
        ULONG_PTR ProcessId;
        MemoryRegionInfo* Regions;
        ULONG MaxRegions;
        ULONG RegionCount;
    };

    // Connect to driver
    bool Connect() {
        driverHandle = CreateFileA(
            devicePath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (driverHandle == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to open driver. Error: " << GetLastError() << std::endl;
            std::cerr << "Make sure the driver is loaded and the device path is correct." << std::endl;
            return false;
        }

        std::cout << "Successfully connected to driver" << std::endl;
        return true;
    }

    // Helper to get the name of a memory region
    std::string GetRegionName(DWORD processId, ULONG_PTR address) {
        if (driverHandle == INVALID_HANDLE_VALUE) return "";

        char buffer[1024] = { 0 };
        MemoryOperation op = { 0 };
        op.ProcessId = processId;
        op.Address = address;

        // Copy the request into the buffer
        std::memcpy(buffer, &op, sizeof(op));

        DWORD bytesReturned = 0;
        BOOL result = DeviceIoControl(
            driverHandle,
            IOCTL_GET_REGION_NAME,
            buffer,
            sizeof(buffer),
            buffer,
            sizeof(buffer),
            &bytesReturned,
            nullptr
        );

        if (!result) {
            // ERROR 1 = ERROR_INVALID_FUNCTION (Means driver doesn't support this IOCTL)
            // ERROR 50 = ERROR_NOT_SUPPORTED
            std::cerr << " [IOCTL Error: " << GetLastError() << "] ";
            return "";
        }

        return std::string(buffer);
    }

    // Close driver handle
    void Close() {
        if (driverHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(driverHandle);
            driverHandle = INVALID_HANDLE_VALUE;
        }
    }

    // Read raw memory
    bool ReadMemory(DWORD processId, ULONG_PTR address, void* buffer, SIZE_T size, bool quiet = false) {
        if (driverHandle == INVALID_HANDLE_VALUE) {
            if (!quiet) std::cerr << "Driver not connected" << std::endl;
            return false;
        }

        MemoryOperation operation = { 0 };
        operation.ProcessId = processId;
        operation.Address = address;
        operation.Buffer = buffer;
        operation.Size = size;
        operation.OperationType = 0;

        DWORD bytesReturned = 0;
        BOOL result = DeviceIoControl(
            driverHandle,
            IOCTL_READ_MEMORY,
            &operation,
            sizeof(operation),
            &operation,
            sizeof(operation),
            &bytesReturned,
            nullptr
        );

        if (!result || operation.BytesProcessed != size) {
            // ONLY print error if NOT quiet
            if (!quiet) {
                g_LogFile << "DeviceIoControl failed. Error: " << GetLastError() << std::endl;
            }
            return false;
        }

        return operation.BytesProcessed > 0;
    }

    // Wrapper for ReadMemory to support bulk buffer reading
    bool ReadBuffer(DWORD processId, ULONG_PTR address, void* buffer, SIZE_T size) {
        // Just call the existing ReadMemory function which handles the DeviceIoControl
        return ReadMemory(processId, address, buffer, size);
    }

    // Update your template helper too
    template<typename T>
    bool Read(DWORD processId, ULONG_PTR address, T& value, bool quiet = false) {
        return ReadMemory(processId, address, &value, sizeof(T), quiet);
    }

    bool ReadInt16(DWORD processId, ULONG_PTR address, int16_t& value, bool quiet = false) {
        return Read(processId, address, value, quiet);
    }

    // Read byte
    bool ReadByte(DWORD processId, ULONG_PTR address, uint8_t& value) {
        return Read(processId, address, value);
    }

    // Read int32
    bool ReadInt32(DWORD processId, ULONG_PTR address, int32_t& value) {
        return Read(processId, address, value);
    }

    // Read uint32
    bool ReadUInt32(DWORD processId, ULONG_PTR address, uint32_t& value) {
        return Read(processId, address, value);
    }

    // Read int64
    bool ReadInt64(DWORD processId, ULONG_PTR address, int64_t& value) {
        return Read(processId, address, value);
    }

    // Read float
    bool ReadFloat(DWORD processId, ULONG_PTR address, float& value) {
        return Read(processId, address, value);
    }

    // Read double
    bool ReadDouble(DWORD processId, ULONG_PTR address, double& value) {
        return Read(processId, address, value);
    }

    // Read bool
    bool ReadBool(DWORD processId, ULONG_PTR address, bool& value) {
        return Read(processId, address, value);
    }

    // Read pointer (64-bit)
    bool ReadPointer(DWORD processId, ULONG_PTR address, ULONG_PTR& value) {
        return Read(processId, address, value);
    }

    // Read string (null-terminated)
    bool ReadString(DWORD processId, ULONG_PTR address, std::string& str, size_t maxLength = 256) {
        std::vector<char> buffer(maxLength);
        if (!ReadMemory(processId, address, buffer.data(), maxLength)) {
            return false;
        }

        // Find null terminator
        size_t len = 0;
        for (size_t i = 0; i < maxLength; i++) {
            if (buffer[i] == '\0') {
                len = i;
                break;
            }
        }

        str.assign(buffer.data(), len);
        return true;
    }

    // Read wide string (null-terminated UTF-16)
    bool ReadWideString(DWORD processId, ULONG_PTR address, std::wstring& str, size_t maxLength = 512) {
        std::vector<wchar_t> buffer(maxLength / sizeof(wchar_t));
        if (!ReadMemory(processId, address, buffer.data(), maxLength)) {
            return false;
        }

        // Find null terminator
        size_t len = 0;
        for (size_t i = 0; i < buffer.size(); i++) {
            if (buffer[i] == L'\0') {
                len = i;
                break;
            }
        }

        str.assign(buffer.data(), len);
        return true;
    }

    // Hex dump memory
    void HexDump(DWORD processId, ULONG_PTR address, SIZE_T size, size_t bytesPerLine = 16) {
        std::vector<uint8_t> buffer(size);
        if (!ReadMemory(processId, address, buffer.data(), size)) {
            std::cerr << "Failed to read memory for hex dump" << std::endl;
            return;
        }

        std::cout << "Memory dump at 0x" << std::hex << std::uppercase << address
            << " (" << std::dec << size << " bytes):" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        for (size_t i = 0; i < size; i += bytesPerLine) {
            // Address
            std::cout << std::hex << std::uppercase << std::setfill('0')
                << std::setw(16) << (address + i) << "  ";

            // Hex values
            for (size_t j = 0; j < bytesPerLine; j++) {
                if (i + j < size) {
                    std::cout << std::setw(2) << static_cast<int>(buffer[i + j]) << " ";
                }
                else {
                    std::cout << "   ";
                }
            }

            std::cout << " ";

            // ASCII representation
            for (size_t j = 0; j < bytesPerLine && i + j < size; j++) {
                uint8_t byte = buffer[i + j];
                if (byte >= 32 && byte < 127) {
                    std::cout << static_cast<char>(byte);
                }
                else {
                    std::cout << '.';
                }
            }
            Sleep(5);

            std::cout << std::endl;
        }
        std::cout << std::dec << std::endl;
    }

    // Search for byte pattern
    std::vector<ULONG_PTR> SearchPattern(DWORD processId, ULONG_PTR startAddress,
        ULONG_PTR endAddress, const std::vector<uint8_t>& pattern,
        size_t chunkSize = 4096) {
        std::vector<ULONG_PTR> matches;
        std::vector<uint8_t> buffer(chunkSize);

        std::cout << "Searching for pattern in range 0x" << std::hex << startAddress
            << " - 0x" << endAddress << std::dec << std::endl;

        for (ULONG_PTR addr = startAddress; addr < endAddress; addr += chunkSize) {
            size_t readSize = MinValue(chunkSize, static_cast<size_t>(endAddress - addr));

            if (!ReadMemory(processId, addr, buffer.data(), readSize)) {
                continue;
            }

            // Search for pattern in buffer
            for (size_t i = 0; i <= readSize - pattern.size(); i++) {
                bool found = true;
                for (size_t j = 0; j < pattern.size(); j++) {
                    if (buffer[i + j] != pattern[j]) {
                        found = false;
                        break;
                    }
                }
                if (found) {
                    matches.push_back(addr + i);
                }
            }
        }

        return matches;
    }

    std::vector<MemoryRegionInfo> EnumerateMemoryRegions(DWORD processId) {
        std::vector<MemoryRegionInfo> allRegions;

        // We need to allocate the buffer correctly for METHOD_BUFFERED
        // The input and output buffers are COPIED by the kernel
        const ULONG maxRegionsPerCall = MAX_REGIONS_PER_CALL;

        // Create a combined structure for input and output
        struct EnumRequest {
            ULONG_PTR ProcessId;
            ULONG MaxRegions;
            ULONG RegionCount;
            ULONG Reserved;  // Padding for alignment
            MemoryRegionInfo Regions[MAX_REGIONS_PER_CALL];
        };

        EnumRequest* request = new EnumRequest();
        request->ProcessId = processId;
        request->MaxRegions = maxRegionsPerCall;
        request->RegionCount = 0;

        DWORD bytesReturned = 0;
        BOOL result = DeviceIoControl(
            driverHandle,
            IOCTL_ENUMERATE_MEMORY,
            request,                    // Input buffer
            sizeof(EnumRequest),         // Input size
            request,                    // Output buffer (same)
            sizeof(EnumRequest),         // Output size
            &bytesReturned,
            nullptr
        );

        if (!result) {
            std::cerr << "Failed to enumerate memory. Error: " << GetLastError() << std::endl;
            delete request;
            return {};
        }

        // Copy results
        for (ULONG i = 0; i < request->RegionCount; i++) {
            allRegions.push_back(request->Regions[i]);
        }

        std::cout << "Enumerated " << request->RegionCount << " memory regions" << std::endl;

        delete request;
        return allRegions;
    }

    // Search for hex string pattern (e.g., "48 8B 05 ?? ?? ?? ??")
    std::vector<ULONG_PTR> SearchHexPattern(DWORD processId, ULONG_PTR startAddress,
        ULONG_PTR endAddress, const std::string& hexPattern) {
        std::vector<uint8_t> pattern;
        std::vector<bool> mask;

        // Parse hex pattern
        std::istringstream iss(hexPattern);
        std::string token;
        while (iss >> token) {
            if (token == "??" || token == "?") {
                pattern.push_back(0);
                mask.push_back(false); // Wildcard
            }
            else {
                pattern.push_back(static_cast<uint8_t>(std::stoi(token, nullptr, 16)));
                mask.push_back(true); // Exact match
            }
        }

        // Search with wildcards
        std::vector<ULONG_PTR> matches;
        std::vector<uint8_t> buffer(4096);

        for (ULONG_PTR addr = startAddress; addr < endAddress; addr += 4096) {
            size_t readSize = MinValue(static_cast<size_t>(4096), static_cast<size_t>(endAddress - addr));

            if (!ReadMemory(processId, addr, buffer.data(), readSize)) {
                continue;
            }

            for (size_t i = 0; i <= readSize - pattern.size(); i++) {
                bool found = true;
                for (size_t j = 0; j < pattern.size(); j++) {
                    if (mask[j] && buffer[i + j] != pattern[j]) {
                        found = false;
                        break;
                    }
                }
                if (found) {
                    matches.push_back(addr + i);
                }
            }
        }

        return matches;
    }

    // Find value in memory
    template<typename T>
    std::vector<ULONG_PTR> FindValue(DWORD processId, ULONG_PTR startAddress,
        ULONG_PTR endAddress, T value) {
        std::vector<uint8_t> pattern(sizeof(T));
        std::memcpy(pattern.data(), &value, sizeof(T));
        return SearchPattern(processId, startAddress, endAddress, pattern);
    }

    // Dump memory region to file
    bool DumpToFile(DWORD processId, ULONG_PTR address, SIZE_T size, const std::string& filename) {
        std::vector<uint8_t> buffer(size);
        if (!ReadMemory(processId, address, buffer.data(), size)) {
            std::cerr << "Failed to read memory for dump" << std::endl;
            return false;
        }

        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return false;
        }

        file.write(reinterpret_cast<const char*>(buffer.data()), size);
        file.close();

        std::cout << "Dumped " << size << " bytes to " << filename << std::endl;
        return true;
    }

    // Follow pointer chain
    void AnalyzePointerChain(DWORD processId, ULONG_PTR address, int depth = 3) {
        std::cout << "Analyzing pointer chain from 0x" << std::hex << address << std::dec << std::endl;

        ULONG_PTR current = address;
        for (int i = 0; i < depth; i++) {
            ULONG_PTR value;
            if (!ReadPointer(processId, current, value)) {
                std::cout << "  Level " << i << ": Read failed at 0x"
                    << std::hex << current << std::dec << std::endl;
                break;
            }

            std::cout << "  Level " << i << ": 0x" << std::hex << current
                << " -> 0x" << value << std::dec << std::endl;

            // Check if pointer looks valid
            if (value < 0x10000 || value > 0x7FFFFFFFFFFF) {
                std::cout << "    (Invalid pointer, stopping)" << std::endl;
                break;
            }

            current = value;
        }
    }

    // Monitor value changes
    template<typename T>
    void MonitorValue(DWORD processId, ULONG_PTR address, int intervalMs = 1000) {
        std::cout << "Monitoring 0x" << std::hex << address << std::dec
            << " (Ctrl+C to stop)" << std::endl;
        std::cout << std::string(60, '-') << std::endl;

        T lastValue;
        bool hasLastValue = false;

        while (true) {
            T currentValue;
            if (!Read(processId, address, currentValue)) {
                std::cerr << "Failed to read value" << std::endl;
                break;
            }

            if (!hasLastValue || currentValue != lastValue) {
                SYSTEMTIME st;
                GetLocalTime(&st);

                std::cout << "[" << std::setfill('0') << std::setw(2) << st.wHour << ":]"
                    << std::setw(2) << st.wMinute << ":" << std::setw(2) << st.wSecond << "] ";

                if (hasLastValue) {
                    std::cout << "Value changed: " << lastValue << " -> " << currentValue << std::endl;
                }
                else {
                    std::cout << "Initial value: " << currentValue << std::endl;
                }

                lastValue = currentValue;
                hasLastValue = true;
            }

            Sleep(intervalMs);
        }
    }

    // Read structure (custom size)
    bool ReadStructure(DWORD processId, ULONG_PTR address, void* structure, size_t size) {
        return ReadMemory(processId, address, structure, size);
    }

    // Configuration for memory scanning heuristics
    struct ScanConfig {
        size_t initialProbeSize;      // Size of initial test read (default: 8 bytes)
        size_t chunkSize;              // Size to read when memory found (default: 4KB)
        size_t skipOnFailure;          // How far to skip when read fails (default: 64KB)
        size_t skipOnGap;              // How far to skip after finding gap (default: 1MB)
        size_t maxGapSize;             // Max gap before declaring large skip (default: 256KB)
        bool useCommonRanges;          // Only scan known common ranges (default: true)
        bool expandRegions;            // Try to expand when region found (default: true)

        ScanConfig()
            : initialProbeSize(8)
            , chunkSize(4096)
            , skipOnFailure(65536)      // 64KB
            , skipOnGap(1048576)         // 1MB
            , maxGapSize(262144)         // 256KB
            , useCommonRanges(true)
            , expandRegions(true)
        {
        }
    };

    // Dump all accessible memory regions for a process with heuristics
    bool DumpProcessMemory(DWORD processId, const std::string& outputPath, const ScanConfig& config = ScanConfig()) {
        std::cout << "Enumerating memory regions via kernel driver..." << std::endl;

        auto regions = EnumerateMemoryRegions(processId);

        if (regions.empty()) {
            std::cerr << "Failed to enumerate regions" << std::endl;
            return false;
        }

        std::cout << "Found " << regions.size() << " committed memory regions" << std::endl;

        std::ofstream dumpFile(outputPath, std::ios::binary);
        std::ofstream mapFile(outputPath + ".map", std::ios::out);

        if (!dumpFile || !mapFile) {
            std::cerr << "Failed to create output files" << std::endl;
            return false;
        }

        mapFile << "Virtual Address,Size,File Offset,Type,Protect" << std::endl;

        size_t totalDumped = 0;
        ULONG_PTR fileOffset = 0;
        size_t dumpedRegions = 0;

        for (const auto& region : regions) {
            // Filter based on config
            bool shouldDump = true;

            // Skip guard pages and inaccessible memory
            if (region.Protect & PAGE_GUARD || region.Protect & PAGE_NOACCESS) {
                shouldDump = false;
            }

            if (!shouldDump) continue;

            // Read this region
            std::vector<uint8_t> buffer(region.RegionSize);
            if (ReadMemory(processId, region.BaseAddress, buffer.data(), region.RegionSize)) {
                dumpFile.write(reinterpret_cast<const char*>(buffer.data()), region.RegionSize);

                // Write map entry
                mapFile << "0x" << std::hex << region.BaseAddress << ","
                    << std::dec << region.RegionSize << ","
                    << "0x" << std::hex << fileOffset << ",";

                // Type
                if (region.Type == MEM_IMAGE) mapFile << "IMAGE,";
                else if (region.Type == MEM_MAPPED) mapFile << "MAPPED,";
                else if (region.Type == MEM_PRIVATE) mapFile << "PRIVATE,";
                else mapFile << "UNKNOWN,";

                // Protection
                mapFile << "0x" << std::hex << region.Protect << std::dec << std::endl;

                totalDumped += region.RegionSize;
                fileOffset += region.RegionSize;
                dumpedRegions++;

                std::cout << "Region: 0x" << std::hex << region.BaseAddress
                    << " - 0x" << (region.BaseAddress + region.RegionSize)
                    << " (" << std::dec << (region.RegionSize / 1024) << " KB)" << std::endl;
            }
        }

        dumpFile.close();
        mapFile.close();

        std::cout << "\n[+] Filtered dump complete. Regions dumped: " << dumpedRegions << std::endl;
        std::cout << "[+] Total bytes: " << totalDumped << " (" << (totalDumped / 1024.0 / 1024.0) << " MB)" << std::endl;

        return true;
    }

    // Dump specific memory regions (by type)
    // Added: implement filtering in user-space using enumeration from the driver.
    // includeImage/includeMapped/includePrivate control which MEM_* types are written.
    bool DumpMemoryRegions(DWORD processId, const std::string& outputPath,
        bool includeImage = true,
        bool includeMapped = true,
        bool includePrivate = true) {
        std::cout << "Enumerating memory regions via kernel driver..." << std::endl;

        auto regions = EnumerateMemoryRegions(processId);
        if (regions.empty()) {
            std::cerr << "Failed to enumerate regions" << std::endl;
            return false;
        }

        std::ofstream dumpFile(outputPath, std::ios::binary);
        std::ofstream mapFile(outputPath + ".map", std::ios::out);

        if (!dumpFile || !mapFile) {
            std::cerr << "Failed to create output files" << std::endl;
            return false;
        }

        mapFile << "Virtual Address,Size,File Offset,Type,Protect" << std::endl;

        size_t totalDumped = 0;
        ULONG_PTR fileOffset = 0;
        size_t dumpedRegions = 0;

        for (const auto& region : regions) {
            // Skip guard pages and inaccessible memory
            if (region.Protect & PAGE_GUARD || region.Protect & PAGE_NOACCESS) {
                continue;
            }

            bool typeSelected = false;
            if (region.Type == MEM_IMAGE && includeImage) typeSelected = true;
            if (region.Type == MEM_MAPPED && includeMapped) typeSelected = true;
            if (region.Type == MEM_PRIVATE && includePrivate) typeSelected = true;

            if (!typeSelected) continue;

            // Read region
            std::vector<uint8_t> buffer(region.RegionSize);
            if (!ReadMemory(processId, region.BaseAddress, buffer.data(), region.RegionSize)) {
                std::cerr << "Warning: Failed to read region at 0x" << std::hex << region.BaseAddress << std::dec << std::endl;
                continue;
            }

            dumpFile.write(reinterpret_cast<const char*>(buffer.data()), region.RegionSize);

            // Map entry
            mapFile << "0x" << std::hex << region.BaseAddress << ","
                << std::dec << region.RegionSize << ","
                << "0x" << std::hex << fileOffset << ",";

            if (region.Type == MEM_IMAGE) mapFile << "IMAGE,";
            else if (region.Type == MEM_MAPPED) mapFile << "MAPPED,";
            else if (region.Type == MEM_PRIVATE) mapFile << "PRIVATE,";
            else mapFile << "UNKNOWN,";

            mapFile << "0x" << std::hex << region.Protect << std::dec << std::endl;

            totalDumped += region.RegionSize;
            fileOffset += region.RegionSize;
            dumpedRegions++;

            std::cout << "Dumped Region: 0x" << std::hex << region.BaseAddress
                << " - 0x" << (region.BaseAddress + region.RegionSize)
                << " (" << std::dec << (region.RegionSize / 1024) << " KB) Type:"
                << (region.Type == MEM_PRIVATE ? "PRIVATE" : region.Type == MEM_MAPPED ? "MAPPED" : region.Type == MEM_IMAGE ? "IMAGE" : "UNKNOWN")
                << std::endl;
        }

        dumpFile.close();
        mapFile.close();

        std::cout << "\n[+] Dumped " << dumpedRegions << " regions" << std::endl;
        std::cout << "[+] Total: " << totalDumped << " bytes (" << (totalDumped / 1024.0 / 1024.0) << " MB)" << std::endl;

        return true;
    }
};