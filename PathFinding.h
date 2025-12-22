#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <string>
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#include <queue>
#include <iomanip>
#include <utility>

#include "Vector.h"

// --- CONFIGURATION ---
const float PATH_STEP_SIZE = 15.0f;
const float MAX_STEP_HEIGHT = 10.0f;
const float FLY_ALTITUDE = 30.0f;
const float AGENT_RADIUS = 2.0f;

struct Portal {
    int neighborId;
    Vector3 v1; // Left side of the "door"
    Vector3 v2; // Right side of the "door"
};

// For fuzzy stitching map keys
struct QuantizedVector3 {
    int x, y, z;

    QuantizedVector3(const Vector3& v) {
        // Round to 2 decimal places (x100 and cast to int)
        x = static_cast<int>(std::round(v.x * 100.0f));
        y = static_cast<int>(std::round(v.y * 100.0f));
        z = static_cast<int>(std::round(v.z * 100.0f));
    }

    bool operator<(const QuantizedVector3& o) const {
        if (x != o.x) return x < o.x;
        if (y != o.y) return y < o.y;
        return z < o.z;
    }

    bool operator==(const QuantizedVector3& o) const {
        return x == o.x && y == o.y && z == o.z;
    }
};

// --- DATA STRUCTURES ---
// RENAMED to NavPolygon to avoid conflict with Windows 'Polygon' macro
struct NavPolygon {
    int id;
    std::vector<int> vertIndices;
    std::vector<Portal> portals;
    Vector3 centroid;
};

// --- FILE HEADERS ---
#pragma pack(push, 1)

struct MmapTileHeader {
    uint32_t mmapMagic;
    uint32_t dtVersion;
    uint32_t mmapVersion;
    uint32_t size;
    char usesLiquids;
    char padding[3];
};

struct DtMeshHeader {
    int magic;
    int version;
    int x, y, layer;
    unsigned int userId;
    int polyCount;
    int vertCount;
    int maxLinkCount;
    int detailMeshCount;
    int detailVertCount;
    int detailTriCount;
    int bvNodeCount;
    int offMeshConCount;
    int offMeshConBase;
    float walkableHeight;
    float walkableRadius;
    float walkableClimb;
    float bmin[3];
    float bmax[3];
    float bvQuantFactor;
};

// The raw format of a polygon in the file
struct DtPolyRaw {
    unsigned int firstLink;
    unsigned short verts[6];
    unsigned short neighbors[6];
    unsigned short flags;
    unsigned char vertCount;
    unsigned char areaAndType;
};
#pragma pack(pop)

// --- NAVMESH CLASS ---
class NavMesh {
public:
    std::vector<Vector3> vertices;
    std::vector<NavPolygon> polygons; // Updated type name

    bool Load(const std::string& filepath, float agentRadius) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open " << filepath << std::endl;
            return false;
        }

        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<char> buffer(fileSize);
        if (!file.read(buffer.data(), fileSize)) return false;

        char* ptr = buffer.data();

        // 1. Mmap Header
        if (fileSize < sizeof(MmapTileHeader)) return false;
        MmapTileHeader* mHeader = reinterpret_cast<MmapTileHeader*>(ptr);

        // Magic check
        if (mHeader->mmapMagic != 0x50414D4D && mHeader->mmapMagic != 0x4D4D4150) {
            std::cerr << "Error: Invalid Magic" << std::endl;
            return false;
        }

        ptr += sizeof(MmapTileHeader);

        // 2. Detour Header
        DtMeshHeader* dtHeader = reinterpret_cast<DtMeshHeader*>(ptr);
        int polyCount = dtHeader->polyCount;
        int vertCount = dtHeader->vertCount;
        ptr += sizeof(DtMeshHeader);

        // 3. Read Vertices
        float* rawVerts = reinterpret_cast<float*>(ptr);
        vertices.resize(vertCount);

        for (int i = 0; i < vertCount; ++i) {
            float x = rawVerts[i * 3 + 0];
            float y = rawVerts[i * 3 + 1];
            float z = rawVerts[i * 3 + 2];

            // SWAP COORDINATES: Recast [x, y, z] -> WoW [x, z, y]
            vertices[i] = Vector3(x, z, y);
        }
        ptr += (vertCount * 3 * sizeof(float));

        // 4. Read Polygons
        DtPolyRaw* rawPolys = reinterpret_cast<DtPolyRaw*>(ptr);
        polygons.resize(polyCount);

        for (int i = 0; i < polyCount; ++i) {
            DtPolyRaw& rp = rawPolys[i];
            NavPolygon& p = polygons[i]; // Updated type name
            p.id = i;

            // Extract Vertices
            Vector3 sum(0, 0, 0);
            for (int k = 0; k < rp.vertCount; ++k) {
                int vIdx = rp.verts[k];
                p.vertIndices.push_back(vIdx);
                sum = sum + vertices[vIdx];
            }

            // Calculate Centroid
            if (rp.vertCount > 0) {
                p.centroid = sum / (float)rp.vertCount;
            }
        }

        std::cout << "Loaded " << polygons.size() << " polygons. Running repair..." << std::endl;
        RepairConnectivity(agentRadius); // Pass radius here
        return true;
    }

    NavPolygon* FindNearestPoly(const Vector3& target) {
        NavPolygon* best = nullptr; // Updated type name
        float minDist = 1e9f;

        for (auto& poly : polygons) {
            float d = poly.centroid.Dist3D(target);
            if (d < minDist) {
                minDist = d;
                best = &poly;
            }
        }
        return best;
    }

private:
    void RepairConnectivity(float agentRadius) {
        using Edge = std::pair<QuantizedVector3, QuantizedVector3>;
        std::map<Edge, std::vector<int>> edgeMap;

        // 1. Map all edges (Same as before)
        for (auto& poly : polygons) {
            int n = (int)poly.vertIndices.size();
            for (int i = 0; i < n; ++i) {
                Vector3 v1 = vertices[poly.vertIndices[i]];
                Vector3 v2 = vertices[poly.vertIndices[(i + 1) % n]];

                QuantizedVector3 q1(v1);
                QuantizedVector3 q2(v2);

                Edge edge = (q1 < q2) ? Edge(q1, q2) : Edge(q2, q1);
                edgeMap[edge].push_back(poly.id);
            }
        }

        // 2. Process Links
        for (auto& kv : edgeMap) {
            std::vector<int>& ids = kv.second;

            // --- FIX START: Calculate Edge Width ---
            // We interpret the quantized coordinates back to floats to check distance
            float dx = (float)(kv.first.first.x - kv.first.second.x) / 100.0f;
            float dy = (float)(kv.first.first.y - kv.first.second.y) / 100.0f;
            float dz = (float)(kv.first.first.z - kv.first.second.z) / 100.0f;
            float edgeWidth = std::sqrt(dx * dx + dy * dy + dz * dz);

            // REJECT: If the "door" is narrower than the agent's diameter (radius * 2)
            if (edgeWidth < (agentRadius * 2.0f)) {
                continue;
            }
            // --- FIX END ---

            if (ids.size() >= 2) {
                // Get accurate float positions for the portal data
                NavPolygon& referencePoly = polygons[ids[0]];
                // Note: Using the Quantized key values is safer than looking up vertIndices 
                // because vertIndices might point to slightly different floats that quantized to the same int.
                Vector3 edgeV1((float)kv.first.first.x / 100.0f, (float)kv.first.first.y / 100.0f, (float)kv.first.first.z / 100.0f);
                Vector3 edgeV2((float)kv.first.second.x / 100.0f, (float)kv.first.second.y / 100.0f, (float)kv.first.second.z / 100.0f);

                for (size_t i = 0; i < ids.size(); ++i) {
                    for (size_t j = i + 1; j < ids.size(); ++j) {
                        NavPolygon& p1 = polygons[ids[i]];
                        NavPolygon& p2 = polygons[ids[j]];

                        float hDiff = std::abs(p1.centroid.z - p2.centroid.z);
                        if (hDiff > MAX_STEP_HEIGHT) continue;

                        p1.portals.push_back({ p2.id, edgeV1, edgeV2 });
                        p2.portals.push_back({ p1.id, edgeV1, edgeV2 });
                    }
                }
            }
        }
    }
};

// --- A* PATHFINDING ---

struct Node {
    int id;
    float f; // f = g + h

    // For priority queue: lowest f is top
    bool operator>(const Node& other) const {
        return f > other.f;
    }
};

Vector3 GetSafePortalPoint(const Vector3& v1, const Vector3& v2, float agentRadius = AGENT_RADIUS) {
    // 1. Calculate edge length
    Vector3 edgeDir = v2 - v1;
    float edgeLen = edgeDir.Length();

    // 2. Normalize direction
    if (edgeLen < 0.001f) return v1;
    edgeDir = edgeDir / edgeLen;

    // 3. Clamp radius
    // If the gap is too small (e.g. 0.5m wide but agent is 0.6m), just go to the exact center
    if (edgeLen <= agentRadius * 2.0f) {
        return v1 + (edgeDir * (edgeLen * 0.5f));
    }

    // 4. Calculate safe range
    // We want to be at least 'agentRadius' away from V1 and 'agentRadius' away from V2
    float distFromV1 = agentRadius;
    float distFromV2 = edgeLen - agentRadius;

    // 5. Pick the center of the safe zone
    // (You can also pick the point closest to the previous waypoint for smoother paths, 
    // but Center of Safe Zone is robust and easiest to implement).
    float midSafe = (distFromV1 + distFromV2) * 0.5f;

    return v1 + (edgeDir * midSafe);
}

// Marked inline to allow inclusion in multiple cpp files
inline std::vector<Vector3> ResamplePath(const std::vector<Vector3>& coarsePath) {
    if (coarsePath.size() < 2) return coarsePath;

    std::vector<Vector3> result;
    result.push_back(coarsePath[0]);

    for (size_t i = 0; i < coarsePath.size() - 1; ++i) {
        Vector3 start = coarsePath[i];
        Vector3 end = coarsePath[i + 1];

        Vector3 vec = end - start;
        float dist = vec.Length();

        if (dist < 0.001f) continue;

        Vector3 dir = vec / dist;
        float currentDist = PATH_STEP_SIZE;

        while (currentDist < dist) {
            result.push_back(start + (dir * currentDist));
            currentDist += PATH_STEP_SIZE;
        }
        // Ensure the waypoint is included
        result.push_back(end);
    }
    return result;
}

// Marked inline
inline std::vector<Vector3> FindPath(NavMesh& mesh, const Vector3& startCoord, const Vector3& endCoord) {
    std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
    NavPolygon* startPoly = mesh.FindNearestPoly(startCoord); // Updated type name
    NavPolygon* endPoly = mesh.FindNearestPoly(endCoord);     // Updated type name

	logFile << "Centroid: (" << startPoly->centroid.x << ", " << startPoly->centroid.y << ", " << startPoly->centroid.z << ")" << std::endl;

    if (!startPoly || !endPoly) {
        std::cerr << "Error: Could not snap points to navmesh." << std::endl;
        return {};
    }

    std::cout << "Pathfinding: Start Poly " << startPoly->id << " -> End Poly " << endPoly->id << std::endl;

    // A* Setup
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;
    openSet.push({ startPoly->id, 0.0f });

    std::vector<float> gScore(mesh.polygons.size(), 1e9f);
    gScore[startPoly->id] = 0.0f;

    std::vector<int> cameFrom(mesh.polygons.size(), -1);

    while (!openSet.empty()) {
        Node current = openSet.top();
        openSet.pop();

        if (current.id == endPoly->id) {
            std::vector<Vector3> path;
            int currId = endPoly->id;

            // We work backwards: End -> ... -> Start
            path.push_back(endCoord); // Explicit End Point

            while (currId != startPoly->id) {
                int prevId = cameFrom[currId];
                if (prevId == -1) break;

                // Find the portal connecting prevId -> currId
                NavPolygon& prevPoly = mesh.polygons[prevId];
                for (const auto& portal : prevPoly.portals) {
                    if (portal.neighborId == currId) {
                        // FOUND THE DOOR!
                        // Calculate a point in the middle of the door, buffered by radius
                        Vector3 safePoint = GetSafePortalPoint(portal.v1, portal.v2, AGENT_RADIUS);
                        path.push_back(safePoint);
                        break;
                    }
                }
                currId = prevId;
            }

            path.push_back(startCoord); // Explicit Start Point
            std::reverse(path.begin(), path.end());

            return ResamplePath(path);
        }

        NavPolygon& currPoly = mesh.polygons[current.id];
        for (const auto& portal : currPoly.portals) { // ITERATE PORTALS
            int neighborId = portal.neighborId;
            NavPolygon& nextPoly = mesh.polygons[neighborId];

            float dist = currPoly.centroid.Dist3D(nextPoly.centroid);
            float tentativeG = gScore[current.id] + dist;

            if (tentativeG < gScore[neighborId]) {
                cameFrom[neighborId] = current.id;
                gScore[neighborId] = tentativeG;
                float h = nextPoly.centroid.Dist3D(endPoly->centroid);
                openSet.push({ neighborId, tentativeG + h });
            }
        }
    }

    std::cout << "No path found." << std::endl;
    return {};
}

// --- FLIGHT LOGIC ---
// Marked inline
inline std::vector<Vector3> CalculateFlightPath(NavMesh& mesh, const Vector3& start, const Vector3& end) {
    std::vector<Vector3> path;
    int samples = 20;

    for (int i = 0; i <= samples; ++i) {
        float t = (float)i / (float)samples;
        Vector3 pos = start + (end - start) * t;

        NavPolygon* nearest = mesh.FindNearestPoly(pos); // Updated type name
        if (nearest) {
            float groundZ = nearest->centroid.z;
            if (groundZ + FLY_ALTITUDE > pos.z) {
                pos.z = groundZ + FLY_ALTITUDE;
            }
        }
        path.push_back(pos);
    }
    return path;
}

// Marked inline
inline std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end, bool FlyingPath, float agentRadius = AGENT_RADIUS) {
    // --- 1. SETUP ---
    std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
    NavMesh navMesh;
    std::vector<Vector3> path;

    Vector3 temp;
    temp = start;
    start.x = temp.y;
    start.y = temp.x;
    temp = end;
    end.x = temp.y;
    end.y = temp.x;

    logFile << "Calculating path from (" << start.x << ", " << start.y << ", " << start.z << ") to ("
        << end.x << ", " << end.y << ", " << end.z << ")" << std::endl;

    // Fixed narrowing conversions
    int y_start = static_cast<int>(std::ceil(std::abs(31.0f - (start.y / 533.33f))));
    int x_start = static_cast<int>(std::ceil(std::abs(31.0f - (start.x / 533.33f))));

    // Update path to your mmtile location
    std::string filename = "C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/mmaps/0530_" + std::to_string(y_start) + "_" + std::to_string(x_start) + ".mmtile";

    if (!navMesh.Load(filename, agentRadius)) {
        return {};
    }

    if (FlyingPath) {
        path = CalculateFlightPath(navMesh, start, end);
    }
    else {
        path = FindPath(navMesh, start, end);
    }

    // --- SWAP RESULTING X AND Y AXIS ---
    for (auto& p : path) {
        float t = p.x;
        p.x = p.y;
        p.y = t;
    }

    return path;
}