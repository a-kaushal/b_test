#pragma once
#include <cmath>
#include <iostream>
#include <limits>
#include <fstream>

struct Vector3 {
    float x, y, z;

    // Constructors
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    // 1. Vector Subtraction (eye - at)
    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);
    }

    // 2. Vector Addition
    Vector3 operator+(const Vector3& other) const {
        return Vector3(x + other.x, y + other.y, z + other.z);
    }

    // 3. Scalar Multiplication
    Vector3 operator*(float scalar) const {
        return Vector3(x * scalar, y * scalar, z * scalar);
    }

    // 4. Scalar Division
    Vector3 operator/(float scalar) const {
        return Vector3(x / scalar, y / scalar, z / scalar);
    }
    
    // --- NEW: Equality Operators
    bool operator==(const Vector3& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    bool operator!=(const Vector3& other) const {
        return !(*this == other);
    }

    // --- REQUIRED MATH FUNCTIONS ---

    // 5. Length (Magnitude)
    float Length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    // 6. Normalize: Returns a unit vector (length 1.0)
    // Usage: (eye - at).Normalize()
    Vector3 Normalize() const {
        float len = Length();
        if (len > 0.0001f) {
            float invLen = 1.0f / len;
            return Vector3(x * invLen, y * invLen, z * invLen);
        }
        return *this; // Return zero vector if length is 0
    }

    // 7. Dot Product: Returns scalar projection
    // Usage: xaxis.Dot(eye)
    float Dot(const Vector3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    // 8. Cross Product: Returns perpendicular vector
    // Usage: up.Cross(zaxis)
    Vector3 Cross(const Vector3& other) const {
        return Vector3(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }

    // Distance Helper (for your Pathfinding)
    float Dist3D(const Vector3& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        float dz = z - other.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // Distance Helper (for your Pathfinding)
    float Dist2D(const Vector3& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Sphere Scan (checks if 'other' is inside a sphere of 'radius' centered at this vector)
    bool SphereSearch(float radius, const Vector3& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        float dz = z - other.z;

        float distSqr = dx * dx + dy * dy + dz * dz;

        // Optimization: Compare against squared radius to avoid expensive std::sqrt()
        return distSqr <= (radius * radius);
    }
};

// --- MATRIX STRUCT (Keep this if you haven't added it yet) ---
struct Matrix4x4 {
    float m[4][4];

    static Matrix4x4 Identity() {
        Matrix4x4 res = { 0 };
        res.m[0][0] = 1; res.m[1][1] = 1; res.m[2][2] = 1; res.m[3][3] = 1;
        return res;
    }

    Matrix4x4 operator*(const Matrix4x4& other) const {
        Matrix4x4 res = { 0 };
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                for (int k = 0; k < 4; ++k) {
                    res.m[r][c] += m[r][k] * other.m[k][c];
                }
            }
        }
        return res;
    }
};

enum PathType {
    PATH_GROUND = 0,
    PATH_AIR = 1
};

struct PathNode {
    Vector3 pos;
    int type; // PathType (0 = Ground, 1 = Air)

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

// --- MATRIX HELPERS ---

inline Matrix4x4 MatrixLookAtRH(Vector3 eye, Vector3 at, Vector3 up) {
    Vector3 zaxis = (eye - at).Normalize();
    Vector3 xaxis = up.Cross(zaxis).Normalize();
    Vector3 yaxis = zaxis.Cross(xaxis);

    Matrix4x4 res = Matrix4x4::Identity();
    res.m[0][0] = xaxis.x; res.m[0][1] = yaxis.x; res.m[0][2] = zaxis.x; res.m[0][3] = 0;
    res.m[1][0] = xaxis.y; res.m[1][1] = yaxis.y; res.m[1][2] = zaxis.y; res.m[1][3] = 0;
    res.m[2][0] = xaxis.z; res.m[2][1] = yaxis.z; res.m[2][2] = zaxis.z; res.m[2][3] = 0;
    res.m[3][0] = -xaxis.Dot(eye);
    res.m[3][1] = -yaxis.Dot(eye);
    res.m[3][2] = -zaxis.Dot(eye);
    res.m[3][3] = 1;
    return res;
}

inline Matrix4x4 MatrixPerspectiveFovRH(float fovY, float aspect, float zn, float zf) {
    Matrix4x4 res = { 0 };
    float yScale = 1.0f / std::tan(fovY / 2.0f);
    float xScale = yScale / aspect;

    res.m[0][0] = xScale;
    res.m[1][1] = yScale;
    res.m[2][2] = zf / (zn - zf);
    res.m[2][3] = -1.0f;
    res.m[3][2] = zn * zf / (zn - zf);
    return res;
}

// Finds the closest waypoint in a path to the input position
inline int FindClosestWaypoint(std::vector<Vector3>& path, std::vector<PathNode>& pathNode, Vector3& position) {
    std::ofstream log("C:\\SMM\\SMM_Debug.log", std::ios::app);
    if (path.empty() && pathNode.empty()) return -1;

    int closestIndex = 0;
    float minDistance = (std::numeric_limits<float>::max)();

    if (path.size() > 0) {
        for (size_t i = 0; i < (path.size() - 1); ++i) {
            float dist = position.Dist3D(path[i]);
            //log << "Distance : " << dist << " | minDistance: " << minDistance << " | closestIndex: " << closestIndex << std::endl;
            if (dist < minDistance) {
                minDistance = dist;
                closestIndex = (int)i;
            }
        }
    }
    else if (pathNode.size() > 0) {
        for (size_t i = 0; i < (pathNode.size() - 1); ++i) {
            float dist = position.Dist3D(pathNode[i].pos);
            //log << "Distance : " << dist << " | minDistance: " << minDistance << " | closestIndex: " << closestIndex << std::endl;
            if (dist < minDistance) {
                minDistance = dist;
                closestIndex = (int)i;
            }
        }
    }
    log.close();

    return closestIndex;
}