#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <list>
#include <set>
#include <utility>

// DETOUR INCLUDES
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourCommon.h"

#include "Vector.h"

// FMap function declarations (replaces VMap)
extern "C" bool CheckFMapLine(int mapId, float x1, float y1, float z1, float x2, float y2, float z2);
extern "C" float GetFMapFloorHeight(int mapId, float x, float y, float z);
extern "C" bool CanFlyAt(int mapId, float x, float y, float z);

// --- AREA CONSTANTS (Must match Generator/PathCommon.h) ---
const unsigned short AREA_GROUND = 0x01; // 1
const unsigned short AREA_MAGMA = 0x02; // 2
const unsigned short AREA_SLIME = 0x04; // 4
const unsigned short AREA_WATER = 0x08; // 8 (Surface)
const unsigned short AREA_UNDERWATER = 0x10; // 16 (Sea Floor)
const unsigned short AREA_ROAD = 0x20; // 32 (Roads

// --- CONFIGURATION ---
const float COLLISION_STEP_SIZE = 0.5f;    // Was 1.5f - much finer sampling
const float MIN_CLEARANCE = 2.0f;          // Was 1.0f - more safety margin
const float AGENT_RADIUS = 1.0f;           // Was 1.3f - larger safety bubble
const float WAYPOINT_STEP_SIZE = 4.0f;
const int MAX_POLYS = 512;
const float FLIGHT_GRID_SIZE = 25.0f;  // Optimized for FMap voxels
const float FLIGHT_MAX_SEARCH_RADIUS = 500.0f;
const int FLIGHT_MAX_ITERATIONS = 5000;
const float APPROACH_HEIGHT = 10.0f;
const float APPROACH_DISTANCE = 15.0f;
const float LANDING_AGENT_RADIUS = 2.0f;
const float FMAP_VERTICAL_TOLERANCE = 2.0f;  // Tolerance for floor snapping

// PATH CACHE LIMITS
const size_t MAX_CACHE_SIZE = 100;
const size_t CACHE_CLEANUP_THRESHOLD = 120;

const bool DEBUG_PATHFINDING = false;  // Enable for flight debugging

enum FlightSegmentResult {
    SEGMENT_VALID = 0,
    SEGMENT_COLLISION = 1,
    SEGMENT_NO_FLY_ZONE = 2
};

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

// --- 3D GRID HASH FUNCTION ---
struct GridKey {
    int x, y, z;

    GridKey(const Vector3& pos, float gridSize) {
        x = (int)std::floor(pos.x / gridSize);
        y = (int)std::floor(pos.y / gridSize);
        z = (int)std::floor(pos.z / gridSize);
    }

    bool operator==(const GridKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct GridKeyHash {
    size_t operator()(const GridKey& k) const {
        return ((size_t)k.x * 73856093) ^ ((size_t)k.y * 19349663) ^ ((size_t)k.z * 83492791);
    }
};

// --- LIGHTWEIGHT 3D A* NODE ---
struct FlightNode3D {
    Vector3 pos;
    float gScore;
    float hScore;
    int parentIdx;

    FlightNode3D() : gScore(1e9f), hScore(0.0f), parentIdx(-1) {}

    float fScore() const { return gScore + hScore; }
};

// Custom priority queue that uses indices
struct IndexPriorityQueue {
    std::vector<int> heap;
    const std::vector<FlightNode3D>* nodes;

    IndexPriorityQueue(const std::vector<FlightNode3D>* n) : nodes(n) {}

    void push(int idx) {
        heap.push_back(idx);
        std::push_heap(heap.begin(), heap.end(), [this](int a, int b) {
            return (*nodes)[a].fScore() > (*nodes)[b].fScore();
            });
    }

    int top() const { return heap.front(); }

    void pop() {
        std::pop_heap(heap.begin(), heap.end(), [this](int a, int b) {
            return (*nodes)[a].fScore() > (*nodes)[b].fScore();
            });
        heap.pop_back();
    }

    bool empty() const { return heap.empty(); }
    size_t size() const { return heap.size(); }
};

// --- LRU CACHE FOR PATHS ---
struct PathCacheKey {
    int sx, sy, sz, ex, ey, ez;
    bool flying;
    bool ignoreWater;

    PathCacheKey(const Vector3& start, const Vector3& end, bool fly, bool ignoreWater_ = false)
        : flying(fly), ignoreWater(ignoreWater_) {
        sx = (int)start.x; sy = (int)start.y; sz = (int)start.z;
        ex = (int)end.x; ey = (int)end.y; ez = (int)end.z;
    }

    bool operator<(const PathCacheKey& o) const {
        if (sx != o.sx) return sx < o.sx;
        if (sy != o.sy) return sy < o.sy;
        if (sz != o.sz) return sz < o.sz;
        if (ex != o.ex) return ex < o.ex;
        if (ey != o.ey) return ey < o.ey;
        if (ez != o.ez) return ez < o.ez;
        if (flying != o.flying) return flying < o.flying;
        return ignoreWater < o.ignoreWater;
    }
};

class PathCache {
private:
    std::map<PathCacheKey, std::vector<PathNode>> cache;
    std::list<PathCacheKey> lruList;
    std::map<PathCacheKey, std::list<PathCacheKey>::iterator> lruMap;

public:
    std::vector<PathNode>* Get(const PathCacheKey& key) {
        auto it = cache.find(key);
        if (it == cache.end()) return nullptr;

        lruList.erase(lruMap[key]);
        lruList.push_front(key);
        lruMap[key] = lruList.begin();

        return &it->second;
    }

    void Put(const PathCacheKey& key, const std::vector<PathNode>& path) {
        if (cache.size() >= CACHE_CLEANUP_THRESHOLD) {
            while (cache.size() >= MAX_CACHE_SIZE && !lruList.empty()) {
                PathCacheKey oldest = lruList.back();
                lruList.pop_back();
                lruMap.erase(oldest);
                cache.erase(oldest);
            }
        }

        cache[key] = path;
        lruList.push_front(key);
        lruMap[key] = lruList.begin();
    }

    void Clear() {
        cache.clear();
        lruList.clear();
        lruMap.clear();
    }

    size_t Size() const { return cache.size(); }
};

static PathCache globalPathCache;

// --- PATH CLEANING FUNCTION ---
inline void CleanGroundPath(std::vector<PathNode>& path, int mapId) {
    if (path.empty()) return;

    for (auto& node : path) {
        if (node.type == PATH_GROUND) {
            // Probe from slightly above (5.0f) to find the floor even if the waypoint is buried
            float probeZ = node.pos.z + 5.0f;
            float fmapZ = GetFMapFloorHeight(mapId, node.pos.x, node.pos.y, probeZ);

            // If a valid floor exists
            if (fmapZ > -90000.0f) {
                // If the path point is below the FMap floor (underground)
                if (node.pos.z < fmapZ) {
                    // Check if it's "close enough" to be a mesh error (e.g., within 15 units)
                    // This prevents snapping to a bridge far above if we are genuinely deep underground
                    if (fmapZ - node.pos.z < 15.0f) {
                        node.pos.z = fmapZ + 1.0f; // Snap to floor + 1.0f clearance
                    }
                }
                // Optional: Snap down if floating slightly (within 2 units)
                else if (node.pos.z > fmapZ && (node.pos.z - fmapZ) < 2.0f) {
                    node.pos.z = fmapZ + 1.0f;
                }
            }
        }
    }
}

class NavMesh {
public:
    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;
    int currentMapId = -1;
    std::set<std::pair<int, int>> loadedTiles; // Tracks loaded tile coordinates (x, y)

    NavMesh() { query = dtAllocNavMeshQuery(); }
    ~NavMesh() { dtFreeNavMesh(mesh); dtFreeNavMeshQuery(query); }

    void Clear() {
        dtFreeNavMesh(mesh); mesh = nullptr;
        dtFreeNavMeshQuery(query); query = nullptr;
        currentMapId = -1;
        globalPathCache.Clear();
    }

    // Use FMap for precise floor height
    float GetLocalGroundHeight(const Vector3& pos) {
        if (currentMapId < 0) return -99999.0f;

        // Try FMap first for precise voxel-based height
        float fmapHeight = GetFMapFloorHeight(currentMapId, pos.x, pos.y, pos.z);
        if (fmapHeight > -90000.0f) {
            return fmapHeight;
        }

        // Fallback to NavMesh if FMap unavailable
        if (!query || !mesh) return -99999.0f;
        float center[3] = { pos.y, pos.z, pos.x };
        float extent[3] = { 10.0f, 20.0f, 10.0f };
        dtPolyRef polyRef;
        float nearest[3];
        dtQueryFilter filter;

        if (dtStatusSucceed(query->findNearestPoly(center, extent, &filter, &polyRef, nearest))) {
            return nearest[1];
        }
        return -99999.0f;
    }

    // Enhanced flight point validation using FMap
    bool CheckFlightPoint(const Vector3& pos, int mapId, bool allowGroundProximity = false) {
        if (DEBUG_PATHFINDING) {
            //log << "[CheckFlightPoint] Testing " << mapId << " (" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
        }

        // First check if we're in flyable space using FMap
        bool flyable = CanFlyAt(mapId, pos.x, pos.y, pos.z);
        if (!flyable) {
            if (DEBUG_PATHFINDING) {
                //log << "  ? Not flyable according to FMap\n";
            }
            return false;
        }

        // Check for collision in a small sphere around the point
        if (CheckFMapLine(mapId, pos.x, pos.y, pos.z - 0.5f, pos.x, pos.y, pos.z + 0.5f)) {
            if (DEBUG_PATHFINDING) {
                //log << "  ? Vertical collision detected\n";
            }
            return false;
        }

        // Skip ground clearance check if allowed (for takeoff points)
        if (allowGroundProximity) {
            return true;
        }

        // Verify ground clearance
        float gZ = GetLocalGroundHeight(pos);
        if (gZ > -90000.0f && pos.z < gZ + MIN_CLEARANCE) {
            if (DEBUG_PATHFINDING) {
                //log << "  ? Too close to ground: floor=" << gZ << ", clearance=" << (pos.z - gZ) << "\n";
            }
            return false;
        }

        if (DEBUG_PATHFINDING) {
            //log << "  ? Valid flight point\n";
        }
        return true;
    }

    // Search for nearest valid flyable point if we hit a no-fly zone
    Vector3 FindNearestFlyablePoint(const Vector3& pos, int mapId) {
        // Try center first
        if (CanFlyAt(mapId, pos.x, pos.y, pos.z) && 
            !CheckFMapLine(mapId, pos.x, pos.y, pos.z - 0.2f, pos.x, pos.y, pos.z + 0.2f)) {
             return pos;
        }

        const float maxRadius = 30.0f;
        const float step = 2.5f;

        // Spiral out
        for (float r = step; r <= maxRadius; r += step) {
            // Check cardinal directions and vertical first (likely to be nearest exit)
            Vector3 offsets[] = {
                Vector3(0, 0, r), Vector3(0, 0, -r),               // Up/Down (Highest Priority for tunnels)
                Vector3(r, 0, 0), Vector3(-r, 0, 0),               // X
                Vector3(0, r, 0), Vector3(0, -r, 0),               // Y
                Vector3(r, r, 0), Vector3(-r, -r, 0),              // Diagonals
                Vector3(r, -r, 0), Vector3(-r, r, 0),
                Vector3(0, r, r), Vector3(0, -r, -r)               // Vertical Diagonals
            };

            for (const auto& off : offsets) {
                Vector3 test = pos + off;
                if (CanFlyAt(mapId, test.x, test.y, test.z)) {
                    // Double check it's not inside a wall (CheckFMapLine for tiny movement)
                    if (!CheckFMapLine(mapId, test.x, test.y, test.z - 0.2f, test.x, test.y, test.z + 0.2f)) {
                        return test;
                    }
                }
            }
        }
        return Vector3(0, 0, 0); // Failed
    }

    // FMap-based collision checking for flight segments
    FlightSegmentResult CheckFlightSegmentDetailed(const Vector3& start, const Vector3& end, int mapId, Vector3& outFailPos, bool isFlying, bool strict = true, bool verbose = false) {
        if (verbose && DEBUG_PATHFINDING) {
            g_LogFile << "   [CheckSegment] Checking " << start.x << "," << start.y << "," << start.z
                << " -> " << end.x << "," << end.y << "," << end.z << "\n";
        }

        Vector3 dir = end - start;
        float totalDist = dir.Length();
        if (totalDist < 0.1f) return SEGMENT_VALID;

        float startGroundZ = GetLocalGroundHeight(start);
        bool startingFromGround = !isFlying && (startGroundZ > -90000.0f && std::abs(start.z - startGroundZ) < 3.0f);

        if (verbose && DEBUG_PATHFINDING) {
            g_LogFile << "      Starting from ground: " << startingFromGround
                << " (playerZ=" << start.z << ", groundZ=" << startGroundZ << ")\n";
        }

        // Multi-ray collision check
        float r = AGENT_RADIUS;
        float diag = r * 0.7071f;

        int numRays = strict ? 9 : 1;
        Vector3 offsets[9] = {
            Vector3(0,0,0),
            Vector3(r,0,0), Vector3(-r,0,0), Vector3(0,r,0), Vector3(0,-r,0),
            Vector3(diag,diag,0), Vector3(-diag,-diag,0), Vector3(diag,-diag,0), Vector3(-diag,diag,0)
        };

        // If starting from ground, skip collision check for first 5 yards (takeoff)
        float skipCollisionDist = startingFromGround ? 5.0f : 0.0f;

        for (int i = 0; i < numRays; ++i) {
            Vector3 s = start + offsets[i];
            Vector3 e = end + offsets[i];

            // If we're taking off from ground, check collision only after clearing ground
            if (skipCollisionDist > 0.0f && totalDist > skipCollisionDist) {
                // Calculate where we clear the ground (5 yards from start)
                Vector3 clearancePoint = start + ((dir / totalDist) * skipCollisionDist);

                // Only check from clearance point onward
                if (CheckFMapLine(mapId, clearancePoint.x + offsets[i].x,
                    clearancePoint.y + offsets[i].y,
                    clearancePoint.z + offsets[i].z,
                    e.x, e.y, e.z)) {
                    if (verbose && DEBUG_PATHFINDING) {
                        g_LogFile << "      FAIL: Ray " << i << " hit obstacle after takeoff.\n";
                    }
                    return SEGMENT_COLLISION;
                }
            }
            // Normal check if not taking off or segment is too short
            else if (skipCollisionDist == 0.0f || totalDist <= skipCollisionDist) {
                if (CheckFMapLine(mapId, s.x, s.y, s.z, e.x, e.y, e.z)) {
                    if (verbose && DEBUG_PATHFINDING) {
                        g_LogFile << "      FAIL: Ray " << i << " hit obstacle (Wall/Tree/Building).\n";
                    }
                    return SEGMENT_COLLISION;
                }
            }
        }

        // Sample points along the path for ground clearance
        dir = dir / totalDist;
        int numSteps = (int)(totalDist / COLLISION_STEP_SIZE);
        if (numSteps < 1) numSteps = 1;

        for (int i = 1; i <= numSteps; ++i) {
            float distFromStart = (float)(i * COLLISION_STEP_SIZE);
            Vector3 pt = start + (dir * distFromStart);

            // Check if we're in flyable space
            if (!CanFlyAt(mapId, pt.x, pt.y, pt.z)) {
                outFailPos = pt;
                if (verbose && DEBUG_PATHFINDING) {
                    g_LogFile << "      FAIL: Point in No-Fly Zone at " << pt.x << "," << pt.y << "," << pt.z << "\n";
                }
                return SEGMENT_NO_FLY_ZONE; // Blocked by invisible wall/zone
            }

            float gZ = GetLocalGroundHeight(pt);
            if (gZ > -90000.0f) {
                // Allow lower clearance near start (Takeoff) and end (Landing)
                // If the player starts on the ground, strict clearance would fail immediately.
                bool isNearStart = (distFromStart < 8.0f);  // Increased from 5.0f
                bool isNearEnd = (distFromStart > totalDist - 8.0f);  // Increased from 5.0f

                // NEW: Very lenient for landing
                if (startingFromGround && isNearStart) {
                    if (pt.z < gZ - 0.5f) {
                        outFailPos = pt;
                        if (verbose && DEBUG_PATHFINDING) {
                            g_LogFile << "      FAIL: Below ground during takeoff (Z=" << pt.z << " < Ground=" << gZ << ")\n";
                        }
                        return SEGMENT_COLLISION;
                    }
                    continue;
                }

                // NEW: Very lenient clearance for landing approach
                if (isNearEnd && !strict) {
                    // Allow being very close to ground when landing
                    if (pt.z < gZ - 0.5f) { // Only fail if below ground
                        outFailPos = pt;
                        if (verbose && DEBUG_PATHFINDING) {
                            g_LogFile << "      FAIL: Below ground during landing (Z=" << pt.z << " < Ground=" << gZ << ")\n";
                        }
                        return SEGMENT_COLLISION;
                    }
                    continue; // Skip normal clearance check
                }

                // Normal clearance check for mid-flight
                float requiredClearance = (strict && !isNearStart && !isNearEnd) ? MIN_CLEARANCE : 0.5f;
                float limit = gZ + requiredClearance;

                if (pt.z < limit) {
                    outFailPos = pt;
                    if (verbose && DEBUG_PATHFINDING) {
                        g_LogFile << "      FAIL: Too close to ground (Z=" << pt.z << " < Limit=" << limit
                            << ") at dist " << distFromStart << "\n";
                    }
                    return SEGMENT_COLLISION;
                }
            }
        }
        if (verbose && DEBUG_PATHFINDING) g_LogFile << "      PASS: Segment Clear.\n";
        return SEGMENT_VALID;
    }

    // FMap-based collision checking for flight segments (Wrapper for backward compatibility)
    bool CheckFlightSegment(const Vector3& start, const Vector3& end, int mapId, bool isFlying, bool strict = true, bool verbose = false) {
        Vector3 dummy;
        return CheckFlightSegmentDetailed(start, end, mapId, dummy, isFlying, strict, verbose) == SEGMENT_VALID;
    }

    bool CheckApproachPath(const Vector3& start, const Vector3& end, int mapId) {
        Vector3 dir = end - start;
        float totalDist = dir.Length();
        if (totalDist < 0.1f) return true;

        dir = dir / totalDist;

        Vector3 offsets[5] = {
            Vector3(0, 0, 0),
            Vector3(AGENT_RADIUS, 0, 0),
            Vector3(-AGENT_RADIUS, 0, 0),
            Vector3(0, AGENT_RADIUS, 0),
            Vector3(0, -AGENT_RADIUS, 0)
        };

        float stepSize = 2.0f;
        int numSteps = (int)(totalDist / stepSize);
        if (numSteps < 2) numSteps = 2;

        for (int step = 0; step <= numSteps; ++step) {
            float t = (float)step / (float)numSteps;
            Vector3 checkPos = start + (dir * (totalDist * t));

            for (int i = 0; i < 5; ++i) {
                Vector3 testPos = checkPos + offsets[i];

                // Use FMap for collision detection
                if (CheckFMapLine(mapId, testPos.x, testPos.y, testPos.z - 0.3f,
                    testPos.x, testPos.y, testPos.z + 0.3f)) {
                    return false;
                }

                float gZ = GetLocalGroundHeight(testPos);
                if (gZ > -90000.0f && testPos.z < gZ + 0.5f) {
                    return false;
                }
            }
        }

        return true;
    }

    bool CheckLandingDescent(const Vector3& hoverPos, const Vector3& landPos, int mapId) {
        return CheckApproachPath(hoverPos, landPos, mapId);
    }
    
    void GetTileCoords(const Vector3& pos, int& tx, int& ty) {
        // Based on params.orig: {-17600, -1000, -17600} and tile size 533.33333
        // And the coordinate swap: Recast X = WoW Y, Recast Z = WoW X
        float tileWidth = 533.33333f;
        float originX = -17600.0f;
        float originZ = -17600.0f; // This corresponds to WoW X origin for Recast Z

        tx = (int)std::floor((pos.y - originX) / tileWidth);
        ty = (int)std::floor((pos.x - originZ) / tileWidth);
    }

    bool LoadMap(const std::string& directory, int mapId, const std::vector<Vector3>* path = nullptr, bool sparseLoad = true) {
        bool isNewMap = (currentMapId != mapId);

        // If map ID changed, we must clear everything
        if (isNewMap) {
            Clear();
        }
        // If map ID is the same, but we don't have a mesh, also clear/reinit
        else if (!mesh || !query) {
            Clear();
            isNewMap = true;
        }

        // Initialize Mesh object if needed
        if (isNewMap) {
            dtNavMeshParams params;
            memset(&params, 0, sizeof(params));
            params.orig[0] = -17600.0f;
            params.orig[1] = -1000.0f;
            params.orig[2] = -17600.0f;
            params.tileWidth = 533.33333f;
            params.tileHeight = 533.33333f;
            params.maxTiles = 1 << 21;
            params.maxPolys = 1U << 31;

            mesh = dtAllocNavMesh();
            if (dtStatusFailed(mesh->init(&params))) {
                dtFreeNavMesh(mesh);
                mesh = nullptr;
                return false;
            }

            query = dtAllocNavMeshQuery();
            if (!query) {
                dtFreeNavMesh(mesh);
                mesh = nullptr;
                return false;
            }

            currentMapId = mapId;
        }

        // Determine which tiles we need
        std::set<std::pair<int, int>> neededTiles;
        bool loadAll = !sparseLoad || (path == nullptr) || path->empty();

        if (!loadAll) {
            for (const auto& pt : *path) {
                int tx, ty;
                GetTileCoords(pt, tx, ty);

                // Add 3x3 grid around the point
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        neededTiles.insert({ tx + dx, ty + dy });
                    }
                }
            }
        }

        std::stringstream ss;
        ss << std::setw(4) << std::setfill('0') << mapId;
        std::string prefix = ss.str();

        int tilesLoadedCount = 0;

        // SAFETY CHECK: Ensure directory exists before iterating
        if (!std::filesystem::exists(directory)) return false;

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().filename().string().find(prefix) == 0 &&
                entry.path().extension() == ".mmtile") {

                // If we are loading all, just add it if not loaded (though 'loadAll' usually implies fresh start,
                // we check loadedTiles to support incremental full load if ever needed)
                // However, optimization: if loading ALL and isNewMap, we skip the peek check for speed? 
                // No, sticking to robust check.

                // If sparse loading, we must check if this file matches our needed coordinates.
                // We peek the header to get coordinates without loading the whole file.
                if (!loadAll) {
                    std::ifstream file(entry.path().string(), std::ios::binary);
                    if (file.is_open()) {
                        MmapTileHeader mmapHeader;
                        file.read((char*)&mmapHeader, sizeof(MmapTileHeader));
                        dtMeshHeader dtHeader;
                        file.read((char*)&dtHeader, sizeof(dtMeshHeader));
                        file.close();

                        // Check if this tile is needed
                        if (neededTiles.count({ dtHeader.x, dtHeader.y })) {
                            // Only load if not already loaded
                            if (loadedTiles.find({ dtHeader.x, dtHeader.y }) == loadedTiles.end()) {
                                if (AddTile(entry.path().string())) {
                                    loadedTiles.insert({ dtHeader.x, dtHeader.y });
                                    tilesLoadedCount++;
                                }
                            }
                        }
                    }
                }
                else {
                    // Load ALL mode: We still check loadedTiles to avoid duplicates if called incrementally
                    // (But usually LoadAll is done on fresh map).
                    // We can't easily know coords without peeking, so we just try AddTile.
                    // Actually, AddTile duplicates might be rejected by Detour, but let's be safe.
                    // To be strictly correct with 'loadedTiles' set maintenance, we should peek.
                    // But for 'LoadAll' performance, we might skip peeking if we know it's a fresh map.

                    if (isNewMap) {
                        // Just load everything
                        if (AddTile(entry.path().string())) {
                            // Ideally we'd update loadedTiles, but peeking every file is slow?
                            // User requirement implies sparse loading is the optimization.
                            // We will just let AddTile run. We won't maintain loadedTiles accurately for LoadAll 
                            // because we don't want to peek 1000 files.
                            // BUT: If we switch from LoadAll -> Sparse, loadedTiles will be empty.
                            // This suggests Sparse Logic assumes it knows what's loaded.
                            // If we want to support mixed usage, we should maintain loadedTiles.
                            // Let's take the hit and peek, or rely on Detour rejection.
                            // Simpler: If loadAll, we clear loadedTiles (done in Clear) and just don't track them?
                            // No, user said "Keep the option...".
                            // Let's implement the loop with peeking to keep state consistent.

                            std::ifstream file(entry.path().string(), std::ios::binary);
                            if (file.is_open()) {
                                MmapTileHeader mmapHeader;
                                file.read((char*)&mmapHeader, sizeof(MmapTileHeader));
                                dtMeshHeader dtHeader;
                                file.read((char*)&dtHeader, sizeof(dtMeshHeader));
                                file.close();

                                if (AddTile(entry.path().string())) {
                                    loadedTiles.insert({ dtHeader.x, dtHeader.y });
                                }
                            }
                        }
                    }
                    else {
                        // Incremental Load All? Unusual case. Just assume checking.
                        // Implementation for simplicity: Just peek and load if missing.
                        std::ifstream file(entry.path().string(), std::ios::binary);
                        if (file.is_open()) {
                            MmapTileHeader mmapHeader;
                            file.read((char*)&mmapHeader, sizeof(MmapTileHeader));
                            dtMeshHeader dtHeader;
                            file.read((char*)&dtHeader, sizeof(dtMeshHeader));
                            file.close();

                            if (loadedTiles.find({ dtHeader.x, dtHeader.y }) == loadedTiles.end()) {
                                if (AddTile(entry.path().string())) {
                                    loadedTiles.insert({ dtHeader.x, dtHeader.y });
                                }
                            }
                        }
                    }
                }
            }
        }

        // If we loaded new tiles or reset the map, we need to init the query
        // dtNavMeshQuery::init can be called safely to reset/update
        if (dtStatusFailed(query->init(mesh, 2048))) return false;
        currentMapId = mapId;
        return true;
    }

    bool AddTile(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) return false;

        MmapTileHeader header;
        file.read((char*)&header, sizeof(MmapTileHeader));
        unsigned char* data = (unsigned char*)dtAlloc(header.size, DT_ALLOC_PERM);
        file.read((char*)data, header.size);
        dtMeshHeader* meshHeader = (dtMeshHeader*)data;
        int tileIndex = meshHeader->x + meshHeader->y * 64;
        dtTileRef tileRefHint = ((dtTileRef)tileIndex) << 20;
        dtTileRef tileRef = 0;
        mesh->addTile(data, header.size, DT_TILE_FREE_DATA, tileRefHint, &tileRef);
        return true;
    }

    std::vector<PathNode> SubdivideOnMesh(const std::vector<PathNode>& input) {
        if (input.empty()) return {};
        if (!query || !mesh) return input;

        std::vector<PathNode> output;
        output.push_back(input[0]);
        dtQueryFilter filter;
        filter.setIncludeFlags(0xFFFF);
        filter.setExcludeFlags(0);

        for (size_t i = 0; i < input.size() - 1; ++i) {
            Vector3 start = input[i].pos;
            Vector3 end = input[i + 1].pos;
            float dist = start.Dist3D(end);
            if (dist < 0.1f) continue;

            int steps = (int)(dist / WAYPOINT_STEP_SIZE);
            if (steps == 0) {
                output.push_back(input[i + 1]);
                continue;
            }

            Vector3 dir = (end - start).Normalize();
            dtPolyRef startPoly;
            float startPt[3] = { start.y, start.z, start.x };
            float extent[3] = { 5.0f, 10.0f, 5.0f };
            query->findNearestPoly(startPt, extent, &filter, &startPoly, startPt);

            for (int j = 1; j <= steps; ++j) {
                Vector3 target = start + (dir * (float)(j * WAYPOINT_STEP_SIZE));
                float destPt[3] = { target.y, target.z, target.x };
                float resultPt[3];
                dtPolyRef visited[16];
                int visitedCount = 0;
                query->moveAlongSurface(startPoly, startPt, destPt, &filter, resultPt,
                    visited, &visitedCount, 16);

                Vector3 safePoint(resultPt[2], resultPt[0], resultPt[1]);

                // Snap to FMap floor for precision
                float fmapFloor = GetFMapFloorHeight(currentMapId, safePoint.x, safePoint.y, safePoint.z);
                if (fmapFloor > -90000.0f && std::abs(safePoint.z - fmapFloor) < FMAP_VERTICAL_TOLERANCE) {
                    safePoint.z = fmapFloor;
                }

                // Subdivided points on mesh are GROUND points
                output.push_back(PathNode(safePoint, PATH_GROUND));

                if (visitedCount > 0) startPoly = visited[visitedCount - 1];
                dtVcopy(startPt, resultPt);
            }
            if (output.back().pos.Dist3D(end) > 0.5f) output.push_back(input[i + 1]);
        }
        return output;
    }

    // --- GET AREA ID AT COORDINATE ---
    // Returns the Area ID (Ground=1, Water=8, Underwater=16, etc.) at the given position.
    // Returns 0 if no navmesh is found there.
    unsigned char GetAreaID(const Vector3& pos, float searchRadius = 2.0f, float heightRange = 10.0f) {
        if (!query || !mesh) return 0;

        float center[3] = { pos.y, pos.z, pos.x }; // WoW -> Recast coords
        float extent[3] = { searchRadius, heightRange, searchRadius };

        dtQueryFilter filter;
        filter.setIncludeFlags(0xFFFF); // Search ALL polygons (Ground, Water, everything)
        filter.setExcludeFlags(0);

        dtPolyRef ref;
        float nearest[3];

        // Find the nearest polygon to the point
        if (dtStatusSucceed(query->findNearestPoly(center, extent, &filter, &ref, nearest))) {
            if (ref) {
                const dtMeshTile* tile = 0;
                const dtPoly* poly = 0;

                // Get the polygon data to read the Area ID
                if (dtStatusSucceed(mesh->getTileAndPolyByRef(ref, &tile, &poly))) {
                    return poly->getArea();
                }
            }
        }
        return 0; // NAV_EMPTY
    }
};

static NavMesh globalNavMesh;

inline std::vector<PathNode> FindPath(const Vector3& start, const Vector3& end, bool ignoreWater) {
    if (!globalNavMesh.query || !globalNavMesh.mesh) return {};

    dtQueryFilter filter;

    // --- UPDATED FILTERING LOGIC ---
    // Start with default walkable areas (Ground, Magma, Slime, Road)
    unsigned short includeFlags = AREA_GROUND | AREA_MAGMA | AREA_SLIME | AREA_ROAD;

    // If ignoring water is disabled, we add Water (Swimming) and Underwater (Sea Floor)
    if (!ignoreWater) {
        includeFlags |= AREA_WATER;      // 0x08
        includeFlags |= AREA_UNDERWATER; // 0x10
    }

    filter.setIncludeFlags(includeFlags);
    filter.setExcludeFlags(0);

    dtPolyRef startRef = 0, endRef = 0;
    float startPt[3], endPt[3];
    float detourStart[3] = { start.y, start.z, start.x };
    float detourEnd[3] = { end.y, end.z, end.x };
    float extent[3] = { 500.0f, 1000.0f, 500.0f };

    globalNavMesh.query->findNearestPoly(detourStart, extent, &filter, &startRef, startPt);
    globalNavMesh.query->findNearestPoly(detourEnd, extent, &filter, &endRef, endPt);

    if (!startRef || !endRef) return {};

    dtPolyRef pathPolys[MAX_POLYS];
    int pathCount = 0;
    globalNavMesh.query->findPath(startRef, endRef, startPt, endPt, &filter,
        pathPolys, &pathCount, MAX_POLYS);

    if (pathCount <= 0) return {};

    float straightPath[MAX_POLYS * 3];
    int straightPathCount = 0;
    globalNavMesh.query->findStraightPath(startPt, endPt, pathPolys, pathCount,
        straightPath, 0, 0, &straightPathCount, MAX_POLYS);

    std::vector<PathNode> result;
    for (int i = 0; i < straightPathCount; ++i) {
        result.push_back(PathNode(
            Vector3(straightPath[i * 3 + 2], straightPath[i * 3 + 0], straightPath[i * 3 + 1]),
            PATH_GROUND // NavMesh paths are always GROUND
        ));
    }
    return result;
}

// ANGLED FLIGHT PATHFINDING - Natural diagonal ascent/descent
inline std::vector<PathNode> Calculate3DFlightPath(const Vector3& start, const Vector3& end, int mapId, bool isFlying) {
    std::ofstream g_LogFile;
    // Always open log for this specific debug request, or keep using DEBUG_PATHFINDING flag
    if (DEBUG_PATHFINDING) {
        g_LogFile.open("C:\\Driver\\SMM_Debug.log", std::ios::app);
        g_LogFile << "\n=== CALCULATE 3D FLIGHT PATH ===\n";
        g_LogFile << "Start: (" << start.x << ", " << start.y << ", " << start.z << ")\n";
        g_LogFile << "End:   (" << end.x << ", " << end.y << ", " << end.z << ")\n";
    }

    float GROUND_HEIGHT_THRESHOLD = 15.0f;

    // 1. VALIDATE END POSITION
    Vector3 actualStart = start;
    Vector3 actualEnd = end;
    float groundZ = globalNavMesh.GetLocalGroundHeight(start);

    if (groundZ > -90000.0f && start.z < groundZ + MIN_CLEARANCE) {
        actualStart.z = groundZ + MIN_CLEARANCE + 1.0f;
        if (DEBUG_PATHFINDING) {
            g_LogFile << "Adjusted start height to: " << actualStart.z << " (ground: " << groundZ << ")\n";
        }
    }

    groundZ = globalNavMesh.GetLocalGroundHeight(end);
    if (groundZ > -90000.0f && end.z < groundZ + MIN_CLEARANCE) {
        actualEnd.z = groundZ + MIN_CLEARANCE + 1.0f;
        if (DEBUG_PATHFINDING) {
            g_LogFile << "Adjusted end height to: " << actualEnd.z << " (ground: " << groundZ << ")\n";
        }
    }

    // If the goal is on the ground and we're descending to it, be more lenient
    bool isDescendingToGround = false;
    if (groundZ > -90000.0f && end.z < groundZ + GROUND_HEIGHT_THRESHOLD) {
        isDescendingToGround = true;
        // Adjust end to be just slightly above ground instead of MIN_CLEARANCE
        actualEnd.z = groundZ + 1.0f; // Changed from MIN_CLEARANCE + 1.0f
        if (DEBUG_PATHFINDING) {
            g_LogFile << "Goal is on ground, adjusted end height to: " << actualEnd.z
                << " (ground: " << groundZ << ")\n";
        }
    }

    // 2. TRY DIRECT PATH FIRST (Optimization)
    if (DEBUG_PATHFINDING) g_LogFile << "Checking direct path...\n";

    // REPLACED: Use Detailed check to handle No-Fly Zones gracefully
    // Pass isFlying flag to collision check
    Vector3 failPos;
    FlightSegmentResult directCheck = globalNavMesh.CheckFlightSegmentDetailed(
        actualStart, actualEnd, mapId, failPos, isFlying, true, true);

    if (directCheck == SEGMENT_VALID) {
        if (DEBUG_PATHFINDING) {
            g_LogFile << "✓ Direct path clear!\n";
        }
        return { PathNode(actualStart, PATH_AIR), PathNode(actualEnd, PATH_AIR) };
    }
    else if (directCheck == SEGMENT_NO_FLY_ZONE) {
        // --- SMART RECOVERY FOR NO-FLY ZONES ---
        if (DEBUG_PATHFINDING) {
            g_LogFile << "⚠ Direct path hit No-Fly Zone at (" << failPos.x << ", " << failPos.y << ", " << failPos.z << ")\n";
            g_LogFile << "  Attempting to find nearest valid flyable point...\n";
        }

        Vector3 detourPoint = globalNavMesh.FindNearestFlyablePoint(failPos, mapId);

        // If we found a valid detour point (not 0,0,0)
        if (detourPoint.x != 0 || detourPoint.y != 0 || detourPoint.z != 0) {
            if (DEBUG_PATHFINDING) {
                g_LogFile << "  Found detour point: (" << detourPoint.x << ", " << detourPoint.y << ", " << detourPoint.z << ")\n";
                g_LogFile << "  Validating legs: Start->Detour and Detour->End...\n";
            }

            // Validate the legs
            if (globalNavMesh.CheckFlightSegment(actualStart, detourPoint, mapId, isFlying, true, true) &&
                globalNavMesh.CheckFlightSegment(detourPoint, actualEnd, mapId, isFlying, true, true)) {

                if (DEBUG_PATHFINDING) {
                    g_LogFile << "✓ Smart Recovery Successful! Created detour path.\n";
                }
                return { PathNode(actualStart, PATH_AIR), PathNode(detourPoint, PATH_AIR), PathNode(actualEnd, PATH_AIR) };
            }
            else {
                // --- NEW LOGIC: DETOUR LEGS BLOCKED (CHECK END POINT) ---
                if (DEBUG_PATHFINDING) {
                    g_LogFile << "  ✗ Detour legs were blocked. Checking if End Point is the issue...\n";
                }

                // Check if the destination itself is inside a No-Fly Zone
                if (!CanFlyAt(mapId, actualEnd.x, actualEnd.y, actualEnd.z)) {
                    if (DEBUG_PATHFINDING) {
                        g_LogFile << "  ! END POINT is in a No-Fly Zone. Searching for nearest safe destination...\n";
                    }

                    Vector3 safeEnd = globalNavMesh.FindNearestFlyablePoint(actualEnd, mapId);

                    // If we found a safe replacement for the destination
                    if (safeEnd.x != 0 || safeEnd.y != 0 || safeEnd.z != 0) {
                        if (DEBUG_PATHFINDING) {
                            g_LogFile << "  Found safe destination: (" << safeEnd.x << ", " << safeEnd.y << ", " << safeEnd.z << ")\n";
                            g_LogFile << "  Retrying with safe destination...\n";
                        }

                        // Retry the path: Start -> Detour -> SafeEnd
                        if (globalNavMesh.CheckFlightSegment(actualStart, detourPoint, mapId, isFlying, true, true) &&
                            globalNavMesh.CheckFlightSegment(detourPoint, safeEnd, mapId, isFlying, true, true)) {

                            if (DEBUG_PATHFINDING) {
                                g_LogFile << "✓ Smart Recovery (Modified End) Successful!\n";
                            }
                            return { PathNode(actualStart, PATH_AIR), PathNode(detourPoint, PATH_AIR), PathNode(safeEnd, PATH_AIR) };
                        }
                    }
                }
            }
        }
        else {
            if (DEBUG_PATHFINDING) {
                g_LogFile << "  ✗ Could not find a valid flyable point nearby.\n";
            }
        }
    }

    // 4. A* SEARCH WITH RETRY LOGIC (Relaxing Conditions)
    std::vector<PathNode> finalPath;

    // Attempt 1-3: Strict Collision
    // Attempt 4-5: Relaxed Collision
    for (int attempt = 1; attempt <= 5; ++attempt) {
        if (DEBUG_PATHFINDING) {
            g_LogFile << ">>> A* Attempt " << attempt << " / 5 <<<\n";
        }

        // --- RELAXATION PARAMETERS ---
        // Relax collision on later attempts
        bool strictCollision = (attempt <= 3);

        // Vary grid size: Standard (15) -> Coarse (25) -> Standard -> Coarse -> Coarse
        float currentGridSize = (attempt % 2 == 0) ? 15.0f : 25.0f;

        // Increase node limit progressively
        int maxNodes = 200000 + ((attempt - 1) * 100000);

        // Increase search radius on last attempts
        Vector3 midpoint = (actualStart + actualEnd) * 0.5f;
        float halfDist = actualStart.Dist3D(actualEnd) * 0.5f;

        // NEW: Increase search radius more aggressively
        //float currentSearchRadius = (std::min)(FLIGHT_MAX_SEARCH_RADIUS * 1.5f, halfDist + 300.0f); // Increased from 150.0f
        float currentSearchRadius = halfDist + 200.0f;

        if (DEBUG_PATHFINDING) {
            g_LogFile << "   Grid: " << currentGridSize << " | Strict: " << strictCollision
                << " | MaxNodes: " << maxNodes << " | Radius: " << currentSearchRadius << "\n";
        }

        // --- A* SETUP ---
        std::vector<FlightNode3D> nodes;
        nodes.reserve(maxNodes);
        std::unordered_map<GridKey, int, GridKeyHash> gridToIndex;
        std::unordered_set<int> closedSet;
        IndexPriorityQueue openSet(&nodes);

        // Helper to track failures for logging (don't log every single one, just summary/examples)
        int invalidPoints = 0;
        int blockedSegments = 0;

        auto GetOrCreateNode = [&](const Vector3& pos) -> int {
            GridKey key(pos, currentGridSize);
            auto it = gridToIndex.find(key);
            if (it != gridToIndex.end()) return it->second;

            Vector3 gridCenter(
                (key.x + 0.5f) * currentGridSize,
                (key.y + 0.5f) * currentGridSize,
                (key.z + 0.5f) * currentGridSize
            );

            // Check if this is near the start position (within 10 yards)
            bool isNearStart = gridCenter.Dist3D(actualStart) < 10.0f;

            // More lenient check for positions near start
            if (isNearStart) {
                // Only check if it's in a no-fly zone, don't check ground clearance
                if (!CanFlyAt(mapId, gridCenter.x, gridCenter.y, gridCenter.z)) {
                    invalidPoints++;
                    return -1;
                }
            }
            else {
                // Normal check for other positions
                if (!globalNavMesh.CheckFlightPoint(gridCenter, mapId)) {
                    invalidPoints++;
                    return -1;
                }
            }

            size_t idx = nodes.size();
            nodes.emplace_back();
            nodes[idx].pos = gridCenter;
            nodes[idx].hScore = gridCenter.Dist3D(actualEnd);
            gridToIndex[key] = idx;
            return idx;
            };

        // Create start node
        size_t startIdx = nodes.size();
        nodes.emplace_back();
        nodes[startIdx].pos = actualStart;
        nodes[startIdx].gScore = 0.0f;
        nodes[startIdx].hScore = actualStart.Dist3D(actualEnd);

        // Add start node to grid map
        GridKey startKey(actualStart, currentGridSize);
        gridToIndex[startKey] = startIdx;

        // Create end node
        size_t endIdx = nodes.size();
        nodes.emplace_back();
        nodes[endIdx].pos = actualEnd;
        nodes[endIdx].hScore = 0.0f; // At goal, h=0

        // CRITICAL FIX: Validate the end position before adding to grid
        // If the end position itself is invalid, we need to know NOW
        bool endIsValid = true;

        // Check if end is in a no-fly zone
        if (!CanFlyAt(mapId, actualEnd.x, actualEnd.y, actualEnd.z)) {
            if (DEBUG_PATHFINDING) {
                g_LogFile << "⚠ WARNING: Goal position is in a no-fly zone!\n";
                g_LogFile << "  Searching for nearest valid position...\n";
            }

            Vector3 nearestValid = globalNavMesh.FindNearestFlyablePoint(actualEnd, mapId);
            if (nearestValid.x != 0 || nearestValid.y != 0 || nearestValid.z != 0) {
                actualEnd = nearestValid;
                nodes[endIdx].pos = actualEnd;
                if (DEBUG_PATHFINDING) {
                    g_LogFile << "  Adjusted goal to: (" << actualEnd.x << "," << actualEnd.y << "," << actualEnd.z << ")\n";
                }
            }
            else {
                endIsValid = false;
                if (DEBUG_PATHFINDING) {
                    g_LogFile << "  ✗ Could not find valid goal position!\n";
                }
            }
        }

        // Check for collision at end point
        if (endIsValid && CheckFMapLine(mapId, actualEnd.x, actualEnd.y, actualEnd.z - 0.5f,
            actualEnd.x, actualEnd.y, actualEnd.z + 0.5f)) {
            if (DEBUG_PATHFINDING) {
                g_LogFile << "⚠ WARNING: Goal position has collision!\n";
            }
            endIsValid = false;
        }

        // Only add end node to grid if it's valid
        if (endIsValid) {
            GridKey endKey(actualEnd, currentGridSize);
            gridToIndex[endKey] = endIdx;
        }
        else {
            if (DEBUG_PATHFINDING) {
                g_LogFile << "⚠ End node marked as invalid - will try to get as close as possible\n";
            }
        }

        openSet.push(startIdx);

        midpoint = (actualStart + actualEnd) * 0.5f;
        int goalIdx = -1;
        int iterations = 0;

        // PRIORITIZE DIAGONAL MOVEMENT (horizontal + vertical combined)
        // Order: diagonals with vertical, then pure horizontal/vertical
        const int neighborDirs[][3] = {
            // Diagonal ascent (PRIORITIZED - move toward target while gaining height)
            {1,0,1}, {-1,0,1}, {0,1,1}, {0,-1,1},
            {1,1,1}, {1,-1,1}, {-1,1,1}, {-1,-1,1},
            // Horizontal movement
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0},
            {1,1,0}, {1,-1,0}, {-1,1,0}, {-1,-1,0},
            // Pure vertical (DEPRIORITIZED)
            {0,0,1}, {0,0,-1},
            // Diagonal descent
            {1,0,-1}, {-1,0,-1}, {0,1,-1}, {0,-1,-1}
        };

        while (!openSet.empty() && iterations < maxNodes * 2) {
            if (nodes.size() >= maxNodes) {
                if (DEBUG_PATHFINDING) g_LogFile << "   ! Max nodes reached (" << maxNodes << ")\n";
                break;
            }

            int currentIdx = openSet.top();
            openSet.pop();

            if (closedSet.count(currentIdx)) continue;
            closedSet.insert(currentIdx);
            iterations++;

            FlightNode3D& current = nodes[currentIdx];

            // CRITICAL FIX 1: Check if we ARE the goal node
            if (currentIdx == endIdx) {
                goalIdx = endIdx;
                if (DEBUG_PATHFINDING) {
                    g_LogFile << "   ✓ Reached goal node (was in open set)!\n";
                }
                break;
            }
            // CRITICAL FIX 2: Check distance in 3D, not just grid distance
            float distToGoal = current.pos.Dist3D(actualEnd);

            // If we're very close to goal (within 5 yards), try to connect
            if (distToGoal < 5.0f) {
                if (DEBUG_PATHFINDING) {
                    g_LogFile << "   Attempting final connection from (" << current.pos.x << ","
                        << current.pos.y << "," << current.pos.z << ") to goal (dist="
                        << distToGoal << ")\n";
                }

                // Use NON-STRICT check for final connection
                Vector3 failPos;
                FlightSegmentResult result = globalNavMesh.CheckFlightSegmentDetailed(
                    current.pos, actualEnd, mapId, failPos, isFlying,
                    false,  // strict = FALSE for final approach
                    DEBUG_PATHFINDING); // verbose for debugging

                if (result == SEGMENT_VALID) {
                    nodes[endIdx].parentIdx = currentIdx;
                    nodes[endIdx].gScore = current.gScore + distToGoal;
                    goalIdx = endIdx;
                    if (DEBUG_PATHFINDING) {
                        g_LogFile << "   ✓ Successfully connected to goal!\n";
                    }
                    break;
                }
                else {
                    if (DEBUG_PATHFINDING) {
                        g_LogFile << "   ✗ Final connection blocked - Reason: "
                            << (result == SEGMENT_COLLISION ? "Collision" : "No-Fly Zone")
                            << " at (" << failPos.x << "," << failPos.y << "," << failPos.z << ")\n";
                    }
                }
            }

            for (int i = 0; i < 22; ++i) {
                Vector3 neighborPos = current.pos + Vector3(
                    neighborDirs[i][0] * currentGridSize,
                    neighborDirs[i][1] * currentGridSize,
                    neighborDirs[i][2] * currentGridSize
                );

                float distFromLine = (neighborPos - midpoint).Length();
                if (distFromLine > currentSearchRadius) continue;

                int neighborIdx = GetOrCreateNode(neighborPos);
                if (neighborIdx < 0) continue;
                if (closedSet.count(neighborIdx)) continue;

                // CRITICAL FIX 3: Special handling if neighbor IS the goal
                bool isGoalNeighbor = (neighborIdx == endIdx);

                // Check segment (use non-strict if connecting to goal)
                if (!globalNavMesh.CheckFlightSegment(current.pos, nodes[neighborIdx].pos, mapId,
                    isFlying, !isGoalNeighbor, false)) {
                    blockedSegments++;
                    continue;
                }

                float dist = current.pos.Dist3D(nodes[neighborIdx].pos);
                float bonusWeight = 1.0f;
                if (neighborDirs[i][2] > 0 && (neighborDirs[i][0] != 0 || neighborDirs[i][1] != 0)) {
                    bonusWeight = 0.9f;
                }

                float tentativeG = current.gScore + (dist * bonusWeight);

                if (tentativeG < nodes[neighborIdx].gScore) {
                    nodes[neighborIdx].parentIdx = currentIdx;
                    nodes[neighborIdx].gScore = tentativeG;
                    openSet.push(neighborIdx);
                }
            }
        }

        //DIAGNOSTICS
        if (goalIdx < 0 && DEBUG_PATHFINDING) {
            g_LogFile << "\n   [DIAGNOSTIC] Why did A* fail?\n";

            // Check if end node exists
            if (endIdx < nodes.size()) {
                g_LogFile << "   - End node exists at index " << endIdx << "\n";
                g_LogFile << "   - End node position: (" << nodes[endIdx].pos.x << ","
                    << nodes[endIdx].pos.y << "," << nodes[endIdx].pos.z << ")\n";
                g_LogFile << "   - End node gScore: " << nodes[endIdx].gScore << "\n";
                g_LogFile << "   - End node parent: " << nodes[endIdx].parentIdx << "\n";

                // Check if it was ever added to open set
                bool inClosed = closedSet.count(endIdx);
                g_LogFile << "   - End node in closed set: " << inClosed << "\n";

                // Find closest node that WAS explored
                float closestDist = 1e9f;
                int closestIdx = -1;
                for (int idx : closedSet) {
                    float d = nodes[idx].pos.Dist3D(actualEnd);
                    if (d < closestDist) {
                        closestDist = d;
                        closestIdx = idx;
                    }
                }

                if (closestIdx >= 0) {
                    g_LogFile << "   - Closest explored node: (" << nodes[closestIdx].pos.x << ","
                        << nodes[closestIdx].pos.y << "," << nodes[closestIdx].pos.z << ")\n";
                    g_LogFile << "   - Distance from closest to goal: " << closestDist << "\n";

                    // Try to connect them
                    Vector3 failPos;
                    FlightSegmentResult testResult = globalNavMesh.CheckFlightSegmentDetailed(
                        nodes[closestIdx].pos, actualEnd, mapId, failPos, isFlying, false, true);

                    g_LogFile << "   - Can connect closest to goal: "
                        << (testResult == SEGMENT_VALID ? "YES" : "NO") << "\n";
                    if (testResult != SEGMENT_VALID) {
                        g_LogFile << "   - Blockage at: (" << failPos.x << "," << failPos.y << "," << failPos.z << ")\n";
                    }
                }
            }
            g_LogFile << "\n";
        }

        // --- RESULT CHECK ---
        if (goalIdx >= 0) {
            // ... (Reconstruct and Return Success as before) ...
            int currIdx = goalIdx;
            while (currIdx >= 0) {
                finalPath.push_back(PathNode(nodes[currIdx].pos, PATH_AIR));
                currIdx = nodes[currIdx].parentIdx;
            }
            std::reverse(finalPath.begin(), finalPath.end());

            // Smoothing
            if (finalPath.size() > 2) {
                std::vector<PathNode> smoothed;
                smoothed.push_back(finalPath[0]);
                size_t current = 0;
                while (current < finalPath.size() - 1) {
                    size_t farthest = current + 1;
                    for (size_t next = finalPath.size() - 1; next > current + 1; --next) {
                        if (globalNavMesh.CheckFlightSegment(finalPath[current].pos, finalPath[next].pos, mapId, isFlying, strictCollision)) {
                            farthest = next;
                            break;
                        }
                    }
                    smoothed.push_back(finalPath[farthest]);
                    current = farthest;
                }
                finalPath = smoothed;
            }

            if (DEBUG_PATHFINDING) {
                g_LogFile << "✓ A* SUCCESS on Attempt " << attempt << " (" << finalPath.size() << " wps)\n";
            }
            return finalPath; // RETURN SUCCESS
        }
        else {
            if (DEBUG_PATHFINDING) {
                g_LogFile << "✗ A* Failed Attempt " << attempt << ".\n";
                g_LogFile << "   Nodes Exp: " << closedSet.size() << " | Nodes Created: " << nodes.size() << "\n";
                g_LogFile << "   Invalid Points (Wall/Ground): " << invalidPoints << "\n";
                g_LogFile << "   Blocked Segments: " << blockedSegments << "\n";

                // If this was the last attempt, log nearest node
                if (attempt == 5) {
                    float closest = 1e9f;
                    Vector3 closestPos;
                    for (const auto& node : nodes) {
                        float d = node.pos.Dist3D(actualEnd);
                        if (d < closest) { closest = d; closestPos = node.pos; }
                    }
                    g_LogFile << "   Closest approach to goal: " << closest << " yds at ("
                        << closestPos.x << "," << closestPos.y << "," << closestPos.z << ")\n";
                }
            }
        }
    }

    // 5. ALL ATTEMPTS FAILED
    if (DEBUG_PATHFINDING) {
        g_LogFile << "!!! CRITICAL: All 5 pathfinding attempts failed. Stopping script.\n";
    }

    return {};
}

inline std::vector<PathNode> SubdivideFlightPath(const std::vector<PathNode>& input, int mapId) {
    if (input.empty()) return {};
    std::vector<PathNode> output;
    output.reserve(input.size() * 4);
    output.push_back(input[0]);

    for (size_t i = 0; i < input.size() - 1; ++i) {
        Vector3 start = input[i].pos;
        Vector3 end = input[i + 1].pos;

        // Preserve type from the starting node of the segment
        int segmentType = input[i].type;

        float dist3D = start.Dist3D(end);

        bool isFinalApproach = (i == input.size() - 2);
        float stepSize = isFinalApproach ? (WAYPOINT_STEP_SIZE * 0.4f) : WAYPOINT_STEP_SIZE;

        int count = (int)(dist3D / stepSize);
        if (count < 1) count = 1;

        Vector3 dir = (end - start) / (float)count;

        for (int j = 1; j <= count; ++j) {
            Vector3 pt = start + (dir * (float)j);

            if (!isFinalApproach && segmentType == PATH_AIR) {
                // Use FMap for precise floor snapping
                float gZ = GetFMapFloorHeight(mapId, pt.x, pt.y, pt.z);
                if (gZ > -90000.0f && pt.z < gZ + MIN_CLEARANCE) {
                    pt.z = gZ + MIN_CLEARANCE + 0.5f;
                }
            }

            output.push_back(PathNode(pt, segmentType));
        }
    }
    return output;
}

// Detect if a line segment passes through a no-fly zone (tunnel/indoor)
inline bool DetectNoFlyZone(const Vector3& start, const Vector3& end, int mapId,
    Vector3& entryPoint, Vector3& exitPoint) {
    Vector3 dir = end - start;
    float dist = dir.Length();
    if (dist < 0.1f) return false;

    dir = dir / dist;

    // Sample points along the line
    const int NUM_SAMPLES = 20;
    bool foundNoFly = false;
    float entryDist = -1.0f;
    float exitDist = -1.0f;

    for (int i = 0; i <= NUM_SAMPLES; ++i) {
        float t = (float)i / (float)NUM_SAMPLES;
        Vector3 testPos = start + (dir * (dist * t));

        bool canFly = CanFlyAt(mapId, testPos.x, testPos.y, testPos.z);

        if (!canFly && !foundNoFly) {
            // Found entry to no-fly zone
            foundNoFly = true;
            entryDist = dist * t;
            entryPoint = testPos;
        }
        else if (canFly && foundNoFly && exitDist < 0.0f) {
            // Found exit from no-fly zone
            exitDist = dist * t;
            exitPoint = testPos;
            return true; // Found a tunnel section
        }
    }

    // Check if we're entirely in a no-fly zone
    if (foundNoFly && exitDist < 0.0f) {
        exitPoint = end;
        return true;
    }

    return false;
}

// Create a hybrid path that handles tunnels
inline std::vector<PathNode> CreateHybridPath(const Vector3& start, const Vector3& end,
    int mapId, bool flying, bool isFlying, bool ignoreWater) {
    std::ofstream g_LogFile;
    if (DEBUG_PATHFINDING) {
        g_LogFile.open("C:\\Driver\\SMM_Debug.log", std::ios::app);
        g_LogFile << "\n[HYBRID] Checking for no-fly zones between start and end\n";
    }
    g_LogFile << start.x << " " << start.y << " " << start.z << std::endl;
    g_LogFile << end.x << " " << end.y << " " << end.z << std::endl;

    // Check if path goes through a no-fly zone
    Vector3 tunnelEntry, tunnelExit;
    if (!flying || !DetectNoFlyZone(start, end, mapId, tunnelEntry, tunnelExit)) {
        // No tunnel detected, use normal pathfinding
        if (DEBUG_PATHFINDING) {
            g_LogFile << "[HYBRID] No tunnel detected, using standard pathfinding\n";
        }
        return {};
    }

    if (DEBUG_PATHFINDING) {
        g_LogFile << "[HYBRID] ✓ Tunnel detected!\n";
        g_LogFile << "  Entry: (" << tunnelEntry.x << "," << tunnelEntry.y << "," << tunnelEntry.z << ")\n";
        g_LogFile << "  Exit:  (" << tunnelExit.x << "," << tunnelExit.y << "," << tunnelExit.z << ")\n";
    }

    std::vector<PathNode> hybridPath;

    // SEGMENT 1: Fly to tunnel entrance
    if (start.Dist3D(tunnelEntry) > 5.0f) {
        std::vector<PathNode> approachPath = Calculate3DFlightPath(start, tunnelEntry, mapId, isFlying);
        if (!approachPath.empty()) {
            hybridPath.insert(hybridPath.end(), approachPath.begin(), approachPath.end());
            if (DEBUG_PATHFINDING) {
                g_LogFile << "[HYBRID] Approach flight: " << approachPath.size() << " waypoints\n";
            }
        }
        else {
            if (DEBUG_PATHFINDING) {
                g_LogFile << "[HYBRID] ✗ Failed to create approach path\n";
            }
            return {};
        }
    }
    else {
        hybridPath.push_back(PathNode(start, PATH_AIR));
    }

    // SEGMENT 2: Walk through tunnel (GROUND PATHFINDING)
    if (DEBUG_PATHFINDING) {
        g_LogFile << "[HYBRID] Computing ground path through tunnel...\n";
    }

    // Pass ignoreWater to FindPath
    std::vector<PathNode> tunnelPath = FindPath(tunnelEntry, tunnelExit, ignoreWater);
    if (!tunnelPath.empty()) {
        // Add tunnel waypoints (skip first if it overlaps with approach end)
        if (!hybridPath.empty() && hybridPath.back().pos.Dist3D(tunnelPath[0].pos) < 3.0f) {
            hybridPath.insert(hybridPath.end(), tunnelPath.begin() + 1, tunnelPath.end());
        }
        else {
            hybridPath.insert(hybridPath.end(), tunnelPath.begin(), tunnelPath.end());
        }
        if (DEBUG_PATHFINDING) {
            g_LogFile << "[HYBRID] Tunnel walk: " << tunnelPath.size() << " waypoints\n";
        }
    }
    else {
        // Fallback: straight line through tunnel
        if (DEBUG_PATHFINDING) {
            g_LogFile << "[HYBRID] ⚠ NavMesh failed, using straight line through tunnel\n";
        }
        hybridPath.push_back(PathNode(tunnelEntry, PATH_GROUND));
        hybridPath.push_back(PathNode(tunnelExit, PATH_GROUND));
    }

    // SEGMENT 3: Fly from tunnel exit to final destination
    if (tunnelExit.Dist3D(end) > 5.0f) {
        std::vector<PathNode> exitPath = Calculate3DFlightPath(tunnelExit, end, mapId, isFlying);
        if (!exitPath.empty()) {
            // Skip first waypoint if it overlaps
            if (!hybridPath.empty() && hybridPath.back().pos.Dist3D(exitPath[0].pos) < 3.0f) {
                hybridPath.insert(hybridPath.end(), exitPath.begin() + 1, exitPath.end());
            }
            else {
                hybridPath.insert(hybridPath.end(), exitPath.begin(), exitPath.end());
            }
            if (DEBUG_PATHFINDING) {
                g_LogFile << "[HYBRID] Exit flight: " << exitPath.size() << " waypoints\n";
            }
        }
    }
    else {
        hybridPath.push_back(PathNode(end, PATH_AIR));
    }

    if (DEBUG_PATHFINDING) {
        g_LogFile << "[HYBRID] ✓ Complete hybrid path: " << hybridPath.size() << " total waypoints\n";
        g_LogFile << "  Structure: Fly(" << (start.Dist3D(tunnelEntry) > 5.0f ? "yes" : "no")
            << ") → Walk(tunnel) → Fly(" << (tunnelExit.Dist3D(end) > 5.0f ? "yes" : "no") << ")\n";
    }

    return hybridPath;
}

// NEW FUNCTION TO CLEAN GROUND Z
inline void CleanPathGroundZ(std::vector<PathNode>& path, int mapId) {
    if (DEBUG_PATHFINDING && !path.empty()) {
        g_LogFile << "[CLEANUP] Cleaning ground Z for " << path.size() << " waypoints..." << std::endl;
    }

    for (auto& node : path) {
        if (node.type == PATH_GROUND) {
            // Check for floor height starting slightly above the node to catch cases 
            // where the node sunk below the actual mesh.
            // We search from node.z + 5.0 down to find the real floor.
            float realZ = GetFMapFloorHeight(mapId, node.pos.x, node.pos.y, node.pos.z + 5.0f);

            // If a valid floor is found
            if (realZ > -90000.0f) {
                // If the current node is below this floor (or very close), snap it UP.
                // We trust FMap (collision) over MMap (navmesh) for Z height.
                if (node.pos.z < realZ + 0.5f) {
                    node.pos.z = realZ + 0.5f; // Snap to floor + 0.5f clearance
                }
            }
        }
    }
}

// MODIFIED: CalculatePath accepts ignoreWater and passes it to FindPath/Cache
inline std::vector<PathNode> CalculatePath(const std::vector<Vector3>& inputPath, const Vector3& startPos,
    int currentIndex, bool flying, int mapId, bool isFlying, bool ignoreWater, bool path_loop = false) {
    

    std::string mmapFolder = "C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/mmaps/";

    // --- ADD THIS CHECK ---
    if (!std::filesystem::exists(mmapFolder)) {
        g_LogFile << "[ERROR] CRITICAL: MMap folder does not exist: " << mmapFolder << std::endl;
        g_LogFile << "[ERROR] Please update 'mmapFolder' in Pathfinding2.h to your correct path." << std::endl;
        return {}; // Return empty path instead of crashing
    }
    // ----------------------

    std::vector<PathNode> stitchedPath;
    std::vector<Vector3> modifiedInput;

    if (inputPath.empty() || currentIndex < 0 || currentIndex >= inputPath.size()) return {};

    if (!path_loop) {
        modifiedInput.push_back(startPos);
        for (int i = currentIndex; i < inputPath.size(); ++i) modifiedInput.push_back(inputPath[i]);
        g_LogFile << inputPath[0].x << std::endl;
    }
    else {
        for (int i = currentIndex; i < inputPath.size(); ++i) modifiedInput.push_back(inputPath[i]);
        for (int i = 0; i < currentIndex; ++i) modifiedInput.push_back(inputPath[i]);
    }

    for (int i = currentIndex; i < modifiedInput.size(); ++i) g_LogFile << modifiedInput[i].x << " " << modifiedInput[i].y << std::endl;
    
    // Sparse loading of only needed tiles
    if (!globalNavMesh.LoadMap(mmapFolder, mapId, &modifiedInput, true)) {
        g_LogFile << "Load Map failed" << std::endl;
        return {};
    }

    size_t lookahead = modifiedInput.size() - 1;

    for (size_t i = 0; i < lookahead; ++i) {
        Vector3 start = modifiedInput[i];
        Vector3 end = modifiedInput[i + 1];
        // Pass ignoreWater to cache key so we don't reuse a water path when water is ignored
        PathCacheKey key(start, end, flying, ignoreWater);

        std::vector<PathNode>* cached = globalPathCache.Get(key);
        if (!cached) {
            std::vector<PathNode> segment;

            // SMART PATHFINDING LOGIC
            bool shouldTryGroundFirst = false;

            if (flying) {
                // First check for tunnels/no-fly zones
                // Pass ignoreWater to hybrid path generation
                // std::vector<PathNode> hybridPath = CreateHybridPath(start, end, mapId, flying, isFlying, ignoreWater);
                std::vector<PathNode> hybridPath = {};
                if (!hybridPath.empty()) {
                    // Found a tunnel, use the hybrid path
                    globalPathCache.Put(key, hybridPath);
                    cached = globalPathCache.Get(key);

                    if (cached && cached->size() > 0) {
                        if (stitchedPath.empty()) {
                            stitchedPath.insert(stitchedPath.end(), cached->begin(), cached->end());
                        }
                        else {
                            stitchedPath.insert(stitchedPath.end(), cached->begin() + 1, cached->end());
                        }
                    }
                    continue; // Skip to next segment
                }

                // Check if both positions are on ground
                float startGroundZ = globalNavMesh.GetLocalGroundHeight(start);
                float endGroundZ = globalNavMesh.GetLocalGroundHeight(end);
                bool startOnGround = (startGroundZ > -90000.0f &&
                    std::abs(start.z - startGroundZ) < 5.0f);
                bool endOnGround = (endGroundZ > -90000.0f &&
                    std::abs(end.z - endGroundZ) < 5.0f);

                float horizontalDist = start.Dist2D(end);

                // If both on ground and close enough, try ground path first
                if (startOnGround && endOnGround && horizontalDist < 50.0f) {
                    shouldTryGroundFirst = true;

                    if (DEBUG_PATHFINDING) {
                        
                        g_LogFile << "[PATHFINDING] Detected ground-to-ground segment (dist="
                            << horizontalDist << "), trying NavMesh first" << std::endl;
                    }
                }
            }

            // Try ground path first if conditions are met
            if (shouldTryGroundFirst) {
                // Pass ignoreWater to FindPath
                segment = FindPath(start, end, ignoreWater);

                if (!segment.empty()) {
                    if (DEBUG_PATHFINDING) {
                        
                        g_LogFile << "[PATHFINDING] ✓ Ground path successful ("
                            << segment.size() << " waypoints)" << std::endl;
                    }
                }
                else {
                    if (DEBUG_PATHFINDING) {
                        
                        g_LogFile << "[PATHFINDING] ✗ Ground path failed, falling back to flight" << std::endl;
                    }
                    segment = Calculate3DFlightPath(start, end, mapId, isFlying);
                }
            }
            else {
                // Normal logic: use requested path type
                if (flying) {
                    segment = Calculate3DFlightPath(start, end, mapId, isFlying);
                    if (segment.empty()) {
                        // segment = FindPath(start, end, ignoreWater);
                        return segment;
                    }
                }
                else {
                    // Pass ignoreWater to FindPath
                    segment = FindPath(start, end, ignoreWater);
                }
            }

            if (segment.empty()) {
                g_LogFile << "[PATHFINDING] ✗ CRITICAL: Segment " << i << " failed to generate path!" << std::endl;
                g_LogFile << "   From: (" << start.x << "," << start.y << "," << start.z << ")" << std::endl;
                g_LogFile << "   To: (" << end.x << "," << end.y << "," << end.z << ")" << std::endl;
                return {}; // Return empty to trigger retry
            }

            globalPathCache.Put(key, segment);
            cached = globalPathCache.Get(key);

            globalPathCache.Put(key, segment);
            cached = globalPathCache.Get(key);
        }

        if (cached && cached->size() > 0) {
            if (stitchedPath.empty()) {
                stitchedPath.insert(stitchedPath.end(), cached->begin(), cached->end());
            }
            else {
                stitchedPath.insert(stitchedPath.end(), cached->begin() + 1, cached->end());
            }
        }
    }

    // Don't subdivide hybrid paths - they're already properly segmented
    if (flying) {
        // Check if this is a hybrid path (has both high and low altitude sections)
        bool isHybrid = false;
        if (stitchedPath.size() > 5) {
            float minZ = stitchedPath[0].pos.z;
            float maxZ = stitchedPath[0].pos.z;
            for (const auto& wp : stitchedPath) {
                minZ = (std::min)(minZ, wp.pos.z);
                maxZ = (std::max)(maxZ, wp.pos.z);
            }
            // If altitude varies significantly, it might be a hybrid path
            isHybrid = (maxZ - minZ) > 30.0f && stitchedPath.size() > 20;
        }

        if (!isHybrid) {
            stitchedPath = SubdivideFlightPath(stitchedPath, mapId);
        }

        // NEW: Only clean GROUND nodes in flight paths
        for (auto& node : stitchedPath) {
            if (node.type == PATH_GROUND) {
                float realZ = GetFMapFloorHeight(mapId, node.pos.x, node.pos.y, node.pos.z + 5.0f);
                if (realZ > -90000.0f && node.pos.z < realZ + 0.5f) {
                    node.pos.z = realZ + 0.5f;
                }
            }
        }

        return stitchedPath;
    }
    else {
        stitchedPath = globalNavMesh.SubdivideOnMesh(stitchedPath);
        CleanPathGroundZ(stitchedPath, mapId);
        return stitchedPath;
    }
}