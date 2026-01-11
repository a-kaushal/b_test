#pragma once
#include "MemoryRead.h"
#include "Vector.h"
#include "SimpleMouseClient.h"
#include "Logger.h"

class Camera {
private:
    MemoryAnalyzer& mem;
    SimpleMouseClient& mouse;
    DWORD procId;
    ULONG_PTR cameraMgr = 0;
    ULONG_PTR cameraPtr = 0;
    int screenW, screenH;
    
    // --- NEW: Cache Camera State ---
    Vector3 camPos;
    Vector3 camForward;
    Vector3 camRight;
    Vector3 camUp;

    // Hardcoded FOV from settings
    // Note: WoW "90" usually refers to Horizontal FOV on 4:3 screens, 
    // but in modern engines it's often Vertical. We'll start with 90 Vertical.
    // If points are "zoomed in" too much, try converting Horizontal -> Vertical.
    const float FOV_DEGREES = 90.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;

public:
    Camera(MemoryAnalyzer& m, SimpleMouseClient& mouse, DWORD pid) : mem(m), procId(pid), mouse(mouse) {
        screenW = GetSystemMetrics(SM_CXSCREEN);
        screenH = GetSystemMetrics(SM_CYSCREEN);
    }

    bool Update(ULONG_PTR baseAddress) {

        // 1. Get CameraPtr (Static Offset)
        if (cameraPtr == 0) {
            mem.ReadPointer(procId, baseAddress + 0x3DEFB68, cameraMgr);
            mem.ReadPointer(procId, cameraMgr + 0x488, cameraPtr);
        }

        // Camera world position
        mem.ReadFloat(procId, cameraPtr + CAMERA_POSITION_X, camPos.x);
        mem.ReadFloat(procId, cameraPtr + CAMERA_POSITION_Y, camPos.y);
        mem.ReadFloat(procId, cameraPtr + CAMERA_POSITION_Z, camPos.z);

        // Reading Camera Matrix Rows directly
        // NOTE: WoW matrices are often stored [X,Y,Z] for the axis vectors.
        // Let's read them as 3 vectors.
        // Forward Vector (x = 1 when facing north, y = 1 when facing west, z = 1 when facing up and z = -1 when facing down)
        mem.ReadFloat(procId, cameraPtr + CAMERA_PROJECTION_MATRIX_FORWARD_X, camForward.x);
        mem.ReadFloat(procId, cameraPtr + CAMERA_PROJECTION_MATRIX_FORWARD_Y, camForward.y);
        mem.ReadFloat(procId, cameraPtr + CAMERA_PROJECTION_MATRIX_FORWARD_Z, camForward.z);

        // Right Vector (Dot product with forward should be ~0) (Need to double check may be swapped with right) 
        mem.ReadFloat(procId, cameraPtr + CAMERA_PROJECTION_MATRIX_RIGHT_X, camRight.x);
        mem.ReadFloat(procId, cameraPtr + CAMERA_PROJECTION_MATRIX_RIGHT_Y, camRight.y);
        mem.ReadFloat(procId, cameraPtr + CAMERA_PROJECTION_MATRIX_RIGHT_Z, camRight.z);

        // Up Vector (Dot product with forward should be ~0, and z is always 0) (Need to double check may be swapped with right)    
        mem.ReadFloat(procId, cameraPtr + CAMERA_PROJECTION_MATRIX_UP_X, camUp.x);
        mem.ReadFloat(procId, cameraPtr + CAMERA_PROJECTION_MATRIX_UP_Y, camUp.y);
        mem.ReadFloat(procId, cameraPtr + CAMERA_PROJECTION_MATRIX_UP_Z, camUp.z);

		//g_LogFile << "[CAMERA] Position: (" << camPos.x << ", " << camPos.y << ", " << camPos.z << ")" << std::endl;

        return (cameraPtr != 0);
    }

    // Add a method to update screen size from the actual window
    void UpdateScreenSize(int width, int height) {
        if (width > 0 && height > 0) {
            screenW = width;
            screenH = height;
        }
    }
    
    // --- Getters for ActionLoot ---
    Vector3 GetPosition() {
        Update(baseAddress);
        return camPos; 
    }
    Vector3 GetForward() {
        Update(baseAddress);
        return camForward;
    }
    int GetScreenWidth() const { return screenW; }
    int GetScreenHeight() const { return screenH; }

    bool WorldToScreen(Vector3 targetPos, int& outX, int& outY) {
        Update(baseAddress);
        if (!cameraPtr) return false;

        // Construct View Matrix
        // Since we have the raw vectors, we can build the matrix directly 
        // without guessing Euler angles.
        // We use LookAtRH: Eye, Target(Eye+Fwd), Up
        Matrix4x4 view = MatrixLookAtRH(camPos, camPos + camForward, camUp);

        // Construct Projection Matrix
        float aspect = (float)screenW / (float)screenH;

        // Convert FOV to Radians
        // If "90" is Horizontal FOV, we must convert to Vertical for the math function
        // VFOV = 2 * atan( tan(HFOV/2) / aspect )
        // Let's assume 90 is Horizontal (common for gaming config)
        float hFovRad = FOV_DEGREES * 3.14159f / 180.0f;
        float vFovRad = 2.0f * std::atan(std::tan(hFovRad / 2.0f) / aspect);

        // If dots are too far apart, try using just 'hFovRad' directly here.
        Matrix4x4 proj = MatrixPerspectiveFovRH(vFovRad, aspect, 0.2f, 2000.0f);

        // 5. Combine & Project
        Matrix4x4 viewProj = view * proj;

        // Manual Multiplication (Target * Matrix)
        float clipX = targetPos.x * viewProj.m[0][0] + targetPos.y * viewProj.m[1][0] + targetPos.z * viewProj.m[2][0] + viewProj.m[3][0];
        float clipY = targetPos.x * viewProj.m[0][1] + targetPos.y * viewProj.m[1][1] + targetPos.z * viewProj.m[2][1] + viewProj.m[3][1];
        float clipZ = targetPos.x * viewProj.m[0][2] + targetPos.y * viewProj.m[1][2] + targetPos.z * viewProj.m[2][2] + viewProj.m[3][2];
        float clipW = targetPos.x * viewProj.m[0][3] + targetPos.y * viewProj.m[1][3] + targetPos.z * viewProj.m[2][3] + viewProj.m[3][3];

        if (clipW < 0.001f) return false; // Behind camera

        float ndcX = clipX / clipW;
        float ndcY = clipY / clipW;

        if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f) return false; // Off screen

        outX = (int)((ndcX + 1.0f) * 0.5f * screenW);
        outY = (int)((1.0f - ndcY) * 0.5f * screenH);

        return true;
    }
};