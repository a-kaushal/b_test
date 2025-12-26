//#pragma once
//
//#include <iostream>
//#include <fstream>
//#include <vector>
//#include <cmath>
//#include <string>
//#include <cstring>
//#include <algorithm>
//#include <map>
//#include <set>
//#include <queue>
//#include <iomanip>
//#include <utility>
//#include <filesystem>
//
//#include "Vector.h"
//
//// --- CONFIGURATION ---
//const float PATH_STEP_SIZE = 4.0f;
//const float MAX_STEP_HEIGHT = 1.5f;
//const float FLY_ALTITUDE = 30.0f;
//const float AGENT_RADIUS = 0.6f;
//const float TILE_SIZE = 533.33333f; // Standard WoW Tile Size
//
//struct Portal {
//    int neighborId;
//    Vector3 v1; // Left side of the "door"
//    Vector3 v2; // Right side of the "door"
//};
//
//// For fuzzy stitching map keys
//struct QuantizedVector3 {
//    int x, y, z;
//    QuantizedVector3(const Vector3& v) {
//        x = static_cast<int>(std::round(v.x * 100.0f));
//        y = static_cast<int>(std::round(v.y * 100.0f));
//        z = static_cast<int>(std::round(v.z * 100.0f));
//    }
//    bool operator<(const QuantizedVector3& o) const {
//        if (x != o.x) return x < o.x;
//        if (y != o.y) return y < o.y;
//        return z < o.z;
//    }
//};
//
//// --- DATA STRUCTURES ---
//struct NavPolygon {
//    int id;
//    int tileId; // Track which tile this belongs to
//    std::vector<int> vertIndices;
//    std::vector<Portal> portals;
//    Vector3 centroid;
//};
//
//// --- FILE HEADERS ---
//#pragma pack(push, 1)
//
//struct MmapTileHeader {
//    uint32_t mmapMagic;
//    uint32_t dtVersion;
//    uint32_t mmapVersion;
//    uint32_t size;
//    char usesLiquids;
//    char padding[3];
//};
//
//struct DtMeshHeader {
//    int magic;
//    int version;
//    int x, y, layer;
//    unsigned int userId;
//    int polyCount;
//    int vertCount;
//    int maxLinkCount;
//    int detailMeshCount;
//    int detailVertCount;
//    int detailTriCount;
//    int bvNodeCount;
//    int offMeshConCount;
//    int offMeshConBase;
//    float walkableHeight;
//    float walkableRadius;
//    float walkableClimb;
//    float bmin[3];
//    float bmax[3];
//    float bvQuantFactor;
//};
//
//struct DtPolyRaw {
//    unsigned int firstLink;
//    unsigned short verts[6];
//    unsigned short neighbors[6];
//    unsigned short flags;
//    unsigned char vertCount;
//    unsigned char areaAndType;
//};
//#pragma pack(pop)
//
//// --- NAVMESH CLASS ---
//class NavMesh {
//public:
//    std::vector<Vector3> vertices;
//    std::vector<NavPolygon> polygons;
//    int currentMapId = -1;
//
//    void Clear() { vertices.clear(); polygons.clear(); currentMapId = -1; }
//
//    bool AddTile(const std::string& filepath, int tileIdx) {
//        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
//        if (!file.is_open()) return false;
//
//        std::streamsize fileSize = file.tellg();
//        file.seekg(0, std::ios::beg);
//        std::vector<char> buffer(fileSize);
//        file.read(buffer.data(), fileSize);
//
//        char* ptr = buffer.data() + sizeof(MmapTileHeader);
//        DtMeshHeader* dtHeader = reinterpret_cast<DtMeshHeader*>(ptr);
//        ptr += sizeof(DtMeshHeader);
//
//        int baseVert = (int)vertices.size();
//        int basePoly = (int)polygons.size();
//
//        float* rawVerts = reinterpret_cast<float*>(ptr);
//        for (int i = 0; i < dtHeader->vertCount; ++i) {
//			vertices.push_back(Vector3(rawVerts[i * 3], rawVerts[i * 3 + 2], rawVerts[i * 3 + 1]));  // Reading (X,Z,Y). Not sure if correct.
//        }
//        ptr += (dtHeader->vertCount * 3 * sizeof(float));
//
//        DtPolyRaw* rawPolys = reinterpret_cast<DtPolyRaw*>(ptr);
//        for (int i = 0; i < dtHeader->polyCount; ++i) {
//            NavPolygon p;
//            p.id = basePoly + i;
//            p.tileId = tileIdx;
//            Vector3 sum(0, 0, 0);
//            for (int k = 0; k < rawPolys[i].vertCount; ++k) {
//                int vIdx = baseVert + rawPolys[i].verts[k];
//                p.vertIndices.push_back(vIdx);
//                sum = sum + vertices[vIdx];
//            }
//            p.centroid = sum / (float)rawPolys[i].vertCount;
//
//            for (int k = 0; k < rawPolys[i].vertCount; ++k) {
//                unsigned short neighborIdx = rawPolys[i].neighbors[k];
//                if (neighborIdx != 0xFFFF && neighborIdx < 0x8000) {
//                    p.portals.push_back({ basePoly + (int)neighborIdx, vertices[p.vertIndices[k]], vertices[p.vertIndices[(k + 1) % rawPolys[i].vertCount]] });
//                }
//            }
//            polygons.push_back(p);
//        }
//        return true;
//    }
//
//    void LoadMap(const std::string& directory, int mapId, float agentRadius) {
//        if (currentMapId == mapId && !polygons.empty()) return;
//        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
//        Clear();
//
//        std::stringstream ss;
//        ss << std::setw(4) << std::setfill('0') << mapId;
//        std::string prefix = ss.str();
//
//        int loaded = 0;
//        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
//            if (entry.path().filename().string().find(prefix) == 0 && entry.path().extension() == ".mmtile") {
//                if (AddTile(entry.path().string(), loaded)) loaded++;
//            }
//        }
//        currentMapId = mapId;
//        Build(agentRadius);
//        logFile << "[NAVMESH] Map " << mapId << " Loaded (" << loaded << " tiles, " << polygons.size() << " polys)." << std::endl;
//    }
//
//    void Build(float agentRadius) {
//        using Edge = std::pair<QuantizedVector3, QuantizedVector3>;
//        std::map<Edge, std::vector<int>> edgeMap;
//        int stitchCount = 0;
//
//        for (auto& poly : polygons) {
//            int n = (int)poly.vertIndices.size();
//            for (int i = 0; i < n; ++i) {
//                QuantizedVector3 q1(vertices[poly.vertIndices[i]]), q2(vertices[poly.vertIndices[(i + 1) % n]]);
//                edgeMap[(q1 < q2) ? Edge(q1, q2) : Edge(q2, q1)].push_back(poly.id);
//            }
//        }
//
//        for (auto& kv : edgeMap) {
//            if (kv.second.size() < 2) continue;
//            Vector3 v1((float)kv.first.first.x / 100.0f, (float)kv.first.first.y / 100.0f, (float)kv.first.first.z / 100.0f);
//            Vector3 v2((float)kv.first.second.x / 100.0f, (float)kv.first.second.y / 100.0f, (float)kv.first.second.z / 100.0f);
//
//            for (size_t i = 0; i < kv.second.size(); ++i) {
//                for (size_t j = i + 1; j < kv.second.size(); ++j) {
//                    int idA = kv.second[i], idB = kv.second[j];
//                    if (polygons[idA].tileId != polygons[idB].tileId) { // Check if cross-tile
//                        bool exists = false;
//                        for (auto& p : polygons[idA].portals) if (p.neighborId == idB) exists = true;
//                        if (!exists) {
//                            polygons[idA].portals.push_back({ idB, v1, v2 });
//                            polygons[idB].portals.push_back({ idA, v1, v2 });
//                            stitchCount++;
//                        }
//                    }
//                }
//            }
//        }
//        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
//        logFile << "[NAVMESH] Stitched " << stitchCount << " cross-tile boundaries." << std::endl;
//    }
//
//    NavPolygon* FindNearestPoly(const Vector3& target) {
//        NavPolygon* best = nullptr; float minDist = 1e9f;
//        for (auto& poly : polygons) {
//            float d = poly.centroid.Dist3D(target);
//            if (d < minDist) { minDist = d; best = &poly; }
//        }
//        return best;
//    }
//
//    bool IsPointInPoly(const Vector3& p, const NavPolygon& poly) {
//        bool inside = false;
//        size_t n = poly.vertIndices.size();
//        for (size_t i = 0, j = n - 1; i < n; j = i++) {
//            Vector3 vi = vertices[poly.vertIndices[i]];
//            Vector3 vj = vertices[poly.vertIndices[j]];
//
//            if (((vi.y > p.y) != (vj.y > p.y)) &&
//                (p.x < (vj.x - vi.x) * (p.y - vi.y) / (vj.y - vi.y) + vi.x)) {
//                inside = !inside;
//            }
//        }
//        return inside;
//    }
//
//    float GetCeilingHeight(const Vector3& pos) {
//        float minCeiling = 999999.0f;
//        bool found = false;
//
//        for (const auto& poly : polygons) {
//            if (poly.centroid.z < pos.z + 2.0f) continue;
//
//            float dx = poly.centroid.x - pos.x;
//            float dy = poly.centroid.y - pos.y;
//            if (dx * dx + dy * dy > 100.0f) continue;
//
//            if (IsPointInPoly(pos, poly)) {
//                if (poly.centroid.z < minCeiling) {
//                    minCeiling = poly.centroid.z;
//                    found = true;
//                }
//            }
//        }
//        return found ? minCeiling : -1.0f;
//    }
//
//private:
//    void RepairConnectivity(float agentRadius) {
//        using Edge = std::pair<QuantizedVector3, QuantizedVector3>;
//        std::map<Edge, std::vector<int>> edgeMap;
//
//        // 1. Map Edges (Global)
//        for (auto& poly : polygons) {
//            int n = (int)poly.vertIndices.size();
//            for (int i = 0; i < n; ++i) {
//                Vector3 v1 = vertices[poly.vertIndices[i]];
//                Vector3 v2 = vertices[poly.vertIndices[(i + 1) % n]];
//
//                QuantizedVector3 q1(v1);
//                QuantizedVector3 q2(v2);
//
//                // Sort to ensure direction doesn't matter for matching
//                Edge edge = (q1 < q2) ? Edge(q1, q2) : Edge(q2, q1);
//                edgeMap[edge].push_back(poly.id);
//            }
//        }
//
//        // 2. Stitch
//        for (auto& kv : edgeMap) {
//            std::vector<int>& ids = kv.second;
//
//            // Check width
//            float dx = (float)(kv.first.first.x - kv.first.second.x) / 100.0f;
//            float dy = (float)(kv.first.first.y - kv.first.second.y) / 100.0f;
//            float dz = (float)(kv.first.first.z - kv.first.second.z) / 100.0f;
//            float edgeWidth = std::sqrt(dx * dx + dy * dy + dz * dz);
//
//            if (edgeWidth < (agentRadius * 2.0f)) continue;
//
//            // If we have 2+ polys sharing this edge (likely 2, one from each side or tile)
//            if (ids.size() >= 2) {
//                Vector3 edgeV1((float)kv.first.first.x / 100.0f, (float)kv.first.first.y / 100.0f, (float)kv.first.first.z / 100.0f);
//                Vector3 edgeV2((float)kv.first.second.x / 100.0f, (float)kv.first.second.y / 100.0f, (float)kv.first.second.z / 100.0f);
//
//                // Connect them
//                for (size_t i = 0; i < ids.size(); ++i) {
//                    for (size_t j = i + 1; j < ids.size(); ++j) {
//                        // Access via global array (ids contains global indices)
//                        NavPolygon& p1 = polygons[ids[i]];
//                        NavPolygon& p2 = polygons[ids[j]];
//
//                        float hDiff = std::abs(p1.centroid.z - p2.centroid.z);
//                        if (hDiff > MAX_STEP_HEIGHT) continue;
//
//                        p1.portals.push_back({ p2.id, edgeV1, edgeV2 });
//                        p2.portals.push_back({ p1.id, edgeV1, edgeV2 });
//                    }
//                }
//            }
//        }
//    }
//};
//
//// --- A* PATHFINDING HELPERS ---
//
//struct Node {
//    int id;
//    float f;
//    bool operator>(const Node& other) const { return f > other.f; }
//};
//
//Vector3 GetSafePortalPoint(const Vector3& v1, const Vector3& v2, float agentRadius = AGENT_RADIUS) {
//    Vector3 dir = v2 - v1; float len = dir.Length();
//    if (len < 0.001f) return v1;
//    dir = dir / len;
//    float mid = len * 0.5f;
//    return v1 + (dir * mid);
//}
//
//inline std::vector<Vector3> ResamplePath(const std::vector<Vector3>& coarsePath) {
//    if (coarsePath.size() < 2) return coarsePath;
//
//    std::vector<Vector3> result;
//    result.push_back(coarsePath[0]);
//
//    for (size_t i = 0; i < coarsePath.size() - 1; ++i) {
//        Vector3 start = coarsePath[i];
//        Vector3 end = coarsePath[i + 1];
//
//        Vector3 vec = end - start;
//        float dist = vec.Length();
//
//        if (dist < 0.001f) continue;
//
//        Vector3 dir = vec / dist;
//        float currentDist = PATH_STEP_SIZE;
//
//        while (currentDist < dist) {
//            result.push_back(start + (dir * currentDist));
//            currentDist += PATH_STEP_SIZE;
//        }
//        result.push_back(end);
//    }
//    return result;
//}
//
//inline std::vector<Vector3> FindPath(NavMesh& mesh, const Vector3& startCoord, const Vector3& endCoord) {
//    std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
//    NavPolygon* startPoly = mesh.FindNearestPoly(startCoord);
//    NavPolygon* endPoly = mesh.FindNearestPoly(endCoord);
//
//    if (!startPoly || !endPoly) {
//        logFile << "[A* ERROR] Points off mesh. StartPoly: " << (startPoly ? "OK" : "NULL") << " EndPoly: " << (endPoly ? "OK" : "NULL") << std::endl;
//        return {};
//    }
//
//    logFile << "[A* START] Poly " << startPoly->id << " (Tile " << startPoly->id << ") -> Poly " << endPoly->id << " (Tile " << endPoly->tileId << ")" << std::endl;
//
//    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;
//    openSet.push({ startPoly->id, 0.0f });
//
//    std::vector<float> gScore(mesh.polygons.size(), 1e9f);
//    gScore[startPoly->id] = 0.0f;
//    std::vector<int> cameFrom(mesh.polygons.size(), -1);
//
//    int iterations = 0;
//    const int MAX_ITERATIONS = 5000; // Prevent runaway search
//
//    while (!openSet.empty() && iterations < MAX_ITERATIONS) {
//        iterations++;
//        Node currentNode = openSet.top();
//        openSet.pop();
//
//        if (currentNode.id == endPoly->id) {
//            logFile << "[A* SUCCESS] Path found in " << iterations << " iterations." << std::endl;
//            std::vector<Vector3> path;
//            int currId = endPoly->id;
//            path.push_back(endCoord);
//            while (currId != startPoly->id) {
//                int prevId = cameFrom[currId];
//                for (const auto& portal : mesh.polygons[prevId].portals) {
//                    if (portal.neighborId == currId) {
//                        path.push_back(GetSafePortalPoint(portal.v1, portal.v2));
//                        break;
//                    }
//                }
//                currId = prevId;
//            }
//            path.push_back(startCoord);
//            std::reverse(path.begin(), path.end());
//            return path;
//        }
//
//        NavPolygon& currPoly = mesh.polygons[currentNode.id];
//
//        // LOGGING: Only log every 50 nodes to avoid massive file size, or log when tile changes
//        bool crossTileEvaluated = false;
//
//        for (const auto& portal : currPoly.portals) {
//            NavPolygon& nextPoly = mesh.polygons[portal.neighborId];
//
//            // USE PORTAL DISTANCE (More accurate than centroid-to-centroid)
//            Vector3 mid = (portal.v1 + portal.v2) * 0.5f;
//            float stepDist = currPoly.centroid.Dist3D(mid) + mid.Dist3D(nextPoly.centroid);
//            float tentativeG = gScore[currentNode.id] + stepDist;
//
//            if (tentativeG < gScore[nextPoly.id]) {
//                cameFrom[nextPoly.id] = currentNode.id;
//                gScore[nextPoly.id] = tentativeG;
//                float h = nextPoly.centroid.Dist3D(endPoly->centroid);
//                openSet.push({ nextPoly.id, tentativeG + h });
//
//                // LOG CROSS-TILE LINKS
//                if (currPoly.tileId != nextPoly.tileId) {
//                    logFile << "[A* TILE CROSS] Poly " << currPoly.id << " (T" << currPoly.tileId << ") -> "
//                        << nextPoly.id << " (T" << nextPoly.tileId << ") G:" << tentativeG << " F:" << tentativeG + h << std::endl;
//                }
//            }
//        }
//    }
//
//    if (iterations >= MAX_ITERATIONS) logFile << "[A* FAIL] Max iterations reached (Search space too large or disconnected)." << std::endl;
//    else logFile << "[A* FAIL] No path found after " << iterations << " nodes." << std::endl;
//
//    return {};
//}
//
//// --- FLIGHT LOGIC ---
//inline std::vector<Vector3> CalculateFlightPath(NavMesh& mesh, const Vector3& start, const Vector3& end) {
//    std::vector<Vector3> path;
//    std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
//
//    float maxTerrainHeight = -99999.0f;
//    int samples = 20;
//
//    for (int i = 0; i <= samples; ++i) {
//        float t = (float)i / (float)samples;
//        Vector3 probe = start + (end - start) * t;
//        NavPolygon* poly = mesh.FindNearestPoly(probe);
//        if (poly && poly->centroid.z > maxTerrainHeight) {
//            maxTerrainHeight = poly->centroid.z;
//        }
//    }
//
//    float safeAltitude = maxTerrainHeight + FLY_ALTITUDE;
//    float cruiseZ = (std::max)(safeAltitude, (std::max)(start.z, end.z));
//
//    // Check Ceilings
//    for (int i = 0; i <= samples; ++i) {
//        float t = (float)i / (float)samples;
//        Vector3 probe = start + (end - start) * t;
//
//        float ceilingZ = mesh.GetCeilingHeight(probe);
//        if (ceilingZ != -1.0f && ceilingZ < cruiseZ + 2.0f) {
//            logFile << "[FLIGHT] Blocked by ceiling at Z=" << ceilingZ << ". Reverting to Ground." << std::endl;
//            return {};
//        }
//    }
//
//    path.push_back(start);
//    if (cruiseZ > start.z + 5.0f) path.push_back(Vector3(start.x, start.y, cruiseZ));
//    path.push_back(Vector3(end.x, end.y, cruiseZ));
//    path.push_back(end);
//
//    return path;
//}
//
//// --- MAIN INTERFACE ---
//inline int GetTileIndex(float val) {
//    // Formula from previous code: ceil(abs(31 - val/SIZE))
//    // Note: Standard logic is usually floor(32 - val/SIZE) for WoW coordinates.
//    // We stick to the user's working formula for consistency.
//    return static_cast<int>(std::ceil(std::abs(31.0f - (val / TILE_SIZE))));
//}
//
//// PERSISTENT GLOBAL MESH
//static NavMesh globalNavMesh;
//
//inline std::vector<Vector3> CalculatePath(Vector3 start, Vector3 end, bool FlyingPath, int mapId) {
//    std::string mmapFolder = "C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/mmaps/";
//    globalNavMesh.LoadMap(mmapFolder, mapId, AGENT_RADIUS);
//
//    // WoW Coordinates [X, Y, Z] aligned with Mesh Centroids
//    Vector3 s(start.y, start.x, start.z), e(end.y, end.x, end.z);
//
//    std::vector<Vector3> path;
//    if (FlyingPath) path = CalculateFlightPath(globalNavMesh, s, e);
//    if (path.empty()) path = FindPath(globalNavMesh, s, e);
//
//    for (auto& p : path) std::swap(p.x, p.y);
//    return path;
//}