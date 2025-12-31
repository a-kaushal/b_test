#pragma once
#include "Vector.h"
#include "MovementController.h"
#include "Pathfinding2.h"
#include <vector>
#include <iostream>

#include "SimpleMouseClient.h"
#include "Camera.h"
#include "Memory.h"
#include "Profile.h"
#include "SimpleKeyboardClient.h"

// This allows us to return a pointer to ANY state type
struct ActionState {
    virtual ~ActionState() = default; // Virtual destructor ensures proper cleanup

    // Common variables moved here
    std::vector<Vector3> activePath;
    int activeIndex = 0;
    bool actionChange = 0;
    bool flyingPath = true;
    int bagFreeSlots;
};

struct GlobalState : public ActionState {
};

struct Looting : public ActionState {
    // Looting
    bool enabled = false;
    Vector3 position;
    std::vector<Vector3> path;
    int index;
    bool hasLoot = false;
    ULONG_PTR guidLow;
    ULONG_PTR guidHigh;
};

struct Gathering : public ActionState {
    // Gathering (Nodes/Herbs)
    bool enabled = true;
    Vector3 position;
    std::vector<Vector3> path;
    bool hasNode = false;
    bool nodeActive = false;
    int index;
    ULONG_PTR guidLow;
    ULONG_PTR guidHigh;
};

struct PathFollowing : public ActionState {
    // Follow a set path
    bool enabled = true;
    std::vector<Vector3> path;
    std::vector<Vector3> presetPath;
    int index;
    int presetIndex;
    bool looping = true; // If true the path repeats from the beginning when it finishes
    bool startNearest = true; // If true starts at the closest waypoint to player
    bool hasPath = false;
    bool pathIndexChange = false;
};

struct WaypointReturn : public ActionState {
    // Follow a set path
    bool enabled = false;
    std::vector<Vector3> path = { { -1000.0f,-1000.0f, -1000.0f } };
    std::vector<Vector3> savedPath = { { -1000.0f,-1000.0f, -1000.0f } };
    int index;
    int savedIndex;
    bool hasPath = false;
};

struct Combat : public ActionState {
    bool enabled = false;
    std::vector<Vector3> path;
    bool inCombat;
    bool underAttack;
    Vector3 enemyPosition;
    ULONG_PTR targetGuidLow;
    ULONG_PTR targetGuidHigh;
};

// --- WORLD STATE (The Knowledge) ---
struct WorldState {
    GlobalState globalState;
    Looting lootState;
    Gathering gatherState;
    PathFollowing pathFollowState;
    WaypointReturn waypointReturnState;

    std::vector<GameEntity> entities;
    PlayerInfo player;

    // Danger / Interrupts
    bool isInDanger = false; // e.g., standing in fire
    Vector3 dangerPos;       // Where the fire is    
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

    // Returns a pointer to the specific state in WorldState used by this action
    virtual ActionState* GetState(WorldState& ws) = 0;

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

    int GetPriority() override { return 500; } // HIGHEST PRIORITY
    std::string GetName() override { return "Escape Danger"; }

    // Escape Danger doesn't have a persistent state struct, return nullptr
    ActionState* GetState(WorldState& ws) override { return nullptr; }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        // Simple logic: Run directly away from danger
        Vector3 dir = ws.player.position - ws.dangerPos;
        // Normalize and pick a spot 10 yards away
        float len = dir.Length();
        if (len > 0) dir = dir / len;

        Vector3 safeSpot = ws.player.position + (dir * 10.0f);

        // FIX: Added ws.player.isFlying
        pilot.SteerTowards(ws.player.position, ws.player.rotation, safeSpot, false, ws.player);

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
    ConsoleInput inputCommand;

    const float INTERACT_RANGE = 4.0f; // Yards
    const float ACCEPTANCE_RADIUS = 3.0f;

    // SEARCH PATTERN OFFSETS
    struct Point { int x, y; };
    const std::vector<POINT> searchOffsets = {
        {0, 0}, {0, -15}, {0, 15}, {-15, 0}, {15, 0},
        {-10, -10}, {10, -10}, {10, 10}, {-10, 10}
    };

    // --- STATE MACHINE VARIABLES ---
    enum LootState {
        STATE_CREATE_PATH, // Create the path to goal
        STATE_APPROACH,     // Moving to loot point
        STATE_INIT_SCAN,    // Calculate screen pos
        STATE_ALIGN_CAMERA, // 
        STATE_MOVE_MOUSE,   // Move to next offset
        STATE_WAIT_HOVER,   // Wait for game to update GUID
        STATE_CLICK,        // Perform click
        STATE_WAIT_WINDOW,  // Wait for loot window
        STATE_RESET         // Error/Retry
    };

    LootState currentState = STATE_CREATE_PATH;
    int currentOffsetIndex = 0;
    DWORD timerStart = 0;
    int screenX = 0, screenY = 0;

public:
    ActionLoot(SimpleMouseClient& m, SimpleKeyboardClient& k, Camera& c, MemoryAnalyzer& memRef, DWORD pid, ULONG_PTR base, HWND hGameWindow)
        : mouse(m), keyboard(k), camera(c), analyzer(memRef), procId(pid), baseAddress(base), hGameWindow(hGameWindow), inputCommand(k) {
    }

    bool CanExecute(const WorldState& ws) override {
        return ws.lootState.hasLoot;
    }

    ActionState* GetState(WorldState& ws) override {
        return &ws.lootState;
    }

    int GetPriority() override { return 60; }
    std::string GetName() override { return "Loot Corpse"; }

    // Reset state if we are switching to this action fresh
    void ResetState() override {
        currentState = STATE_CREATE_PATH;
        currentOffsetIndex = 0;
        timerStart = 0;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        ws.globalState.activePath = ws.lootState.path;
        ws.globalState.activeIndex = ws.lootState.index;
        if (!logFile.is_open()) {
            logFile.open("SMM_Debug.log", std::ios::app);  // fallback to current dir
        }
        float dist = ws.player.position.Dist3D(ws.lootState.position);
        int sx, sy;

        switch (currentState) {

        case STATE_CREATE_PATH: {
            ws.gatherState.path = CalculatePath({ ws.gatherState.position }, ws.player.position, 0, true, 530);
            ws.gatherState.index = 0;
            currentState = STATE_APPROACH;
            return false;
        }

        // 1. APPROACHING
        case STATE_APPROACH: {
            if (ws.lootState.index >= ws.lootState.path.size()) {
                pilot.Stop();
                if ((ws.player.flyingMounted == true) || (ws.player.groundMounted == true)) {
                    if (timerStart != 0) {
                        if (GetTickCount() - timerStart > 1000) {
                            keyboard.SendKey('X', 0, false);  // Descend
                            if (GetTickCount() - timerStart > 1200) {
                                if (inputCommand.SendData(std::wstring(L"run if IsMounted() then Dismount()end"))) {
                                    currentState = STATE_INIT_SCAN;;
                                }
                            }
                        }
                        return false;
                    }
                    if (inputCommand.GetState() == "OPEN_CHAT_WINDOW") {
                        if (timerStart == 0) {
                            timerStart = GetTickCount();
                        }
                        if (GetTickCount() - timerStart < 1000) {
                            keyboard.SendKey('X', 0, true);  // Descend
                        }
                        return false;
                    }
                }
                if ((ws.player.flyingMounted == false) && (ws.player.groundMounted == false)) {
                    inputCommand.Reset();
                }
            }

            // 2. Get current waypoint
            Vector3 target = ws.lootState.path[ws.lootState.index];

            // 3. Check distance
            float dx = target.x - ws.player.position.x;
            float dy = target.y - ws.player.position.y;
            float dz = target.z - ws.player.position.z;
            if (ws.lootState.flyingPath == true) {
                float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            else {
                float dist = std::sqrt(dx * dx + dy * dy);
            }

            if (dist < ACCEPTANCE_RADIUS) {
                ws.lootState.index++; // Advance state
                std::cout << "[GOAP] Waypoint " << ws.lootState.index << " Reached." << std::endl;
                return false; // Not done with action, just sub-step
            }


            bool canFly = (ws.player.isFlying || ws.player.flyingMounted);
            pilot.SteerTowards(ws.player.position, ws.player.rotation, target, ws.lootState.flyingPath, ws.player);
            return false;
        }

        // 2. INITIALIZE SCAN
        case STATE_INIT_SCAN: {
            if (camera.WorldToScreen(ws.lootState.position, screenX, screenY, &mouse)) {
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

            Vector3 toLoot = ws.lootState.position - camPos;
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
            if (camera.WorldToScreen(ws.lootState.position, sx, sy, &mouse))
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

            if (currentLow == ws.lootState.guidLow && currentHigh == ws.lootState.guidHigh) {
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
            ws.lootState.hasLoot = false;
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
            ResetState(); // Restart logic from approach
            return false;
        }
        }
        return false;
    }
};

// --- CONCRETE ACTION: GATHER NODE (NEW) ---
class ActionGather : public GoapAction {
private:
    SimpleMouseClient& mouse;
    SimpleKeyboardClient& keyboard;
    Camera& camera;
    MemoryAnalyzer& analyzer;
    DWORD procId;
    ULONG_PTR baseAddress;
    HWND hGameWindow;
    ConsoleInput inputCommand;

    const float ACCEPTANCE_RADIUS = 3.0f;
    const std::vector<POINT> searchOffsets = {
        {0, 0}, {0, -10}, {0, 10}, {-10, 0}, {10, 0},
        {-20, -20}, {20, -20}, {20, 20}, {-20, 20}
    };

    enum GatherState {
        STATE_CREATE_PATH, STATE_APPROACH, STATE_INIT_SCAN, STATE_ALIGN_CAMERA,
        STATE_MOVE_MOUSE, STATE_WAIT_HOVER, STATE_CLICK,
        STATE_WAIT_CAST, STATE_WAIT_LOOT, STATE_FINISH, STATE_RESET
    };

    GatherState currentState = STATE_CREATE_PATH;
    int currentOffsetIndex = 0;
    DWORD timerStart = 0;
    int screenX = 0, screenY = 0;

public:
    ActionGather(SimpleMouseClient& m, SimpleKeyboardClient& k, Camera& c, MemoryAnalyzer& memRef, DWORD pid, ULONG_PTR base, HWND hGameWindow)
        : mouse(m), keyboard(k), camera(c), analyzer(memRef), procId(pid), baseAddress(base), hGameWindow(hGameWindow), inputCommand(k) {
    }

    bool CanExecute(const WorldState& ws) override {
        return ws.gatherState.hasNode;
    }

    ActionState* GetState(WorldState& ws) override {
        return &ws.gatherState;
    }

    // Priority 70: Higher than Pathing (50), Higher than Looting (60)
    int GetPriority() override { return 100; }
    std::string GetName() override { return "Gather Node"; }

    void ResetState() override {
        currentState = STATE_CREATE_PATH;
        currentOffsetIndex = 0;
        timerStart = 0;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        ws.globalState.activePath = ws.gatherState.path;
        ws.globalState.activeIndex = ws.gatherState.index;
        int sx, sy;

        if (ws.gatherState.nodeActive == false) {
            currentState = STATE_FINISH;
        }

        switch (currentState) {
        case STATE_CREATE_PATH: {
            ws.gatherState.path = CalculatePath({ ws.gatherState.position }, ws.player.position, 0, true, 530);
            for (const auto& point : ws.gatherState.path) {
                logFile << "Gather Path: " << point.x << ", " << point.y << ", " << point.z << std::endl;
            }
            ws.gatherState.index = 0;
            currentState = STATE_APPROACH;
            return false;
        }

        case STATE_APPROACH: {
            if (ws.gatherState.index >= ws.gatherState.path.size()) {
                pilot.Stop();
                if ((ws.player.flyingMounted == true) || (ws.player.groundMounted == true)) {
                    if (timerStart != 0) {
                        if (GetTickCount() - timerStart > 1000) {
                            keyboard.SendKey('X', 0, false);  // Descend
                            if (GetTickCount() - timerStart > 1200) {
                                if (inputCommand.SendData(std::wstring(L"run if IsMounted() then Dismount()end"))) {
                                    inputCommand.Reset();
                                    currentState = STATE_INIT_SCAN;
                                }
                            }
                        }
                        return false;
                    }
                    else if (inputCommand.GetState() == "OPEN_CHAT_WINDOW") {
                        if (timerStart == 0) {
                            timerStart = GetTickCount();
                        }
                        if (GetTickCount() - timerStart < 1000) {
                            keyboard.SendKey('X', 0, true);  // Descend
                        }
                        return false;
                    }
                }
                if ((ws.player.flyingMounted == false) && (ws.player.groundMounted == false)) {
                    inputCommand.Reset();
                }
                return false;
            }

            // 2. Get current waypoint
            Vector3 target = ws.gatherState.path[ws.gatherState.index];

            // 3. Check distance
            float dx = target.x - ws.player.position.x;
            float dy = target.y - ws.player.position.y;
            float dz = target.z - ws.player.position.z;
            float dist;
            if (ws.gatherState.flyingPath == true) {
                dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            else {
                dist = std::sqrt(dx * dx + dy * dy);
            }
            logFile << "Distance to waypoint: " << ws.gatherState.flyingPath << "  waypoint: " << target.x << " " << target.y << " " << target.z << " " << std::endl;
            logFile << "Player Pos: " << dx*dx << " " << dy*dy << " " << dz*dz << " " << std::endl;

            if (dist < ACCEPTANCE_RADIUS) {
                ws.gatherState.index++; // Advance state
                std::cout << "[GOAP] Waypoint " << ws.gatherState.index << " Reached." << std::endl;
                return false; // Not done with action, just sub-step
            }

            bool canFly = (ws.player.isFlying || ws.player.flyingMounted);
            pilot.SteerTowards(ws.player.position, ws.player.rotation, target, ws.gatherState.flyingPath, ws.player);
            return false;
        }

        case STATE_INIT_SCAN: {
            if (GetTickCount() - timerStart < 1500) {
                return false;
            }
            if (camera.WorldToScreen(ws.gatherState.position, screenX, screenY, &mouse)) {
                // Align if too far from center
                int centerX = camera.GetScreenWidth() / 2;
                int centerY = camera.GetScreenHeight() / 2;
                int distFromCenter = (int)std::sqrt(std::pow(screenX - centerX, 2) + std::pow(screenY - centerY, 2));

                if (distFromCenter > 350) {
                    currentState = STATE_ALIGN_CAMERA;
                    return false;
                }
                currentOffsetIndex = 0;
                currentState = STATE_MOVE_MOUSE;
                return false;
            }
            currentState = STATE_ALIGN_CAMERA;
            return false;
        }

        case STATE_ALIGN_CAMERA: {
            // Reuse logic from Loot: Rotate camera to face node
            Vector3 camFwd = camera.GetForward();
            float camYaw = std::atan2(camFwd.y, camFwd.x);
            Vector3 toNode = ws.gatherState.position - camera.GetPosition();
            float nodeYaw = std::atan2(toNode.y, toNode.x);
            float diff = nodeYaw - camYaw;

            const float PI = 3.14159265f;
            while (diff <= -PI) diff += 2 * PI;
            while (diff > PI) diff -= 2 * PI;

            int pixels = (int)(diff * -800.0f);
            if (pixels > 100) pixels = 100;
            if (pixels < -100) pixels = -100;

            if (std::abs(pixels) < 5) {
                currentState = STATE_INIT_SCAN;
                return false;
            }

            RECT rect;
            GetClientRect(hGameWindow, &rect);
            POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
            ClientToScreen(hGameWindow, &center);
            mouse.MoveAbsolute(center.x, center.y);
            Sleep(5);
            mouse.PressButton(MOUSE_LEFT);
            mouse.Move(pixels, 0);
            mouse.ReleaseButton(MOUSE_LEFT);
            return false;
        }

        case STATE_MOVE_MOUSE: {
            if (currentOffsetIndex >= searchOffsets.size()) {
                currentState = STATE_RESET;
                return false;
            }
            POINT p = searchOffsets[currentOffsetIndex];
            ClientToScreen(hGameWindow, &p);
            if (camera.WorldToScreen(ws.gatherState.position, sx, sy, &mouse))
                mouse.MoveAbsolute(sx + p.x, sy + p.y);

            timerStart = GetTickCount();
            currentState = STATE_WAIT_HOVER;
            return false;
        }

        case STATE_WAIT_HOVER: {
            if (GetTickCount() - timerStart < 100) return false;

            ULONG_PTR currentLow = 0, currentHigh = 0;
            analyzer.ReadPointer(procId, baseAddress + MOUSE_OVER_GUID_OFFSET, currentLow);
            analyzer.ReadPointer(procId, baseAddress + MOUSE_OVER_GUID_OFFSET + 0x8, currentHigh);

            if (currentLow == ws.gatherState.guidLow && currentHigh == ws.gatherState.guidHigh) {
                logFile << "[GATHER] Node verified. Clicking." << std::endl;
                currentState = STATE_CLICK;
            }
            else {
                currentOffsetIndex++;
                currentState = STATE_MOVE_MOUSE;
            }
            return false;
        }

        case STATE_CLICK: {
            mouse.Click(MOUSE_RIGHT);
            timerStart = GetTickCount();
            currentState = STATE_WAIT_CAST;
            return false;
        }

        case STATE_WAIT_CAST: {
            // Wait ~3.5s for mining/herb cast
            if (GetTickCount() - timerStart < 3500) return false;
            currentState = STATE_WAIT_LOOT;
            return false;
        }

        case STATE_WAIT_LOOT: {
            // Wait 1s for loot window interaction (auto-loot)
            if (GetTickCount() - timerStart < 4500) return false;
            currentState = STATE_FINISH;
            return false;
        }

        case STATE_FINISH: {
            for (int i = 0; i < 3; ++i) { keyboard.TypeKey(VK_END); Sleep(5); } // Reset Camera
            ResetState();
            ws.gatherState.hasNode = false;
            ws.gatherState.nodeActive = false;
            ws.gatherState.path.clear();
            return true;
        }

        case STATE_RESET: {
            ResetState();
            return false;
        }
        }
        return false;
    }
};

// --- Return to a saved waypoint after performing a different action --- //
class ActionReturnWaypoint : public GoapAction {
private:
    const float ACCEPTANCE_RADIUS = 3.0f;

public:
    bool CanExecute(const WorldState& ws) override {
        return ws.waypointReturnState.enabled;
    }

    int GetPriority() override { return 70; } // STANDARD PRIORITY
    std::string GetName() override { return "Return to previous waypoint"; }

    ActionState* GetState(WorldState& ws) override {
        return &ws.waypointReturnState;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        if (ws.waypointReturnState.hasPath == false) {
            ws.waypointReturnState.path = CalculatePath({ ws.waypointReturnState.savedPath[ws.waypointReturnState.savedIndex] }, ws.player.position, 0, ws.globalState.flyingPath, 530, false);
            ws.waypointReturnState.hasPath = true;
            ws.waypointReturnState.index = 0;
        }

        ws.globalState.activePath = ws.waypointReturnState.path;
        ws.globalState.activeIndex = ws.waypointReturnState.index;

        // 1. Check if we finished the path
        if (ws.waypointReturnState.index >= ws.waypointReturnState.path.size()) {
            pilot.Stop();
            ws.waypointReturnState.path = { { -1000.0f,-1000.0f, -1000.0f } };
            ws.waypointReturnState.savedPath = { { -1000.0f,-1000.0f, -1000.0f } };
            ws.waypointReturnState.enabled = false;
            ws.waypointReturnState.hasPath = false;
            return true; // Action Complete
        }
        // 2. Get current waypoint
        Vector3 target = ws.waypointReturnState.path[ws.waypointReturnState.index];
        float dist;
        // 3. Check distance
        float dx = target.x - ws.player.position.x;
        float dy = target.y - ws.player.position.y;
        float dz = target.z - ws.player.position.z;
        if (ws.waypointReturnState.flyingPath == true) {
            dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        else {
            dist = std::sqrt(dx * dx + dy * dy);
        }

        if (dist < ACCEPTANCE_RADIUS) {
            ws.waypointReturnState.index++; // Advance state
            std::cout << "[GOAP] Waypoint " << ws.waypointReturnState.index << " Reached." << std::endl;
            return false; // Not done with action, just sub-step
        }

        // 4. Steer
        bool canFly = (ws.player.isFlying || ws.player.flyingMounted);
        pilot.SteerTowards(ws.player.position, ws.player.rotation, target, ws.waypointReturnState.flyingPath, ws.player);
        return false; // Still running
    }
};

// --- CONCRETE ACTION: FOLLOW PATH ---
class ActionFollowPath : public GoapAction {
private:
    const float ACCEPTANCE_RADIUS = 3.0f;

public:
    bool CanExecute(const WorldState& ws) override {
        return ws.pathFollowState.enabled;
    }

    int GetPriority() override { return 50; } // STANDARD PRIORITY
    std::string GetName() override { return "Follow Path"; }

    ActionState* GetState(WorldState& ws) override {
        return &ws.pathFollowState;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);

        if (ws.globalState.actionChange == true) {
            ws.globalState.actionChange = false;
        }

        ws.globalState.activePath = ws.pathFollowState.path;
        ws.globalState.activeIndex = ws.pathFollowState.index;
        // 1. Check if we finished the path
        if (ws.pathFollowState.index >= ws.pathFollowState.path.size()) {
            pilot.Stop();
            ws.pathFollowState.hasPath = false; // Clear state
            ws.pathFollowState.path.clear();
            return true; // Action Complete
        }

        // 2. Get current waypoint
        Vector3 target = ws.pathFollowState.path[ws.pathFollowState.index];
        float dist;
        // 3. Check distance
        float dx = target.x - ws.player.position.x;
        float dy = target.y - ws.player.position.y;
        float dz = target.z - ws.player.position.z;
        if (ws.pathFollowState.flyingPath == true) {
            dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        else {
            dist = std::sqrt(dx * dx + dy * dy);
        }

        if (dist < ACCEPTANCE_RADIUS) {
            ws.pathFollowState.index++; // Advance state
            ws.pathFollowState.pathIndexChange = true;
            std::cout << "[GOAP] Waypoint " << ws.pathFollowState.index << " Reached." << std::endl;
            return false; // Not done with action, just sub-step
        }

        // 4. Steer
        bool canFly = (ws.player.isFlying || ws.player.flyingMounted);
        pilot.SteerTowards(ws.player.position, ws.player.rotation, target, ws.pathFollowState.flyingPath, ws.player);
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
        availableActions.push_back(new ActionReturnWaypoint());
        availableActions.push_back(new ActionGather(mouse, keyboard, cam, mem, pid, base, hGameWindow));
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
                state.waypointReturnState.savedPath = GetCurrentActionState()->activePath;
                state.waypointReturnState.savedIndex = GetCurrentActionState()->activeIndex;
                logFile << "[GOAP] Stopping action: " << currentAction->GetName() << std::endl;
                pilot.Stop();
            }
            if (bestAction) {
                // Return to saved waypoint if it exists
                Vector3 temp = { -1000.0f,-1000.0f, -1000.0f };
                if (state.waypointReturnState.savedPath[0] != temp) {
                    state.waypointReturnState.enabled = true;
                }
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
    
    // Helper to get current running state
    ActionState* GetCurrentActionState() {
        if (currentAction) return currentAction->GetState(state);
        return nullptr;
    }
};