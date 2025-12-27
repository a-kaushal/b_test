#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <sstream>

// DETOUR INCLUDES
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourCommon.h"

#include "Vector.h"

// --- CONFIGURATION ---
const float PATH_STEP_SIZE = 4.0f;
const float FLY_ALTITUDE = 30.0f;
const float AGENT_RADIUS = 0.6f;
const float TILE_SIZE = 533.33333f;
const int MAX_POLYS = 256;

#pragma pack(push, 1)
struct MmapTileHeader {
    uint32_t mmapMagic;
    uint32_t dtVersion;
    float mmapVersion;
    uint32_t size;
    char usesLiquids;
    char padding[3];
};
#pragma pack(pop)

// Helper function to round up to next power of 2
inline int nextPowerOfTwo(int n) {
    if (n <= 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

class NavMesh {
public:
    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;
    int currentMapId = -1;

    NavMesh() {
        query = dtAllocNavMeshQuery();
    }

    ~NavMesh() {
        dtFreeNavMesh(mesh);
        dtFreeNavMeshQuery(query);
    }

    void Clear() {
        dtFreeNavMesh(mesh);
        mesh = nullptr;
        dtFreeNavMeshQuery(query);
        query = nullptr;
        currentMapId = -1;
    }

    bool LoadMap(const std::string& directory, int mapId) {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        if (currentMapId == mapId && mesh && query) return true;
        Clear();

        query = dtAllocNavMeshQuery();
        if (!query) {
            logFile << "[ERROR] Failed to allocate query" << std::endl;
            return false;
        }

        std::stringstream ss;
        ss << std::setw(4) << std::setfill('0') << mapId;
        std::string prefix = ss.str();

        std::string mmapPath = directory + prefix + ".mmap";
        dtNavMeshParams params;
        bool paramsValid = false;

        // Try to read .mmap file
        std::ifstream mmapFile(mmapPath, std::ios::binary);
        if (mmapFile.is_open()) {
            // Get file size
            mmapFile.seekg(0, std::ios::end);
            size_t fileSize = mmapFile.tellg();
            mmapFile.seekg(0, std::ios::beg);

            logFile << "[MMAP] File size: " << fileSize << " bytes" << std::endl;
            logFile << "[MMAP] Expected: " << sizeof(dtNavMeshParams) << " bytes" << std::endl;

            if (fileSize >= sizeof(dtNavMeshParams)) {
                mmapFile.read((char*)&params, sizeof(dtNavMeshParams));

                logFile << "[MMAP] Read params (raw):" << std::endl;
                logFile << "  orig: [" << params.orig[0] << ", " << params.orig[1] << ", " << params.orig[2] << "]" << std::endl;
                logFile << "  tileWidth: " << params.tileWidth << std::endl;
                logFile << "  tileHeight: " << params.tileHeight << std::endl;
                logFile << "  maxTiles: " << params.maxTiles << std::endl;
                logFile << "  maxPolys: " << params.maxPolys << " (as int: " << (int)params.maxPolys << ")" << std::endl;

                // Don't modify the values from the file - they're already correct
                // Just validate they're reasonable

                // Validate parameters
                bool validOrig = !std::isnan(params.orig[0]) && !std::isnan(params.orig[1]) && !std::isnan(params.orig[2]) &&
                    !std::isinf(params.orig[0]) && !std::isinf(params.orig[1]) && !std::isinf(params.orig[2]) &&
                    params.orig[0] > -50000 && params.orig[0] < 50000 &&
                    params.orig[1] > -5000 && params.orig[1] < 5000 &&
                    params.orig[2] > -50000 && params.orig[2] < 50000;

                bool validTiles = params.tileWidth > 0 && params.tileWidth < 1000 &&
                    params.tileHeight > 0 && params.tileHeight < 1000;

                bool validCounts = params.maxTiles > 0 && params.maxTiles <= 8192 &&
                    params.maxPolys > 0 && params.maxPolys <= (1U << 20);

                paramsValid = validOrig && validTiles && validCounts;

                if (!paramsValid) {
                    logFile << "[MMAP] Validation FAILED - parameters are invalid" << std::endl;
                }
            }
            else {
                logFile << "[MMAP] File too small" << std::endl;
            }

            mmapFile.close();
        }

        // If .mmap file is invalid or doesn't exist, use calculated fallback
        if (!paramsValid) {
            logFile << "[MMAP] Using fallback parameters" << std::endl;

            // IMPORTANT: We need to determine the correct origin BEFORE initializing the mesh
            // Temporarily load ONE tile to get its coordinates
            std::string firstTilePath;
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (entry.path().filename().string().find(prefix) == 0 &&
                    entry.path().extension() == ".mmtile") {
                    firstTilePath = entry.path().string();
                    break;
                }
            }

            if (!firstTilePath.empty()) {
                // Read first tile to determine origin
                std::ifstream tileFile(firstTilePath, std::ios::binary);
                if (tileFile.is_open()) {
                    MmapTileHeader tileHeader;
                    tileFile.read((char*)&tileHeader, sizeof(MmapTileHeader));

                    unsigned char* tileData = (unsigned char*)dtAlloc(tileHeader.size, DT_ALLOC_PERM);
                    tileFile.read((char*)tileData, tileHeader.size);
                    tileFile.close();

                    // Parse tile header to get coordinates
                    dtMeshHeader* meshHeader = (dtMeshHeader*)tileData;

                    // Calculate origin from tile coordinates
                    float calcOrig[3];
                    calcOrig[0] = meshHeader->bmin[0] - (meshHeader->x * 533.33333f);
                    calcOrig[1] = meshHeader->bmin[1];
                    calcOrig[2] = meshHeader->bmin[2] - (meshHeader->y * 533.33333f);

                    logFile << "[MMAP] Calculated origin from first tile: ["
                        << calcOrig[0] << ", " << calcOrig[1] << ", " << calcOrig[2] << "]" << std::endl;

                    dtFree(tileData);

                    memset(&params, 0, sizeof(params));
                    params.orig[0] = calcOrig[0];
                    params.orig[1] = calcOrig[1];
                    params.orig[2] = calcOrig[2];
                    params.tileWidth = 533.33333f;
                    params.tileHeight = 533.33333f;
                    params.maxTiles = 1 << 21;      // 2^21
                    params.maxPolys = 1U << 31;     // 2^31
                }
                else {
                    // Couldn't read tile, use defaults
                    memset(&params, 0, sizeof(params));
                    params.orig[0] = -17600.0f;
                    params.orig[1] = -1000.0f;
                    params.orig[2] = -17600.0f;
                    params.tileWidth = 533.33333f;
                    params.tileHeight = 533.33333f;
                    params.maxTiles = 1 << 21;
                    params.maxPolys = 1U << 31;
                }
            }
            else {
                // No tiles found, use defaults
                memset(&params, 0, sizeof(params));
                params.orig[0] = -17600.0f;
                params.orig[1] = -1000.0f;
                params.orig[2] = -17600.0f;
                params.tileWidth = 533.33333f;
                params.tileHeight = 533.33333f;
                params.maxTiles = 1 << 21;
                params.maxPolys = 1U << 31;
            }
        }

        // Validate parameters before init
        logFile << "[VALIDATION] Checking parameter validity..." << std::endl;

        // Check for NaN/Inf in origin
        bool validOrigin = !std::isnan(params.orig[0]) && !std::isinf(params.orig[0]) &&
            !std::isnan(params.orig[1]) && !std::isinf(params.orig[1]) &&
            !std::isnan(params.orig[2]) && !std::isinf(params.orig[2]);
        logFile << "  Origin valid: " << (validOrigin ? "YES" : "NO") << std::endl;

        // Check tile dimensions
        bool validDims = params.tileWidth > 0 && !std::isnan(params.tileWidth) && !std::isinf(params.tileWidth) &&
            params.tileHeight > 0 && !std::isnan(params.tileHeight) && !std::isinf(params.tileHeight);
        logFile << "  Tile dimensions valid: " << (validDims ? "YES" : "NO") << std::endl;

        // Check power of 2 - use unsigned comparison
        bool isPow2Tiles = (params.maxTiles & (params.maxTiles - 1)) == 0 && params.maxTiles > 0;
        bool isPow2Polys = (params.maxPolys & (params.maxPolys - 1)) == 0 && params.maxPolys > 0;
        logFile << "  maxTiles power of 2: " << (isPow2Tiles ? "YES" : "NO") << std::endl;
        logFile << "  maxPolys power of 2: " << (isPow2Polys ? "YES" : "NO") << std::endl;

        // Initialize mesh with params
        mesh = dtAllocNavMesh();
        logFile << "[MESH] Attempting init with params:" << std::endl;
        logFile << "  orig: [" << params.orig[0] << ", " << params.orig[1] << ", " << params.orig[2] << "]" << std::endl;
        logFile << "  tileWidth: " << params.tileWidth << ", tileHeight: " << params.tileHeight << std::endl;
        logFile << "  tiles: " << params.maxTiles << ", polys: " << params.maxPolys << std::endl;

        dtStatus initStatus = mesh->init(&params);
        if (dtStatusFailed(initStatus)) {
            logFile << "[ERROR] mesh->init FAILED with status: " << std::hex << initStatus << std::dec << std::endl;

            // The .mmap file may be from an incompatible extractor version
            logFile << "[WORKAROUND] Ignoring .mmap file, using calculated defaults" << std::endl;

            // Use defaults that match your actual Detour configuration
            // DT_TILE_BITS=21 allows up to 2^21 = 2,097,152 tiles
            // DT_POLY_BITS=31 requires 2^31 = 2,147,483,648 polys
            memset(&params, 0, sizeof(params));
            params.orig[0] = (32.0f - 64.0f) * 533.33333f - 533.33333f;  // -17599.999
            params.orig[1] = -1000.0f;
            params.orig[2] = (32.0f - 64.0f) * 533.33333f - 533.33333f;  // -17599.999
            params.tileWidth = 533.33333f;
            params.tileHeight = 533.33333f;
            params.maxTiles = 1 << 21;     // 2^21 = 2,097,152 (matches DT_TILE_BITS=21)
            params.maxPolys = 1U << 31;    // 2^31 = 2,147,483,648 (matches DT_POLY_BITS=31, note: unsigned)

            logFile << "[RETRY] Using defaults matching DT_TILE_BITS=21, DT_POLY_BITS=31:" << std::endl;
            logFile << "  maxTiles=" << params.maxTiles << ", maxPolys=" << params.maxPolys << std::endl;

            dtFreeNavMesh(mesh);
            mesh = dtAllocNavMesh();
            initStatus = mesh->init(&params);

            if (dtStatusFailed(initStatus)) {
                logFile << "[ERROR] Even defaults failed! Status: " << std::hex << initStatus << std::dec << std::endl;

                if (initStatus & DT_OUT_OF_MEMORY)
                    logFile << "[ERROR] OUT_OF_MEMORY - Reduce maxTiles or maxPolys" << std::endl;
                if (initStatus & DT_INVALID_PARAM)
                    logFile << "[ERROR] INVALID_PARAM - Bit configuration mismatch" << std::endl;

                dtFreeNavMesh(mesh);
                mesh = nullptr;
                return false;
            }
        }

        logFile << "[MESH] Init succeeded" << std::endl;

        // Load tiles...
        int tilesLoaded = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().filename().string().find(prefix) == 0 &&
                entry.path().extension() == ".mmtile") {
                if (AddTile(entry.path().string())) {
                    tilesLoaded++;
                }
            }
        }

        logFile << "[INFO] Loaded " << tilesLoaded << " tiles" << std::endl;

        // Initialize query
        dtStatus status = query->init(mesh, 2048);
        logFile << "[DEBUG] Query init status: " << (dtStatusSucceed(status) ? "SUCCESS" : "FAILED")
            << " (code: " << status << ")" << std::endl;

        if (dtStatusFailed(status)) {
            logFile << "[ERROR] Query init failed" << std::endl;
            return false;
        }

        currentMapId = mapId;
        return true;
    }

    bool AddTile(const std::string& filepath) {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) return false;

        MmapTileHeader header;
        file.read((char*)&header, sizeof(MmapTileHeader));

        // Log version info from first tile only
        static bool versionLogged = false;
        if (!versionLogged) {
            logFile << "[TILE VERSION CHECK]" << std::endl;
            logFile << "  mmapMagic: 0x" << std::hex << header.mmapMagic << std::dec << std::endl;
            logFile << "  dtVersion: " << header.dtVersion << std::endl;
            logFile << "  mmapVersion: " << header.mmapVersion << std::endl;
            logFile << "  Current DT_NAVMESH_VERSION: " << DT_NAVMESH_VERSION << std::endl;

            if (header.dtVersion != DT_NAVMESH_VERSION) {
                logFile << "  [WARNING] VERSION MISMATCH! Tiles generated with different Detour version!" << std::endl;
                logFile << "  Expected: " << DT_NAVMESH_VERSION << " Got: " << header.dtVersion << std::endl;
            }
            versionLogged = true;
        }

        unsigned char* data = (unsigned char*)dtAlloc(header.size, DT_ALLOC_PERM);
        file.read((char*)data, header.size);

        // CRITICAL: Check what's actually in the tile data BEFORE adding
        dtMeshHeader* meshHeader = (dtMeshHeader*)data;
        static int diagCount = 0;
        if (diagCount < 5) {
            logFile << "[TILE DIAG] File: " << filepath << std::endl;
            logFile << "  Tile header x=" << meshHeader->x << ", y=" << meshHeader->y << std::endl;
            logFile << "  Tile bmin: [" << meshHeader->bmin[0] << ", " << meshHeader->bmin[1] << ", " << meshHeader->bmin[2] << "]" << std::endl;
            logFile << "  Tile bmax: [" << meshHeader->bmax[0] << ", " << meshHeader->bmax[1] << ", " << meshHeader->bmax[2] << "]" << std::endl;
            logFile << "  polyCount: " << meshHeader->polyCount << std::endl;

            // Calculate what the tile index SHOULD be
            const dtNavMeshParams* params = mesh->getParams();
            float tileCenterX = (meshHeader->bmin[0] + meshHeader->bmax[0]) / 2.0f;
            float tileCenterZ = (meshHeader->bmin[2] + meshHeader->bmax[2]) / 2.0f;
            int expectedTileX = (int)floorf((tileCenterX - params->orig[0]) / params->tileWidth);
            int expectedTileY = (int)floorf((tileCenterZ - params->orig[2]) / params->tileHeight);

            logFile << "  Tile center: [" << tileCenterX << ", " << tileCenterZ << "]" << std::endl;
            logFile << "  Expected tileX from position: " << expectedTileX << std::endl;
            logFile << "  Expected tileY from position: " << expectedTileY << std::endl;
            logFile << "  Actual header x,y: " << meshHeader->x << "," << meshHeader->y << std::endl;

            if (meshHeader->x != expectedTileX || meshHeader->y != expectedTileY) {
                logFile << "  [ERROR] TILE COORDINATE MISMATCH!" << std::endl;
            }

            diagCount++;
        }

        // CRITICAL FIX: WoW uses a 64x64 tile grid, not 2048x2048!
        // The tile coordinates in the files are in WoW's grid system
        int tx = meshHeader->x;
        int ty = meshHeader->y;
        int layer = 0;

        // WoW maps use 64x64 tiles (from -32 to +31 in the WoW coordinate system)
        // So tileLutSize should be 64, not 2048
        const int tileLutSize = 64;

        int tileIndex = tx + ty * tileLutSize;

        if (diagCount < 5) {
            logFile << "[TILE INDEX CALC] tileLutSize=" << tileLutSize
                << ", tileIndex=" << tileIndex << " (0x" << std::hex << tileIndex << std::dec << ")" << std::endl;
        }

        // Try to remove any existing tile at this index first
        const dtMeshTile* existingTile = mesh->getTileAt(tx, ty, layer);
        if (existingTile && existingTile->header) {
            dtTileRef existingRef = mesh->getTileRef(existingTile);
            logFile << "[TILE] Removing existing tile at (" << tx << "," << ty << "), ref=" << existingRef << std::endl;
            mesh->removeTile(existingRef, nullptr, nullptr);
        }

        // Create a tileRef hint: shift tileIndex by DT_POLY_BITS (which is 20)
        // tileRef format: [12 bits salt][12 bits tile][20 bits poly]
        dtTileRef tileRefHint = ((dtTileRef)tileIndex) << 20;  // Shift by DT_POLY_BITS=20

        dtTileRef tileRef = 0;
        dtStatus status = mesh->addTile(data, header.size, DT_TILE_FREE_DATA, tileRefHint, &tileRef);
        if (dtStatusFailed(status)) {
            logFile << "[ERROR] addTile failed for " << filepath << " status: " << std::hex << status << std::dec << std::endl;
            logFile << "[ERROR] Tile coords: x=" << tx << ", y=" << ty << ", tileIndex=" << tileIndex << std::endl;
            dtFree(data);
            return false;
        }

        // Decode the tileRef to see where it was placed
        static int addCount = 0;
        if (addCount < 5) { // Only log first 5 tiles
            unsigned int salt, tileIdx, polyIdx;
            mesh->decodePolyId(tileRef, salt, tileIdx, polyIdx);  // Use references, not pointers
            logFile << "[TILE] Added " << filepath << " -> tileRef=" << tileRef
                << " (tileIdx=" << tileIdx << ", salt=" << salt << ")" << std::endl;
            addCount++;
        }

        return true;
    }

    float GetTerrainHeight(const Vector3& pos) {
        dtPolyRef polyRef;
        float nearest[3];
        float extent[3] = { 2.0f, 100.0f, 2.0f };
        dtQueryFilter filter;

        if (dtStatusSucceed(query->findNearestPoly(&pos.x, extent, &filter, &polyRef, nearest))) {
            return nearest[1];
        }
        return -99999.0f;
    }

    // Test function to verify query is working
    bool TestQuery() {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        logFile << "[TEST] Testing query object..." << std::endl;

        if (!mesh || !query) {
            logFile << "[TEST] FAIL - mesh or query is NULL" << std::endl;
            return false;
        }

        // Get first valid tile by scanning ALL slots
        const dtNavMesh* nav = mesh;
        const dtMeshTile* testTile = nullptr;
        int tileIndex = -1;

        for (int i = 0; i < nav->getMaxTiles(); i++) {
            const dtPoly* poly = nullptr;
            dtPolyRef testRef = nav->encodePolyId(0, i, 0);  // Use salt=0
            nav->getTileAndPolyByRef(testRef, &testTile, &poly);

            if (testTile && testTile->header && testTile->header->polyCount > 0) {
                tileIndex = i;
                logFile << "[TEST] Found tile at index " << i << " with " << testTile->header->polyCount << " polys" << std::endl;
                logFile << "[TEST] Tile coords: x=" << testTile->header->x << ", y=" << testTile->header->y << std::endl;
                logFile << "[TEST] Tile salt: " << testTile->salt << std::endl;
                logFile << "[TEST] Tile bounds: [" << testTile->header->bmin[0] << "," << testTile->header->bmin[1] << "," << testTile->header->bmin[2]
                    << "] to [" << testTile->header->bmax[0] << "," << testTile->header->bmax[1] << "," << testTile->header->bmax[2] << "]" << std::endl;
                break;
            }
        }

        if (!testTile) {
            logFile << "[TEST] FAIL - no valid tiles found in mesh" << std::endl;
            return false;
        }

        // Get center of first polygon
        const dtPoly* poly = &testTile->polys[0];
        float testPos[3] = { 0, 0, 0 };
        for (int i = 0; i < poly->vertCount; i++) {
            const float* v = &testTile->verts[poly->verts[i] * 3];
            testPos[0] += v[0];
            testPos[1] += v[1];
            testPos[2] += v[2];
        }
        testPos[0] /= poly->vertCount;
        testPos[1] /= poly->vertCount;
        testPos[2] /= poly->vertCount;

        logFile << "[TEST] Testing with position: [" << testPos[0] << ", "
            << testPos[1] << ", " << testPos[2] << "]" << std::endl;

        // Try findNearestPoly
        dtPolyRef resultRef = 0;
        float resultPt[3];
        float extent[3] = { 10.0f, 10.0f, 10.0f };
        dtQueryFilter filter;

        try {
            dtStatus status = query->findNearestPoly(testPos, extent, &filter, &resultRef, resultPt);
            logFile << "[TEST] findNearestPoly status: " << status << " (success=" << dtStatusSucceed(status) << ")" << std::endl;
            logFile << "[TEST] resultRef: " << resultRef << " (0x" << std::hex << resultRef << std::dec << ")" << std::endl;

            if (resultRef) {
                // Try to decode the polyRef
                const dtMeshTile* refTile = nullptr;
                const dtPoly* refPoly = nullptr;
                mesh->getTileAndPolyByRef(resultRef, &refTile, &refPoly);

                if (!refTile || !refPoly) {
                    logFile << "[TEST] FAIL - polyRef " << resultRef << " couldn't be decoded" << std::endl;
                    logFile << "[TEST] refTile=" << refTile << ", refPoly=" << refPoly << std::endl;

                    // Try to manually decode
                    unsigned int salt, tileIdx, polyIdx;
                    mesh->decodePolyId(resultRef, salt, tileIdx, polyIdx);  // Use references
                    logFile << "[TEST] Decoded: salt=" << salt << ", tileIdx=" << tileIdx << ", polyIdx=" << polyIdx << std::endl;
                    logFile << "[TEST] Max tiles in mesh: " << mesh->getMaxTiles() << std::endl;

                    // Check if any tiles exist at all by scanning
                    int foundTiles = 0;
                    for (int scan = 0; scan < mesh->getMaxTiles() && foundTiles < 5; scan++) {
                        const dtMeshTile* scanTile = nullptr;
                        const dtPoly* scanPoly = nullptr;
                        // Encode a polyRef for this tile index and try to get it (use salt=0)
                        dtPolyRef testRef = mesh->encodePolyId(0, scan, 0);
                        mesh->getTileAndPolyByRef(testRef, &scanTile, &scanPoly);
                        if (scanTile && scanTile->header) {
                            logFile << "[TEST]   Found tile at index " << scan
                                << " coords(" << scanTile->header->x << "," << scanTile->header->y
                                << ") salt=" << scanTile->salt << std::endl;
                            foundTiles++;
                        }
                    }
                    logFile << "[TEST] Found " << foundTiles << " tiles total" << std::endl;

                    return false;
                }
                else {
                    logFile << "[TEST] SUCCESS - Query is working!" << std::endl;
                    logFile << "[TEST] Found poly at tile (" << refTile->header->x << "," << refTile->header->y << ")" << std::endl;
                    return true;
                }
            }
            else {
                logFile << "[TEST] FAIL - No polygon found (status: " << std::hex << status << std::dec << ")" << std::endl;
                return false;
            }
        }
        catch (...) {
            logFile << "[TEST] EXCEPTION during findNearestPoly" << std::endl;
            return false;
        }
    }
};

// GLOBAL PERSISTENT MESH
static NavMesh globalNavMesh;

inline std::vector<Vector3> FindPath(const Vector3& start, const Vector3& end) {
    std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);

    if (!globalNavMesh.query || !globalNavMesh.mesh) {
        logFile << "[ERROR] Query or mesh is NULL" << std::endl;
        return {};
    }

    // Verify the query still has a valid reference to the mesh
    const dtNavMesh* queryMesh = globalNavMesh.query->getAttachedNavMesh();
    if (!queryMesh || queryMesh != globalNavMesh.mesh) {
        logFile << "[ERROR] Query mesh mismatch! query->mesh: " << queryMesh
            << " global mesh: " << globalNavMesh.mesh << std::endl;
        return {};
    }

    dtQueryFilter filter;
    filter.setIncludeFlags(0xFFFF);
    filter.setExcludeFlags(0);

    dtPolyRef startRef = 0, endRef = 0;
    float startPt[3] = { 0 }, endPt[3] = { 0 };

    // WoW to Detour coordinate mapping
    float detourStart[3] = { start.y, start.z, start.x };
    float detourEnd[3] = { end.y, end.z, end.x };

    float extent[3] = { 500.0f, 1000.0f, 500.0f };

    logFile << "[Query] Detour coords: [" << detourStart[0] << ", "
        << detourStart[1] << ", " << detourStart[2] << "]" << std::endl;

    // Verify coordinates are within mesh bounds
    const dtNavMeshParams* params = globalNavMesh.mesh->getParams();
    logFile << "[Query] Mesh origin: [" << params->orig[0] << ", "
        << params->orig[1] << ", " << params->orig[2] << "]" << std::endl;
    logFile << "[Query] Tile size: " << params->tileWidth << std::endl;

    // Calculate expected tile coordinates
    int tileX = (int)floor((detourStart[0] - params->orig[0]) / params->tileWidth);
    int tileY = (int)floor((detourStart[2] - params->orig[2]) / params->tileHeight);
    logFile << "[Query] Expected tile coords: (" << tileX << ", " << tileY << ")" << std::endl;

    // Verify that tile exists
    const dtMeshTile* tile = globalNavMesh.mesh->getTileAt(tileX, tileY, 0);
    if (!tile || !tile->header) {
        logFile << "[ERROR] Target tile doesn't exist!" << std::endl;
        return {};
    }
    logFile << "[Query] Target tile valid with " << tile->header->polyCount << " polys" << std::endl;

    logFile << "[Query] About to call findNearestPoly - query ptr: "
        << globalNavMesh.query << " mesh ptr: " << globalNavMesh.mesh << std::endl;
    logFile.flush(); // Force write before potential crash

    // USE DETOUR'S findNearestPoly PROPERLY
    dtStatus status = globalNavMesh.query->findNearestPoly(detourStart, extent, &filter, &startRef, startPt);
    logFile << "[Query] START findNearestPoly returned status: " << status << " startRef: " << startRef << std::endl;

    if (!startRef || dtStatusFailed(status)) {
        logFile << "[ERROR] Failed to find start polygon (status: " << status << ")" << std::endl;

        // Provide more diagnostic info
        logFile << "[DEBUG] Search position: [" << detourStart[0] << ", " << detourStart[1] << ", " << detourStart[2] << "]" << std::endl;
        logFile << "[DEBUG] Search extent: [" << extent[0] << ", " << extent[1] << ", " << extent[2] << "]" << std::endl;
        return {};
    }

    status = globalNavMesh.query->findNearestPoly(detourEnd, extent, &filter, &endRef, endPt);
    logFile << "[Query] END findNearestPoly returned status: " << status << " endRef: " << endRef << std::endl;

    if (!endRef || dtStatusFailed(status)) {
        logFile << "[ERROR] Failed to find end polygon (status: " << status << ")" << std::endl;
        return {};
    }

    dtPolyRef pathPolys[MAX_POLYS];
    int pathCount = 0;

    logFile << "[PATH] Attempting findPath from " << startRef << " to " << endRef << std::endl;
    dtStatus pathStatus = globalNavMesh.query->findPath(startRef, endRef, startPt, endPt,
        &filter, pathPolys, &pathCount, MAX_POLYS);
    logFile << "[PATH] findPath status: " << pathStatus << " pathCount: " << pathCount << std::endl;

    if (pathCount <= 0) {
        logFile << "[ERROR] No path found between polygons (status: " << pathStatus << ")" << std::endl;
        return {};
    }

    float straightPath[MAX_POLYS * 3];
    int straightPathCount = 0;
    globalNavMesh.query->findStraightPath(startPt, endPt, pathPolys, pathCount,
        straightPath, 0, 0, &straightPathCount, MAX_POLYS);

    std::vector<Vector3> result;
    for (int i = 0; i < straightPathCount; ++i) {
        // Convert back from Detour coords [Y, Z, X] to WoW coords [X, Y, Z]
        float y = straightPath[i * 3 + 0];
        float z = straightPath[i * 3 + 1];
        float x = straightPath[i * 3 + 2];
        result.push_back(Vector3(x, y, z));
    }

    logFile << "[SUCCESS] Path found with " << result.size() << " points" << std::endl;
    return result;
}

void DebugNavMesh(dtNavMesh* mesh) {
    std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
    if (!mesh) {
        logFile << "[DEBUG] NavMesh is NULL." << std::endl;
        return;
    }

    const dtNavMesh* nav = mesh;
    int validTiles = 0;
    float totalBmin[3] = { 1e9f, 1e9f, 1e9f };
    float totalBmax[3] = { -1e9f, -1e9f, -1e9f };

    // Track first few tile locations
    int firstTileIndices[10];
    int indexCount = 0;

    // Scan through ALL tile slots (up to maxTiles, which is 4096)
    for (int i = 0; i < nav->getMaxTiles(); ++i) {
        const dtMeshTile* tile = nullptr;
        const dtPoly* poly = nullptr;

        // Try to get tile by encoding a reference with salt=0 (tiles start with salt 0)
        dtPolyRef testRef = nav->encodePolyId(0, i, 0);
        nav->getTileAndPolyByRef(testRef, &tile, &poly);

        if (!tile || !tile->header || !tile->dataSize) continue;

        validTiles++;
        if (indexCount < 10) {
            firstTileIndices[indexCount++] = i;
        }

        if (tile->header->bmin[0] < totalBmin[0]) totalBmin[0] = tile->header->bmin[0];
        if (tile->header->bmin[1] < totalBmin[1]) totalBmin[1] = tile->header->bmin[1];
        if (tile->header->bmin[2] < totalBmin[2]) totalBmin[2] = tile->header->bmin[2];

        if (tile->header->bmax[0] > totalBmax[0]) totalBmax[0] = tile->header->bmax[0];
        if (tile->header->bmax[1] > totalBmax[1]) totalBmax[1] = tile->header->bmax[1];
        if (tile->header->bmax[2] > totalBmax[2]) totalBmax[2] = tile->header->bmax[2];
    }

    logFile << "[DEBUG] Map Statistics:" << std::endl;
    logFile << " - Loaded Tiles: " << validTiles << std::endl;
    logFile << " - Max tile slots: " << nav->getMaxTiles() << std::endl;

    // Show first 10 tile indices
    logFile << " - First " << indexCount << " tile indices with data: ";
    for (int i = 0; i < indexCount; i++) {
        logFile << firstTileIndices[i] << " ";
    }
    logFile << std::endl;

    if (validTiles > 0) {
        logFile << " - Mesh Bounds (Detour Coords):" << std::endl;
        logFile << "   Min: [" << totalBmin[0] << ", " << totalBmin[1] << ", " << totalBmin[2] << "]" << std::endl;
        logFile << "   Max: [" << totalBmax[0] << ", " << totalBmax[1] << ", " << totalBmax[2] << "]" << std::endl;
    }
}

inline std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end, bool flying, int mapId) {
    std::string folder = "C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/mmaps/";
    if (!globalNavMesh.LoadMap(folder, mapId)) return {};
    DebugNavMesh(globalNavMesh.mesh);

    // TEST THE QUERY FIRST
    if (!globalNavMesh.TestQuery()) {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        printf("[RUNTIME] sizeof(dtBVNode) = %zu bytes\n", sizeof(dtBVNode));
        printf("[RUNTIME] offsetof(bmin) = %zu\n", offsetof(dtBVNode, bmin));
        printf("[RUNTIME] offsetof(bmax) = %zu\n", offsetof(dtBVNode, bmax));
        printf("[RUNTIME] offsetof(i) = %zu\n", offsetof(dtBVNode, i));
        logFile << "[CRITICAL] Query test failed - something is fundamentally broken" << std::endl;
        return {};
    }

    std::vector<Vector3> path = FindPath(start, end);
    return path; // Already in correct WoW coordinate format
}