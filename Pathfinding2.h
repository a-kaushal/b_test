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

// DETOUR INCLUDES
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourCommon.h"

#include "Vector.h"

// FMap function declarations (replaces VMap)
extern "C" bool CheckFMapLine(int mapId, float x1, float y1, float z1, float x2, float y2, float z2);
extern "C" float GetFMapFloorHeight(int mapId, float x, float y, float z);
extern "C" bool CanFlyAt(int mapId, float x, float y, float z);

// --- CONFIGURATION ---
const float COLLISION_STEP_SIZE = 0.5f;    // Was 1.5f - much finer sampling
const float MIN_CLEARANCE = 2.0f;          // Was 1.0f - more safety margin
const float AGENT_RADIUS = 2.0f;           // Was 1.3f - larger safety bubble
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

const bool DEBUG_PATHFINDING = true;  // Enable for flight debugging

enum PathType {
    PATH_GROUND = 0,
    PATH_AIR = 1
};

struct PathNode {
    Vector3 pos;
    int type; // PathType

    PathNode() : pos(0, 0, 0), type(PATH_GROUND) {}
    PathNode(Vector3 p, int t) : pos(p), type(t) {}
    PathNode(float x, float y, float z, int t) : pos(x, y, z), type(t) {}

    bool operator==(const PathNode& other) const {
        return pos == other.pos && type == other.type;
    }
    bool operator!=(const PathNode& other) const {
        return !(*this == other);
    }
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

    PathCacheKey(const Vector3& start, const Vector3& end, bool fly) : flying(fly) {
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
        return flying < o.flying;
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

class NavMesh {
public:
    dtNavMesh* mesh = nullptr;
    dtNavMeshQuery* query = nullptr;
    int currentMapId = -1;

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
    bool CheckFlightPoint(const Vector3& pos, int mapId) {
        if (DEBUG_PATHFINDING) {
            std::ofstream log("C:\\Driver\\SMM_Debug.log", std::ios::app);
            log << "[CheckFlightPoint] Testing " << mapId << " (" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
            log.close();
        }

        // First check if we're in flyable space using FMap
        bool flyable = CanFlyAt(mapId, pos.x, pos.y, pos.z);
        if (!flyable) {
            if (DEBUG_PATHFINDING) {
                std::ofstream log("C:\\Driver\\SMM_Debug.log", std::ios::app);
                log << "  ? Not flyable according to FMap\n";
                log.close();
            }
            return false;
        }

        // Check for collision in a small sphere around the point
        if (CheckFMapLine(mapId, pos.x, pos.y, pos.z - 0.5f, pos.x, pos.y, pos.z + 0.5f)) {
            if (DEBUG_PATHFINDING) {
                std::ofstream log("C:\\Driver\\SMM_Debug.log", std::ios::app);
                log << "  ? Vertical collision detected\n";
                log.close();
            }
            return false;
        }

        // Verify ground clearance
        float gZ = GetLocalGroundHeight(pos);
        if (gZ > -90000.0f && pos.z < gZ + MIN_CLEARANCE) {
            if (DEBUG_PATHFINDING) {
                std::ofstream log("C:\\Driver\\SMM_Debug.log", std::ios::app);
                log << "  ? Too close to ground: floor=" << gZ << ", clearance=" << (pos.z - gZ) << "\n";
                log.close();
            }
            return false;
        }

        if (DEBUG_PATHFINDING) {
            std::ofstream log("C:\\Driver\\SMM_Debug.log", std::ios::app);
            log << "  ? Valid flight point\n";
            log.close();
        }
        return true;
    }

    // FMap-based collision checking for flight segments
    bool CheckFlightSegment(const Vector3& start, const Vector3& end, int mapId, bool strict = true) {
        Vector3 dir = end - start;
        float totalDist = dir.Length();
        if (totalDist < 0.1f) return true;

        // Multi-ray collision check
        float r = AGENT_RADIUS;
        float diag = r * 0.7071f;

        int numRays = strict ? 9 : 1;
        Vector3 offsets[9] = {
            Vector3(0,0,0),
            Vector3(r,0,0), Vector3(-r,0,0), Vector3(0,r,0), Vector3(0,-r,0),
            Vector3(diag,diag,0), Vector3(-diag,-diag,0), Vector3(diag,-diag,0), Vector3(-diag,diag,0)
        };

        for (int i = 0; i < numRays; ++i) {
            Vector3 s = start + offsets[i];
            Vector3 e = end + offsets[i];

            // Use FMap for precise collision detection
            if (CheckFMapLine(mapId, s.x, s.y, s.z, e.x, e.y, e.z)) {
                return false;
            }
        }

        // Sample points along the path for ground clearance
        dir = dir / totalDist;
        int numSteps = (int)(totalDist / COLLISION_STEP_SIZE);
        if (numSteps < 1) numSteps = 1;

        for (int i = 1; i <= numSteps; ++i) {
            Vector3 pt = start + (dir * (float)(i * COLLISION_STEP_SIZE));

            // Check if we're in flyable space
            if (!CanFlyAt(mapId, pt.x, pt.y, pt.z)) {
                return false;
            }

            float gZ = GetLocalGroundHeight(pt);
            if (gZ > -90000.0f) {
                float limit = strict ? (gZ + MIN_CLEARANCE) : (gZ + 0.5f);
                if (pt.z < limit) return false;
            }
        }

        return true;
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

    bool LoadMap(const std::string& directory, int mapId) {
        if (currentMapId == mapId && mesh && query) return true;
        Clear();
        query = dtAllocNavMeshQuery();
        if (!query) return false;

        std::stringstream ss;
        ss << std::setw(4) << std::setfill('0') << mapId;
        std::string prefix = ss.str();

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

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().filename().string().find(prefix) == 0 &&
                entry.path().extension() == ".mmtile") {
                AddTile(entry.path().string());
            }
        }

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
};

static NavMesh globalNavMesh;

inline std::vector<PathNode> FindPath(const Vector3& start, const Vector3& end) {
    if (!globalNavMesh.query || !globalNavMesh.mesh) return {};

    dtQueryFilter filter;
    filter.setIncludeFlags(0xFFFF);
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
inline std::vector<PathNode> Calculate3DFlightPath(const Vector3& start, const Vector3& end, int mapId) {
    std::ofstream logFile;
    if (DEBUG_PATHFINDING) {
        logFile.open("C:\\Driver\\SMM_Debug.log", std::ios::app);
        logFile << "\n=== CALCULATE 3D FLIGHT PATH ===\n";
        logFile << "Start: (" << start.x << ", " << start.y << ", " << start.z << ")\n";
        logFile << "End:   (" << end.x << ", " << end.y << ", " << end.z << ")\n";
    }

    // 1. VALIDATE END POSITION
    Vector3 actualEnd = end;
    float groundZ = globalNavMesh.GetLocalGroundHeight(end);

    if (groundZ > -90000.0f && end.z < groundZ + MIN_CLEARANCE) {
        actualEnd.z = groundZ + MIN_CLEARANCE + 1.0f;
        if (DEBUG_PATHFINDING) {
            logFile << "Adjusted end height to: " << actualEnd.z << " (ground: " << groundZ << ")\n";
        }
    }

    // 2. TRY DIRECT PATH FIRST
    if (DEBUG_PATHFINDING) {
        logFile << "Checking direct path...\n";
    }

    if (globalNavMesh.CheckFlightSegment(start, actualEnd, mapId, true)) {
        if (DEBUG_PATHFINDING) {
            logFile << "✓ Direct path clear!\n";
            logFile.close();
        }
        return { PathNode(start, PATH_AIR), PathNode(actualEnd, PATH_AIR) };
    }

    // 3. CALCULATE SAFE FLIGHT HEIGHT
    float startGroundZ = globalNavMesh.GetLocalGroundHeight(start);
    float endGroundZ = globalNavMesh.GetLocalGroundHeight(actualEnd);
    float maxGroundZ = (std::max)(startGroundZ, endGroundZ);
    float safeHeight = (maxGroundZ > -90000.0f) ? maxGroundZ + 40.0f : (std::max)(start.z, actualEnd.z) + 40.0f;

    // 4. TRY ANGLED ASCENT PATH (gain altitude while moving horizontally)
    if (DEBUG_PATHFINDING) {
        logFile << "Direct blocked, trying angled ascent path...\n";
    }

    // Calculate horizontal distance and direction
    Vector3 horizontal = actualEnd - start;
    horizontal.z = 0;
    float horizontalDist = horizontal.Length();

    if (horizontalDist > 0.1f) {
        Vector3 horizontalDir = horizontal / horizontalDist;

        // Create waypoints that gain altitude while moving horizontally
        // Divide the journey into segments
        const int NUM_SEGMENTS = 4;
        std::vector<PathNode> angledPath;
        angledPath.push_back(PathNode(start, PATH_AIR));

        // Start altitude check
        float currentZ = start.z;
        float targetZ = (std::max)(safeHeight, actualEnd.z);
        float zPerSegment = (targetZ - currentZ) / NUM_SEGMENTS;

        bool angledPathClear = true;
        for (int i = 1; i <= NUM_SEGMENTS; ++i) {
            float t = (float)i / (float)NUM_SEGMENTS;
            Vector3 waypoint = start + (horizontal * t);
            waypoint.z = currentZ + (zPerSegment * i);

            // Validate this waypoint
            if (!globalNavMesh.CheckFlightPoint(waypoint, mapId)) {
                angledPathClear = false;
                break;
            }

            // Validate segment to this waypoint
            if (!globalNavMesh.CheckFlightSegment(angledPath.back().pos, waypoint, mapId, true)) {
                angledPathClear = false;
                break;
            }

            angledPath.push_back(PathNode(waypoint, PATH_AIR));
        }

        // Add final descent to actual end if needed
        if (angledPathClear && angledPath.back().pos != actualEnd) {
            if (globalNavMesh.CheckFlightSegment(angledPath.back().pos, actualEnd, mapId, true)) {
                angledPath.push_back(PathNode(actualEnd, PATH_AIR));

                if (DEBUG_PATHFINDING) {
                    logFile << "✓ Angled ascent path clear (" << angledPath.size() << " waypoints)\n";
                    logFile.close();
                }
                return angledPath;
            }
        }
    }

    // 5. A* SEARCH WITH DIAGONAL PREFERENCE
    if (DEBUG_PATHFINDING) {
        logFile << "Angled path blocked, using 3D A* with diagonal preference...\n";
    }

    const float ASTAR_GRID_SIZE = 15.0f;
    std::vector<FlightNode3D> nodes;
    nodes.reserve(2000);

    std::unordered_map<GridKey, int, GridKeyHash> gridToIndex;
    std::unordered_set<int> closedSet;
    IndexPriorityQueue openSet(&nodes);

    auto GetOrCreateNode = [&](const Vector3& pos) -> int {
        GridKey key(pos, ASTAR_GRID_SIZE);
        auto it = gridToIndex.find(key);
        if (it != gridToIndex.end()) return it->second;

        Vector3 gridCenter(
            (key.x + 0.5f) * ASTAR_GRID_SIZE,
            (key.y + 0.5f) * ASTAR_GRID_SIZE,
            (key.z + 0.5f) * ASTAR_GRID_SIZE
        );

        if (!globalNavMesh.CheckFlightPoint(gridCenter, mapId)) {
            return -1;
        }

        int idx = nodes.size();
        nodes.emplace_back();
        nodes[idx].pos = gridCenter;
        nodes[idx].hScore = gridCenter.Dist3D(actualEnd);
        gridToIndex[key] = idx;
        return idx;
        };

    // Create start/end nodes
    int startIdx = nodes.size();
    nodes.emplace_back();
    nodes[startIdx].pos = start;
    nodes[startIdx].gScore = 0.0f;
    nodes[startIdx].hScore = start.Dist3D(actualEnd);

    int endIdx = nodes.size();
    nodes.emplace_back();
    nodes[endIdx].pos = actualEnd;

    openSet.push(startIdx);

    Vector3 midpoint = (start + actualEnd) * 0.5f;
    float halfDist = start.Dist3D(actualEnd) * 0.5f;
    float searchRadius = (std::min)(FLIGHT_MAX_SEARCH_RADIUS, halfDist + 150.0f);

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

    while (!openSet.empty() && iterations < FLIGHT_MAX_ITERATIONS) {
        int currentIdx = openSet.top();
        openSet.pop();

        if (closedSet.count(currentIdx)) continue;
        closedSet.insert(currentIdx);
        iterations++;

        FlightNode3D& current = nodes[currentIdx];

        // Check if we can reach end directly
        if (current.pos.Dist3D(actualEnd) < ASTAR_GRID_SIZE * 2.0f) {
            if (globalNavMesh.CheckFlightSegment(current.pos, actualEnd, mapId, true)) {
                nodes[endIdx].parentIdx = currentIdx;
                nodes[endIdx].gScore = current.gScore + current.pos.Dist3D(actualEnd);
                goalIdx = endIdx;
                break;
            }
        }

        // Process neighbors (prioritizing diagonal ascent)
        for (int i = 0; i < 22; ++i) {
            Vector3 neighborPos = current.pos + Vector3(
                neighborDirs[i][0] * ASTAR_GRID_SIZE,
                neighborDirs[i][1] * ASTAR_GRID_SIZE,
                neighborDirs[i][2] * ASTAR_GRID_SIZE
            );

            float distFromLine = (neighborPos - midpoint).Length();
            if (distFromLine > searchRadius) continue;

            int neighborIdx = GetOrCreateNode(neighborPos);
            if (neighborIdx < 0) continue;
            if (closedSet.count(neighborIdx)) continue;

            if (!globalNavMesh.CheckFlightSegment(current.pos, nodes[neighborIdx].pos, mapId, true)) {
                continue;
            }

            float dist = current.pos.Dist3D(nodes[neighborIdx].pos);

            // BONUS: Favor diagonal ascent movements
            float bonusWeight = 1.0f;
            if (neighborDirs[i][2] > 0 && (neighborDirs[i][0] != 0 || neighborDirs[i][1] != 0)) {
                bonusWeight = 0.9f; // 10% cheaper for diagonal ascent
            }

            float tentativeG = current.gScore + (dist * bonusWeight);

            if (tentativeG < nodes[neighborIdx].gScore) {
                nodes[neighborIdx].parentIdx = currentIdx;
                nodes[neighborIdx].gScore = tentativeG;
                openSet.push(neighborIdx);
            }
        }

        if (nodes.size() > 2000) break;
    }

    if (DEBUG_PATHFINDING) {
        logFile << "A* completed: " << iterations << " iterations, "
            << nodes.size() << " nodes\n";
    }

    // 6. RECONSTRUCT AND SMOOTH PATH
    std::vector<PathNode> path;

    if (goalIdx >= 0) {
        int currIdx = goalIdx;
        while (currIdx >= 0) {
            path.push_back(PathNode(nodes[currIdx].pos, PATH_AIR));
            currIdx = nodes[currIdx].parentIdx;
        }
        std::reverse(path.begin(), path.end());

        // AGGRESSIVE SMOOTHING
        if (path.size() > 2) {
            std::vector<PathNode> smoothed;
            smoothed.push_back(path[0]);

            size_t current = 0;
            while (current < path.size() - 1) {
                size_t farthest = current + 1;

                for (size_t next = path.size() - 1; next > current + 1; --next) {
                    if (globalNavMesh.CheckFlightSegment(path[current].pos, path[next].pos, mapId, true)) {
                        farthest = next;
                        break;
                    }
                }

                smoothed.push_back(path[farthest]);
                current = farthest;
            }

            path = smoothed;
        }

        // FINAL VALIDATION
        for (size_t i = 0; i < path.size(); ++i) {
            if (!globalNavMesh.CheckFlightPoint(path[i].pos, mapId)) {
                Vector3 adjusted = path[i].pos;
                bool foundSafe = false;
                for (int attempt = 0; attempt < 5; ++attempt) {
                    adjusted.z += 5.0f;
                    if (globalNavMesh.CheckFlightPoint(adjusted, mapId)) {
                        path[i].pos = adjusted;
                        foundSafe = true;
                        break;
                    }
                }
            }
        }

        if (DEBUG_PATHFINDING) {
            logFile << "✓ Path found: " << path.size() << " waypoints\n";
            logFile.close();
        }
    }
    else {
        // FALLBACK: Angled ascent to very high altitude
        if (DEBUG_PATHFINDING) {
            logFile << "A* failed, using extreme angled ascent\n";
        }

        float extremeZ = (std::max)(
            (std::max)(start.z, actualEnd.z) + 100.0f,
            (maxGroundZ > -90000.0f ? maxGroundZ + 120.0f : start.z + 100.0f)
            );

        // Create angled ascent instead of vertical
        Vector3 midAscent = start + ((actualEnd - start) * 0.5f);
        midAscent.z = extremeZ;

        path = {
            PathNode(start, PATH_AIR),
            PathNode(midAscent, PATH_AIR),
            PathNode(actualEnd, PATH_AIR)
        };

        if (DEBUG_PATHFINDING) {
            logFile.close();
        }
    }

    return path;
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
    int mapId, bool flying) {
    std::ofstream logFile;
    if (DEBUG_PATHFINDING) {
        logFile.open("C:\\Driver\\SMM_Debug.log", std::ios::app);
        logFile << "\n[HYBRID] Checking for no-fly zones between start and end\n";
    }

    // Check if path goes through a no-fly zone
    Vector3 tunnelEntry, tunnelExit;
    if (!flying || !DetectNoFlyZone(start, end, mapId, tunnelEntry, tunnelExit)) {
        // No tunnel detected, use normal pathfinding
        if (DEBUG_PATHFINDING) {
            logFile << "[HYBRID] No tunnel detected, using standard pathfinding\n";
            logFile.close();
        }
        return {};
    }

    if (DEBUG_PATHFINDING) {
        logFile << "[HYBRID] ✓ Tunnel detected!\n";
        logFile << "  Entry: (" << tunnelEntry.x << "," << tunnelEntry.y << "," << tunnelEntry.z << ")\n";
        logFile << "  Exit:  (" << tunnelExit.x << "," << tunnelExit.y << "," << tunnelExit.z << ")\n";
    }

    std::vector<PathNode> hybridPath;

    // SEGMENT 1: Fly to tunnel entrance
    if (start.Dist3D(tunnelEntry) > 5.0f) {
        std::vector<PathNode> approachPath = Calculate3DFlightPath(start, tunnelEntry, mapId);
        if (!approachPath.empty()) {
            hybridPath.insert(hybridPath.end(), approachPath.begin(), approachPath.end());
            if (DEBUG_PATHFINDING) {
                logFile << "[HYBRID] Approach flight: " << approachPath.size() << " waypoints\n";
            }
        }
        else {
            if (DEBUG_PATHFINDING) {
                logFile << "[HYBRID] ✗ Failed to create approach path\n";
                logFile.close();
            }
            return {};
        }
    }
    else {
        hybridPath.push_back(PathNode(start, PATH_AIR));
    }

    // SEGMENT 2: Walk through tunnel (GROUND PATHFINDING)
    if (DEBUG_PATHFINDING) {
        logFile << "[HYBRID] Computing ground path through tunnel...\n";
    }

    std::vector<PathNode> tunnelPath = FindPath(tunnelEntry, tunnelExit);
    if (!tunnelPath.empty()) {
        // Add tunnel waypoints (skip first if it overlaps with approach end)
        if (!hybridPath.empty() && hybridPath.back().pos.Dist3D(tunnelPath[0].pos) < 3.0f) {
            hybridPath.insert(hybridPath.end(), tunnelPath.begin() + 1, tunnelPath.end());
        }
        else {
            hybridPath.insert(hybridPath.end(), tunnelPath.begin(), tunnelPath.end());
        }
        if (DEBUG_PATHFINDING) {
            logFile << "[HYBRID] Tunnel walk: " << tunnelPath.size() << " waypoints\n";
        }
    }
    else {
        // Fallback: straight line through tunnel
        if (DEBUG_PATHFINDING) {
            logFile << "[HYBRID] ⚠ NavMesh failed, using straight line through tunnel\n";
        }
        hybridPath.push_back(PathNode(tunnelEntry, PATH_GROUND));
        hybridPath.push_back(PathNode(tunnelExit, PATH_GROUND));
    }

    // SEGMENT 3: Fly from tunnel exit to final destination
    if (tunnelExit.Dist3D(end) > 5.0f) {
        std::vector<PathNode> exitPath = Calculate3DFlightPath(tunnelExit, end, mapId);
        if (!exitPath.empty()) {
            // Skip first waypoint if it overlaps
            if (!hybridPath.empty() && hybridPath.back().pos.Dist3D(exitPath[0].pos) < 3.0f) {
                hybridPath.insert(hybridPath.end(), exitPath.begin() + 1, exitPath.end());
            }
            else {
                hybridPath.insert(hybridPath.end(), exitPath.begin(), exitPath.end());
            }
            if (DEBUG_PATHFINDING) {
                logFile << "[HYBRID] Exit flight: " << exitPath.size() << " waypoints\n";
            }
        }
    }
    else {
        hybridPath.push_back(PathNode(end, PATH_AIR));
    }
    
    if (DEBUG_PATHFINDING) {
        logFile << "[HYBRID] ✓ Complete hybrid path: " << hybridPath.size() << " total waypoints\n";
        logFile << "  Structure: Fly(" << (start.Dist3D(tunnelEntry) > 5.0f ? "yes" : "no")
            << ") → Walk(tunnel) → Fly(" << (tunnelExit.Dist3D(end) > 5.0f ? "yes" : "no") << ")\n";
        logFile.close();
    }

    return hybridPath;
}

// MODIFY CalculatePath to use hybrid pathfinding
inline std::vector<PathNode> CalculatePath(const std::vector<Vector3>& inputPath, const Vector3& startPos,
    int currentIndex, bool flying, int mapId, bool path_loop = false) {
    std::string mmapFolder = "C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/mmaps/";
    if (!globalNavMesh.LoadMap(mmapFolder, mapId)) return {};

    std::vector<PathNode> stitchedPath;
    std::vector<Vector3> modifiedInput;

    if (inputPath.empty() || currentIndex < 0 || currentIndex >= inputPath.size()) return {};

    if (!path_loop) {
        modifiedInput.push_back(startPos);
        for (int i = currentIndex; i < inputPath.size(); ++i) modifiedInput.push_back(inputPath[i]);
    }
    else {
        for (int i = currentIndex; i < inputPath.size(); ++i) modifiedInput.push_back(inputPath[i]);
        for (int i = 0; i < currentIndex; ++i) modifiedInput.push_back(inputPath[i]);
    }

    int lookahead = modifiedInput.size() - 1;

    for (int i = 0; i < lookahead; ++i) {
        Vector3 start = modifiedInput[i];
        Vector3 end = modifiedInput[i + 1];
        PathCacheKey key(start, end, flying);

        std::vector<PathNode>* cached = globalPathCache.Get(key);
        if (!cached) {
            std::vector<PathNode> segment;

            // SMART PATHFINDING LOGIC
            bool shouldTryGroundFirst = false;

            if (flying) {
                // First check for tunnels/no-fly zones
                std::vector<PathNode> hybridPath = CreateHybridPath(start, end, mapId, flying);
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
                        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
                        logFile << "[PATHFINDING] Detected ground-to-ground segment (dist="
                            << horizontalDist << "), trying NavMesh first" << std::endl;
                        logFile.close();
                    }
                }
            }

            // Try ground path first if conditions are met
            if (shouldTryGroundFirst) {
                segment = FindPath(start, end);

                if (!segment.empty()) {
                    if (DEBUG_PATHFINDING) {
                        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
                        logFile << "[PATHFINDING] ✓ Ground path successful ("
                            << segment.size() << " waypoints)" << std::endl;
                        logFile.close();
                    }
                }
                else {
                    if (DEBUG_PATHFINDING) {
                        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
                        logFile << "[PATHFINDING] ✗ Ground path failed, falling back to flight" << std::endl;
                        logFile.close();
                    }
                    segment = Calculate3DFlightPath(start, end, mapId);
                }
            }
            else {
                // Normal logic: use requested path type
                if (flying) {
                    segment = Calculate3DFlightPath(start, end, mapId);
                    if (segment.empty()) {
                        segment = FindPath(start, end);
                    }
                }
                else {
                    segment = FindPath(start, end);
                }
            }

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
            return SubdivideFlightPath(stitchedPath, mapId);
        }
        return stitchedPath;
    }
    else {
        return globalNavMesh.SubdivideOnMesh(stitchedPath);
    }
}