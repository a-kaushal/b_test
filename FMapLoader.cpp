// FMapLoader.cpp
    // Voxel-based Flight Map Loader - Compact Vertical Heightfield
    // Grid: 160x160 cells, Row-Major ordering, 0.1 height precision

#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <vector>
#include <map>
#include <string>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <iostream>

// --- CONFIGURATION ---
const int FMAP_GRID_WIDTH = 160;
const int FMAP_GRID_HEIGHT = 160;
const int FMAP_TOTAL_CELLS = 25600;  // 160x160
const float FMAP_CELL_SIZE = 3.33333f;  // Game units per cell
const float FMAP_HEIGHT_PRECISION = 0.1f;  // Quantization scale
const float FMAP_HEIGHT_BASE = 2000.0f;   // Base height that data is stored at
const float FMAP_AGENT_HEIGHT = 2.0f;     // Agent collision height
const float FMAP_HEIGHT_RANGE = 5.0f;  // Range within which a height value is valid
const bool DEBUG_FMAP = false;

// --- DEBUG LOGGER ---
class FMapLogger {
private:
    std::ofstream logFile;
    bool enabled;
    int totalChecks = 0;
    int totalHits = 0;
    int totalFloorQueries = 0;

public:
    FMapLogger() : enabled(DEBUG_FMAP) {
        if (enabled) {
            logFile.open("C:\\Driver\\SMM_FMap_Debug.log", std::ios::app);
            if (logFile.is_open()) {
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                logFile << "\n========================================\n";
                logFile << "FMap Session Started: " << std::ctime(&time);
                logFile << "Format: 160x160 Grid, 24-byte Header\n";
                logFile << "Coordinate System: File=MAPID_TY_TX, tx=f(Y), ty=f(X)\n";
                logFile << "Grid Mapping: gx=f(Y), gy=f(X) [Detour convention]\n";
                logFile << "========================================\n\n";
            }
        }
    }

    ~FMapLogger() {
        if (logFile.is_open() && enabled) {
            logFile << "\n=== FMap Session Summary ===\n";
            logFile << "  LOS checks: " << totalChecks << " (hits: " << totalHits
                << ", " << (totalChecks > 0 ? (100.0f * totalHits / totalChecks) : 0.0f) << "%)\n";
            logFile << "  Floor queries: " << totalFloorQueries << "\n";
            logFile.close();
        }
    }

    void Log(const std::string& msg) {
        if (enabled && logFile.is_open()) {
            logFile << "[FMAP] " << msg << std::endl;
            logFile.flush();
        }
    }

    void LogCheck(int mapId, float x1, float y1, float z1, float x2, float y2, float z2, bool hit) {
        totalChecks++;
        if (hit) totalHits++;

        if (enabled && logFile.is_open() && (totalChecks % 50 == 1)) {
            logFile << "[LOS#" << totalChecks << "] Map=" << mapId << " | ";
            logFile << std::fixed << std::setprecision(2);
            logFile << "(" << x1 << "," << y1 << "," << z1 << ")->(" << x2 << "," << y2 << "," << z2 << ") | ";
            float dist = std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1) + (z2 - z1) * (z2 - z1));
            logFile << "Dist=" << dist << " | " << (hit ? "BLOCKED" : "CLEAR") << std::endl;
            logFile.flush();
        }
    }

    void LogFloorQuery(float x, float y, float z, float result) {
        totalFloorQueries++;
        if (enabled && logFile.is_open()) {
            logFile << "[FLOOR#" << totalFloorQueries << "] (" << std::fixed << std::setprecision(2)
                << x << "," << y << "," << z << ") -> " << result << std::endl;
            logFile.flush();
        }
    }

    void LogTileLoad(const std::string& filename, bool success, int cellsWithData = 0, int totalLayers = 0) {
        if (enabled && logFile.is_open()) {
            if (success) {
                logFile << "[TILE] ? " << filename << " | Cells: " << cellsWithData
                    << "/" << FMAP_TOTAL_CELLS << " | Layers: " << totalLayers << std::endl;
            }
            else {
                logFile << "[TILE] ? Failed: " << filename << std::endl;
            }
            logFile.flush();
        }
    }

    void LogSampleCell(int x, int y, int layerCount, float floor, float ceiling) {
        if (enabled && logFile.is_open()) {
            logFile << "  Sample[" << x << "," << y << "]: " << layerCount << " layer(s) | ";
            if (layerCount > 0) {
                logFile << "Floor=" << floor << " Ceiling=" << ceiling;
                if (ceiling >= 9999.0f) logFile << " (OPEN SKY)";
            }
            else {
                logFile << "VOID/SOLID";
            }
            logFile << std::endl;
        }
    }
};

static FMapLogger g_Logger;

// --- MATH STRUCTURES ---
struct Vector3 {
    float x, y, z;
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
    Vector3 operator+(const Vector3& v) const { return Vector3(x + v.x, y + v.y, z + v.z); }
    Vector3 operator-(const Vector3& v) const { return Vector3(x - v.x, y - v.y, z - v.z); }
    Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }
    Vector3 operator/(float s) const { return Vector3(x / s, y / s, z / s); }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
};

// --- VOXEL LAYER (one walkable surface) ---
struct VoxelLayer {
    uint16_t floorRaw;    // Quantized floor height
    uint16_t ceilingRaw;  // Quantized ceiling height

    float getFloorZ() const {
        return (float)floorRaw * FMAP_HEIGHT_PRECISION - FMAP_HEIGHT_BASE;
    }

    float getCeilingZ() const {
        return (float)ceilingRaw * FMAP_HEIGHT_PRECISION - FMAP_HEIGHT_BASE;
    }

    bool isOpenSky() const {
        // If ceiling is very high or max value, it's open sky
        return ceilingRaw >= 22000;  // ~6000 units is way above any geometry
    }
};

// --- VOXEL CELL (vertical column at one X,Y) ---
struct VoxelCell {
    std::vector<VoxelLayer> layers;

    bool isEmpty() const { return layers.empty(); }

    // Get the floor height at or below a given Z
    float getFloorBelow(float z) const {
        if (layers.empty()) return -99999.0f;

        float bestFloor = -99999.0f;

        for (const auto& layer : layers) {
            float floor = layer.getFloorZ();
            float ceiling = layer.getCeilingZ();

            // If we're within this layer's vertical span
            if ((z + FMAP_HEIGHT_RANGE) >= floor && z <= ceiling) {
                return floor;  // Standing on this floor
            }

            // Track highest floor below us
            if (floor <= z && floor > bestFloor) {
                bestFloor = floor;
            }
        }

        return bestFloor;
    }

    float getCeilingAbove(float z) const {
        if (layers.empty()) return 99999.0f;

        float bestCeiling = 99999.0f;

        for (const auto& layer : layers) {
            float floor = layer.getFloorZ();
            float ceiling = layer.getCeilingZ();

            // If we're within this layer's vertical span
            if ((z + FMAP_HEIGHT_RANGE) >= floor && z <= ceiling) {
                return ceiling;  // Standing on this floor
            }

            // Track highest floor below us
            if (ceiling >= z && ceiling < bestCeiling) {
                bestCeiling = ceiling;
            }
        }

        return bestCeiling;
    }

    // Check if we can fly at height Z
    bool canFlyAt(float z) const {
        std::cout << layers.empty() << std::endl;
        if (layers.empty()) return false;  // Void/solid - can't fly

        for (const auto& layer : layers) {
            float floor = layer.getFloorZ();
            float ceiling = layer.getCeilingZ();
            std::cout << floor << " " << ceiling << std::endl;;
            // Check if Z is within a walkable span
            if ((z + FMAP_HEIGHT_RANGE) >= floor && z <= ceiling) {
                // Can fly if this span has open sky above
                std::cout << "isOpen Sky: " << layer.isOpenSky() << std::endl;;
                return layer.isOpenSky() || (z < ceiling - FMAP_AGENT_HEIGHT);
            }
        }

        // Above all layers - check if top layer is open sky
        const VoxelLayer& topLayer = layers.back();
        if (z > topLayer.getCeilingZ() && topLayer.isOpenSky()) {
            return true;
        }

        return false;
    }

    // Check if line segment from z1 to z2 is clear
    bool isVerticalClear(float z1, float z2) const {
        if (layers.empty()) return false;  // Solid

        float minZ = std::min(z1, z2);
        float maxZ = std::max(z1, z2);

        for (const auto& layer : layers) {
            float floor = layer.getFloorZ();
            float ceiling = layer.getCeilingZ();

            // Check if segment intersects this layer's navigable space
            if ((maxZ + FMAP_HEIGHT_RANGE) >= floor && minZ <= ceiling) {
                return true;  // Found a layer that contains part of the segment
            }
        }

        return false;
    }
};

// --- FMAP TILE (160x160 grid) ---
class FMapTile {
public:
    int mapId;
    int tileX, tileY;
    float originX, originY;  // World coordinates of tile origin

    VoxelCell grid[FMAP_GRID_HEIGHT][FMAP_GRID_WIDTH];  // [Y][X] for row-major

    FMapTile() : mapId(0), tileX(0), tileY(0), originX(0), originY(0) {}

    bool loadFromFile(const std::string& filepath) {
        FILE* f = fopen(filepath.c_str(), "rb");
        if (!f) {
            g_Logger.LogTileLoad(filepath, false);
            return false;
        }

        // Read 24-byte header (NOT 32!)
        char header[24];
        if (fread(header, 1, 24, f) != 24) {
            fclose(f);
            g_Logger.LogTileLoad(filepath, false);
            return false;
        }

        // Verify magic "PAMF" at offset 0
        if (memcmp(header, "PAMF", 4) != 0) {
            fclose(f);
            g_Logger.Log("Invalid magic signature in " + filepath);
            g_Logger.LogTileLoad(filepath, false);
            return false;
        }

        // Read version at offset 4
        uint32_t version;
        memcpy(&version, header + 4, 4);

        // Read cell sizes at offsets 16 and 20 (should be 3.33333)
        float cellSizeX, cellSizeY;
        memcpy(&cellSizeX, header + 16, 4);
        memcpy(&cellSizeY, header + 20, 4);

        if (std::abs(cellSizeX - FMAP_CELL_SIZE) > 0.01f) {
            char msg[128];
            sprintf(msg, "Warning: Cell size mismatch (%.2f vs %.2f)", cellSizeX, FMAP_CELL_SIZE);
            g_Logger.Log(msg);
        }

        // Grid data starts at byte 24 (immediately after header)

        // Read grid data - Row-Major: Y outer loop, X inner loop
        int cellsWithData = 0;
        int totalLayers = 0;

        for (int y = 0; y < FMAP_GRID_HEIGHT; ++y) {
            for (int x = 0; x < FMAP_GRID_WIDTH; ++x) {
                uint8_t layerCount;
                if (fread(&layerCount, 1, 1, f) != 1) {
                    fclose(f);
                    g_Logger.LogTileLoad(filepath, false);
                    return false;
                }

                if (layerCount > 0) {
                    cellsWithData++;
                    totalLayers += layerCount;
                }

                // Read each layer
                for (int i = 0; i < layerCount; ++i) {
                    VoxelLayer layer;

                    if (fread(&layer.floorRaw, 2, 1, f) != 1) {
                        fclose(f);
                        g_Logger.LogTileLoad(filepath, false);
                        return false;
                    }

                    if (fread(&layer.ceilingRaw, 2, 1, f) != 1) {
                        fclose(f);
                        g_Logger.LogTileLoad(filepath, false);
                        return false;
                    }

                    grid[y][x].layers.push_back(layer);
                }

                // Log first few non-empty cells
                if (cellsWithData <= 3 && layerCount > 0) {
                    const VoxelLayer& firstLayer = grid[y][x].layers[0];
                    g_Logger.LogSampleCell(x, y, layerCount,
                        firstLayer.getFloorZ(),
                        firstLayer.getCeilingZ());
                }
            }
        }

        fclose(f);
        g_Logger.LogTileLoad(filepath, true, cellsWithData, totalLayers);

        return true;
    }

    // Convert world coordinates to grid indices
    void worldToGrid(float worldX, float worldY, int& gx, int& gy) const {
        // Calculate local coordinates
        float localX = worldY - originY;  // From WoW Y: 4934.55 - 4800 = 134.55
        float localY = worldX - originX;  // From WoW X: 809.49 - 533.33 = 276.16

        // Grid indices - THIS IS WHERE THE FIX IS
        gx = (int)(localX / FMAP_CELL_SIZE);  // 134.55 / 3.333 = 40
        gy = (int)(localY / FMAP_CELL_SIZE);  // 276.16 / 3.333 = 82

        // Debug logging
        if (DEBUG_FMAP) {
            char msg[400];
            sprintf(msg, "    worldToGrid DEBUG: worldX=%.2f worldY=%.2f", worldX, worldY);
            g_Logger.Log(msg);
            sprintf(msg, "      localX = %.2f - %.2f = %.2f", worldY, originY, localX);
            g_Logger.Log(msg);
            sprintf(msg, "      localY = %.2f - %.2f = %.2f", worldX, originX, localY);
            g_Logger.Log(msg);
            sprintf(msg, "      gx = %.2f / %.2f = %d", localX, FMAP_CELL_SIZE, gx);
            g_Logger.Log(msg);
            sprintf(msg, "      gy = %.2f / %.2f = %d", localY, FMAP_CELL_SIZE, gy);
            g_Logger.Log(msg);
        }
    }

    bool isInBounds(int gx, int gy) const {
        return gx >= 0 && gx < FMAP_GRID_WIDTH && gy >= 0 && gy < FMAP_GRID_HEIGHT;
    }

    const VoxelCell* getCell(float worldX, float worldY) const {
        int gx, gy;
        worldToGrid(worldX, worldY, gx, gy);

        if (DEBUG_FMAP) {
            char msg[300];
            sprintf(msg, "  getCell: WoW(X=%.2f, Y=%.2f) ? Local(%.2f, %.2f) ? Grid[%d,%d]",
                worldX, worldY, worldY - originY, worldX - originX, gx, gy);
            g_Logger.Log(msg);
            sprintf(msg, "    Tile[tx=%d,ty=%d] Origin(X=%.2f,Y=%.2f)", tileX, tileY, originX, originY);
            g_Logger.Log(msg);
        }

        if (!isInBounds(gx, gy)) {
            if (DEBUG_FMAP) {
                g_Logger.Log("  -> OUT OF BOUNDS!");
            }
            return nullptr;
        }
        // Grid access: same as visualizer
        return &grid[gy][gx];
    }

    // Get floor height at world position
    float getFloorHeight(float worldX, float worldY, float worldZ) const {
        const VoxelCell* cell = getCell(worldX, worldY);
        if (!cell) return -99999.0f;

        if (DEBUG_FMAP && !cell->isEmpty()) {
            char msg[256];
            sprintf(msg, "  Cell has %d layer(s)", (int)cell->layers.size());
            g_Logger.Log(msg);

            for (size_t i = 0; i < cell->layers.size() && i < 3; ++i) {
                sprintf(msg, "    Layer %d: Floor=%.1f Ceiling=%.1f (raw: %u, %u)",
                    (int)i, cell->layers[i].getFloorZ(), cell->layers[i].getCeilingZ(),
                    cell->layers[i].floorRaw, cell->layers[i].ceilingRaw);
                g_Logger.Log(msg);
            }
        }

        float result = cell->getFloorBelow(worldZ);

        if (DEBUG_FMAP) {
            char msg[128];
            sprintf(msg, "  getFloorBelow(%.2f) returned: %.2f", worldZ, result);
            g_Logger.Log(msg);
        }

        return result;
    }

    float getCeilingHeight(float worldX, float worldY, float worldZ) const {
        const VoxelCell* cell = getCell(worldX, worldY);
        if (!cell) return -99999.0f;

        if (DEBUG_FMAP && !cell->isEmpty()) {
            char msg[256];
            sprintf(msg, "  Cell has %d layer(s)", (int)cell->layers.size());
            g_Logger.Log(msg);

            for (size_t i = 0; i < cell->layers.size() && i < 3; ++i) {
                sprintf(msg, "    Layer %d: Floor=%.1f Ceiling=%.1f (raw: %u, %u)",
                    (int)i, cell->layers[i].getFloorZ(), cell->layers[i].getCeilingZ(),
                    cell->layers[i].floorRaw, cell->layers[i].ceilingRaw);
                g_Logger.Log(msg);
            }
        }

        float result = cell->getCeilingAbove(worldZ);

        if (DEBUG_FMAP) {
            char msg[128];
            sprintf(msg, "  getCeilingAbove(%.2f) returned: %.2f", worldZ, result);
            g_Logger.Log(msg);
        }

        return result;
    }

    // Check if position is flyable
    bool canFlyAt(float worldX, float worldY, float worldZ) const {
        const VoxelCell* cell = getCell(worldX, worldY);
        if (!cell) return false;
        return cell->canFlyAt(worldZ);
    }

    // Line-of-sight check using DDA (Digital Differential Analyzer)
    bool checkLineOfSight(float x1, float y1, float z1, float x2, float y2, float z2) const {
        Vector3 start(x1, y1, z1);
        Vector3 end(x2, y2, z2);
        Vector3 delta = end - start;
        float dist = delta.length();

        if (dist < 0.001f) return true;  // Zero-length ray is clear

        Vector3 dir = delta / dist;

        // Step along ray checking cells
        int steps = (int)(dist / (FMAP_CELL_SIZE * 0.5f)) + 1;

        for (int i = 0; i <= steps; ++i) {
            float t = (float)i / (float)steps;
            Vector3 pos = start + (delta * t);

            const VoxelCell* cell = getCell(pos.x, pos.y);

            if (!cell) continue;  // Outside tile

            if (cell->isEmpty()) {
                // Solid geometry - blocked
                return false;
            }

            // Check if our Z height can pass through this cell
            if (!cell->isVerticalClear(pos.z - FMAP_AGENT_HEIGHT * 0.5f,
                pos.z + FMAP_AGENT_HEIGHT * 0.5f)) {
                return false;
            }
        }

        return true;
    }
};

// --- FMAP SYSTEM MANAGER ---
class FMapSystem {
private:
    std::string basePath;
    std::map<uint64_t, FMapTile*> loadedTiles;

    uint64_t packKey(int mapId, int x, int y) const {
        return ((uint64_t)mapId << 32) | ((uint64_t)x << 16) | (uint64_t)y;
    }

public:
    FMapTile* getTileAt(int mapId, float x, float y) {
        // CONFIRMED: Detour/Recast coordinate system
        // tx derives from WoW Y, ty derives from WoW X
        int tx = (int)(32 - (y / 533.33333f));  // tx from WoW Y
        int ty = (int)(32 - (x / 533.33333f));  // ty from WoW X

        uint64_t key = packKey(mapId, tx, ty);

        auto it = loadedTiles.find(key);
        if (it != loadedTiles.end()) {
            return it->second;
        }

        // Filename format: MAPID_TY_TX (confirmed from generator)
        char filename[64];
        sprintf(filename, "%04d_%02d_%02d.fmtile", mapId, ty, tx);

        FMapTile* tile = new FMapTile();
        tile->mapId = mapId;
        tile->tileX = tx;
        tile->tileY = ty;

        // Origins (confirmed): originX from ty, originY from tx
        tile->originX = (31 - ty) * 533.33333f;  // X origin from ty
        tile->originY = (31 - tx) * 533.33333f;  // Y origin from tx

        if (DEBUG_FMAP) {
            char msg[256];
            sprintf(msg, "getTileAt(mapId=%d, x=%.2f, y=%.2f)", mapId, x, y);
            g_Logger.Log(msg);
            sprintf(msg, "  Calculated: tx=%d (from y), ty=%d (from x)", tx, ty);
            g_Logger.Log(msg);
            sprintf(msg, "  Filename: %s", filename);
            g_Logger.Log(msg);
        }

        if (tile->loadFromFile(basePath + filename)) {
            if (DEBUG_FMAP) {
                char msg[256];
                sprintf(msg, "  Loaded successfully. Origin=(%.2f, %.2f)", tile->originX, tile->originY);
                g_Logger.Log(msg);
            }
            loadedTiles[key] = tile;
            return tile;
        }
        else {
            delete tile;
            loadedTiles[key] = nullptr;
            return nullptr;
        }
    }

    void init(const std::string& path) {
        basePath = path;
        if (basePath.back() != '/' && basePath.back() != '\\') {
            basePath += "/";
        }
        g_Logger.Log("FMap system initialized: " + basePath);
    }

    ~FMapSystem() {
        for (auto& pair : loadedTiles) {
            delete pair.second;
        }
    }

    // Cleanup tiles retentionRadius away from player or on a diffrent map
    void CleanupTiles(int currentMapId, float playerX, float playerY, float retentionRadius) {
        auto it = loadedTiles.begin();
        while (it != loadedTiles.end()) {
            uint64_t key = it->first;
            FMapTile* tile = it->second;

            // CRITICAL FIX 1: If tile failed to load previously (nullptr), remove it or skip it.
            // We remove it so we can try loading it again later if needed, or just to clean the map key.
            if (tile == nullptr) {
                it = loadedTiles.erase(it);
                continue;
            }

            // Extract mapId from key (upper 32 bits)
            int tileMapId = (int)(key >> 32);

            // CRITICAL FIX 2: Correct X/Y mapping. 
            // originX corresponds to WoW X, originY corresponds to WoW Y.
            float dist = std::sqrt(std::pow(tile->originX - playerX, 2) + std::pow(tile->originY - playerY, 2));

            // Unload if different map OR too far away
            if (tileMapId != currentMapId || dist > retentionRadius) {
                if (DEBUG_FMAP) {
                    // Optional logging
                    // char msg[128];
                    // sprintf(msg, "Unloading Tile: %04d (Dist: %.2f)", tile->mapId, dist);
                    // g_Logger.Log(msg);
                }
                delete tile; // Free memory
                it = loadedTiles.erase(it); // Remove from map
            }
            else {
                ++it;
            }
        }
    }

    bool checkLine(int mapId, float x1, float y1, float z1, float x2, float y2, float z2) {
        // Get tile at midpoint
        float midX = (x1 + x2) * 0.5f;
        float midY = (y1 + y2) * 0.5f;

        FMapTile* tile = getTileAt(mapId, midX, midY);
        if (!tile) {
            g_Logger.LogCheck(mapId, x1, y1, z1, x2, y2, z2, false);
            return false;  // No tile = no collision
        }

        bool blocked = !tile->checkLineOfSight(x1, y1, z1, x2, y2, z2);
        g_Logger.LogCheck(mapId, x1, y1, z1, x2, y2, z2, blocked);
        return blocked;
    }

    float getFloorHeight(int mapId, float x, float y, float z) {
        FMapTile* tile = getTileAt(mapId, x, y);
        if (!tile) {
            if (DEBUG_FMAP) {
                char msg[128];
                sprintf(msg, "getFloorHeight(%.2f, %.2f, %.2f) - No tile loaded", x, y, z);
                g_Logger.Log(msg);
            }
            return -99999.0f;
        }

        if (DEBUG_FMAP) {
            char msg[256];
            sprintf(msg, "getFloorHeight(%.2f, %.2f, %.2f) - Using tile %04d_%02d_%02d",
                x, y, z, mapId, tile->tileY, tile->tileX);
            g_Logger.Log(msg);
        }

        float result = tile->getFloorHeight(x, y, z);
        g_Logger.LogFloorQuery(x, y, z, result);
        return result;
    }

    float getCeilingHeight(int mapId, float x, float y, float z) {
        FMapTile* tile = getTileAt(mapId, x, y);
        if (!tile) {
            if (DEBUG_FMAP) {
                char msg[128];
                sprintf(msg, "getCeilingHeight(%.2f, %.2f, %.2f) - No tile loaded", x, y, z);
                g_Logger.Log(msg);
            }
            return -99999.0f;
        }

        if (DEBUG_FMAP) {
            char msg[256];
            sprintf(msg, "getCeilingHeight(%.2f, %.2f, %.2f) - Using tile %04d_%02d_%02d",
                x, y, z, mapId, tile->tileY, tile->tileX);
            g_Logger.Log(msg);
        }

        float result = tile->getCeilingHeight(x, y, z);
        //g_Logger.LogCeilingQuery(x, y, z, result);
        return result;
    }

    bool canFlyAt(int mapId, float x, float y, float z) {
        FMapTile* tile = getTileAt(mapId, x, y);
        if (!tile) return false;
        return tile->canFlyAt(x, y, z);
    }
};

static FMapSystem g_FMapSys;

// Detect if a position is in an enclosed space (tunnel/cave/indoor)
bool detectTunnel(int mapId, float x, float y, float z) {
    FMapTile* tile = g_FMapSys.getTileAt(mapId, x, y);
    if (!tile) return false;  // No tile = outdoor, not a tunnel

    const VoxelCell* currentCell = tile->getCell(x, y);
    if (!currentCell || currentCell->isEmpty()) {
        return false;  // Solid or void, but not a navigable tunnel
    }

    // === CHECK 1: LOW CEILING ===
    const float TUNNEL_CEILING_HEIGHT = 20.0f;
    bool hasLowCeiling = false;
    float ceilingDistance = 999.0f;

    // Check vertical clearance at current position
    for (const auto& layer : currentCell->layers) {
        float floor = layer.getFloorZ();
        float ceiling = layer.getCeilingZ();

        // Are we in this layer's vertical span?
        if ((z + FMAP_HEIGHT_RANGE) >= floor && z <= ceiling) {
            ceilingDistance = ceiling - z;
            if (ceilingDistance < TUNNEL_CEILING_HEIGHT && !layer.isOpenSky()) {
                hasLowCeiling = true;
            }
            break;
        }
    }

    // === CHECK 2: HORIZONTAL ENCLOSURE ===
    // Sample 8 directions to detect walls
    const float CHECK_RADIUS = 8.0f;  // Check 8 units away
    int blockedDirections = 0;

    struct Direction {
        float dx, dy;
    };

    Direction directions[8] = {
        {CHECK_RADIUS, 0},                 // East
        {-CHECK_RADIUS, 0},                // West
        {0, CHECK_RADIUS},                 // North
        {0, -CHECK_RADIUS},                // South
        {CHECK_RADIUS, CHECK_RADIUS},      // NE
        {-CHECK_RADIUS, CHECK_RADIUS},     // NW
        {CHECK_RADIUS, -CHECK_RADIUS},     // SE
        {-CHECK_RADIUS, -CHECK_RADIUS}     // SW
    };

    for (int i = 0; i < 8; ++i) {
        float testX = x + directions[i].dx;
        float testY = y + directions[i].dy;
        float testZ = z + 1.0f;  // Check at head height

        // Check if ray to this direction is blocked
        if (g_FMapSys.checkLine(mapId, x, y, z + 1.0f, testX, testY, testZ)) {
            blockedDirections++;
        }
    }

    // === CHECK 3: VERTICAL CONFINEMENT ===
    // Check if there's geometry above (ceiling) within a short distance
    bool hasGeometryAbove = false;
    float testHeight = z + 5.0f;
    for (int i = 0; i < 4; ++i) {
        if (g_FMapSys.checkLine(mapId, x, y, z, x, y, testHeight)) {
            hasGeometryAbove = true;
            break;
        }
        testHeight += 5.0f;
        if (testHeight - z > TUNNEL_CEILING_HEIGHT) break;
    }

    // === DECISION LOGIC ===
    // Tunnel criteria:
    // 1. Low ceiling OR geometry above + blocked on 4+ sides = TUNNEL
    // 2. Blocked on 6+ sides regardless = ENCLOSED SPACE
    // 3. Low ceiling + no open sky + blocked on 3+ sides = INDOOR

    bool criteriaTunnel = (hasLowCeiling || hasGeometryAbove) && (blockedDirections >= 4);
    bool criteriaEnclosed = (blockedDirections >= 6);
    bool criteriaIndoor = hasLowCeiling && !currentCell->layers.empty() &&
        !currentCell->layers.back().isOpenSky() && (blockedDirections >= 3);

    bool inTunnel = criteriaTunnel || criteriaEnclosed || criteriaIndoor;

    if (DEBUG_FMAP && inTunnel) {
        g_Logger.Log("=== TUNNEL DETECTED ===");
        char msg[512];
        sprintf(msg, "  Position: (%.2f, %.2f, %.2f)", x, y, z);
        g_Logger.Log(msg);
        sprintf(msg, "  Low ceiling: %s (%.2f units above)", hasLowCeiling ? "YES" : "NO", ceilingDistance);
        g_Logger.Log(msg);
        sprintf(msg, "  Blocked directions: %d/8", blockedDirections);
        g_Logger.Log(msg);
        sprintf(msg, "  Geometry above: %s", hasGeometryAbove ? "YES" : "NO");
        g_Logger.Log(msg);
        sprintf(msg, "  Classification: %s",
            criteriaEnclosed ? "ENCLOSED" : (criteriaTunnel ? "TUNNEL" : "INDOOR"));
        g_Logger.Log(msg);
    }

    return inTunnel;
}

// --- EXPORTED C API ---
extern "C" {
    __declspec(dllexport) bool CheckFMapLine(int mapId, float x1, float y1, float z1,
        float x2, float y2, float z2) {
        static bool initialized = false;
        if (!initialized) {
            g_FMapSys.init("C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/fmaps/");
            initialized = true;
        }
        return g_FMapSys.checkLine(mapId, x1, y1, z1, x2, y2, z2);
    }

    __declspec(dllexport) float GetFMapFloorHeight(int mapId, float x, float y, float z) {
        static bool initialized = false;
        if (!initialized) {
            g_FMapSys.init("C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/fmaps/");
            initialized = true;
        }
        return g_FMapSys.getFloorHeight(mapId, x, y, z);
    }

    __declspec(dllexport) float GetFMapCeilingHeight(int mapId, float x, float y, float z) {
        static bool initialized = false;
        if (!initialized) {
            g_FMapSys.init("C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/fmaps/");
            initialized = true;
        }
        return g_FMapSys.getCeilingHeight(mapId, x, y, z);
    }

    __declspec(dllexport) bool CanFlyAt(int mapId, float x, float y, float z) {
        static bool initialized = false;
        if (!initialized) {
            g_FMapSys.init("C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/fmaps/");
            initialized = true;
        }
        return g_FMapSys.canFlyAt(mapId, x, y, z);
    }

    __declspec(dllexport) bool IsInTunnel(int mapId, float x, float y, float z) {
        static bool initialized = false;
        if (!initialized) {
            g_FMapSys.init("C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/fmaps/");
            initialized = true;
        }
        return detectTunnel(mapId, x, y, z);
    }

    __declspec(dllexport) void CleanupFMapCache(int mapId, float x, float y) {
        static bool initialized = false;
        if (!initialized) {
            // Ensure init is called if not already
            g_FMapSys.init("C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/fmaps/");
            initialized = true;
        }
        // Prune tiles further than 1200 yards away
        g_FMapSys.CleanupTiles(mapId, x, y, 1200.0f);
    }
}