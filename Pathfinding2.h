#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourCommon.h"
#include "Vector.h"

#define MMAP_MAGIC 0x4d4d4150 // 'MMAP' in Little Endian

// EXACT MATCH for SkyFire MapBuilder.cpp Header (20 bytes)
#pragma pack(push, 1)
struct MmapTileHeader {
    uint32_t mmapMagic;
    uint32_t dtVersion;
    float mmapVersion;   // Must be float (5.2f)
    uint32_t size;       // Size of navData
    uint32_t usesLiquids; // Bitfield padding typically results in 4 bytes
};
#pragma pack(pop)

class NavMesh {
public:
    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;
    int currentMapId = -1;

    NavMesh() { query = dtAllocNavMeshQuery(); }
    ~NavMesh() { Clear(); if (query) dtFreeNavMeshQuery(query); }

    void Log(const std::string& msg) {
        std::ofstream log("C:\\Driver\\SMM_Debug.log", std::ios::app);
        log << "[NAVMESH] " << msg << std::endl;
    }

    void Clear() {
        if (mesh) dtFreeNavMesh(mesh);
        mesh = nullptr;
        currentMapId = -1;
    }

    bool LoadMap(const std::string& directory, int mapId) {
        if (currentMapId == mapId && mesh) return true;
        Clear();

        std::stringstream ss;
        ss << std::setw(4) << std::setfill('0') << mapId;
        std::string prefix = ss.str();

        Log("Loading Map " + std::to_string(mapId));

        // 1. SYNC ORIGIN: Read .mmap metadata
        std::string mmapPath = directory + prefix + ".mmap";
        std::ifstream mmapFile(mmapPath, std::ios::binary);
        dtNavMeshParams params;

        if (mmapFile.is_open()) {
            mmapFile.read((char*)&params, sizeof(dtNavMeshParams));
            mmapFile.close();
            Log("Loaded .mmap. Origin: " + std::to_string(params.orig[0]) + ", " +
                std::to_string(params.orig[1]) + ", " + std::to_string(params.orig[2]));
        }
        else {
            Log("WARNING: .mmap NOT FOUND at " + mmapPath + ". Using defaults.");
            memset(&params, 0, sizeof(params));
            params.orig[0] = -17066.666f;
            params.orig[1] = -1000.0f;
            params.orig[2] = -17066.666f;
            params.tileWidth = 533.33333f;
            params.tileHeight = 533.33333f;
            params.maxTiles = 4096;
            params.maxPolys = 2048;
        }

        mesh = dtAllocNavMesh();
        if (dtStatusFailed(mesh->init(&params))) {
            Log("mesh->init failed.");
            return false;
        }

        int found = 0, success = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            std::string filename = entry.path().filename().string();
            if (filename.find(prefix) == 0 && entry.path().extension() == ".mmtile") {
                found++;
                if (AddTile(entry.path().string())) success++;
            }
        }

        Log("Load complete. Tiles: " + std::to_string(found) + " (Success: " + std::to_string(success) + ")");

        if (mesh) query->init(mesh, 2048);
        currentMapId = mapId;
        return success > 0;
    }

    bool AddTile(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) return false;

        MmapTileHeader header;
        file.read((char*)&header, sizeof(MmapTileHeader));

        if (header.mmapMagic != MMAP_MAGIC) {
            Log("Magic Mismatch in " + filepath + " (Got " + std::to_string(header.mmapMagic) + ")");
            return false;
        }

        unsigned char* data = (unsigned char*)dtAlloc(header.size, DT_ALLOC_PERM);
        file.read((char*)data, header.size);
        file.close();

        dtStatus status = mesh->addTile(data, header.size, DT_TILE_FREE_DATA, 0, nullptr);
        if (dtStatusFailed(status)) {
            Log("addTile failed for " + filepath);
            dtFree(data);
            return false;
        }
        return true;
    }
};

static NavMesh globalNavMesh;

inline std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end, bool flying, int mapId) {
    if (!globalNavMesh.query || !globalNavMesh.mesh) return {};

    // --- FIX 1: THE MASTER FILTER ---
    dtQueryFilter filter;
    filter.setIncludeFlags(0xFFFF); // Include ALL bits (Ground, Water, Mobs, etc.)
    filter.setExcludeFlags(0x0000); // Exclude nothing

    dtPolyRef startRef, endRef;
    float startPt[3], endPt[3];

    // Mapping: WoW Y -> Detour X, WoW Z -> Detour Y, WoW X -> Detour Z
    float detourStart[3] = { start.y, start.z, start.x };
    float detourEnd[3] = { end.y, end.z, end.x };

    // Use a large vertical extent (100.0f) to find the floor
    float extent[3] = { 10.0f, 100.0f, 10.0f };

    dtStatus s1 = globalNavMesh.query->findNearestPoly(detourStart, extent, &filter, &startRef, startPt);
    dtStatus s2 = globalNavMesh.query->findNearestPoly(detourEnd, extent, &filter, &endRef, endPt);

    if (!startRef || !endRef) {
        // DIAGNOSTIC: If this still fails, the BVTree in the tile is likely corrupted
        // because the binary header skip was incorrect.
        return {};
    }

    // ... Standard findPath and findStraightPath ...
    dtPolyRef pathPolys[256];
    int pathCount = 0;
    globalNavMesh.query->findPath(startRef, endRef, startPt, endPt, &filter, pathPolys, &pathCount, 256);

    float straightPath[256 * 3];
    int straightCount = 0;
    globalNavMesh.query->findStraightPath(startPt, endPt, pathPolys, pathCount, straightPath, nullptr, nullptr, &straightCount, 256);

    std::vector<Vector3> result;
    for (int i = 0; i < straightCount; ++i) {
        // Swap back to WoW: X=Detour.Z, Y=Detour.X, Z=Detour.Y
        result.push_back(Vector3(straightPath[i * 3 + 2], straightPath[i * 3], straightPath[i * 3 + 1]));
    }
    return result;
}