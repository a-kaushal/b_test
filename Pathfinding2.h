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
#include "MovementController.h"
#include "WorldState.h"

// FMap function declarations (replaces VMap)
extern "C" bool CheckFMapLine(int mapId, float x1, float y1, float z1, float x2, float y2, float z2);
extern "C" float GetFMapFloorHeight(int mapId, float x, float y, float z);
extern "C" bool CanFlyAt(int mapId, float x, float y, float z);

// --- AREA CONSTANTS (Must match Generator/PathCommon.h) ---
const unsigned short AREA_GROUND = 0x01; // 1 (Ground)
const unsigned short AREA_MAGMA = 0x02; // 2
const unsigned short AREA_SLIME = 0x04; // 4
const unsigned short AREA_WATER = 0x08; // 8 (Surface)
const unsigned short AREA_UNDERWATER = 0x10; // 16 (Sea Floor)
const unsigned short AREA_ROAD = 0x20; // 32 (Roads)
const unsigned short AREA_DEEP_WATER = 0x06; // 6 (Deep Water)

// --- CONFIGURATION ---
const float COLLISION_STEP_SIZE = 0.5f;    // Was 0.5f - less sampling
const float MIN_CLEARANCE = 5.0f;          // Was 1.0f - more safety margin
const float AGENT_RADIUS = 1.0f;           // CHANGED: Increased from 1.0f to 3.0f for a wider safety buffer
const float WAYPOINT_STEP_SIZE = 25.0f;
const int MAX_POLYS = 8192;
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

const float GROUND_PATH_THRESHOLD = 1.0f;

const bool DEBUG_PATHFINDING = true;  // Enable for flight debugging

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
        if (lruMap.find(key) != lruMap.end()) {
            lruList.erase(lruMap[key]);
            lruMap.erase(key);
        }
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

        loadedTiles.clear();
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

    // --- REFINES PATH TO AVOID WALL HUGGING ---
    // pushes waypoints away from walls if they are too close.
    // bufferDist: Desired clearance in yards (e.g., 0.8f for humanoids)
    void RefinePathClearance(std::vector<PathNode>& path, float bufferDist, int mapId) {
        if (!query || !mesh || path.size() < 3) return;

        if (DEBUG_PATHFINDING) {
            g_LogFile << "[RefinePath] Checking wall clearance..." << std::endl;
        }

        dtQueryFilter filter;
        filter.setIncludeFlags(0xFFFF);
        filter.setExcludeFlags(0);

        float searchExt[3] = { 2.0f, 5.0f, 2.0f };

        for (size_t i = 1; i < path.size() - 1; ++i) {
            if (path[i].type != PATH_GROUND) continue;

            Vector3& pos = path[i].pos;
            Vector3 originalPos = pos; // Backup in case of calculation failure

            float center[3] = { pos.y, pos.z, pos.x }; // WoW -> Recast Coords

            dtPolyRef polyRef = 0;
            float nearestPt[3] = { 0, 0, 0 };

            // 1. Find nearest polygon
            dtStatus status = query->findNearestPoly(center, searchExt, &filter, &polyRef, nearestPt);

            if (dtStatusSucceed(status) && polyRef) {
                float hitDist = 0.0f;
                float hitPos[3] = { 0, 0, 0 };
                float hitNormal[3] = { 0, 0, 0 };

                // 2. Find distance to closest wall
                if (dtStatusSucceed(query->findDistanceToWall(polyRef, nearestPt, 10.0f, &filter, &hitDist, hitPos, hitNormal))) {

                    // 3. NAN / INFINITY CHECKS
                    bool validMath = std::isfinite(hitDist) &&
                        std::isfinite(hitNormal[0]) &&
                        std::isfinite(hitNormal[1]) &&
                        std::isfinite(hitNormal[2]);

                    if (!validMath) {
                        if (DEBUG_PATHFINDING) g_LogFile << "[RefinePath] Warning: NaN detected at waypoint " << i << ". Skipping." << std::endl;
                        continue;
                    }

                    // 4. Zero Normal Check (Prevent pushing in unknown direction)
                    float normalLenSq = hitNormal[0] * hitNormal[0] + hitNormal[1] * hitNormal[1] + hitNormal[2] * hitNormal[2];
                    if (normalLenSq < 1e-6f) {
                        continue;
                    }

                    // 5. Apply Push
                    if (hitDist < bufferDist) {
                        float pushAmt = bufferDist - hitDist;

                        nearestPt[0] += hitNormal[0] * pushAmt;
                        nearestPt[2] += hitNormal[2] * pushAmt;

                        // Check result for validity
                        if (!std::isfinite(nearestPt[0]) || !std::isfinite(nearestPt[2])) {
                            pos = originalPos; // Revert
                            continue;
                        }

                        // Apply
                        pos.y = nearestPt[0];
                        pos.x = nearestPt[2];

                        // 6. Fix Z height
                        float floorZ = GetFMapFloorHeight(mapId, pos.x, pos.y, pos.z + 2.5f);
                        if (floorZ > -90000.0f) {
                            pos.z = floorZ + 0.5f;
                        }
                        else {
                            pos.z = nearestPt[1] + 0.5f; // Fallback to NavMesh Z
                        }
                    }
                }
            }
        }
    }

    // Enhanced flight point validation using FMap
    bool CheckFlightPoint(const Vector3& pos, int mapId, bool allowGroundProximity = false) {
        if (DEBUG_PATHFINDING) {
            //log << "[CheckFlightPoint] Testing " << mapId << " (" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
        }

        // First check if we're in flyable space using FMap
        bool flyable = CanFlyAt(mapId, pos.x, pos.y, pos.z);
        if (!flyable) {
            if (DEBUG_PATHFINDING) {
                //log << "  ? Not flyable according to FMap" << std::endl;
            }
            return false;
        }

        // 1. Check Core (Center to Head)
        if (CheckFMapLine(mapId, pos.x, pos.y, pos.z, pos.x, pos.y, pos.z + 2.5f)) {
            return false;
        }
        // 2. Check Feet (Center to Feet)
        if (CheckFMapLine(mapId, pos.x, pos.y, pos.z, pos.x, pos.y, pos.z - 0.5f)) {
            return false;
        }

        // Check for collision in a small sphere around the point
        //if (CheckFMapLine(mapId, pos.x, pos.y, pos.z - 0.5f, pos.x, pos.y, pos.z + 0.5f)) {
        //    if (DEBUG_PATHFINDING) {
        //        //log << "  ? Vertical collision detected" << std::endl;
        //    }
        //    return false;
        //}

        // Skip ground clearance check if allowed (for takeoff points)
        if (allowGroundProximity) {
            return true;
        }

        // Verify ground clearance
        float gZ = GetLocalGroundHeight(pos);
        if (gZ > -90000.0f && pos.z < gZ + MIN_CLEARANCE) {
            if (DEBUG_PATHFINDING) {
                //log << "  ? Too close to ground: floor=" << gZ << ", clearance=" << (pos.z - gZ) << "" << std::endl;
            }
            return false;
        }

        if (DEBUG_PATHFINDING) {
            //log << "  ? Valid flight point" << std::endl;
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

    // --- NEW: STRICTER SAFETY CHECK FOR ESCAPE LOGIC ---
// Checks if a point has the full AGENT_RADIUS clearance in all directions
    bool IsClearSafePoint(const Vector3& pos, int mapId) {
        float probeZ = pos.z + 2.0f;
        float floorZ = GetFMapFloorHeight(mapId, pos.x, pos.y, probeZ);

        if (floorZ > -90000.0f) {
            // If the floor is above us (with a small epsilon), the point is invalid.
            if (pos.z < floorZ - 0.0f) {
                return false;
            }
        }

        float r = AGENT_RADIUS * 2;
        // Check surrounding sphere using rays
        Vector3 offsets[] = {
            Vector3(r,0,0), Vector3(-r,0,0), Vector3(0,r,0), Vector3(0,-r,0), // Horizontal
            Vector3(0,0,r), Vector3(0,0,-r), // Vertical
            Vector3(0.7f * r, 0.7f * r, 0), Vector3(-0.7f * r, 0.7f * r, 0), // Diagonals
            Vector3(0.7f * r, -0.7f * r, 0), Vector3(-0.7f * r, -0.7f * r, 0)
        };

        for (const auto& off : offsets) {
            if (CheckFMapLine(mapId, pos.x, pos.y, pos.z, pos.x + off.x, pos.y + off.y, pos.z + off.z)) {
                return false;
            }
        }
        return true;
    }

    // --- UNIVERSAL SPIRAL ESCAPE ---
    Vector3 FindNearestSafePoint(const Vector3& pos, int mapId, bool ground) {
        if (IsClearSafePoint(pos, mapId)) return pos;

        const float maxRadius = 20.0f; // Universal Escape Limit
        const float step = 2.0f;

        for (float r = step; r <= maxRadius; r += step) {
            Vector3 offsets[] = {
                // PRIORITY 1: HORIZONTAL (Get away from the wall/tree)
                Vector3(r, 0, 0), Vector3(-r, 0, 0),               // X
                Vector3(0, r, 0), Vector3(0, -r, 0),               // Y

                // PRIORITY 2: DIAGONALS
                Vector3(r, r, 0), Vector3(-r, -r, 0),
                Vector3(r, -r, 0), Vector3(-r, r, 0),

                // PRIORITY 3: VERTICAL (Last resort for pits/void)
                Vector3(0, 0, r), Vector3(0, 0, -r)                // Up/Down
            };

            for (const auto& off : offsets) {
                Vector3 test = pos + off;
                float groundZ = GetFMapFloorHeight(mapId, test.x, test.y, test.z);
                if (test.z < groundZ) {
                    test.z = groundZ + AGENT_RADIUS;
                }
                /*if (ground) {
                    if (IsClearSafePoint(test, mapId)) return test;
                }*/
                if (CanFlyAt(mapId, test.x, test.y, test.z)) {
                    if (IsClearSafePoint(test, mapId)) return test;
                }
            }

            
        }
        return Vector3(0, 0, 0);
    }

    // ----------------------------------------------------------------------
    //  UPDATED SEGMENT CHECK WITH PERPENDICULAR RAYS (Fixes Clipping)
    // ----------------------------------------------------------------------
    FlightSegmentResult CheckFlightSegmentDetailed(const Vector3& start, const Vector3& end, int mapId, Vector3& outFailPos, bool isFlying, bool strict = true, bool verbose = false) {
        Vector3 dir = end - start;
        float totalDist = dir.Length();
        if (totalDist < 0.1f) return SEGMENT_VALID;

        // Determine if starting from ground (ignore flight flag logic if passed)
        float startGroundZ = GetLocalGroundHeight(start);
        bool startingFromGround = !isFlying && (startGroundZ > -90000.0f && std::abs(start.z - startGroundZ) < 3.0f);

        // --- CALCULATE DYNAMIC OFFSETS BASED ON MOVEMENT DIRECTION ---
        // This prevents the "Gaps" that happen when moving along X or Y axis with fixed offsets.
        Vector3 forward = dir / totalDist;
        Vector3 up(0, 0, 1);

        // Handle vertical flight edge case
        Vector3 right;
        if (std::abs(forward.z) > 0.9f) {
            right = Vector3(1, 0, 0); // If flying straight up/down, use X as right
        }
        else {
            right = forward.Cross(up).Normalize();
        }
        Vector3 realUp = right.Cross(forward).Normalize();

        // --- FIX START ---
        // 1. Determine Radius based on Strictness
        // Strict = Full AGENT_RADIUS (Safety)
        // Relaxed = Small Radius (0.6f) - Fits in caves, but catches ceilings!
        float checkRadius = strict ? AGENT_RADIUS : AGENT_RADIUS / 2;
        float r = checkRadius;
        float diag = r * 0.85f;

        // Construct 9 offsets perpendicular to the path
        Vector3 offsets[9];
        offsets[0] = Vector3(0, 0, 0); // Center
        offsets[1] = right * r;
        offsets[2] = right * -r;
        offsets[3] = realUp * r;
        offsets[4] = realUp * -r;
        // Diagonals
        offsets[5] = (right * diag) + (realUp * diag);
        offsets[6] = (right * -diag) + (realUp * -diag);
        offsets[7] = (right * diag) + (realUp * -diag);
        offsets[8] = (right * -diag) + (realUp * diag);

        // Always use 9 rays if strict. If relaxed, we still use 9 but with smaller radius?
        // User requested fix for clipping on Attempt 1 (Strict), so Strict must be robust.
        // For relaxed attempts (strict=false), we can reduce count or radius.
        // Here we keep count but could reduce radius if needed. For now, we trust the 'strict' flag logic.
        //int numRays = strict ? 9 : 1;
        int numRays = strict ? 9 : 5;

        float skipCollisionDist = startingFromGround ? 5.0f : 0.0f;

        // Define vertical extents from the center of the bot.
        // Standard WoW humanoid is ~2.0 yards tall.
        // Assuming 'start' and 'end' are the CENTER of the bot:
        // --- VERTICAL OFFSET CONFIGURATION ---
        // Mounted characters are tall (~2.5y). We must check the full height.
        // If we only check +2.5, we might skip OVER a thin branch at +1.5.

        float headTop = 2.5f;   // Check max height (Top of Mount/Head)
        float headMid = 1.25f;  // GAP FILLER: Check between Center and Top
        float knees   = -0.5f;           // Gap filler
        float feetBot = -AGENT_RADIUS;  // Bottom of Feet check (catches ground clipping)

        // If relaxed (Attempt 4+), we shrink the box to fit in tunnels
        if (!strict) {
            headTop = 1.5f;
            headMid = 0.75f;
            knees = -0.3f;
        }
        Vector3 actualEnd = Vector3{ -36.8802f, 5284.5f, 24.4288f };
        if (actualEnd.Dist3D(end) < 10.0f) {
            g_LogFile << "Start: " << start.x << " " << start.y << " " << start.z << " | End: " << end.x << " " << end.y << " " << end.z << " " << std::endl;
            verbose = 1;
        }

        for (int i = 0; i < numRays; ++i) {
            Vector3 s = start + offsets[i];
            Vector3 e = end + offsets[i];

            // 1. GROUND PROXIMITY CHECK (Existing logic)
            if (skipCollisionDist > 0.0f && totalDist > skipCollisionDist) {
                Vector3 clearancePoint = start + (forward * skipCollisionDist);
                if (CheckFMapLine(mapId, clearancePoint.x + offsets[i].x,
                    clearancePoint.y + offsets[i].y,
                    clearancePoint.z + offsets[i].z,
                    e.x, e.y, e.z)) return SEGMENT_COLLISION;
            }
            // 2. OBSTACLE CHECK
            else if (skipCollisionDist == 0.0f || totalDist <= skipCollisionDist) {
                // 1. CENTER
                if (CheckFMapLine(mapId, s.x, s.y, s.z, e.x, e.y, e.z)) {
                    if (verbose && DEBUG_PATHFINDING) g_LogFile << "      FAIL: Ray " << i << " CENTER hit obstacle." << std::endl;
                    return SEGMENT_COLLISION;
                }
                // 2. FEET (Bottom) - Critical for ground clipping
                if (CheckFMapLine(mapId, s.x, s.y, s.z + feetBot, e.x, e.y, e.z + feetBot)) {
                    if (verbose && DEBUG_PATHFINDING) g_LogFile << "      FAIL: Ray " << i << " FEET hit obstacle." << std::endl;
                    return SEGMENT_COLLISION;
                }
                // 3. KNEES (Gap Filler)
                if (CheckFMapLine(mapId, s.x, s.y, s.z + knees, e.x, e.y, e.z + knees)) {
                    if (verbose && DEBUG_PATHFINDING) g_LogFile << "      FAIL: Ray " << i << " KNEES hit obstacle." << std::endl;
                    return SEGMENT_COLLISION;
                }
                // 4. HEAD MID
                if (CheckFMapLine(mapId, s.x, s.y, s.z + headMid, e.x, e.y, e.z + headMid)) {
                    if (verbose && DEBUG_PATHFINDING) g_LogFile << "      FAIL: Ray " << i << " HEAD-MID hit obstacle." << std::endl;
                    return SEGMENT_COLLISION;
                }
                // 5. HEAD TOP
                if (CheckFMapLine(mapId, s.x, s.y, s.z + headTop, e.x, e.y, e.z + headTop)) {
                    if (verbose && DEBUG_PATHFINDING) g_LogFile << "      FAIL: Ray " << i << " HEAD-TOP hit obstacle." << std::endl;
                    return SEGMENT_COLLISION;
                }
            }
        }

        // --- STEPPED CHECKS (Ground Clearance / No Fly Zone) ---
        int numSteps = (int)(totalDist / COLLISION_STEP_SIZE);
        if (numSteps < 1) numSteps = 1;

        for (int i = 1; i <= numSteps; ++i) {
            float distFromStart = (float)(i * COLLISION_STEP_SIZE);
            Vector3 pt = start + (forward * distFromStart);

            if (!CanFlyAt(mapId, pt.x, pt.y, pt.z)) {
                outFailPos = pt;
                return SEGMENT_NO_FLY_ZONE;
            }

            float gZ = GetLocalGroundHeight(pt);
            if (gZ > -90000.0f) {
                bool isNearStart = (distFromStart < 8.0f);
                bool isNearEnd = (distFromStart > totalDist - 8.0f);

                if (startingFromGround && isNearStart) {
                    if (pt.z < gZ - 0.5f) {
                        outFailPos = pt;
                        return SEGMENT_COLLISION;
                    }
                    continue;
                }

                if (isNearEnd && !strict) {
                    float distToEnd = totalDist - distFromStart;
                    if (distToEnd < 5.0f) {
                        continue;
                    }
                    if (pt.z < gZ - 1.0f) {
                        outFailPos = pt;
                        return SEGMENT_COLLISION;
                    }
                    continue;
                }

                float requiredClearance = (strict && !isNearStart && !isNearEnd) ? MIN_CLEARANCE : 0.5f;
                float limit = gZ + requiredClearance;

                if (pt.z < limit) {
                    outFailPos = pt;
                    if (verbose) {
                        g_LogFile << "      FAIL: Too close to ground (Z=" << pt.z << " < Limit=" << limit
                            << ") at dist " << distFromStart << " | " << pt.x << " " << pt.y << " " << pt.z << std::endl;
                    }
                    return SEGMENT_COLLISION;
                }
            }
        }
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
        if (loadedTiles.size() > 50) {
            if (DEBUG_PATHFINDING) {
                g_LogFile << "[Memory] Pruning NavMesh tiles (Limit reached)" << std::endl;
            }
            Clear();
            // Force re-initialization logic below to run
            currentMapId = -1;
        }

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
        if (dtStatusFailed(query->init(mesh, 65535))) return false;
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

inline std::vector<PathNode> FindPath(const Vector3& start, const Vector3& end, bool ignoreWater, bool pointAdjust = true, bool zCheck = true, float zExtent = 5.0f) {
    if (!globalNavMesh.query || !globalNavMesh.mesh) return {};

    // --- ESCAPE LOGIC FOR GROUND PATHS ---
    // If the start point is inside an obstacle or safety margin, move it out.
    // This applies to pure ground paths AND ground segments of hybrid paths.
    Vector3 actualStart = start;
    int mapId = globalNavMesh.currentMapId; // Use currently loaded map

    // --- ESCAPE LOGIC: USE UNIVERSAL SPIRAL ---
    if ((!globalNavMesh.IsClearSafePoint(actualStart, mapId)) && pointAdjust) {
        if (DEBUG_PATHFINDING) {
            g_LogFile << "[Ground] Start point unsafe/blocked. Seeking escape..." << std::endl;
        }
        Vector3 safeStart = globalNavMesh.FindNearestSafePoint(actualStart, mapId, true);

        // If a safe point was found
        if (safeStart.x != 0.0f || safeStart.y != 0.0f || safeStart.z != 0.0f) {
            actualStart = safeStart;
            if (DEBUG_PATHFINDING) {
                g_LogFile << "[Ground] Escaped to: " << actualStart.x << ", " << actualStart.y << ", " << actualStart.z << std::endl;
            }
        }
    }
    // -------------------------------------------

    dtQueryFilter filter;

    // 1. DEFINE COSTS
    // "Preferring" roads means penalizing everything else.
    // Cost 1.0 is normal. Cost 2.0 means "feels like double the distance".

    // Make standard ground twice as "expensive" as a road
    filter.setAreaCost(1, 5.0f);   // AREA_GROUND (ID 1)

    // Keep Road at standard cost (Preferred)
    filter.setAreaCost(32, 1.0f);  // AREA_ROAD (ID 32)

    // Set high penalties for bad terrain so we only use them if absolutely necessary
    filter.setAreaCost(2, 200.0f);  // AREA_MAGMA (ID 2)
    filter.setAreaCost(4, 200.0f);  // AREA_SLIME (ID 4)

    filter.setAreaCost(8, 5.0f);   // AREA_WATER (ID 8)
    filter.setAreaCost(6, 100.0f);   // AREA_DEEP_WATER (ID 8)

    // If we allow water, make it sluggish (like 3x distance)
    if (!ignoreWater) {
        filter.setAreaCost(16, .0f);  // AREA_UNDERWATER (ID 16)
    }

    // --- UPDATED FILTERING LOGIC ---
    // Start with default walkable areas (Ground, Magma, Slime, Road)
    unsigned short includeFlags = AREA_GROUND | AREA_MAGMA | AREA_SLIME | AREA_ROAD | AREA_WATER | AREA_DEEP_WATER;

    // If ignoring water is disabled, we add Water (Swimming) and Underwater (Sea Floor)
    if (!ignoreWater) {
        includeFlags |= AREA_UNDERWATER; // 0x10
    }

    filter.setIncludeFlags(includeFlags);
    filter.setExcludeFlags(0);

    dtPolyRef startRef = 0, endRef = 0;
    float startPt[3], endPt[3];
    float detourStart[3] = { start.y, start.z, start.x };
    float detourEnd[3] = { end.y, end.z, end.x };
    // Reduced from { 500.0f, 1000.0f, 500.0f } to prevent snapping through walls
    float extent[3] = { 5.0f, zExtent, 5.0f };

    globalNavMesh.query->findNearestPoly(detourStart, extent, &filter, &startRef, startPt);
    globalNavMesh.query->findNearestPoly(detourEnd, extent, &filter, &endRef, endPt);

    // --- DEBUGGING: Why is path empty? ---
    if (!startRef) {
        if (DEBUG_PATHFINDING) g_LogFile << "[FindPath] FAIL: Could not find NavMesh near START point." << std::endl;
        return {};
    }
    if (!endRef) {
        if (DEBUG_PATHFINDING) g_LogFile << "[FindPath] FAIL: Could not find NavMesh near END point." << std::endl;
        return {};
    }

    // --- VERIFY Z-LEVEL MATCH ---
    // Even if we found a poly, check if it's way above or below the target.
    // detourEnd[1] is Z. endPt[1] is snapped Z.
    if (zCheck && (std::abs(endPt[1] - detourEnd[1]) > 5.0f)) {
        if (DEBUG_PATHFINDING) g_LogFile << "[FindPath] FAIL: End Poly is " << endPt[1]-detourEnd[1] << " above target." << std::endl;
        // The snapped point is more than 5 yards vertically from the request.
        // We likely snapped to a bridge/floor above. Treat as unreachable.
        return {};
    }

    dtPolyRef pathPolys[MAX_POLYS];
    int pathCount = 0;
    globalNavMesh.query->findPath(startRef, endRef, startPt, endPt, &filter,
        pathPolys, &pathCount, MAX_POLYS);

    if (pathCount <= 0) {
        if (DEBUG_PATHFINDING) g_LogFile << "[FindPath] FAIL: No path exists between Start and End (Islands)." << std::endl;
        return {};
    }

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

// -----------------------------------------------------------------------------------------
// REWRITTEN A* FLIGHT LOGIC WITH DYNAMIC GRID SIZING
// -----------------------------------------------------------------------------------------
struct AStarAttempt {
    float baseGridSize; // The SMALLEST step size (used near target)
    bool strict;
    int maxNodes;
    const char* name;
    bool dynamic;       // NEW: Enable variable step size
};

// ANGLED FLIGHT PATHFINDING - Natural diagonal ascent/descent
inline std::vector<PathNode> Calculate3DFlightPath(Vector3& start, Vector3& end, int mapId, bool isFlying, bool ignoreWater = true, float partialPathThreshold = 25.0f) {
    Vector3 rawStartPos = start;
    // Always open log for this specific debug request, or keep using DEBUG_PATHFINDING flag
    if (DEBUG_PATHFINDING) {
        g_LogFile << "\n=== CALCULATE 3D FLIGHT PATH ===" << std::endl;
        g_LogFile << "Start: (" << start.x << ", " << start.y << ", " << start.z << ")" << std::endl;
        g_LogFile << "End:   (" << end.x << ", " << end.y << ", " << end.z << ")" << std::endl;
    }

    // 3. ATTEMPT CONFIGURATIONS
    // We use a small 'baseGridSize' for precision, but 'dynamic=true' allows it to scale up when far away.
    AStarAttempt attempts[] = {
        // Base=4.0f means near the target we move in 4yd steps (Very precise).
        // Fixed coarse grid
        { 4.0f,  true,  10000, "Coarse Fixed",     true },

        { 2.0f,  false,  40000, "Standard Dynamic", true },

        // Fallback: Relaxed collision for tight spots
        { 4.0f,  false, 10000, "Relaxed Precision", true },

        // Last Resort: Ultra strict/fine for impossible spots
        { 3.0f,  false, 20000, "Ultra-Precision",   false }
    };

    float GROUND_HEIGHT_THRESHOLD = 7.0f;

    // 1. VALIDATE END POSITION
    Vector3 actualStart = start;
    Vector3 actualEnd = end;

    float groundZ = globalNavMesh.GetLocalGroundHeight(start);
    bool startingFromGround = (groundZ > -90000.0f && std::abs(start.z - groundZ) < 6.0f);

    //if (groundZ > -90000.0f && start.z < groundZ + MIN_CLEARANCE) {
    //    actualStart.z = groundZ + MIN_CLEARANCE;
    //    if (CheckFMapLine(mapId, actualStart.x, actualStart.y, actualStart.z,
    //        actualStart.x, actualStart.y, actualStart.z - 1.0f)) {
    //        // Adjusted position is invalid (feet hit terrain)
    //        // Don't adjust - use original position and let escape logic handle it
    //        if (DEBUG_PATHFINDING) {
    //            g_LogFile << "Adjusted position invalid (terrain at feet), using original Z" << std::endl;
    //        }
    //        actualStart.z = start.z;
    //    }
    //    else {
    //        start.z = actualStart.z;
    //        if (DEBUG_PATHFINDING) {
    //            g_LogFile << "Adjusted start height to: " << actualStart.z << " (ground: " << groundZ << ")" << std::endl;
    //        }
    //    }
    //}

    // --- ESCAPE LOGIC: USE UNIVERSAL SPIRAL ---
    if (!globalNavMesh.IsClearSafePoint(actualStart, mapId) && g_GameState->player.isFlying) {
        if (DEBUG_PATHFINDING) g_LogFile << "[Flight] Start point is inside obstacle safety margin (" << AGENT_RADIUS << "yd). Seeking escape..." << std::endl;

        Vector3 safeStart = globalNavMesh.FindNearestSafePoint(actualStart, mapId, false);

        if (safeStart.x != 0.0f || safeStart.y != 0.0f || safeStart.z != 0.0f) {
            if (DEBUG_PATHFINDING) {
                g_LogFile << "[Flight] Found escape point at (" << safeStart.x << ", " << safeStart.y << ", " << safeStart.z
                    << ") - Dist: " << actualStart.Dist3D(safeStart) << std::endl;
            }
            actualStart = safeStart;
        }
        else {
            if (DEBUG_PATHFINDING) g_LogFile << "[Flight] ! Could not find a safe escape point nearby." << std::endl;
        }
    }

    // FLIGHT POINT CHECK WITH A*
    //if (!globalNavMesh.CheckFlightPoint(actualStart, mapId, false)) {
    //    if (DEBUG_PATHFINDING) g_LogFile << "[Flight] Start point invalid. Scanning local grid for valid A* node..." << std::endl;

    //    Vector3 bestStart = actualStart;
    //    float bestDist = 1e9f;
    //    bool found = false;

    //    // Search Radius: 4 nodes (approx 16 yards) in every direction
    //    int radius = 4;
    //    float step = attempts[0].baseGridSize; // Must match att.baseGridSize]
    //    
    //    g_LogFile << actualStart.x << " " << actualStart.y << " " << actualStart.z << std::endl;

    //    for (int z = -radius; z <= radius; ++z) {
    //        for (int y = -radius; y <= radius; ++y) {
    //            for (int x = -radius; x <= radius; ++x) {
    //                if (x == 0 && y == 0 && z == 0) continue;

    //                Vector3 candidate = actualStart + Vector3(x * step, y * step, z * step);

    //                g_LogFile << "Candidate: " << candidate.x << " " << candidate.y << " " << candidate.z << std::endl;
    //                // 1. IS IT A VALID A* NODE?
    //                // This uses the EXACT same check A* uses to accept/reject nodes.
    //                if (!globalNavMesh.CheckFlightPoint(candidate, mapId, false)) continue;

    //                // 2. IS IT REACHABLE?
    //                // Use strict collision to ensure we don't clip the cliff edge getting there.
    //                Vector3 fail;
    //                if (globalNavMesh.CheckFlightSegmentDetailed(actualStart, candidate, mapId, fail, isFlying, true, true) == SEGMENT_VALID) {
    //                    g_LogFile << "A" << std::endl;
    //                    float d = candidate.Dist3D(actualStart);
    //                    if (d < bestDist) {
    //                        bestDist = d;
    //                        bestStart = candidate;
    //                        found = true;
    //                    }
    //                }
    //            }
    //        }
    //    }

    //    if (found) {
    //        if (DEBUG_PATHFINDING) g_LogFile << "[Flight] Relocated start to valid node at: " << bestStart.x << " " << bestStart.y << " " << bestStart.z << std::endl;
    //        actualStart = bestStart;
    //    }
    //    else {
    //        if (DEBUG_PATHFINDING) g_LogFile << "[Flight] FAILED to find any valid A* node nearby." << std::endl;
    //    }
    //}


    float startGround = GetFMapFloorHeight(mapId, actualStart.x, actualStart.y, actualStart.z + 2.0f);
    if (startGround > -90000.0f && actualStart.z < startGround + 1.0f && startingFromGround && !g_GameState->player.isFlying) {
        actualStart.z = startGround + 2.0f;
        actualStart.z = actualStart.z;
        if (DEBUG_PATHFINDING) {
            g_LogFile << "Adjusted start height to: " << actualStart.z << " (ground: " << startGround << ")" << std::endl;
        }
    }

    float endGround = GetFMapFloorHeight(mapId, end.x, end.y, end.z + 2.0f);
    if (endGround > -90000.0f && end.z < endGround + 1.0f) {
        actualEnd.z = endGround + 2.0f;
        end.z = actualEnd.z;
        if (DEBUG_PATHFINDING) {
            g_LogFile << "Adjusted end height to: " << actualEnd.z << " (ground: " << endGround << ")" << std::endl;
        }
    }

    // If the goal is on the ground and we're descending to it, be more lenient
    if (endGround > -90000.0f && end.z < endGround + GROUND_HEIGHT_THRESHOLD) {
        // Adjust end to be just slightly above ground instead of MIN_CLEARANCE
        actualEnd.z = endGround + 1.0f; // Changed from MIN_CLEARANCE + 1.0f
        end.z = actualEnd.z;
        if (DEBUG_PATHFINDING) {
            g_LogFile << "Goal is on ground, adjusted end height to: " << actualEnd.z
                << " (ground: " << endGround << ")" << std::endl;
        }
    }

    Vector3 flightGoal = actualEnd; // The target for the Flight A*
    Vector3 groundStart = actualStart; // The start for the Flight A*
    std::vector<PathNode> groundApproach; // The walking path from landing to final goal
    std::vector<PathNode> launchApproach; // The walking path from start to launch spot

    // -------------------------------------------------------------------------
    // 1. UNREACHABLE START RESCUE (The Logic You Requested)
    // -------------------------------------------------------------------------

    // Check if the final destination is valid for flight
    bool isGoalFlyable = globalNavMesh.CheckFlightPoint(actualStart, mapId, true); // Allow ground proximity

    if (!isGoalFlyable) {
        if (DEBUG_PATHFINDING) g_LogFile << "[Flight] Start is NOT valid for flight (Indoor/Blocked). Searching for Start Flight Spot..." << std::endl;

        // Spiral Search settings
        const float MAX_LANDING_SEARCH = 60.0f; // Look up to 100y away
        const float STEP_SIZE = 4.0f;
        bool foundLanding = false;

        // Spiral loop
        for (float r = STEP_SIZE; r <= MAX_LANDING_SEARCH; r += STEP_SIZE) {
            if (foundLanding) break;

            // Check 8 directions
            Vector3 offsets[] = {
                Vector3(r,0,0), Vector3(-r,0,0), Vector3(0,r,0), Vector3(0,-r,0),
                Vector3(r,r,0), Vector3(-r,-r,0), Vector3(r,-r,0), Vector3(-r,r,0)
            };

            for (const auto& off : offsets) {
                Vector3 candidate = actualStart + off;

                // 1. Snap Candidate to Ground (Crucial: Must be walkable)
                float gZ = globalNavMesh.GetLocalGroundHeight(candidate);
                if (gZ <= -90000.0f) continue; // No ground here
                candidate.z = gZ + 1.0f;       // Stand on ground

                // 2. Is this spot Flyable? (Can we land here?)
                // We check if we can fly at 'Head Height' (gZ + 2.0f) or 'Hover Height' (gZ + 5.0f)
                if (CanFlyAt(mapId, candidate.x, candidate.y, candidate.z + 2.0f)) {

                    // 3. Is it connected to the Goal? (Ground Path Check)
                    // We calculate path FROM Launch TO Goal
                    std::vector<PathNode> approach = FindPath(actualStart, candidate, ignoreWater, false);

                    if (!approach.empty() && approach.back().pos.Dist3D(actualStart) < 5.0f) {
                        // SUCCESS!
                        groundStart = candidate;
                        // Lift flight goal slightly to ensure we don't crash into terrain while landing
                        groundStart.z += 2.0f;
                        launchApproach = approach;
                        foundLanding = true;

                        if (DEBUG_PATHFINDING) {
                            g_LogFile << "[Flight] ✓ Found Launch Spot at (" << candidate.x << ", " << candidate.y << ", " << candidate.z << ")!" << std::endl;
                            g_LogFile << "         Ground path length: " << approach.size() << " nodes." << std::endl;
                        }
                        break;
                    }
                }
            }
        }

        if (!foundLanding) {
            if (DEBUG_PATHFINDING) g_LogFile << "[Flight] ⚠ Could not find any connected launch spot nearby. A* might fail." << std::endl;
        }
    }
    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // 1. UNREACHABLE GOAL RESCUE (The Logic You Requested)
    // -------------------------------------------------------------------------

    // Check if the final destination is valid for flight
    isGoalFlyable = globalNavMesh.CheckFlightPoint(actualEnd, mapId, true); // Allow ground proximity

    if (!isGoalFlyable) {
        if (DEBUG_PATHFINDING) g_LogFile << "[Flight] Destination is NOT valid for flight (Indoor/Blocked). Searching for Landing Spot..." << std::endl;

        // Spiral Search settings
        const float MAX_LANDING_SEARCH = 60.0f; // Look up to 100y away
        const float STEP_SIZE = 4.0f;
        bool foundLanding = false;

        // Spiral loop
        for (float r = STEP_SIZE; r <= MAX_LANDING_SEARCH; r += STEP_SIZE) {
            if (foundLanding) break;

            // Check 8 directions
            Vector3 offsets[] = {
                Vector3(r,0,0), Vector3(-r,0,0), Vector3(0,r,0), Vector3(0,-r,0),
                Vector3(r,r,0), Vector3(-r,-r,0), Vector3(r,-r,0), Vector3(-r,r,0)
            };

            for (const auto& off : offsets) {
                Vector3 candidate = actualEnd + off;

                // 1. Snap Candidate to Ground (Crucial: Must be walkable)
                float gZ = globalNavMesh.GetLocalGroundHeight(candidate);
                if (gZ <= -90000.0f) continue; // No ground here
                candidate.z = gZ + 1.0f;       // Stand on ground

                // 2. Is this spot Flyable? (Can we land here?)
                // We check if we can fly at 'Head Height' (gZ + 2.0f) or 'Hover Height' (gZ + 5.0f)
                if (CanFlyAt(mapId, candidate.x, candidate.y, candidate.z + 2.0f)) {

                    // 3. Is it connected to the Goal? (Ground Path Check)
                    // We calculate path FROM Landing TO Goal (as you requested)
                    std::vector<PathNode> approach = FindPath(candidate, actualEnd, ignoreWater, false);

                    if (!approach.empty() && approach.back().pos.Dist3D(actualEnd) < 5.0f) {
                        // SUCCESS!
                        flightGoal = candidate;
                        // Lift flight goal slightly to ensure we don't crash into terrain while landing
                        flightGoal.z += 2.0f;
                        groundApproach = approach;
                        foundLanding = true;

                        if (DEBUG_PATHFINDING) {
                            g_LogFile << "[Flight] ✓ Found Landing Spot at (" << candidate.x << ", " << candidate.y << ", " << candidate.z << ")!" << std::endl;
                            g_LogFile << "         Ground path length: " << approach.size() << " nodes." << std::endl;
                        }
                        break;
                    }
                }
            }
        }

        if (!foundLanding) {
            if (DEBUG_PATHFINDING) g_LogFile << "[Flight] ⚠ Could not find any connected landing spot nearby. A* might fail." << std::endl;
        }
    }
    // -------------------------------------------------------------------------

    // 2. TRY DIRECT PATH FIRST (Optimization)
    if (DEBUG_PATHFINDING) g_LogFile << "Checking direct path..." << std::endl;

    // REPLACED: Use Detailed check to handle No-Fly Zones gracefully
    // Pass isFlying flag to collision check
    Vector3 failPos;
    FlightSegmentResult directCheck = globalNavMesh.CheckFlightSegmentDetailed(
        groundStart, flightGoal, mapId, failPos, isFlying, true, true);

    if (directCheck == SEGMENT_VALID) {
        if (DEBUG_PATHFINDING) g_LogFile << "✓ Direct path clear!" << std::endl;

        std::vector<PathNode> path;

        // APPEND LAUNCH APPROACH
        if (!launchApproach.empty()) {
            // Add transition node (Launching)
            path.insert(path.end(), launchApproach.begin(), launchApproach.end());
            path.push_back(PathNode(groundStart, PATH_GROUND));
        }
        else {
            path.push_back(PathNode(groundStart, PATH_AIR));
        }

        // APPEND GROUND APPROACH
        if (!groundApproach.empty()) {
            // Add transition node (Landing)
            path.push_back(PathNode(flightGoal, PATH_GROUND));
            path.insert(path.end(), groundApproach.begin(), groundApproach.end());
        }
        else {
            path.push_back(PathNode(flightGoal, PATH_AIR));
        }
        return path;
    }
    //else if (directCheck == SEGMENT_NO_FLY_ZONE) {
    //    // --- SMART RECOVERY FOR NO-FLY ZONES ---
    //    if (DEBUG_PATHFINDING) {
    //        g_LogFile << "⚠ Direct path hit No-Fly Zone at (" << failPos.x << ", " << failPos.y << ", " << failPos.z << ")" << std::endl;
    //        g_LogFile << "  Attempting to find nearest valid flyable point..." << std::endl;
    //    }

    //    Vector3 detourPoint = globalNavMesh.FindNearestFlyablePoint(failPos, mapId);

    //    // If we found a valid detour point (not 0,0,0)
    //    if (detourPoint.x != 0 || detourPoint.y != 0 || detourPoint.z != 0) {
    //        if (DEBUG_PATHFINDING) {
    //            g_LogFile << "  Found detour point: (" << detourPoint.x << ", " << detourPoint.y << ", " << detourPoint.z << ")" << std::endl;
    //            g_LogFile << "  Validating legs: Start->Detour and Detour->End..." << std::endl;
    //        }

    //        // Validate the legs
    //        if (globalNavMesh.CheckFlightSegment(actualStart, detourPoint, mapId, isFlying, true, true) &&
    //            globalNavMesh.CheckFlightSegment(detourPoint, flightGoal, mapId, isFlying, true, true)) {

    //            if (DEBUG_PATHFINDING) {
    //                g_LogFile << "✓ Smart Recovery Successful! Created detour path." << std::endl;
    //            }
    //            return { PathNode(actualStart, PATH_AIR), PathNode(detourPoint, PATH_AIR), PathNode(flightGoal, PATH_AIR) };
    //        }
    //        else {
    //            // --- NEW LOGIC: DETOUR LEGS BLOCKED (CHECK END POINT) ---
    //            if (DEBUG_PATHFINDING) {
    //                g_LogFile << "  ✗ Detour legs were blocked. Checking if End Point is the issue..." << std::endl;
    //            }

    //            // Check if the destination itself is inside a No-Fly Zone
    //            if (!CanFlyAt(mapId, flightGoal.x, flightGoal.y, actualEnd.z)) {
    //                if (DEBUG_PATHFINDING) {
    //                    g_LogFile << "  ! END POINT is in a No-Fly Zone. Searching for nearest safe destination..." << std::endl;
    //                }

    //                Vector3 safeEnd = globalNavMesh.FindNearestFlyablePoint(actualEnd, mapId);

    //                // If we found a safe replacement for the destination
    //                if (safeEnd.x != 0 || safeEnd.y != 0 || safeEnd.z != 0) {
    //                    if (DEBUG_PATHFINDING) {
    //                        g_LogFile << "  Found safe destination: (" << safeEnd.x << ", " << safeEnd.y << ", " << safeEnd.z << ")" << std::endl;
    //                        g_LogFile << "  Retrying with safe destination..." << std::endl;
    //                    }

    //                    // Retry the path: Start -> Detour -> SafeEnd
    //                    if (globalNavMesh.CheckFlightSegment(actualStart, detourPoint, mapId, isFlying, true, true) &&
    //                        globalNavMesh.CheckFlightSegment(detourPoint, safeEnd, mapId, isFlying, true, true)) {

    //                        if (DEBUG_PATHFINDING) {
    //                            g_LogFile << "✓ Smart Recovery (Modified End) Successful!" << std::endl;
    //                        }
    //                        return { PathNode(actualStart, PATH_AIR), PathNode(detourPoint, PATH_AIR), PathNode(safeEnd, PATH_AIR) };
    //                    }
    //                }
    //            }
    //        }
    //    }
    //    else {
    //        if (DEBUG_PATHFINDING) {
    //            g_LogFile << "  ✗ Could not find a valid flyable point nearby." << std::endl;
    //        }
    //    }
    //}


    // Pre-allocate containers
    std::vector<FlightNode3D> nodes;
    nodes.reserve(500000);
    std::unordered_map<GridKey, int, GridKeyHash> gridToIndex;
    gridToIndex.reserve(500000);
    std::unordered_set<int> closedSet;
    closedSet.reserve(150000);
    // -----------------------------------------------------------

    for (int i = 0; i < 4; ++i) {
        AStarAttempt& att = attempts[i];
        // RESET
        nodes.clear();
        gridToIndex.clear();
        closedSet.clear();

        IndexPriorityQueue openSet(&nodes); 

        if (DEBUG_PATHFINDING) {
            g_LogFile << ">>> A* Attempt " << (i + 1) << " (" << att.name << ") <<<" << std::endl;
            g_LogFile << "   BaseGrid: " << att.baseGridSize << " | Dynamic: " << att.dynamic << std::endl;
        }

        float currentSearchRadius = (groundStart.Dist3D(flightGoal) * 0.8f) + 200.0f; // Increased search radius slightly
        Vector3 midpoint = (groundStart + flightGoal) * 0.5f;

        // NOTE: GridKey ALWAYS uses baseGridSize for hashing to ensure nodes align correctly 
        // regardless of the step size used to reach them.

        auto GetOrCreateNode = [&](const Vector3& pos) -> int {
            GridKey key(pos, att.baseGridSize);
            auto it = gridToIndex.find(key);
            if (it != gridToIndex.end()) return it->second;

            // Snap position to base grid to prevent floating point drift
            // This ensures a "Large Step" lands exactly on a "Small Step" node.
            Vector3 snappedPos(
                (key.x + 0.5f) * att.baseGridSize,
                (key.y + 0.5f) * att.baseGridSize,
                (key.z + 0.5f) * att.baseGridSize
            );

            if (snappedPos.Dist3D(midpoint) > currentSearchRadius + 50.0f) return -1;
            if (!globalNavMesh.CheckFlightPoint(snappedPos, mapId, !att.strict)) return -1;

            size_t idx = nodes.size();
            nodes.emplace_back();
            nodes[idx].pos = snappedPos;
            nodes[idx].hScore = snappedPos.Dist3D(flightGoal);
            gridToIndex[key] = idx;
            return idx;
            };

        // Initialize Start/End (Keep existing logic)
        size_t startIdx = nodes.size();
        nodes.emplace_back();
        nodes[startIdx].pos = groundStart;
        nodes[startIdx].gScore = 0.0f;
        nodes[startIdx].hScore = groundStart.Dist3D(flightGoal);
        gridToIndex[GridKey(groundStart, att.baseGridSize)] = startIdx; // Hash with Base Grid

        size_t endIdx = nodes.size();
        nodes.emplace_back();
        nodes[endIdx].pos = flightGoal;
        nodes[endIdx].hScore = 0.0f;

        bool endIsValid = globalNavMesh.CheckFlightPoint(flightGoal, mapId, true);
        if (endIsValid) {
            gridToIndex[GridKey(flightGoal, att.baseGridSize)] = endIdx;
        }

        openSet.push(startIdx);
        int goalIdx = -1;
        int iterations = 0;

        // Neighbor Directions (Keep existing array)
        const int neighborDirs[][3] = {
            {1,0,1}, {-1,0,1}, {0,1,1}, {0,-1,1},
            {1,1,1}, {1,-1,1}, {-1,1,1}, {-1,-1,1},
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0},
            {1,1,0}, {1,-1,0}, {-1,1,0}, {-1,-1,0},
            {0,0,1}, {0,0,-1},
            {1,0,-1}, {-1,0,-1}, {0,1,-1}, {0,-1,-1}
        };

        float totalDistToGoal = groundStart.Dist3D(flightGoal);
        if (totalDistToGoal > 2000.0f) att.maxNodes *= 16;
        else if (totalDistToGoal > 1000.0f) att.maxNodes *= 8;
        else if (totalDistToGoal > 200.0f) att.maxNodes *= 4;
        else if (totalDistToGoal > 50.0f) att.maxNodes *= 2;

        while (!openSet.empty() && iterations < att.maxNodes) {
            int currentIdx = openSet.top();
            openSet.pop();

            if (closedSet.count(currentIdx)) continue;
            closedSet.insert(currentIdx);
            iterations++;

            Vector3 currentPos = nodes[currentIdx].pos;
            float currentG = nodes[currentIdx].gScore;

            float distToGoal = currentPos.Dist3D(flightGoal);

            // --- DYNAMIC STEP SIZING LOGIC ---
            float currentStep = att.baseGridSize;

            if (att.dynamic) {
                // If far away, use larger steps to cover ground quickly.
                // Multipliers must be integers so nodes align with the base grid.
                bool nearStart = currentPos.Dist3D(groundStart) < 20.0f;
                bool nearEnd = currentPos.Dist3D(flightGoal) < 20.0f;
                //if (nearStart || nearEnd) {
                    //currentStep /= 2.0f;  // Causes issues for some reason
                //}
                //bool nearStart = current.gScore < 20.0f;
                //bool nearEnd = distToGoal < 20.0f;

                //if (!nearStart || !nearEnd) {
                    if (distToGoal > 1000.0f) { currentStep *= 8.0f; } // e.g. 8.0 * 2 = 32 yard steps
                    else if (distToGoal > 200.0f) { currentStep *= 4.0f; } // e.g. 4.0 * 4 = 16 yard steps
                    else if (distToGoal > 50.0f) { currentStep *= 2.0f; } // e.g. 4.0 * 2 = 8 yard steps
                //}
                // else: < 50 yards, use baseGridSize (4 yards) for precision
            }

            // Check connection to goal (Keep existing logic)
            if (currentIdx == endIdx || distToGoal < (currentStep * 1.5f)) { // Adjusted threshold based on step
                Vector3 failPos;
                if (globalNavMesh.CheckFlightSegmentDetailed(currentPos, flightGoal, mapId, failPos, isFlying, false) == SEGMENT_VALID) {
                    if (currentIdx != endIdx) {
                        nodes[endIdx].parentIdx = currentIdx;
                        nodes[endIdx].gScore = currentG + distToGoal;
                    }
                    goalIdx = endIdx;
                    break;
                }
                g_LogFile << currentPos.x << " " << currentPos.y << " " << currentPos.z << std::endl;
            }

            // Expand Neighbors
            for (int j = 0; j < 22; ++j) {
                // Apply dynamic step size here
                Vector3 offset(
                    neighborDirs[j][0] * currentStep,
                    neighborDirs[j][1] * currentStep,
                    neighborDirs[j][2] * currentStep
                );

                Vector3 neighborPos = currentPos + offset;

                if (neighborPos.Dist3D(midpoint) > currentSearchRadius) continue;

                int neighborIdx = GetOrCreateNode(neighborPos);
                if (neighborIdx < 0 || closedSet.count(neighborIdx)) continue;

                bool isGoalNeighbor = (neighborIdx == endIdx);
                bool strictCheck = att.strict && !isGoalNeighbor;

                if (nodes[neighborIdx].pos.Dist3D(currentPos) < currentStep) {
                    strictCheck = false;
                }

                // Collision check must cover the full dynamic step distance
                if (!globalNavMesh.CheckFlightSegment(currentPos, nodes[neighborIdx].pos, mapId,
                    isFlying, strictCheck, false)) {
                    continue;
                }

                float dist = currentPos.Dist3D(nodes[neighborIdx].pos);
                float bonus = (neighborDirs[j][2] > 0) ? 0.95f : 1.0f;

                float tentativeG = currentG + (dist * bonus);
                if (tentativeG < nodes[neighborIdx].gScore) {
                    nodes[neighborIdx].parentIdx = currentIdx;
                    nodes[neighborIdx].gScore = tentativeG;
                    openSet.push(neighborIdx);
                }
            }
        }

        // 1. FIND BEST PARTIAL NODE (If goal not reached)
        int bestPartialIdx = -1;
        if (goalIdx < 0 && !closedSet.empty()) {
            float closestDist = 1e9f;
            for (int idx : closedSet) {
                float d = nodes[idx].pos.Dist3D(flightGoal);
                if (d < closestDist) {
                    closestDist = d;
                    bestPartialIdx = idx;
                }
            }
        }

        //DIAGNOSTICS
        if (goalIdx < 0 && DEBUG_PATHFINDING) {
            g_LogFile << "\n   [DIAGNOSTIC] Why did A* fail?" << std::endl;

            // Check if end node exists
            if (endIdx < nodes.size()) {
                g_LogFile << "   - End node exists at index " << endIdx << "" << std::endl;
                g_LogFile << "   - End node position: (" << nodes[endIdx].pos.x << ","
                    << nodes[endIdx].pos.y << "," << nodes[endIdx].pos.z << ")" << std::endl;
                g_LogFile << "   - End node gScore: " << nodes[endIdx].gScore << "" << std::endl;
                g_LogFile << "   - End node parent: " << nodes[endIdx].parentIdx << "" << std::endl;

                // Check if it was ever added to open set
                bool inClosed = closedSet.count(endIdx);
                g_LogFile << "   - End node in closed set: " << inClosed << "" << std::endl;

                // Find closest node that WAS explored
                float closestDist = 1e9f;
                int closestIdx = -1;
                for (int idx : closedSet) {
                    float d = nodes[idx].pos.Dist3D(flightGoal);
                    if (d < closestDist) {
                        closestDist = d;
                        closestIdx = idx;
                    }
                }

                if (closestIdx >= 0) {
                    g_LogFile << "   - Closest explored node: (" << nodes[closestIdx].pos.x << ","
                        << nodes[closestIdx].pos.y << "," << nodes[closestIdx].pos.z << ")" << std::endl;
                    g_LogFile << "   - Distance from closest to goal: " << closestDist << "" << std::endl;

                    g_LogFile << nodes[closestIdx].pos.x << " " << nodes[closestIdx].pos.y << " " << nodes[closestIdx].pos.z << " | " << flightGoal.x << " " << flightGoal.y << " " << flightGoal.z << " " << std::endl;

                    // Try to connect them
                    Vector3 failPos;
                    FlightSegmentResult testResult = globalNavMesh.CheckFlightSegmentDetailed(
                        nodes[closestIdx].pos, flightGoal, mapId, failPos, isFlying, false, true);

                    g_LogFile << "   - Can connect closest to goal: "
                        << (testResult == SEGMENT_VALID ? "YES" : "NO") << "" << std::endl;
                    if (testResult != SEGMENT_VALID) {
                        g_LogFile << "   - Blockage at: (" << failPos.x << "," << failPos.y << "," << failPos.z << ")" << std::endl;
                    }
                }
            }
            g_LogFile << "" << std::endl;
        }

        // --- RESULT CHECK & RECONSTRUCTION ---
        // Determine which node to start tracing back from
        int traceStartIdx = -1;
        bool isPartial = false;
        if (goalIdx >= 0) {
            traceStartIdx = goalIdx; // Success
        }
        else if (bestPartialIdx >= 0) {
            // Check if partial path is worth it (must be closer than start)
            float startDist = groundStart.Dist3D(flightGoal);
            float currentDist = nodes[bestPartialIdx].pos.Dist3D(flightGoal);
            g_LogFile << currentDist << " " << startDist << std::endl;

            // Allow partial if we made progress (e.g. moved at least 5 yards closer)
            if (currentDist < startDist - partialPathThreshold) {
                traceStartIdx = bestPartialIdx;
                isPartial = true;
                if (DEBUG_PATHFINDING) {
                    g_LogFile << "A* Partial Success: Returning path to closest point (Dist: " << currentDist << ")" << std::endl;
                }
            }
        }

        // Reconstruct Path if we have a valid start index
        if (traceStartIdx >= 0) {
            std::vector<PathNode> path;

            // APPEND LAUNCH APPROACH
            if (!launchApproach.empty()) {
                // Optional: Add landing node
                path.insert(path.end(), launchApproach.begin(), launchApproach.end());
                path.push_back(PathNode(groundStart, PATH_GROUND));
            }

            int curr = traceStartIdx;
            // Safety check for loop limit to prevent infinite loops if parentIdx is corrupted
            int safety = 0;
            while (curr >= 0 && safety++ < 5000) {
                path.push_back(PathNode(nodes[curr].pos, PATH_AIR));
                curr = nodes[curr].parentIdx;
            }
            std::reverse(path.begin(), path.end());

            // --- STRICT SUCCESS CHECK (Skipped if we explicitly allowed partial) ---
            if (!isPartial && path.back().pos.Dist3D(flightGoal) > 5.0f) {
                if (DEBUG_PATHFINDING) {
                    g_LogFile << "✗ Attempt " << (i + 1) << " found path but it stops short ("
                        << path.back().pos.Dist3D(flightGoal) << " yds). Retrying..." << std::endl;
                }
                // If you want to force retries for better paths, uncomment continue. 
                // But since you want partial paths, we generally accept this.
                // continue; 
            }

            // Path Smoothing
            if (path.size() > 2) {
                std::vector<PathNode> smoothed;
                smoothed.push_back(path[0]);
                size_t c = 0;
                while (c < path.size() - 1) {
                    size_t f = c + 1;
                    for (size_t n = path.size() - 1; n > c + 1; --n) {
                        // Change 'strictCollision' to 'true'
                        // This forces the optimized path to ALWAYS respect the full safety radius,
                        // even if the original search had to relax constraints to find a route.
                        if (globalNavMesh.CheckFlightSegment(path[c].pos, path[n].pos, mapId, isFlying, true)) {
                            f = n;
                            break;
                        }
                    }
                    smoothed.push_back(path[f]);
                    c = f;
                }
                path = smoothed;
            }

            // APPEND GROUND APPROACH
            if (!groundApproach.empty()) {
                // Optional: Add landing node
                path.push_back(PathNode(flightGoal, PATH_GROUND));
                path.insert(path.end(), groundApproach.begin(), groundApproach.end());
            }

            if (DEBUG_PATHFINDING) g_LogFile << "✓ A* SUCCESS on Attempt " << (i + 1) << std::endl;
            return path;
        }

        // --- FAILURE FALLBACK (On Final Attempt) ---
        if (i == 1) {
            return {};
            if (DEBUG_PATHFINDING) g_LogFile << "   [FALLBACK] Attempting Hybrid Air-to-Ground Recovery..." << std::endl;

            // 1. Find the node closest to the destination
            int bestIdx = -1;
            float closestDist = 1e9f;

            for (int idx : closedSet) {
                float d = nodes[idx].pos.Dist3D(flightGoal);
                if (d < closestDist) {
                    closestDist = d;
                    bestIdx = idx;
                }
            }

            if (bestIdx != -1) {
                // 2. Reconstruct Flight Path to this "Best" point
                std::vector<PathNode> recoveryPath;
                int curr = bestIdx;
                while (curr >= 0) {
                    recoveryPath.push_back(PathNode(nodes[curr].pos, PATH_AIR));
                    curr = nodes[curr].parentIdx;
                }
                std::reverse(recoveryPath.begin(), recoveryPath.end());

                Vector3 lastAirPos = recoveryPath.back().pos;

                // 3. Find Ground Position below/near this point
                float groundZ = globalNavMesh.GetLocalGroundHeight(lastAirPos);
                Vector3 landPos = lastAirPos;

                // If we found a valid floor below us, drop to it
                if (groundZ > -90000.0f) {
                    landPos.z = groundZ + 1.0f; // Landing clearance
                }
                else {
                    // Fallback: If GetLocalGroundHeight fails, we rely on FindPath finding the mesh 
                    // nearest to the air coordinates.
                }

                if (DEBUG_PATHFINDING) {
                    g_LogFile << "   [FALLBACK] Landing at: " << landPos.x << "," << landPos.y << "," << landPos.z << std::endl;
                }

                // 4. Calculate Ground Path to actual target
                std::vector<PathNode> groundPath = FindPath(landPos, flightGoal, ignoreWater);

                if (!groundPath.empty() && groundPath.back().pos.Dist3D(end) < 3.0f) {
                    if (DEBUG_PATHFINDING) g_LogFile << "   [FALLBACK] ✓ Found Ground Path (" << groundPath.size() << " wps). Stitching..." << std::endl;

                    // 5. Stitch: Flight -> Landing Spot -> Ground Path
                    recoveryPath.push_back(PathNode(landPos, PATH_GROUND));
                    recoveryPath.insert(recoveryPath.end(), groundPath.begin(), groundPath.end());

                    return recoveryPath; // Return the hybrid path
                }
            }
        }
    }
    
    if (DEBUG_PATHFINDING) g_LogFile << "!!! CRITICAL: All flight attempts failed." << std::endl;
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

// --- PATH OPTIMIZATION (Removes collinear points) ---
inline std::vector<PathNode> OptimizeFlightPath(const std::vector<PathNode>& path, float minDistance = 25.0f) {
    if (path.size() < 3) return path;

    std::vector<PathNode> optimized;
    optimized.reserve(path.size());
    optimized.push_back(path[0]);

    for (size_t i = 1; i < path.size() - 1; ++i) {
        const Vector3& prev = optimized.back().pos; // Reference the LAST KEPT point
        const Vector3& curr = path[i].pos;
        const Vector3& next = path[i + 1].pos;

        // 1. Mandatory Keep: Type Change (Always keep transitions like Ground -> Air)
        if (optimized.back().type != path[i].type) {
            optimized.push_back(path[i]);
            continue;
        }

        // 2. Minimum Distance Check
        // If this point is too close to the last kept point, skip it to sparse out the path.
        float distFromLast = prev.Dist3D(curr);
        if (distFromLast < minDistance) {
            continue;
        }

        // 3. Collinear Check
        // Only keep if there is a meaningful turn relative to the last kept point
        Vector3 v1 = (curr - prev).Normalize();
        Vector3 v2 = (next - curr).Normalize();

        float dot = v1.Dot(v2);

        if (dot < 0.995f) { // Keep point if there is a turn (~5.7 degrees)
            optimized.push_back(path[i]);
        }
    }

    // Always keep the destination
    if (optimized.back().pos.Dist3D(path.back().pos) > 0.1f) {
        optimized.push_back(path.back());
    }

    return optimized;
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

// --- PATH POST-PROCESSING (Fixes Doorways/Stairs) ---
// "Nudges" waypoints away from walls/corners to prevent clipping.
inline std::vector<PathNode> NudgeGroundPath(std::vector<PathNode> path, float amount, int mapId) {
    if (path.size() < 3) return path;

    if (DEBUG_PATHFINDING) {
        g_LogFile << "[PathCleaner] Nudging ground path to prevent corner clipping..." << std::endl;
    }

    std::vector<PathNode> newPath = path;

    for (size_t i = 1; i < path.size() - 1; ++i) {
        // Only process ground waypoints
        if (path[i].type != PATH_GROUND) continue;

        Vector3 prev = path[i - 1].pos;
        Vector3 curr = path[i].pos;
        Vector3 next = path[i + 1].pos;

        // Calculate vectors (Ignore Z to ensure horizontal nudging only)
        Vector3 v1 = (prev - curr);
        v1.z = 0; v1 = v1.Normalize();

        Vector3 v2 = (next - curr);
        v2.z = 0; v2 = v2.Normalize();

        // Calculate Bisector: (A-B) + (C-B) gives the vector pointing "inwards" to the open space
        // for a path wrapping around a corner.
        Vector3 bisector = (v1 + v2).Normalize();

        // If path is effectively straight, don't nudge
        if (bisector.Length() < 0.1f) continue;

        // Calculate target position
        Vector3 nudgeVec = bisector * amount;
        Vector3 testPos = curr + nudgeVec;

        // --- VALIDATION ---
        // 1. Check if we pushed into a wall (Line of Sight from old pos to new pos)
        // Check slightly above ground to avoid curb collision
        if (!CheckFMapLine(mapId, curr.x, curr.y, curr.z + 1.0f, testPos.x, testPos.y, curr.z + 1.0f)) {

            // 2. Check if there is valid floor at the new position
            // (Don't nudge off a bridge or stair edge)
            float floorZ = GetFMapFloorHeight(mapId, testPos.x, testPos.y, curr.z + 2.5f);

            if (floorZ > -90000.0f) {
                // Ensure the floor isn't too far up/down (e.g., dropped off a cliff)
                if (std::abs(floorZ - curr.z) < 2.5f) {
                    // Apply Nudge
                    newPath[i].pos.x = testPos.x;
                    newPath[i].pos.y = testPos.y;
                    newPath[i].pos.z = floorZ + 0.5f; // Snap to new floor with slight clearance
                }
            }
        }
    }
    return newPath;
}

// Probes for all valid Z-layers at a specific X, Y location
inline std::vector<float> GetPossibleZLayers(int mapId, float x, float y) {
    std::vector<float> layers;

    // Start scanning from high up (e.g., 2000.0f or map ceiling) down to the bottom
    float checkZ = 2000.0f;

    // Safety break after 50 layers to prevent infinite loops
    int safety = 0;

    while (checkZ > -1000.0f && safety < 50) {
        // Find the floor immediately below checkZ
        float floorZ = GetFMapFloorHeight(mapId, x, y, checkZ);

        // Check for invalid return (usually -99999.0f or similar large negative)
        if (floorZ < -5000.0f) break;

        // If it's a new layer (distinct from the last one found), add it
        // We use a 2.0f tolerance to avoid duplicate hits on uneven terrain
        if (layers.empty() || std::abs(layers.back() - floorZ) > 2.0f) {
            layers.push_back(floorZ);
        }

        // Prepare to scan for the next floor below this one
        checkZ = floorZ - 2.0f;
        safety++;
    }

    return layers;
}

// MODIFIED: CalculatePath accepts ignoreWater and passes it to FindPath/Cache
inline std::vector<PathNode> CalculatePath(const std::vector<Vector3>& inputPath, const Vector3& startPos,
    int currentIndex, bool canFly, int mapId, bool isFlying, bool ignoreWater, bool path_loop = false, float pathThreshold = 25.0f, bool zCheck = true, float groundZExtent = 5.0f) {
    std::string mmapFolder = "C:/SMM/data/mmaps/";

    if (!std::filesystem::exists(mmapFolder)) {
        g_LogFile << "[ERROR] CRITICAL: MMap folder does not exist: " << mmapFolder << std::endl;
        g_LogFile << "[ERROR] Please update 'mmapFolder' in Pathfinding2.h to your correct path." << std::endl;
        return {}; // Return empty path instead of crashing
    }
    
    // --- MODE SELECTION BASED ON MOUNTABLE STATE ---
    bool canMount = false;
    // Assuming g_GameState is accessible via MovementController extern or inclusion
    if (g_GameState) {
        canMount = g_GameState->player.areaMountable;
    }

    std::vector<PathNode> stitchedPath;
    std::vector<Vector3> modifiedInput;
    if (inputPath.empty() || currentIndex < 0 || currentIndex >= inputPath.size()) return {};
    
    // If area is mountable -> Strictly try Flying.
    // If area is NOT mountable -> Strictly try Ground.
    bool attemptFlight = (canMount && canFly && inputPath.back().Dist3D(startPos) > 20.0f) || isFlying || g_GameState->player.flyingMounted;

    // Log the decision
    if (DEBUG_PATHFINDING) {
        g_LogFile << "[Pathfinding] Overhaul Mode: " << (attemptFlight ? "FLIGHT (Area Mountable)" : "GROUND (Area Not Mountable)") << std::endl;
    }
    // ------------------------------------------------

    if (!path_loop) {
        modifiedInput.push_back(startPos);
        for (int i = currentIndex; i < inputPath.size(); ++i) modifiedInput.push_back(inputPath[i]);
    }
    else {
        for (int i = currentIndex; i < inputPath.size(); ++i) modifiedInput.push_back(inputPath[i]);
        for (int i = 0; i < currentIndex; ++i) modifiedInput.push_back(inputPath[i]);
    }

    std::vector<Vector3> mapLoadPoints;
    if (!modifiedInput.empty()) {
        mapLoadPoints.push_back(modifiedInput[0]);
        for (size_t i = 0; i < modifiedInput.size() - 1; ++i) {
            Vector3 start = modifiedInput[i];
            Vector3 end = modifiedInput[i + 1];
            float dist = start.Dist3D(end);
            const float LOAD_STEP = 200.0f;
            if (dist > LOAD_STEP) {
                int steps = (int)(dist / LOAD_STEP);
                Vector3 dir = (end - start).Normalize();
                for (int j = 1; j < steps; ++j) {
                    mapLoadPoints.push_back(start + (dir * (float)(j * LOAD_STEP)));
                }
            }
            mapLoadPoints.push_back(end);
        }
    }
    
    // Sparse loading of only needed tiles
    if (!globalNavMesh.LoadMap(mmapFolder, mapId, &mapLoadPoints, true)) {
        g_LogFile << "Load Map failed" << std::endl;
        return {};
    }

    size_t lookahead = modifiedInput.size() - 1;

    for (size_t i = 0; i < lookahead; ++i) {
        Vector3 start = modifiedInput[i];
        Vector3 end = modifiedInput[i + 1];

        // Include mode in cache key to ensure we don't mix ground/flight paths
        PathCacheKey key(start, end, attemptFlight, ignoreWater);

        std::vector<PathNode>* cached = globalPathCache.Get(key);
        std::vector<PathNode> segment;

        if (cached) {
            segment = *cached;
        }
        else {
            if (attemptFlight) {
                // --- ATTEMPT FLIGHT PATH ---
                // We pass 'true' for isFlying because we are in flight mode.
                segment = Calculate3DFlightPath(start, end, mapId, true);

                // --- VALIDATION: Check Final Point ---
                if (segment.empty()) {
                    g_LogFile << "[Pathfinding] Flight path failed (Empty path generated)." << std::endl;
                    return {}; // STRICT FAILURE -> Stop Script
                }

                if (segment.back().pos.Dist3D(end) > pathThreshold) {
                    g_LogFile << "[Pathfinding] Flight path incomplete. Final point is "
                        << segment.back().pos.Dist3D(end) << " yards from destination (Threshold: " << pathThreshold << ")." << std::endl;
                    return {}; // STRICT FAILURE -> Stop Script
                }
            }
            else {
                // --- ATTEMPT GROUND PATH ---
                segment = FindPath(start, end, ignoreWater, true, zCheck, groundZExtent);

                // --- VALIDATION: Check Final Point ---
                if (segment.empty()) {
                    g_LogFile << "[Pathfinding] Ground path failed (Empty path generated)." << std::endl;
                    return {}; // STRICT FAILURE -> Stop Script
                }

                if (segment.back().pos.Dist3D(end) > pathThreshold) {
                    g_LogFile << "[Pathfinding] Ground path incomplete. Final point is "
                        << segment.back().pos.Dist3D(end) << " yards from destination (Threshold: " << pathThreshold << ")." << std::endl;
                    return {}; // STRICT FAILURE -> Stop Script
                }
            }

            // If valid, cache it
            globalPathCache.Put(key, segment);
        }

        // Stitch segment to full path
        if (stitchedPath.empty()) {
            stitchedPath.insert(stitchedPath.end(), segment.begin(), segment.end());
        }
        else {
            if (!segment.empty()) {
                // Prevent duplicate waypoints at seams
                if (stitchedPath.back().pos.Dist3D(segment.front().pos) < 0.1f) {
                    stitchedPath.insert(stitchedPath.end(), segment.begin() + 1, segment.end());
                }
                else {
                    stitchedPath.insert(stitchedPath.end(), segment.begin(), segment.end());
                }
            }
        }
    }

    // --- APPLY PATH REFINEMENT (New Logic) ---
    // 0.8f is a good buffer for standard doorways (approx 2.0y wide). 
    // It pushes the bot towards the center (1.0y) without blocking the path.
    // If you use a value > 1.0f, you might block the bot from passing through narrow doors.

    if (!attemptFlight) {
        for (int i = 0; i < stitchedPath.size(); i++) {
            g_LogFile << "Pre: " << stitchedPath[i].pos.x << " " << stitchedPath[i].pos.y << " " << stitchedPath[i].pos.z << " " << std::endl;
        }
        globalNavMesh.RefinePathClearance(stitchedPath, 0.7f, mapId);
        for (int i = 0; i < stitchedPath.size(); i++) {
            g_LogFile << "Post: " << stitchedPath[i].pos.x << " " << stitchedPath[i].pos.y << " " << stitchedPath[i].pos.z << " " << std::endl;
        }
    }

    // Optional: Keep NudgeGroundPath only if RefinePathClearance isn't enough, 
    // but RefinePathClearance is generally superior for wall collisions.
    // stitchedPath = NudgeGroundPath(stitchedPath, 1.0f, mapId);

    // --- SUBDIVISION BASED ON MODE ---
    if (attemptFlight) {
        // Subdivide for flight (simple linear interpolation + ground clearance check)
        stitchedPath = SubdivideFlightPath(stitchedPath, mapId);

        // Optional: Clean only GROUND nodes in flight path
        for (auto& node : stitchedPath) {
            if (node.type == PATH_GROUND) {
                float realZ = GetFMapFloorHeight(mapId, node.pos.x, node.pos.y, node.pos.z + 5.0f);
                if (realZ > -90000.0f && node.pos.z < realZ + 0.5f) {
                    node.pos.z = realZ + 0.5f;
                }
            }
        }
        // --- CLEAN UP STRAIGHT LINES ---
        // This removes the excessive waypoint density from Subdivision 
        // where it isn't needed (straight lines), but keeps it for curves/terrain.
        // Pass 25.0f to enforce minimum spacing
        stitchedPath = OptimizeFlightPath(stitchedPath, 25.0f);

        return stitchedPath;
    }
    else {
        // Subdivide for ground (NavMesh surface fitting)
        stitchedPath = globalNavMesh.SubdivideOnMesh(stitchedPath);
        CleanPathGroundZ(stitchedPath, mapId);
        return stitchedPath;
    }
}