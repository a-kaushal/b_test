//#pragma once
//
//#include <iostream>
//#include <fstream>
//#include <vector>
//#include <string>
//#include <algorithm>
//#include <filesystem>
//#include <sstream>
//
//// DETOUR INCLUDES
//#include "DetourNavMesh.h"
//#include "DetourNavMeshQuery.h"
//#include "DetourCommon.h"
//
//#include "Vector.h"
//
//extern "C" bool CheckVMapLine(int mapId, float x1, float y1, float z1, float x2, float y2, float z2);
//
//// --- CONFIGURATION ---
//const float COLLISION_STEP_SIZE = 3.0f; // Step size for TERRAIN checks only
//const float WAYPOINT_STEP_SIZE = 4.0f;
//const float HOVER_ALTITUDE = 10.0f;
//const float MIN_CLEARANCE = 3.0f;       // Increased clearance to avoid grazing ground
//const float AGENT_RADIUS = 1.2f;
//const int MAX_POLYS = 256;
//
//// --- CONSTANTS FOR DETECTION ---
//const unsigned char AREA_GROUND = 1;
//const unsigned char AREA_MAGMA = 2;  // WARNING: Your generator also sets Roads/Bridges to 2! (Need to recreate mmap)
//const unsigned char AREA_SLIME = 4;
//const unsigned char AREA_WATER = 8;  // 0x08
//const unsigned char AREA_ROAD = 16;
//
//#pragma pack(push, 1)
//struct MmapTileHeader {
//    uint32_t mmapMagic;
//    uint32_t dtVersion;
//    float mmapVersion;
//    uint32_t size;
//    char usesLiquids;
//    char padding[3];
//};
//#pragma pack(pop)
//
//// --- CACHE HELPERS ---
//
//// Custom key for our cache map
//struct PathCacheKey {
//    Vector3 start;
//    Vector3 end;
//    bool flying;
//
//    // Comparison operator for std::map
//    bool operator<(const PathCacheKey& other) const {
//        if (start.x != other.start.x) return start.x < other.start.x;
//        if (start.y != other.start.y) return start.y < other.start.y;
//        if (start.z != other.start.z) return start.z < other.start.z;
//        if (end.x != other.end.x) return end.x < other.end.x;
//        if (end.y != other.end.y) return end.y < other.end.y;
//        if (end.z != other.end.z) return end.z < other.end.z;
//        return flying < other.flying;
//    }
//};
//
//// Global Cache: Stores the calculated path between two waypoints
//static std::map<PathCacheKey, std::vector<Vector3>> globalPathCache;
//
//// Helper function to round up to next power of 2
//inline int nextPowerOfTwo(int n) {
//    if (n <= 0) return 1;
//    n--;
//    n |= n >> 1;
//    n |= n >> 2;
//    n |= n >> 4;
//    n |= n >> 8;
//    n |= n >> 16;
//    return n + 1;
//}
//
//class NavMesh {
//public:
//    dtNavMesh* mesh = nullptr;
//    dtNavMeshQuery* query = nullptr;
//    int currentMapId = -1;
//
//    NavMesh() {
//        query = dtAllocNavMeshQuery();
//    }
//
//    ~NavMesh() {
//        dtFreeNavMesh(mesh);
//        dtFreeNavMeshQuery(query);
//    }
//
//    void Clear() {
//        dtFreeNavMesh(mesh);
//        mesh = nullptr;
//        dtFreeNavMeshQuery(query);
//        query = nullptr;
//        currentMapId = -1;
//        // IMPORTANT: Clear the path cache when the mesh is destroyed/changed
//        // because old coordinates might mean different things on a new map.
//        globalPathCache.clear();
//    }
//
//    // 1. DETECT WATER
//    bool IsUnderwater(const Vector3& pos) {
//        if (!query || !mesh) return false;
//        float center[3] = { pos.y, pos.z, pos.x };
//        float extent[3] = { 10.0f, 20.0f, 10.0f };
//        dtPolyRef ref; float nearest[3]; dtQueryFilter filter;
//        query->findNearestPoly(center, extent, &filter, &ref, nearest);
//        if (!ref) return false;
//
//        const dtMeshTile* tile = 0; const dtPoly* poly = 0;
//        if (dtStatusSucceed(mesh->getTileAndPolyByRef(ref, &tile, &poly))) {
//            unsigned char areaID = poly->getArea();
//            if (areaID == AREA_WATER || areaID == AREA_SLIME) return true;
//        }
//        return false;
//    }
//
//    // 2. DETECT INDOORS (Ceiling Check)
//    bool IsUnderground(const Vector3& pos) {
//        if (!query || !mesh) return false;
//        // Look for ground significantly above the player
//        Vector3 highProbe = pos;
//        highProbe.z += 40.0f;
//        float ceilingZ = GetLocalGroundHeight(highProbe);
//        // If we find a surface > 10y above us, we are likely indoors/cave
//        if (ceilingZ > -90000.0f && ceilingZ > (pos.z + 10.0f)) return true;
//        return false;
//    }
//
//    bool LoadMap(const std::string& directory, int mapId) {
//        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
//        // OPTIMIZATION: Keep map loaded in memory until mapId changes
//        if (currentMapId == mapId && mesh && query) {
//            return true;
//        }
//        Clear();// This now also clears the path cache
//
//        query = dtAllocNavMeshQuery();
//        if (!query) {
//            logFile << "[ERROR] Failed to allocate query" << std::endl;
//            return false;
//        }
//
//        std::stringstream ss;
//        ss << std::setw(4) << std::setfill('0') << mapId;
//        std::string prefix = ss.str();
//
//        std::string mmapPath = directory + prefix + ".mmap";
//        dtNavMeshParams params;
//        bool paramsValid = false;
//
//        // Try to read .mmap file
//        std::ifstream mmapFile(mmapPath, std::ios::binary);
//        if (mmapFile.is_open()) {
//            // Get file size
//            mmapFile.seekg(0, std::ios::end);
//            size_t fileSize = mmapFile.tellg();
//            mmapFile.seekg(0, std::ios::beg);
//
//            if (fileSize >= sizeof(dtNavMeshParams)) {
//                mmapFile.read((char*)&params, sizeof(dtNavMeshParams));
//                // Validate parameters
//                bool validOrig = !std::isnan(params.orig[0]) && !std::isnan(params.orig[1]) && !std::isnan(params.orig[2]) &&
//                    !std::isinf(params.orig[0]) && !std::isinf(params.orig[1]) && !std::isinf(params.orig[2]) &&
//                    params.orig[0] > -50000 && params.orig[0] < 50000 &&
//                    params.orig[1] > -5000 && params.orig[1] < 5000 &&
//                    params.orig[2] > -50000 && params.orig[2] < 50000;
//
//                bool validTiles = params.tileWidth > 0 && params.tileWidth < 1000 &&
//                    params.tileHeight > 0 && params.tileHeight < 1000;
//
//                bool validCounts = params.maxTiles > 0 && params.maxTiles <= 8192 &&
//                    params.maxPolys > 0 && params.maxPolys <= (1U << 20);
//
//                paramsValid = validOrig && validTiles && validCounts;
//
//                if (!paramsValid) {
//                    logFile << "[MMAP] Validation FAILED - parameters are invalid" << std::endl;
//                }
//            }
//            else {
//                logFile << "[MMAP] File too small" << std::endl;
//            }
//
//            mmapFile.close();
//        }
//
//        // If .mmap file is invalid or doesn't exist, use calculated fallback
//        if (!paramsValid) {
//
//            // IMPORTANT: We need to determine the correct origin BEFORE initializing the mesh
//            // Temporarily load ONE tile to get its coordinates
//            std::string firstTilePath;
//            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
//                if (entry.path().filename().string().find(prefix) == 0 &&
//                    entry.path().extension() == ".mmtile") {
//                    firstTilePath = entry.path().string();
//                    break;
//                }
//            }
//
//            if (!firstTilePath.empty()) {
//                // Read first tile to determine origin
//                std::ifstream tileFile(firstTilePath, std::ios::binary);
//                if (tileFile.is_open()) {
//                    MmapTileHeader tileHeader;
//                    tileFile.read((char*)&tileHeader, sizeof(MmapTileHeader));
//
//                    unsigned char* tileData = (unsigned char*)dtAlloc(tileHeader.size, DT_ALLOC_PERM);
//                    tileFile.read((char*)tileData, tileHeader.size);
//                    tileFile.close();
//
//                    // Parse tile header to get coordinates
//                    dtMeshHeader* meshHeader = (dtMeshHeader*)tileData;
//
//                    // Calculate origin from tile coordinates
//                    float calcOrig[3];
//                    calcOrig[0] = meshHeader->bmin[0] - (meshHeader->x * 533.33333f);
//                    calcOrig[1] = meshHeader->bmin[1];
//                    calcOrig[2] = meshHeader->bmin[2] - (meshHeader->y * 533.33333f);
//
//                    dtFree(tileData);
//
//                    memset(&params, 0, sizeof(params));
//                    params.orig[0] = calcOrig[0];
//                    params.orig[1] = calcOrig[1];
//                    params.orig[2] = calcOrig[2];
//                    params.tileWidth = 533.33333f;
//                    params.tileHeight = 533.33333f;
//                    params.maxTiles = 1 << 21;      // 2^21
//                    params.maxPolys = 1U << 31;     // 2^31
//                }
//                else {
//                    // Couldn't read tile, use defaults
//                    memset(&params, 0, sizeof(params));
//                    params.orig[0] = -17600.0f;
//                    params.orig[1] = -1000.0f;
//                    params.orig[2] = -17600.0f;
//                    params.tileWidth = 533.33333f;
//                    params.tileHeight = 533.33333f;
//                    params.maxTiles = 1 << 21;
//                    params.maxPolys = 1U << 31;
//                }
//            }
//            else {
//                // No tiles found, use defaults
//                memset(&params, 0, sizeof(params));
//                params.orig[0] = -17600.0f;
//                params.orig[1] = -1000.0f;
//                params.orig[2] = -17600.0f;
//                params.tileWidth = 533.33333f;
//                params.tileHeight = 533.33333f;
//                params.maxTiles = 1 << 21;
//                params.maxPolys = 1U << 31;
//            }
//        }
//
//        // Initialize mesh with params
//        mesh = dtAllocNavMesh();
//
//        dtStatus initStatus = mesh->init(&params);
//        if (dtStatusFailed(initStatus)) {
//            logFile << "[ERROR] mesh->init FAILED with status: " << std::hex << initStatus << std::dec << std::endl;
//
//            // The .mmap file may be from an incompatible extractor version
//            logFile << "[WORKAROUND] Ignoring .mmap file, using calculated defaults" << std::endl;
//
//            // Use defaults that match your actual Detour configuration
//            memset(&params, 0, sizeof(params));
//            params.orig[0] = (32.0f - 64.0f) * 533.33333f - 533.33333f;  // -17599.999
//            params.orig[1] = -1000.0f;
//            params.orig[2] = (32.0f - 64.0f) * 533.33333f - 533.33333f;  // -17599.999
//            params.tileWidth = 533.33333f;
//            params.tileHeight = 533.33333f;
//            params.maxTiles = 1 << 21;     // 2^21 = 2,097,152 (matches DT_TILE_BITS=21)
//            params.maxPolys = 1U << 31;    // 2^31 = 2,147,483,648 (matches DT_POLY_BITS=31, note: unsigned)
//
//            logFile << "[RETRY] Using defaults matching DT_TILE_BITS=21, DT_POLY_BITS=31:" << std::endl;
//            logFile << "  maxTiles=" << params.maxTiles << ", maxPolys=" << params.maxPolys << std::endl;
//
//            dtFreeNavMesh(mesh);
//            mesh = dtAllocNavMesh();
//            initStatus = mesh->init(&params);
//
//            if (dtStatusFailed(initStatus)) {
//                logFile << "[ERROR] Even defaults failed! Status: " << std::hex << initStatus << std::dec << std::endl;
//
//                if (initStatus & DT_OUT_OF_MEMORY)
//                    logFile << "[ERROR] OUT_OF_MEMORY - Reduce maxTiles or maxPolys" << std::endl;
//                if (initStatus & DT_INVALID_PARAM)
//                    logFile << "[ERROR] INVALID_PARAM - Bit configuration mismatch" << std::endl;
//
//                dtFreeNavMesh(mesh);
//                mesh = nullptr;
//                return false;
//            }
//        }
//        // Load tiles...
//        int tilesLoaded = 0;
//        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
//            if (entry.path().filename().string().find(prefix) == 0 &&
//                entry.path().extension() == ".mmtile") {
//                if (AddTile(entry.path().string())) {
//                    tilesLoaded++;
//                }
//            }
//        }
//        logFile << "[INFO] Loaded " << tilesLoaded << " tiles" << std::endl;
//
//        // Initialize query
//        dtStatus status = query->init(mesh, 2048);
//        if (dtStatusFailed(status)) {
//            logFile << "[ERROR] Query init failed" << std::endl;
//            return false;
//        }
//
//        currentMapId = mapId;
//        return true;
//    }
//
//    bool AddTile(const std::string& filepath) {
//        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
//        std::ifstream file(filepath, std::ios::binary);
//        if (!file.is_open()) return false;
//
//        MmapTileHeader header;
//        file.read((char*)&header, sizeof(MmapTileHeader));
//
//        // Log version info from first tile only
//        static bool versionLogged = false;
//        if (!versionLogged) {
//            if (header.dtVersion != DT_NAVMESH_VERSION) {
//                logFile << "  [WARNING] VERSION MISMATCH! Tiles generated with different Detour version!" << std::endl;
//                logFile << "  Expected: " << DT_NAVMESH_VERSION << " Got: " << header.dtVersion << std::endl;
//            }
//            versionLogged = true;
//        }
//
//        unsigned char* data = (unsigned char*)dtAlloc(header.size, DT_ALLOC_PERM);
//        file.read((char*)data, header.size);
//
//        // CRITICAL: Check what's actually in the tile data BEFORE adding
//        dtMeshHeader* meshHeader = (dtMeshHeader*)data;
//        static int diagCount = 0;
//        if (diagCount < 5) {
//            // Calculate what the tile index SHOULD be
//            const dtNavMeshParams* params = mesh->getParams();
//            float tileCenterX = (meshHeader->bmin[0] + meshHeader->bmax[0]) / 2.0f;
//            float tileCenterZ = (meshHeader->bmin[2] + meshHeader->bmax[2]) / 2.0f;
//            int expectedTileX = (int)floorf((tileCenterX - params->orig[0]) / params->tileWidth);
//            int expectedTileY = (int)floorf((tileCenterZ - params->orig[2]) / params->tileHeight);
//
//            if (meshHeader->x != expectedTileX || meshHeader->y != expectedTileY) {
//                logFile << "  [ERROR] TILE COORDINATE MISMATCH!" << std::endl;
//            }
//
//            diagCount++;
//        }
//
//        // CRITICAL FIX: WoW uses a 64x64 tile grid, not 2048x2048!
//        // The tile coordinates in the files are in WoW's grid system
//        int tx = meshHeader->x;
//        int ty = meshHeader->y;
//        int layer = 0;
//
//        // WoW maps use 64x64 tiles (from -32 to +31 in the WoW coordinate system)
//        // So tileLutSize should be 64, not 2048
//        const int tileLutSize = 64;
//
//        int tileIndex = tx + ty * tileLutSize;
//
//        // Try to remove any existing tile at this index first
//        const dtMeshTile* existingTile = mesh->getTileAt(tx, ty, layer);
//        if (existingTile && existingTile->header) {
//            dtTileRef existingRef = mesh->getTileRef(existingTile);
//            logFile << "[TILE] Removing existing tile at (" << tx << "," << ty << "), ref=" << existingRef << std::endl;
//            mesh->removeTile(existingRef, nullptr, nullptr);
//        }
//
//        // Create a tileRef hint: shift tileIndex by DT_POLY_BITS (which is 20)
//        // tileRef format: [12 bits salt][12 bits tile][20 bits poly]
//        dtTileRef tileRefHint = ((dtTileRef)tileIndex) << 20;  // Shift by DT_POLY_BITS=20
//
//        dtTileRef tileRef = 0;
//        dtStatus status = mesh->addTile(data, header.size, DT_TILE_FREE_DATA, tileRefHint, &tileRef);
//        if (dtStatusFailed(status)) {
//            logFile << "[ERROR] addTile failed for " << filepath << " status: " << std::hex << status << std::dec << std::endl;
//            logFile << "[ERROR] Tile coords: x=" << tx << ", y=" << ty << ", tileIndex=" << tileIndex << std::endl;
//            dtFree(data);
//            return false;
//        }
//
//        // Decode the tileRef to see where it was placed
//        static int addCount = 0;
//        if (addCount < 5) { // Only log first 5 tiles
//            unsigned int salt, tileIdx, polyIdx;
//            mesh->decodePolyId(tileRef, salt, tileIdx, polyIdx);  // Use references, not pointers
//            addCount++;
//        }
//
//        return true;
//    }
//
//    // UPDATED: Finds the nearest ground surface LOCALLY
//    // Returns -99999.0f if no ground is found within the vertical search range.
//    float GetLocalGroundHeight(const Vector3& pos) {
//        if (!query || !mesh) return -99999.0f;
//        float center[3] = { pos.y, pos.z, pos.x };
//        float extent[3] = { 10.0f, 20.0f, 10.0f };
//        dtPolyRef polyRef; float nearest[3]; dtQueryFilter filter;
//        dtStatus status = query->findNearestPoly(center, extent, &filter, &polyRef, nearest);
//        if (dtStatusSucceed(status) && polyRef) {
//            return nearest[1];
//        }
//        return -99999.0f;
//    }
//
//    // --- RECAST SAFE SUBDIVISION (GROUND) ---
//    std::vector<Vector3> SubdivideOnMesh(const std::vector<Vector3>& input) {
//        if (input.empty()) return {};
//        if (!query || !mesh) return input;
//        std::vector<Vector3> output;
//        output.push_back(input[0]);
//        dtQueryFilter filter;
//        filter.setIncludeFlags(0xFFFF); filter.setExcludeFlags(0);
//
//        for (size_t i = 0; i < input.size() - 1; ++i) {
//            Vector3 start = input[i]; Vector3 end = input[i + 1];
//            float dist = start.Dist3D(end);
//            if (dist < 0.1f) continue;
//            int steps = (int)(dist / WAYPOINT_STEP_SIZE);
//            if (steps == 0) { output.push_back(end); continue; }
//
//            Vector3 dir = (end - start).Normalize();
//            dtPolyRef startPoly;
//            float startPt[3] = { start.y, start.z, start.x };
//            float extent[3] = { 5.0f, 10.0f, 5.0f };
//            query->findNearestPoly(startPt, extent, &filter, &startPoly, startPt);
//
//            for (int j = 1; j <= steps; ++j) {
//                Vector3 target = start + (dir * (float)(j * WAYPOINT_STEP_SIZE));
//                float destPt[3] = { target.y, target.z, target.x };
//                float resultPt[3];
//                dtPolyRef visited[16]; int visitedCount = 0;
//                query->moveAlongSurface(startPoly, startPt, destPt, &filter, resultPt, visited, &visitedCount, 16);
//                Vector3 safePoint(resultPt[2], resultPt[0], resultPt[1]);
//                output.push_back(safePoint);
//                if (visitedCount > 0) startPoly = visited[visitedCount - 1];
//                dtVcopy(startPt, resultPt);
//            }
//            if (output.back().Dist3D(end) > 0.5f) output.push_back(end);
//        }
//        return output;
//    }
//
//    // --- ROBUST COLLISION CHECK (VMAP + TERRAIN) ---
//    // Returns TRUE if the point is safe (not inside a building/tree/mountain)
//    bool CheckFlightPointRobust(const Vector3& pos, int mapId) {
//        // 1. Check VMAPS (Static objects like buildings, trees)
//        // We simulate a tiny line check at the point to detect collision
//        // Or we rely on the Segment check logic which calls CheckVMapLine.
//        // For a single point, we just check terrain clearance.
//
//        float gZ = GetLocalGroundHeight(pos);
//        if (gZ > -90000.0f) {
//            // Collision with Terrain
//            if (pos.z < gZ + MIN_CLEARANCE) return false;
//        }
//        return true;
//    }
//
//    // --- ROBUST COLLISION CHECK ---
//    // Returns TRUE if the segment is clear.
//    bool CheckFlightSegment(const Vector3& start, const Vector3& end, int mapId) {
//        Vector3 dir = end - start;
//        float totalDist = dir.Length();
//        if (totalDist < 0.1f) return true;
//
//        // 1. VMAP CHECK (Thick Parallel Raycasts)
//        // We cast 5 continuous rays. If ANY ray hits a VMap object, the path is blocked.
//        // This ensures we don't skip over thin walls between step points.
//        float r = AGENT_RADIUS;
//        Vector3 offsets[5] = {
//            Vector3(0, 0, 0),
//            Vector3(r, r, 0),
//            Vector3(-r, -r, 0),
//            Vector3(r, -r, 0),
//            Vector3(-r, r, 0)
//        };
//
//        for (const auto& off : offsets) {
//            Vector3 s = start + off;
//            Vector3 e = end + off;
//
//            // Note: CheckVMapLine returns true if BLOCKED
//            if (CheckVMapLine(mapId, s.x, s.y, s.z, e.x, e.y, e.z)) {
//                return false;
//            }
//        }
//
//        // 2. TERRAIN CHECK (Iterative)
//        // We still need to step along the line to check NavMesh height, 
//        // because "Terrain" isn't always fully covered by CheckVMapLine depending on the core.
//        dir = dir / totalDist;
//        int numSteps = (int)(totalDist / COLLISION_STEP_SIZE);
//
//        for (int i = 1; i <= numSteps; ++i) {
//            Vector3 pt = start + (dir * (float)(i * COLLISION_STEP_SIZE));
//
//            // Only checking center point for terrain height is usually sufficient 
//            // unless terrain is extremely jagged.
//            float gZ = GetLocalGroundHeight(pt);
//            if (gZ > -90000.0f) {
//                // If the path dips below the terrain (plus clearance)
//                if (pt.z < gZ + MIN_CLEARANCE) return false;
//            }
//        }
//
//        return true;
//    }
//};
//
//// GLOBAL PERSISTENT MESH
//static NavMesh globalNavMesh;
//
//inline std::vector<Vector3> FindPath(const Vector3& start, const Vector3& end) {
//    std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
//
//    if (!globalNavMesh.query || !globalNavMesh.mesh) {
//        logFile << "[ERROR] Query or mesh is NULL" << std::endl;
//        return {};
//    }
//
//    // Verify the query still has a valid reference to the mesh
//    const dtNavMesh* queryMesh = globalNavMesh.query->getAttachedNavMesh();
//    if (!queryMesh || queryMesh != globalNavMesh.mesh) {
//        logFile << "[ERROR] Query mesh mismatch! query->mesh: " << queryMesh
//            << " global mesh: " << globalNavMesh.mesh << std::endl;
//        return {};
//    }
//
//    dtQueryFilter filter;
//    filter.setIncludeFlags(0xFFFF);
//    filter.setExcludeFlags(0);
//
//    dtPolyRef startRef = 0, endRef = 0;
//    float startPt[3] = { 0 }, endPt[3] = { 0 };
//
//    // WoW to Detour coordinate mapping
//    float detourStart[3] = { start.y, start.z, start.x };
//    float detourEnd[3] = { end.y, end.z, end.x };
//
//    float extent[3] = { 500.0f, 1000.0f, 500.0f };
//
//    // Verify coordinates are within mesh bounds
//    const dtNavMeshParams* params = globalNavMesh.mesh->getParams();
//
//    // Calculate expected tile coordinates
//    int tileX = (int)floor((detourStart[0] - params->orig[0]) / params->tileWidth);
//    int tileY = (int)floor((detourStart[2] - params->orig[2]) / params->tileHeight);
//
//    // Verify that tile exists
//    const dtMeshTile* tile = globalNavMesh.mesh->getTileAt(tileX, tileY, 0);
//    if (!tile || !tile->header) {
//        logFile << "[ERROR] Target tile doesn't exist!" << std::endl;
//        return {};
//    }
//    logFile.flush(); // Force write before potential crash
//
//    // USE DETOUR'S findNearestPoly PROPERLY
//    dtStatus status = globalNavMesh.query->findNearestPoly(detourStart, extent, &filter, &startRef, startPt);
//
//    if (!startRef || dtStatusFailed(status)) {
//        logFile << "[ERROR] Failed to find start polygon (status: " << status << ")" << std::endl;
//
//        // Provide more diagnostic info
//        logFile << "[DEBUG] Search position: [" << detourStart[0] << ", " << detourStart[1] << ", " << detourStart[2] << "]" << std::endl;
//        logFile << "[DEBUG] Search extent: [" << extent[0] << ", " << extent[1] << ", " << extent[2] << "]" << std::endl;
//        return {};
//    }
//
//    status = globalNavMesh.query->findNearestPoly(detourEnd, extent, &filter, &endRef, endPt);
//
//    if (!endRef || dtStatusFailed(status)) {
//        logFile << "[ERROR] Failed to find end polygon (status: " << status << ")" << std::endl;
//        return {};
//    }
//
//    dtPolyRef pathPolys[MAX_POLYS];
//    int pathCount = 0;
//
//    dtStatus pathStatus = globalNavMesh.query->findPath(startRef, endRef, startPt, endPt,
//        &filter, pathPolys, &pathCount, MAX_POLYS);
//
//    if (pathCount <= 0) {
//        logFile << "[ERROR] No path found between polygons (status: " << pathStatus << ")" << std::endl;
//        return {};
//    }
//
//    float straightPath[MAX_POLYS * 3];
//    int straightPathCount = 0;
//    globalNavMesh.query->findStraightPath(startPt, endPt, pathPolys, pathCount,
//        straightPath, 0, 0, &straightPathCount, MAX_POLYS);
//
//    std::vector<Vector3> result;
//    for (int i = 0; i < straightPathCount; ++i) {
//        // Convert back from Detour coords [Y, Z, X] to WoW coords [X, Y, Z]
//        float y = straightPath[i * 3 + 0];
//        float z = straightPath[i * 3 + 1];
//        float x = straightPath[i * 3 + 2];
//        result.push_back(Vector3(x, y, z));
//    }
//
//    logFile << "[SUCCESS] Path found with " << result.size() << " points" << std::endl;
//    return result;
//}
//
//// --- FLIGHT LOGIC REVAMPED ---
//// Purpose: Create smooth, collision-free paths avoiding zig-zags and clipping.
//inline std::vector<Vector3> SmoothFlightPath(const std::vector<Vector3>& input, int mapId) {
//    if (input.size() < 3) return input;
//    std::vector<Vector3> smoothed;
//    smoothed.push_back(input[0]);
//
//    size_t currentIdx = 0;
//    while (currentIdx < input.size() - 1) {
//        size_t bestNext = currentIdx + 1;
//        size_t maxLook = (std::min)(input.size() - 1, currentIdx + 20);
//
//        for (size_t nextIdx = maxLook; nextIdx > currentIdx + 1; --nextIdx) {
//            // Because CheckFlightSegment is now robust (checking the full line),
//            // this will ONLY skip waypoints if the straight line is truly clear.
//            if (globalNavMesh.CheckFlightSegment(input[currentIdx], input[nextIdx], mapId)) {
//                bestNext = nextIdx;
//                break;
//            }
//        }
//        smoothed.push_back(input[bestNext]);
//        currentIdx = bestNext;
//    }
//    return smoothed;
//}
//
//inline std::vector<Vector3> CalculateFlightPath(const Vector3& start, const Vector3& end, int mapId) {
//    // 1. DIRECT CHECK
//    if (globalNavMesh.CheckFlightSegment(start, end, mapId)) return { start, end };
//
//    // 2. GROUND PATH
//    std::vector<Vector3> groundPath = FindPath(start, end);
//    if (groundPath.empty()) return { start, Vector3(start.x, start.y, start.z + 50), Vector3(end.x, end.y, end.z + 50), end };
//
//    // 3. INDOOR CHECK
//    bool indoors = false;
//    float maxPathZ = -99999.0f;
//    for (const auto& p : groundPath) {
//        if (p.z > maxPathZ) maxPathZ = p.z;
//        if (!indoors && globalNavMesh.IsUnderground(p)) indoors = true;
//    }
//
//    // 4. HIGH CRUISE (Box Path)
//    if (!indoors) {
//        float cruiseZ = (std::max)(maxPathZ + 15.0f, (std::max)(start.z, end.z) + 15.0f);
//        if (cruiseZ > start.z + 150.0f) cruiseZ = start.z + 150.0f;
//
//        Vector3 ascPoint = start; ascPoint.z = cruiseZ;
//        Vector3 descPoint = end; descPoint.z = cruiseZ;
//
//        // Check ALL legs using volumetric check
//        if (globalNavMesh.CheckFlightSegment(start, ascPoint, mapId) &&
//            globalNavMesh.CheckFlightSegment(ascPoint, descPoint, mapId) &&
//            globalNavMesh.CheckFlightSegment(descPoint, end, mapId)) {
//
//            std::vector<Vector3> path;
//            path.push_back(start);
//            if (std::abs(start.z - cruiseZ) > 2.0f) path.push_back(ascPoint);
//            if (std::abs(end.z - cruiseZ) > 2.0f) path.push_back(descPoint);
//            path.push_back(end);
//
//            // Try diagonal cut
//            if (path.size() >= 3 && globalNavMesh.CheckFlightSegment(start, descPoint, mapId)) {
//                return { start, descPoint, end };
//            }
//            return path;
//        }
//    }
//
//    // 5. DRAPED FALLBACK
//    float hoverHeight = indoors ? 3.0f : HOVER_ALTITUDE;
//    std::vector<Vector3> drapedPath;
//    drapedPath.push_back(start);
//    float currentAlt = start.z;
//
//    for (size_t i = 0; i < groundPath.size(); ++i) {
//        Vector3 node = groundPath[i];
//        float targetZ = node.z + hoverHeight;
//        if (indoors) drapedPath.push_back(Vector3(node.x, node.y, targetZ));
//        else {
//            if (targetZ > currentAlt) currentAlt = targetZ;
//            drapedPath.push_back(Vector3(node.x, node.y, currentAlt));
//        }
//    }
//    drapedPath.push_back(end);
//    // Crucial: SmoothFlightPath uses the robust CheckFlightSegment to ensure we don't
//    // cut corners through walls.
//    return SmoothFlightPath(drapedPath, mapId);
//}
//
//// --- SUBDIVISION WITH VERIFICATION ---
//inline std::vector<Vector3> SubdivideFlightPath(const std::vector<Vector3>& input, int mapId) {
//    if (input.empty()) return {};
//    std::vector<Vector3> output; output.push_back(input[0]);
//
//    for (size_t i = 0; i < input.size() - 1; ++i) {
//        Vector3 start = input[i]; Vector3 end = input[i + 1];
//        float dist3D = start.Dist3D(end);
//        int count = (int)(dist3D / WAYPOINT_STEP_SIZE);
//        if (count < 1) count = 1;
//
//        Vector3 dir = (end - start) / (float)count;
//
//        for (int j = 1; j <= count; ++j) {
//            Vector3 pt = start + (dir * (float)j);
//
//            // Terrain Nudge
//            float gZ = globalNavMesh.GetLocalGroundHeight(pt);
//            if (gZ > -90000.0f && pt.z < gZ + MIN_CLEARANCE) {
//                pt.z = gZ + MIN_CLEARANCE + 0.5f;
//            }
//            output.push_back(pt);
//        }
//        output.push_back(end);
//    }
//    return output;
//}
//
//inline std::vector<Vector3> CalculatePath(const std::vector<Vector3>& inputPath, const Vector3& startPos, int currentIndex, bool flying, int mapId, bool path_loop = false) {
//    std::string mmapFolder = "C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/mmaps/";
//    std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
//
//    // LOAD NAVMESH
//    if (!globalNavMesh.LoadMap(mmapFolder, mapId)) return {};
//
//    std::vector<Vector3> stitchedPath;
//    std::vector<Vector3> modifiedInput;
//
//    // Validation
//    if (inputPath.empty() || currentIndex < 0 || currentIndex >= inputPath.size()) {
//        return {};
//    }
//
//    int lookahead = inputPath.size() - 1;
//    int endIndex = inputPath.size();
//
//    if ((currentIndex >= endIndex) && (startPos.Dist3D(inputPath[currentIndex]) < 5.0f)) {
//        return { inputPath[currentIndex] };
//    }
//
//    if (path_loop == false) {
//        modifiedInput.push_back(startPos);
//        for (int i = currentIndex; i < endIndex; ++i) modifiedInput.push_back(inputPath[i]);
//        lookahead = modifiedInput.size() - 1;
//    }
//    else {
//        // Handle looping path logic if needed
//        for (int i = currentIndex; i < endIndex; ++i) modifiedInput.push_back(inputPath[i]);
//        for (int i = 0; i < currentIndex; ++i) modifiedInput.push_back(inputPath[i]);
//        lookahead = modifiedInput.size() - 1;
//    }
//
//    // Process segments
//    for (int i = 0; i < lookahead; ++i) {
//        Vector3 start = modifiedInput[i];
//        Vector3 end = modifiedInput[i + 1];
//
//        PathCacheKey key = { start, end, flying };
//        auto cacheIt = globalPathCache.find(key);
//        std::vector<Vector3> segment;
//
//        if (cacheIt != globalPathCache.end()) {
//            segment = cacheIt->second;
//        }
//        else {
//            if (flying) segment = CalculateFlightPath(start, end, mapId);
//            else segment = FindPath(start, end);
//            globalPathCache[key] = segment;
//        }
//
//        if (segment.size() > 0) {
//            if (stitchedPath.empty()) stitchedPath.insert(stitchedPath.end(), segment.begin(), segment.end());
//            else stitchedPath.insert(stitchedPath.end(), segment.begin() + 1, segment.end());
//        }
//    }
//
//    if (flying) return SubdivideFlightPath(stitchedPath, mapId);
//    else return globalNavMesh.SubdivideOnMesh(stitchedPath);
//}