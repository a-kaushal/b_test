// VMapLoader.cpp
// Zero-Dependency VMap Reader with COMPREHENSIVE DEBUG LOGGING

#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
#include <limits>
#include <fstream>
#include <iomanip>
#include <chrono>

// --- CONFIGURATION ---
const float VMAP_FLIGHT_CLEARANCE = 25.0f;
const bool DEBUG_VMAP = true;  // Enable/disable VMap logging

// --- DEBUG LOGGER ---
class VMapLogger {
private:
    std::ofstream g_LogFile;
    bool enabled;

public:
    VMapLogger() : enabled(DEBUG_VMAP) {
        if (enabled) {
            g_LogFile.open("C:\\Driver\\SMM_VMap_Debug.log", std::ios::app);
            if (g_LogFile.is_open()) {
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                g_LogFile << "\n========================================\n";
                g_LogFile << "VMap Session Started: " << std::ctime(&time);
                g_LogFile << "========================================\n\n";
            }
        }
    }

    ~VMapLogger() {
        if (g_LogFile.is_open()) {
            g_LogFile.close();
        }
    }

    void Log(const std::string& msg) {
        if (enabled && g_LogFile.is_open()) {
            g_LogFile << "[VMAP] " << msg << std::endl;
            g_LogFile.flush(); // Ensure immediate write
        }
    }

    void LogCheck(int mapId, float x1, float y1, float z1, float x2, float y2, float z2, bool hit) {
        if (enabled && g_LogFile.is_open()) {
            g_LogFile << "[CHECK] Map=" << mapId << " | ";
            g_LogFile << std::fixed << std::setprecision(2);
            g_LogFile << "Start=(" << x1 << ", " << y1 << ", " << z1 << ") ";
            g_LogFile << "End=(" << x2 << ", " << y2 << ", " << z2 << ") | ";
            g_LogFile << "Distance=" << std::sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1) + (z2 - z1) * (z2 - z1)) << " | ";
            g_LogFile << "Result: " << (hit ? "BLOCKED ❌" : "CLEAR ✓") << std::endl;
            g_LogFile.flush();
        }
    }

    void LogTileLoad(const std::string& filename, bool success, int instanceCount = 0) {
        if (enabled && g_LogFile.is_open()) {
            if (success) {
                g_LogFile << "[TILE] Loaded: " << filename << " (" << instanceCount << " instances)" << std::endl;
            }
            else {
                g_LogFile << "[TILE] Failed to load: " << filename << std::endl;
            }
            g_LogFile.flush();
        }
    }

    void LogCollision(int instanceIdx, const std::string& bounds) {
        if (enabled && g_LogFile.is_open()) {
            g_LogFile << "  ├─ HIT Instance #" << instanceIdx << " " << bounds << std::endl;
            g_LogFile.flush();
        }
    }

    void LogClearance(float midZ, float threshold) {
        if (enabled && g_LogFile.is_open()) {
            g_LogFile << "  ├─ High altitude flight detected (Z=" << midZ
                << " > threshold=" << threshold << ") - SKIPPING VMap check" << std::endl;
            g_LogFile.flush();
        }
    }
};

static VMapLogger g_Logger;

// ---------------------------------------------------------
// 1. MINIMAL MATH LIBRARY
// ---------------------------------------------------------

struct Vector3 {
    float x, y, z;
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    Vector3 operator+(const Vector3& v) const { return Vector3(x + v.x, y + v.y, z + v.z); }
    Vector3 operator-(const Vector3& v) const { return Vector3(x - v.x, y - v.y, z - v.z); }
    Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }
    Vector3 operator/(float s) const { return Vector3(x / s, y / s, z / s); }

    float length() const { return std::sqrt(x * x + y * y + z * z); }

    bool isZero() const { return (x == 0 && y == 0 && z == 0); }

    Vector3 normalize() const {
        float len = length();
        return (len > 1e-5f) ? (*this / len) : Vector3(0, 0, 0);
    }

    std::string toString() const {
        char buf[64];
        sprintf(buf, "(%.2f, %.2f, %.2f)", x, y, z);
        return std::string(buf);
    }
};

struct AABox {
    Vector3 lo, hi;
    AABox() {}
    AABox(const Vector3& min, const Vector3& max) : lo(min), hi(max) {}

    std::string toString() const {
        char buf[128];
        sprintf(buf, "Min=(%.2f,%.2f,%.2f) Max=(%.2f,%.2f,%.2f)",
            lo.x, lo.y, lo.z, hi.x, hi.y, hi.z);
        return std::string(buf);
    }

    Vector3 center() const {
        return Vector3((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);
    }

    Vector3 size() const {
        return Vector3(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z);
    }
};

struct Ray {
    Vector3 origin;
    Vector3 direction;
    Ray(const Vector3& o, const Vector3& d) : origin(o), direction(d) {}

    Vector3 invDirection() const {
        float x = (std::abs(direction.x) < 1e-5f) ? 1e20f : (1.0f / direction.x);
        float y = (std::abs(direction.y) < 1e-5f) ? 1e20f : (1.0f / direction.y);
        float z = (std::abs(direction.z) < 1e-5f) ? 1e20f : (1.0f / direction.z);
        return Vector3(x, y, z);
    }
};

// Standard AABB Ray Intersection with detailed logging
bool RayAABBIntersection(const Ray& r, const AABox& box, float maxDist, int instanceIdx = -1) {
    Vector3 invDir = r.invDirection();

    float t1 = (box.lo.x - r.origin.x) * invDir.x;
    float t2 = (box.hi.x - r.origin.x) * invDir.x;
    float t3 = (box.lo.y - r.origin.y) * invDir.y;
    float t4 = (box.hi.y - r.origin.y) * invDir.y;
    float t5 = (box.lo.z - r.origin.z) * invDir.z;
    float t6 = (box.hi.z - r.origin.z) * invDir.z;

    float tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
    float tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));

    bool hit = !(tmax < 0 || tmin > tmax || tmin > maxDist);

    if (DEBUG_VMAP && hit && instanceIdx >= 0) {
        char msg[256];
        sprintf(msg, "Ray hit detected: Instance #%d at t=%.2f (tmin=%.2f, tmax=%.2f, maxDist=%.2f)",
            instanceIdx, tmin, tmin, tmax, maxDist);
        g_Logger.Log(msg);
    }

    return hit;
}

// ---------------------------------------------------------
// 2. VMAP DATA PARSER
// ---------------------------------------------------------

namespace VMParser {
    struct ModelInstance {
        uint32_t flags;
        uint32_t adtId;
        uint32_t id;
        Vector3 pos;
        Vector3 scale;
        float rot[4];
        AABox bound;
    };

    class WorldModel {
    public:
        std::vector<ModelInstance> instances;
        std::string filename;

        bool readFile(const std::string& fname) {
            filename = fname;
            FILE* rf = fopen(filename.c_str(), "rb");
            if (!rf) {
                g_Logger.LogTileLoad(filename, false);
                return false;
            }

            // 1. Read header
            char header[16];
            if (fread(header, 1, 16, rf) != 16) {
                fclose(rf);
                g_Logger.LogTileLoad(filename, false);
                return false;
            }

            // 2. Check for VMAP signature
            if (strncmp(header, "VMAP", 4) != 0) {
                fclose(rf);
                g_Logger.LogTileLoad(filename, false);
                return false;
            }

            // 3. Handle Variations
            bool isV4 = false;

            if (strncmp(header, "VMAP_5.3f", 9) == 0) {
                fseek(rf, 17, SEEK_SET);
                isV4 = true;
            }
            else if (strncmp(header, "VMAP004", 7) == 0) {
                fseek(rf, 8, SEEK_SET);
                isV4 = true;
            }
            else {
                fseek(rf, 8, SEEK_SET);
                isV4 = false;
            }

            // 4. Read First Value
            uint32_t firstVal = 0;
            if (fread(&firstVal, sizeof(uint32_t), 1, rf) != 1) {
                fclose(rf);
                g_Logger.LogTileLoad(filename, false);
                return false;
            }

            uint32_t count = 0;

            if (isV4) {
                fseek(rf, firstVal, SEEK_CUR);
                if (fread(&count, sizeof(uint32_t), 1, rf) != 1) {
                    fclose(rf);
                    g_Logger.LogTileLoad(filename, false);
                    return false;
                }
            }
            else {
                count = firstVal;
            }

            if (count > 50000) {
                fclose(rf);
                g_Logger.LogTileLoad(filename, false);
                return false;
            }

            instances.resize(count);
            for (uint32_t i = 0; i < count; ++i) {
                ModelInstance& inst = instances[i];

                if (fread(&inst.flags, 4, 1, rf) != 1) break;
                if (fread(&inst.adtId, 4, 1, rf) != 1) break;
                if (fread(&inst.id, 4, 1, rf) != 1) break;

                fread(&inst.pos, sizeof(float), 3, rf);
                fread(inst.rot, sizeof(float), 3, rf);
                float scale;
                fread(&scale, sizeof(float), 1, rf);
                inst.scale = Vector3(scale, scale, scale);

                float min[3], max[3];
                fread(min, sizeof(float), 3, rf);
                fread(max, sizeof(float), 3, rf);
                inst.bound = AABox(Vector3(min[0], min[1], min[2]), Vector3(max[0], max[1], max[2]));

                if (isV4) {
                    uint32_t nameId;
                    fread(&nameId, 4, 1, rf);
                }
            }

            fclose(rf);
            g_Logger.LogTileLoad(filename, true, count);

            if (DEBUG_VMAP && count > 0) {
                char msg[256];
                sprintf(msg, "  ├─ Sample instance bounds: %s", instances[0].bound.toString().c_str());
                g_Logger.Log(msg);
            }

            return true;
        }
    };
}

// ---------------------------------------------------------
// 3. SYSTEM MANAGER
// ---------------------------------------------------------

class VMapSystem {
    std::string basePath;
    std::map<uint64_t, VMParser::WorldModel*> loadedTiles;
    int totalChecks = 0;
    int totalHits = 0;
    int totalSkipped = 0;

    uint64_t Pack(int mapId, int x, int y) {
        return ((uint64_t)mapId << 32) | ((uint64_t)x << 16) | (uint64_t)y;
    }

public:
    void Init(const std::string& path) {
        basePath = path;
        g_Logger.Log("Initialized with path: " + basePath);
    }

    ~VMapSystem() {
        if (DEBUG_VMAP) {
            char summary[256];
            sprintf(summary, "\nVMap Summary: %d checks, %d hits (%.1f%%), %d skipped (high altitude)",
                totalChecks, totalHits,
                totalChecks > 0 ? (100.0f * totalHits / totalChecks) : 0.0f,
                totalSkipped);
            g_Logger.Log(summary);
        }
    }

    bool Check(int mapId, float x1, float y1, float z1, float x2, float y2, float z2) {
        totalChecks++;

        Vector3 start(x1, y1, z1);
        Vector3 end(x2, y2, z2);

        float midX = (x1 + x2) * 0.5f;
        float midY = (y1 + y2) * 0.5f;
        float midZ = (z1 + z2) * 0.5f;

        // High altitude check
        if (midZ > 50.0f) {
            totalSkipped++;
            if (DEBUG_VMAP && (totalSkipped % 100 == 1)) { // Log every 100th skip
                g_Logger.LogClearance(midZ, 50.0f);
            }
            return false;
        }

        Vector3 dirVec = end - start;
        float dist = dirVec.length();
        if (dist < 0.001f) {
            g_Logger.LogCheck(mapId, x1, y1, z1, x2, y2, z2, false);
            return false;
        }

        Ray ray(start, dirVec.normalize());

        int tx = (int)(32 - (x1 / 533.33333f));
        int ty = (int)(32 - (y1 / 533.33333f));

        uint64_t tid = Pack(mapId, tx, ty);

        // Load if missing
        if (loadedTiles.find(tid) == loadedTiles.end()) {
            char buf[64];
            sprintf(buf, "%04u_%02d_%02d.vmtile", mapId, ty, tx);

            VMParser::WorldModel* m = new VMParser::WorldModel();
            if (m->readFile(basePath + buf)) {
                loadedTiles[tid] = m;
            }
            else {
                delete m;
                loadedTiles[tid] = nullptr;
            }
        }

        VMParser::WorldModel* tile = loadedTiles[tid];
        if (!tile) {
            g_Logger.LogCheck(mapId, x1, y1, z1, x2, y2, z2, false);
            return false;
        }

        // Check Collision
        bool hitDetected = false;
        int hitCount = 0;

        for (size_t i = 0; i < tile->instances.size(); ++i) {
            const auto& inst = tile->instances[i];

            // Skip obstacles far below
            if (inst.bound.hi.z < midZ - VMAP_FLIGHT_CLEARANCE) {
                continue;
            }

            if (RayAABBIntersection(ray, inst.bound, dist, (int)i)) {
                hitDetected = true;
                hitCount++;

                if (DEBUG_VMAP) {
                    char msg[512];
                    Vector3 center = inst.bound.center();
                    Vector3 size = inst.bound.size();
                    sprintf(msg, "  ├─ Collision #%d: Instance #%zu | Center=%s | Size=%.1fx%.1fx%.1f | Flags=0x%X",
                        hitCount, i, center.toString().c_str(),
                        size.x, size.y, size.z, inst.flags);
                    g_Logger.Log(msg);
                }
            }
        }

        if (hitDetected) {
            totalHits++;
            g_Logger.LogCheck(mapId, x1, y1, z1, x2, y2, z2, true);

            if (DEBUG_VMAP) {
                char msg[128];
                sprintf(msg, "  └─ Total collisions: %d obstacle(s) detected", hitCount);
                g_Logger.Log(msg);
            }
        }
        else {
            g_Logger.LogCheck(mapId, x1, y1, z1, x2, y2, z2, false);

            if (DEBUG_VMAP && tile->instances.size() > 0) {
                char msg[128];
                sprintf(msg, "  └─ No collision (checked %zu instances)", tile->instances.size());
                g_Logger.Log(msg);
            }
        }

        return hitDetected;
    }
};

VMapSystem g_Sys;

// ---------------------------------------------------------
// 4. EXPORT
// ---------------------------------------------------------
extern "C" __declspec(dllexport) bool CheckVMapLine(int mapId, float x1, float y1, float z1, float x2, float y2, float z2) {
    static bool init = false;
    if (!init) {
        g_Sys.Init("C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/vmaps/");
        init = true;
    }
    return g_Sys.Check(mapId, x1, y1, z1, x2, y2, z2);
}