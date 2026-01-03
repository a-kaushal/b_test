// VMapLoader.cpp
// Zero-Dependency VMap Reader with DEBUG LOGGING.

#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
#include <limits>
#include <fstream> // Added for logging

// --- DEBUG LOGGER ---
void LogDebug(const std::string& msg) {
    std::ofstream log("C:\\Driver\\SMM_Debug.log", std::ios::app);
    if (log.is_open()) {
        log << "[VMAP] " << msg << std::endl;
    }
}

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
};

struct AABox {
    Vector3 lo, hi;
    AABox() {}
    AABox(const Vector3& min, const Vector3& max) : lo(min), hi(max) {}
};

struct Ray {
    Vector3 origin;
    Vector3 direction;
    Ray(const Vector3& o, const Vector3& d) : origin(o), direction(d) {}

    Vector3 invDirection() const {
        // Prevent div by zero
        float x = (std::abs(direction.x) < 1e-5f) ? 1e20f : (1.0f / direction.x);
        float y = (std::abs(direction.y) < 1e-5f) ? 1e20f : (1.0f / direction.y);
        float z = (std::abs(direction.z) < 1e-5f) ? 1e20f : (1.0f / direction.z);
        return Vector3(x, y, z);
    }
};

// Standard AABB Ray Intersection
bool RayAABBIntersection(const Ray& r, const AABox& box, float maxDist) {
    Vector3 invDir = r.invDirection();

    float t1 = (box.lo.x - r.origin.x) * invDir.x;
    float t2 = (box.hi.x - r.origin.x) * invDir.x;
    float t3 = (box.lo.y - r.origin.y) * invDir.y;
    float t4 = (box.hi.y - r.origin.y) * invDir.y;
    float t5 = (box.lo.z - r.origin.z) * invDir.z;
    float t6 = (box.hi.z - r.origin.z) * invDir.z;

    float tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
    float tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));

    if (tmax < 0) return false; // Box is behind
    if (tmin > tmax) return false; // Miss
    if (tmin > maxDist) return false; // Too far

    return true;
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

        bool readFile(const std::string& filename) {
            FILE* rf = fopen(filename.c_str(), "rb");
            if (!rf) {
                return false;
            }

            // 1. Read the first 16 bytes to detect the header type
            char header[16];
            if (fread(header, 1, 16, rf) != 16) { fclose(rf); return false; }

            // 2. Check for VMAP signature
            if (strncmp(header, "VMAP", 4) != 0) {
                // LogDebug("Invalid Header: " + filename);
                fclose(rf); return false;
            }

            // 3. Handle Variations
            bool isV4 = false;

            // Check for SkyFire "VMAP_5.3f"
            if (strncmp(header, "VMAP_5.3f", 9) == 0) {
                // This header is usually padded. The snippet shows "VMAP_5.3f" + spaces.
                // We need to verify where the data actually starts. 
                // Based on your file snippet, the header is likely null/space padded to 16 or 20 bytes.
                // We've read 16 bytes. Let's peek at the next 4 to see if they look like a size int.
                // However, a safe bet for this specific format is simply to skip the text.
                // The snippet shows 'VMAP_5.3f' (9) + 8 bytes of padding = 17 bytes?
                // Let's reset and seek past the "VMAP_5.3f" string + padding.

                // REWIND and look for the specific format
                fseek(rf, 0, SEEK_SET);
                char longHeader[20];
                fread(longHeader, 1, 20, rf);

                // SkyFire usually pads to 17 or 20 bytes?
                // Let's assume standard V4 logic: Strings first.
                // The 'firstVal' after the string is the StringTableSize.

                // HACK: Scan for the first non-zero/non-space byte after "VMAP"
                // The snippet shows "A8 57" (22440) shortly after header.
                // That looks like a String Table Size.

                // We will assume the header is exactly 17 bytes (VMAP_5.3f + nulls) based on common custom formats
                // OR we can just scan for the alignment.
                fseek(rf, 17, SEEK_SET);
                isV4 = true;
            }
            else if (strncmp(header, "VMAP004", 7) == 0) {
                // Standard V4: 8 byte header
                fseek(rf, 8, SEEK_SET);
                isV4 = true;
            }
            else {
                // Standard V3: 8 byte header
                fseek(rf, 8, SEEK_SET);
                isV4 = false;
            }

            // 4. Read First Value (Count or Table Size)
            uint32_t firstVal = 0;
            if (fread(&firstVal, sizeof(uint32_t), 1, rf) != 1) { fclose(rf); return false; }

            uint32_t count = 0;

            if (isV4) {
                // firstVal is StringTableSize
                // Skip the String Table
                fseek(rf, firstVal, SEEK_CUR);

                // Read the actual Instance Count
                if (fread(&count, sizeof(uint32_t), 1, rf) != 1) { fclose(rf); return false; }
            }
            else {
                // V3: firstVal is the Instance Count
                count = firstVal;
            }

            // Safety check: If count is insanely huge (e.g. > 100,000), we misaligned the header.
            if (count > 50000) {
                // LogDebug("Read Error: Abnormal instance count " + std::to_string(count) + " in " + filename);
                fclose(rf); return false;
            }

            instances.resize(count);
            for (uint32_t i = 0; i < count; ++i) {
                ModelInstance& inst = instances[i];

                if (fread(&inst.flags, 4, 1, rf) != 1) break;
                if (fread(&inst.adtId, 4, 1, rf) != 1) break;
                if (fread(&inst.id, 4, 1, rf) != 1) break;

                fread(&inst.pos, sizeof(float), 3, rf);
                fread(inst.rot, sizeof(float), 3, rf); // V4 uses 3 floats for rotation
                float scale;
                fread(&scale, sizeof(float), 1, rf);
                inst.scale = Vector3(scale, scale, scale);

                float min[3], max[3];
                fread(min, sizeof(float), 3, rf);
                fread(max, sizeof(float), 3, rf);
                inst.bound = AABox(Vector3(min[0], min[1], min[2]), Vector3(max[0], max[1], max[2]));

                if (isV4) {
                    uint32_t nameId;
                    fread(&nameId, 4, 1, rf); // Skip name index
                }
            }

            fclose(rf);
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

    uint64_t Pack(int mapId, int x, int y) {
        return ((uint64_t)mapId << 32) | ((uint64_t)x << 16) | (uint64_t)y;
    }

public:
    void Init(const std::string& path) {
        basePath = path;
        LogDebug("Initialized with path: " + basePath);
    }

    bool Check(int mapId, float x1, float y1, float z1, float x2, float y2, float z2) {
        Vector3 start(x1, y1, z1);
        Vector3 end(x2, y2, z2);
        Vector3 dirVec = end - start;
        float dist = dirVec.length();
        if (dist < 0.001f) return false;

        Ray ray(start, dirVec.normalize());

        int tx = (int)(32 - (x1 / 533.33333f));
        int ty = (int)(32 - (y1 / 533.33333f));

        // Debug Log only if tile changes or periodically to avoid spam
        // For now, log the tile we are looking for
        // char msg[100];
        // sprintf(msg, "Checking Map %d Tile [%d, %d]", mapId, tx, ty);
        // LogDebug(msg);

        uint64_t tid = Pack(mapId, tx, ty);

        // Load if missing
        if (loadedTiles.find(tid) == loadedTiles.end()) {
            char buf[64];
            sprintf(buf, "%04u_%02d_%02d.vmtile", mapId, ty, tx);

            // Log attempt
            // LogDebug("Attempting to load: " + basePath + buf);

            VMParser::WorldModel* m = new VMParser::WorldModel();
            if (m->readFile(basePath + buf)) {
                loadedTiles[tid] = m;
            }
            else {
                delete m;
                loadedTiles[tid] = nullptr;
                LogDebug("Tile NOT FOUND or Load Failed: " + basePath + buf);
            }
        }

        VMParser::WorldModel* tile = loadedTiles[tid];
        if (!tile) return false;

        // Check Collision
        for (const auto& inst : tile->instances) {
            if (RayAABBIntersection(ray, inst.bound, dist)) {
                // LogDebug("COLLISION DETECTED with WMO ID: " + std::to_string(inst.id));
                return true;
            }
        }

        return false;
    }
};

VMapSystem g_Sys;

// ---------------------------------------------------------
// 4. EXPORT
// ---------------------------------------------------------
extern "C" __declspec(dllexport) bool CheckVMapLine(int mapId, float x1, float y1, float z1, float x2, float y2, float z2) {
    static bool init = false;
    if (!init) {
        // HARDCODED PATH - Update this!
        g_Sys.Init("C:/Users/A/Downloads/SkyFire Repack WoW MOP 5.4.8/data/vmaps/");
        init = true;
    }
    return g_Sys.Check(mapId, x1, y1, z1, x2, y2, z2);
}