#pragma once
#include "Vector.h"
#include "MovementController.h"
#include "Pathfinding.h"
#include <vector>
#include <iostream>

#include "SimpleMouseClient.h"
#include "Camera.h"
#include "Memory.h"

// --- WORLD STATE (The Knowledge) ---
struct WorldState {
    GameEntity entities;
    PlayerInfo player;

    // Pathfinding
    Vector3 targetPos;
    bool hasPath = false;
    std::vector<Vector3> currentPath;
    int pathIndex = 0;
    bool flyingPath = false;

    // Danger / Interrupts
    bool isInDanger = false; // e.g., standing in fire
    Vector3 dangerPos;       // Where the fire is

    // Looting
    Vector3 lootPos;
    bool hasLootTarget = false;
    ULONG_PTR lootGuidLow;
    ULONG_PTR lootGuidHigh;
};

// --- ABSTRACT ACTION ---
class GoapAction {
public:
    virtual ~GoapAction() {}

    // Can we run this action right now?
    virtual bool CanExecute(const WorldState& ws) = 0;

    // Execute one "tick" of the action. Returns true if action is complete.
    virtual bool Execute(WorldState& ws, MovementController& pilot) = 0;

    // Priority: Higher number = More important (e.g., Survive > Loot > Move)
    virtual int GetPriority() = 0;

    virtual std::string GetName() = 0;

    // Optional: Called when action starts or stops to reset internal state
    virtual void ResetState() {}
};

// --- CONCRETE ACTION: ESCAPE DANGER ---
class ActionEscapeDanger : public GoapAction {
public:
    bool CanExecute(const WorldState& ws) override {
        return ws.isInDanger;
    }

    int GetPriority() override { return 100; } // HIGHEST PRIORITY
    std::string GetName() override { return "Escape Danger"; }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        // Simple logic: Run directly away from danger
        Vector3 dir = ws.player.position - ws.dangerPos;
        // Normalize and pick a spot 10 yards away
        float len = dir.Length();
        if (len > 0) dir = dir / len;

        Vector3 safeSpot = ws.player.position + (dir * 10.0f);

        // FIX: Added ws.player.isFlying
        pilot.SteerTowards(ws.player.position, ws.player.rotation, safeSpot, false);

        // If we are far enough away, we consider this action "Complete" (for this tick)
        // But for continuous evasion, return false so we keep running until state changes.
        return false;
    }
};

class ActionLoot : public GoapAction {
private:
    SimpleMouseClient& mouse;
    SimpleKeyboardClient& keyboard;
    Camera& camera;
    MemoryAnalyzer& analyzer;
    DWORD procId;
    ULONG_PTR baseAddress;
    HWND hGameWindow;

    const float INTERACT_RANGE = 4.0f; // Yards

    // SEARCH PATTERN OFFSETS
    struct Point { int x, y; };
    const std::vector<POINT> searchOffsets = {
        {0, 0}, {0, -15}, {0, 15}, {-15, 0}, {15, 0},
        {-10, -10}, {10, -10}, {10, 10}, {-10, 10}
    };

    // --- STATE MACHINE VARIABLES ---
    enum LootState {
        STATE_APPROACH,     // Moving to loot point
        STATE_INIT_SCAN,    // Calculate screen pos
        STATE_ALIGN_CAMERA, // 
        STATE_MOVE_MOUSE,   // Move to next offset
        STATE_WAIT_HOVER,   // Wait for game to update GUID
        STATE_CLICK,        // Perform click
        STATE_WAIT_WINDOW,  // Wait for loot window
        STATE_RESET         // Error/Retry
    };

    LootState currentState = STATE_APPROACH;
    int currentOffsetIndex = 0;
    DWORD timerStart = 0;
    int screenX = 0, screenY = 0;

public:
    ActionLoot(SimpleMouseClient& m, SimpleKeyboardClient& k, Camera& c, MemoryAnalyzer& memRef, DWORD pid, ULONG_PTR base, HWND hGameWindow)
        : mouse(m), keyboard(k), camera(c), analyzer(memRef), procId(pid), baseAddress(base), hGameWindow(hGameWindow) {
    }

    bool CanExecute(const WorldState& ws) override {
        return ws.hasLootTarget;
    }

    int GetPriority() override { return 80; }
    std::string GetName() override { return "Loot Corpse"; }

    // Reset state if we are switching to this action fresh
    void ResetState() override {
        currentState = STATE_APPROACH;
        currentOffsetIndex = 0;
        timerStart = 0;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        if (!logFile.is_open()) {
            logFile.open("SMM_Debug.log", std::ios::app);  // fallback to current dir
        }
        float dist = ws.player.position.Dist3D(ws.lootPos);
        int sx, sy;

        switch (currentState) {

        // 1. APPROACHING
        case STATE_APPROACH: {
            float dist = ws.player.position.Dist3D(ws.lootPos);
            if (dist > INTERACT_RANGE) {
                // FIX: Added ws.player.isFlying
                pilot.SteerTowards(ws.player.position, ws.player.rotation, ws.lootPos, ws.flyingPath);
                return false; // Continue next tick
            }
            pilot.Stop();
            logFile << "[LOOT] Initializing Scan for Loot Position" << std::endl;
            currentState = STATE_INIT_SCAN;
            return false;
        }

        // 2. INITIALIZE SCAN
        case STATE_INIT_SCAN: {
            if (camera.WorldToScreen(ws.lootPos, screenX, screenY, &mouse)) {
                // OPTIONAL: Check if centered.
                // If the item is on the very edge of the screen, we might want to center it anyway
                // to make the spiral search more effective.
                int centerX = camera.GetScreenWidth() / 2;
                int centerY = camera.GetScreenHeight() / 2;
                int distFromCenter = (int)std::sqrt(std::pow(screenX - centerX, 2) + std::pow(screenY - centerY, 2));

                if (distFromCenter > 300) { // If > 300 pixels from center
                    logFile << "[LOOT] Target on edge. Centering..." << std::endl;
                    currentState = STATE_ALIGN_CAMERA;
                    return false;
                }

                currentOffsetIndex = 0;
                for (int i = 0; i < 4; ++i) {
                    keyboard.TypeKey(VK_HOME);
                    Sleep(5);
                }
                currentState = STATE_MOVE_MOUSE;
                return false;
            }
            else {
                logFile << "[LOOT] Target off-screen. Rotating..." << std::endl;
                currentState = STATE_ALIGN_CAMERA; // Retry approach/rotate
                return false;
            }
        }

        // ROTATE CAMERA WITH MOUSE
        case STATE_ALIGN_CAMERA: {
            Vector3 camPos = camera.GetPosition();
            Vector3 camFwd = camera.GetForward();

            logFile << "[LOOT] Rotate Camera With Mouse" << std::endl;

            // 1. Calculate Yaw Angles (Top-Down view)
            // atan2(y, x) gives angle from X-axis in radians
            float camYaw = std::atan2(camFwd.y, camFwd.x);

            Vector3 toLoot = ws.lootPos - camPos;
            float lootYaw = std::atan2(toLoot.y, toLoot.x);

            // 2. Calculate Difference
            float diff = lootYaw - camYaw;

            // Normalize to shortest turn (-PI to +PI)
            const float PI = 3.14159265f;
            while (diff <= -PI) diff += 2 * PI;
            while (diff > PI) diff -= 2 * PI;

            // 3. Convert Radian Delta to Mouse Pixels
            // Heuristic: ~800 pixels per radian (Adjust this based on mouse sensitivity)
            // Negative sign because dragging Mouse Left (Negative X) usually turns Camera Left (Positive Yaw)
            int pixels = (int)(diff * -800.0f);

            // 4. Clamp speed (don't snap instantly, looks robotic)
            if (pixels > 100) pixels = 100;
            if (pixels < -100) pixels = -100;

            // 5. If we are close enough, stop rotating
            if (std::abs(pixels) < 5) {
                currentState = STATE_INIT_SCAN;
                return false;
            }

            // --- FIX: RESET CURSOR TO CENTER BEFORE DRAGGING ---
            // If we don't do this, repeated small turns will eventually push the mouse off-screen.

            // 1. Calculate Center of Game Window
            RECT rect;
            GetClientRect(hGameWindow, &rect);
            POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
            ClientToScreen(hGameWindow, &center);

            // 2. Teleport Mouse to Center
            mouse.MoveAbsolute(center.x, center.y);
            Sleep(5); // Wait for update

            // 3. Perform Drag
            mouse.PressButton(MOUSE_LEFT);
            Sleep(5);
            mouse.Move(pixels, 0); // Relative move
            Sleep(5);
            mouse.ReleaseButton(MOUSE_LEFT);

            return false;
        }

        // 3. MOVE MOUSE TO OFFSET
        case STATE_MOVE_MOUSE: {
            logFile << "[LOOT] Moving the mouse to loot position" << std::endl;
            if (currentOffsetIndex >= searchOffsets.size()) {
                currentState = STATE_RESET; // Exhausted all points
                return false;
            }

            POINT p = searchOffsets[currentOffsetIndex];
            ClientToScreen(hGameWindow, &p);
            if (camera.WorldToScreen(ws.lootPos, sx, sy, &mouse))
                mouse.MoveAbsolute(sx + p.x, sy + p.y);

            // Start Timer for Hover
            timerStart = GetTickCount();
            currentState = STATE_WAIT_HOVER;
            return false;
        }

        // 4. WAIT FOR HOVER (Replaces Sleep(40))
        case STATE_WAIT_HOVER: {
            // Non-blocking wait: Check if 40ms passed
            if (GetTickCount() - timerStart < 1000) {
                return false; // Come back next tick
            }

            // Time is up, Check Memory
            ULONG_PTR currentLow = 0, currentHigh = 0;
            // Note: Verify MOUSE_OVER_GUID_OFFSET definition location in Memory.h
            analyzer.ReadPointer(procId, baseAddress + MOUSE_OVER_GUID_OFFSET, currentLow);
            analyzer.ReadPointer(procId, baseAddress + MOUSE_OVER_GUID_OFFSET + 0x8, currentHigh);

            if (currentLow == ws.lootGuidLow && currentHigh == ws.lootGuidHigh) {
                logFile << "[LOOT] Verified. Clicking." << std::endl;
                currentState = STATE_CLICK;
            }
            else {
                // Try next point
                currentOffsetIndex++;
                currentState = STATE_MOVE_MOUSE;
            }
            return false;
        }

        // 5. CLICK
        case STATE_CLICK: {
            mouse.Click(MOUSE_RIGHT);
            timerStart = GetTickCount();
            currentState = STATE_WAIT_WINDOW;
            return false;
        }

         // 6. WAIT FOR WINDOW (Replaces Sleep(50000))
        case STATE_WAIT_WINDOW: {
            // Wait 500ms (0.5s) for window. 
            // NOTE: User code had 50,000 (50s). Assuming 500ms is intended.
            if (GetTickCount() - timerStart < 50000) {
                return false;
            }

            // Action Complete
            ws.hasLootTarget = false;
            for (int i = 0; i < 4; ++i) {
                keyboard.TypeKey(VK_END);
                Sleep(5);
            }
            ResetState(); // Reset for next time
            return true;
        }

        // 7. FAILURE / RESET
        case STATE_RESET: {
            logFile << "[LOOT] Search exhausted. Re-adjusting." << std::endl;
            // Nudge player to reset camera angle
            if (dist > INTERACT_RANGE) {
                // FIX: Added ws.player.isFlying
                pilot.SteerTowards(ws.player.position, ws.player.rotation, ws.lootPos, ws.flyingPath);
            }
            ResetState(); // Restart logic from approach
            return false;
        }
        }
        return false;
    }
};

// --- CONCRETE ACTION: FOLLOW PATH ---
class ActionFollowPath : public GoapAction {
private:
    const float ACCEPTANCE_RADIUS = 3.0f;

public:
    bool CanExecute(const WorldState& ws) override {
        return ws.hasPath && !ws.currentPath.empty();
    }

    int GetPriority() override { return 50; } // STANDARD PRIORITY
    std::string GetName() override { return "Follow Path"; }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        // 1. Check if we finished the path
        if (ws.pathIndex >= ws.currentPath.size()) {
            pilot.Stop();
            ws.hasPath = false; // Clear state
            return true; // Action Complete
        }

        // 2. Get current waypoint
        Vector3 target = ws.currentPath[ws.pathIndex];

        // 3. Check distance
        float dx = target.x - ws.player.position.x;
        float dy = target.y - ws.player.position.y;
        float dz = target.z - ws.player.position.z;
        float dist = std::sqrt(dx * dx + dy * dy);
		logFile << "[GOAP] Distance to waypoint " << dist << ": " << ws.player.position.z << std::endl;

        if (dist < ACCEPTANCE_RADIUS) {
            ws.pathIndex++; // Advance state
            std::cout << "[GOAP] Waypoint " << ws.pathIndex << " Reached." << std::endl;
            return false; // Not done with action, just sub-step
        }

        // 4. Steer
        bool canFly = (ws.player.isFlying || ws.player.flyingMounted);
		logFile << "[GOAP] Steering towards waypoint " << ws.pathIndex << " at (" << target.x << ", " << target.y << ", " << target.z << ")" << std::endl;
		logFile << "Player flying status: " << canFly << std::endl;
        pilot.SteerTowards(ws.player.position, ws.player.rotation, target, ws.flyingPath);
        return false; // Still running
    }
};

// --- THE PLANNER ---
class GoapAgent {
private:
    MovementController& pilot;
    std::vector<GoapAction*> availableActions;
    GoapAction* currentAction = nullptr;

public:
    WorldState state;

    GoapAgent(MovementController& mc, SimpleMouseClient& mouse, SimpleKeyboardClient& keyboard, Camera& cam, MemoryAnalyzer& mem, DWORD pid, ULONG_PTR base, HWND hGameWindow)
        : pilot(mc) {
        // Register Actions
        availableActions.push_back(new ActionEscapeDanger());
        availableActions.push_back(new ActionLoot(mouse, keyboard, cam, mem, pid, base, hGameWindow));
        availableActions.push_back(new ActionFollowPath());
    }

    ~GoapAgent() {
        for (auto a : availableActions) delete a;
    }

    void Tick() {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        if (!logFile.is_open()) {
            logFile.open("SMM_Debug.log", std::ios::app);  // fallback to current dir
        }
        // 1. Evaluate Best Action
        GoapAction* bestAction = nullptr;
        int highestPriority = -1;

        for (auto action : availableActions) {
            if (action->CanExecute(state)) {
                int p = action->GetPriority();
                if (p > highestPriority) {
                    highestPriority = p;
                    bestAction = action;
                }
            }
        }

        // 2. Handle Action Switching
        if (bestAction != currentAction) {
            if (currentAction) {
                logFile << "[GOAP] Stopping action: " << currentAction->GetName() << std::endl;
            }
            if (bestAction) {
                logFile << "[GOAP] Starting action: " << bestAction->GetName() << std::endl;
            }
            currentAction = bestAction;
        }

        // 3. Execute
        if (currentAction) {
            bool complete = currentAction->Execute(state, pilot);
            // If action finished itself (like reaching end of path), clear current
            if (complete) {
                logFile << "[GOAP] Action Complete: " << currentAction->GetName() << std::endl;
                currentAction = nullptr;
            }
        }
        else {
            // Idle
            pilot.Stop();
        }
    }
};