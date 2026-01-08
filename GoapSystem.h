#pragma once
#include <vector>
#include <iostream>

#include "Vector.h"
#include "MovementController.h"
#include "Pathfinding2.h"

#include "SimpleMouseClient.h"
#include "Camera.h"
#include "MemoryRead.h"
#include "Profile.h"
#include "SimpleKeyboardClient.h"
#include "CombatController.h"

// This allows us to return a pointer to ANY state type
struct ActionState {
    virtual ~ActionState() = default; // Virtual destructor ensures proper cleanup

    // Common variables moved here
    std::vector<Vector3> activePath;
    int activeIndex = 0;
    bool actionChange = 0;
    bool flyingPath = true;
    bool inMotion = false;
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
    std::vector<ULONG_PTR> gatheredNodesGuidLow = {};
    std::vector<ULONG_PTR> gatheredNodesGuidHigh = {};
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
    bool enabled = true;
    std::vector<Vector3> path = { { -1000.0f,-1000.0f, -1000.0f } };
    std::vector<Vector3> savedPath = { { -1000.0f,-1000.0f, -1000.0f } };
    int index;
    int savedIndex;
    bool hasTarget = false;
    bool hasPath = false;
};

struct Combat : public ActionState {
    bool enabled = true;
    std::vector<Vector3> path;
    int32_t targetHealth;
    bool inCombat = false; // True when bot is within range of enemy to perform rotation
    int index;
    int entityIndex;
    bool underAttack = false; // True when bot is under attack from a enemy
    bool hasTarget = false; // True when the bot selects a target to attack, but has not reached within range yet
    Vector3 enemyPosition;
    ULONG_PTR targetGuidLow;
    ULONG_PTR targetGuidHigh;
};

struct StuckState : public ActionState {
    bool isStuck = false;
    Vector3 lastPosition;
    DWORD lastCheckTime = 0;
    DWORD stuckStartTime = 0;
    int attemptCount = 0;
};

struct RepairState : public ActionState {
    bool enabled = true;
    std::vector<Vector3> path;
    int index;
    Vector3 npcLocation;
	bool repairNeeded = false;
	ULONG_PTR npcGuidLow;
    ULONG_PTR npcGuidHigh;
};

// --- WORLD STATE (The Knowledge) ---
struct WorldState {
    GlobalState globalState;
    Looting lootState;
    Gathering gatherState;
    PathFollowing pathFollowState;
    WaypointReturn waypointReturnState;
    Combat combatState; 
    StuckState stuckState;
    RepairState repairState;
    
    bool inTunnel = false;

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

// --- SHARED INTERACTION CONTROLLER ---
class InteractionController {
public:
    // State Machine for Interaction
    enum InteractState {
        STATE_IDLE,
        STATE_CREATE_PATH,
        STATE_APPROACH,
        STATE_STABILIZE,
        STATE_ALIGN_CAMERA,
        STATE_SCAN_MOUSE,    // Move mouse to offset
        STATE_WAIT_HOVER,    // Wait for tooltip/GUID
        STATE_CLICK,
        STATE_POST_INTERACT_WAIT, // Wait for cast bar or loot window
        STATE_APPROACH_POST_INTERACT,
        STATE_COMPLETE
    };

private:
    MovementController& pilot;
    SimpleMouseClient& mouse;
    SimpleKeyboardClient& keyboard;
    Camera& camera;
    MemoryAnalyzer& analyzer;
    ConsoleInput inputCommand;
    HWND hGameWindow;
    DWORD procId;
    ULONG_PTR baseAddress;

    InteractState currentState = STATE_IDLE;

    // Internal State Variables
    DWORD stateTimer = 0;
    DWORD pathCalcTimer = 0;
    int pathIndex = 0;
    std::vector<Vector3> currentPath;

    // Scanning Variables
    int offsetIndex = 0;
    const std::vector<POINT> searchOffsets = {
        {0, 0}, {0, -15}, {0, 15}, {-15, 0}, {15, 0},
        {-20, -20}, {20, -20}, {20, 20}, {-20, 20}
    };

public:
    InteractionController(MovementController& mc, SimpleMouseClient& m, SimpleKeyboardClient& k, Camera& c, MemoryAnalyzer& mem, DWORD pid, ULONG_PTR base, HWND hWin)
        : pilot(mc), mouse(m), keyboard(k), camera(c), analyzer(mem), inputCommand(k), procId(pid), baseAddress(base), hGameWindow(hWin) {
    }    

    void Reset() {
        currentState = STATE_IDLE;
        stateTimer = 0;
        pathCalcTimer = 0;
        offsetIndex = 0;
        pathIndex = 0;
        currentPath.clear();
    }

    void SetState(InteractState newState) {
        currentState = newState;
    }

    std::string GetState() {
        switch (currentState) {
        case STATE_IDLE: return "STATE_IDLE";
        case STATE_CREATE_PATH: return "STATE_CREATE_PATH";
        case STATE_APPROACH: return "STATE_APPROACH";
        case STATE_STABILIZE: return "STATE_STABILIZE";
        case STATE_ALIGN_CAMERA: return "STATE_ALIGN_CAMERA";
        case STATE_SCAN_MOUSE: return "STATE_SCAN_MOUSE";
        case STATE_WAIT_HOVER: return "STATE_WAIT_HOVER";
        case STATE_CLICK: return "STATE_CLICK";
        case STATE_POST_INTERACT_WAIT: return "STATE_POST_INTERACT_WAIT";
        case STATE_APPROACH_POST_INTERACT: return "STATE_APPROACH_POST_INTERACT";
        case STATE_COMPLETE: return "STATE_COMPLETE";
        }
    }

    // Main Entry Point
    // returns TRUE if interaction sequence is complete
    bool EngageTarget(Vector3 targetPos, ULONG_PTR targetGuidLow, ULONG_PTR targetGuidHigh, PlayerInfo& player, std::vector<Vector3>& currentPath, int& pathIndex, float approachDist, float interactDist, bool checkTarget, bool targetGuid,
        bool fly, int postClickWaitMs, MouseButton click, bool movingTarget, bool inTunnel) {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        if ((currentState != STATE_CREATE_PATH) && (movingTarget == true) && (pathCalcTimer != 0) && (GetTickCount() - pathCalcTimer > 200)) {
            currentPath = CalculatePath({ targetPos }, player.position, 0, fly, 530);
            pathIndex = 0;
            pathCalcTimer = GetTickCount();
        }

        switch (currentState) {
        case STATE_IDLE:
            currentState = STATE_CREATE_PATH;
            return false;

        case STATE_CREATE_PATH:
            // Assuming CalculatePath is available globally or via included header
            currentPath = CalculatePath({ targetPos }, player.position, 0, fly, 530);
            logFile << "Target Pos x: " << targetPos.x << " | Target Pos y: " << targetPos.y << " | Target Pos z: " << targetPos.z << std::endl;
            for (int i = 0; i < currentPath.size(); i++) {
                logFile << "Path: " << i << " | X coord: " << currentPath[i].x << " | Y coord: " << currentPath[i].y << " | Z coord: " << currentPath[i].z << std::endl;
            }
            pathIndex = 0;
            pathCalcTimer = GetTickCount();
            currentState = STATE_APPROACH;
            return false;

        case STATE_APPROACH:
            if (MoveToTargetLogic(targetPos, currentPath, pathIndex, player, stateTimer, interactDist, fly, inTunnel)) {
                stateTimer = GetTickCount(); // Start stabilization timer
                currentState = STATE_STABILIZE;
            }
            return false;

        case STATE_STABILIZE:
            // Wait 300ms after arriving/dismounting before moving mouse
            if (GetTickCount() - stateTimer > 1000) {
                currentState = STATE_ALIGN_CAMERA;
            }
            return false;

        case STATE_ALIGN_CAMERA:
            if (AlignCameraLogic(targetPos)) {
                currentState = STATE_SCAN_MOUSE;
                offsetIndex = 0;
            }
            return false;

        case STATE_SCAN_MOUSE:
            if (offsetIndex >= searchOffsets.size()) {
                // Failed to find target after checking all offsets
                logFile << "[INTERACT] Failed to find target GUID." << std::endl;
                Reset(); // Reset to try again? Or fail?
                return false;
            }

            // Move Mouse Logic
            {
                int sx, sy;
                POINT p = searchOffsets[offsetIndex];
                ClientToScreen(hGameWindow, &p);

                if (camera.WorldToScreen(targetPos, sx, sy)) {
                    mouse.MoveAbsolute(sx + p.x, sy + p.y);
                }
            }

            stateTimer = GetTickCount(); // Reset timer for hover check
            currentState = STATE_WAIT_HOVER;
            return false;

        case STATE_WAIT_HOVER:
            // Allow 100ms for game to update tooltip
            if (GetTickCount() - stateTimer < 100) return false;

            // Check GUID
            {
                ULONG_PTR currentLow = 0, currentHigh = 0;
                analyzer.ReadPointer(procId, baseAddress + MOUSE_OVER_GUID_OFFSET, currentLow);
                analyzer.ReadPointer(procId, baseAddress + MOUSE_OVER_GUID_OFFSET + 0x8, currentHigh);

                if (currentLow == targetGuidLow && currentHigh == targetGuidHigh) {
                    currentState = STATE_CLICK;
                }
                else {
                    offsetIndex++; // Try next offset
                    currentState = STATE_SCAN_MOUSE;
                }
            }
            return false;

        case STATE_CLICK:
            if (pilot.GetSteering()) {
                mouse.ReleaseButton(MOUSE_RIGHT);
                pilot.ChangeSteering(false);
            }
            mouse.Click(click); // Or Left, depending on need. Right for interact/attack.
            stateTimer = GetTickCount();
            currentState = STATE_POST_INTERACT_WAIT;
            return false;

        case STATE_POST_INTERACT_WAIT:
            if (GetTickCount() - stateTimer >= postClickWaitMs) {
                if (approachDist == interactDist) currentState = STATE_COMPLETE;
                else currentState = STATE_APPROACH_POST_INTERACT;
            }
            return false;

        case STATE_APPROACH_POST_INTERACT:
            if (MoveToTargetLogic(targetPos, currentPath, pathIndex, player, stateTimer, approachDist, fly, inTunnel)) {
                stateTimer = GetTickCount();
                currentState = STATE_COMPLETE;
            }
            return false;

        case STATE_COMPLETE:
            return true;
        }

        return false;
    }

public:
    // Helper: Logic to follow path and dismount
    bool MoveToTargetLogic(Vector3 targetPos, std::vector<Vector3>& path, int& index, PlayerInfo& player, DWORD& timer, float acceptanceRadius, bool fly, bool inTunnel) {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        if (index >= path.size()) {
            pilot.Stop();
            // Dismount Logic
            if (player.flyingMounted || player.groundMounted) {
                if (player.position.Dist3D(path[index - 1]) < 10.0f) {
                    if (inputCommand.SendDataRobust(std::wstring(L"/run if IsMounted() then Dismount()end"))) {
                        inputCommand.Reset();
                        return true;
                    }
                    return false;
                }
                if (timer != 0) {
                    if (GetTickCount() - timer > 1000) {
                        keyboard.SendKey('X', 0, false);
                        if (GetTickCount() - timer > 1200) {
                            if (inputCommand.SendDataRobust(std::wstring(L"/run if IsMounted() then Dismount()end"))) {
                                inputCommand.Reset();
                                return true;
                            }
                        }
                    }
                    return false;
                }
                if (inputCommand.GetState() == "OPEN_CHAT_WINDOW") {
                    if (timer == 0) timer = GetTickCount();
                    if (GetTickCount() - timer < 1000) keyboard.SendKey('X', 0, true);
                    return false;
                }
            }
            else {
                inputCommand.Reset();
                return true;
            }
            return false;
        }

        Vector3 wp = path[index];
        float dist = (fly) ? player.position.Dist3D(wp) : player.position.Dist2D(wp);
        if (dist < acceptanceRadius) {
            index++;
            return false;
        }
        pilot.SteerTowards(player.position, player.rotation, wp, fly, player, inTunnel);
        return false;
    }

    // Helper: Logic to rotate camera
    bool AlignCameraLogic(Vector3 targetPos) {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        Vector3 camFwd = camera.GetForward();
        Vector3 camPos = camera.GetPosition();

        // 1. Calculate Yaw Angles (Top-Down view)
        // atan2(y, x) gives angle from X-axis in radians
        float camYaw = std::atan2(camFwd.y, camFwd.x);
        Vector3 toTarget = targetPos - camPos;
        float targetYaw = std::atan2(toTarget.y, toTarget.x);

        // 2. Calculate Difference
        float diff = targetYaw - camYaw;

        logFile << diff << " " << camYaw << std::endl;

        // Normalize to shortest turn (-PI to +PI)
        const float PI = 3.14159265f;
        while (diff <= -PI) diff += 2 * PI;
        while (diff > PI) diff -= 2 * PI;

        // 3. Convert Radian Delta to Mouse Pixels
        // Heuristic: ~800 pixels per radian (Adjust this based on mouse sensitivity)
        // Negative sign because dragging Mouse Left (Negative X) usually turns Camera Left (Positive Yaw)
        int pixels = (int)(diff * -50.0f);
        if (pixels > 100) pixels = 100;
        if (pixels < -100) pixels = -100;

        // 4. Clamp speed (don't snap instantly, looks robotic)
        if (pixels > 100) pixels = 100;
        if (pixels < -100) pixels = -100;

        // 5. If we are close enough, stop rotating
        if (std::abs(pixels) < 50) return true;

        // Reset Cursor to center to avoid hitting edge
        RECT rect;
        GetClientRect(hGameWindow, &rect);
        POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
        ClientToScreen(hGameWindow, &center);
        mouse.MoveAbsolute(center.x, center.y);
        Sleep(5);

        if (pilot.GetSteering()) {
            mouse.ReleaseButton(MOUSE_RIGHT);
            pilot.ChangeSteering(false);
        }
        mouse.PressButton(MOUSE_LEFT);
        Sleep(5);
        mouse.Move(pixels, 0);
        Sleep(5);
        mouse.ReleaseButton(MOUSE_LEFT);
        return false;
    }
};

class ActionRepair : public GoapAction {
private:
    InteractionController& interact;
    SimpleKeyboardClient& keyboard;
    ConsoleInput input; // Used to send the Lua command

public:
    ActionRepair(InteractionController& ic, SimpleKeyboardClient& k)
        : interact(ic), keyboard(k), input(k) {
    }

    // Priority 150: Higher than Gathering(100)/Looting(60), Lower than Combat(200)
    // If equipment is broken, we should prioritize this over farming, but defend ourselves if attacked.
    int GetPriority() override { return 150; }

    std::string GetName() override { return "Repair Equipment"; }

    ActionState* GetState(WorldState& ws) override { return &ws.repairState; }

    bool CanExecute(const WorldState& ws) override {
        // Execute if enabled AND we actually need repairs
        return ws.repairState.enabled && ws.repairState.repairNeeded;
    }

    void ResetState() override {
        interact.Reset();
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
		logFile << "[ACTION] Repairing Equipment..." << std::endl;
        // 1. Move to NPC and Interact
        // approachDist: 3.0f, interactDist: 5.0f
        // postClickWaitMs: 2500 (Give the vendor window time to open)
        bool complete = interact.EngageTarget(
            ws.repairState.npcLocation,
            ws.repairState.npcGuidLow,
            ws.repairState.npcGuidHigh,
            ws.player,
            ws.repairState.path,
            ws.repairState.index,
            3.0f,  // Approach Distance
            5.0f,  // Interact Distance
            true,  // Check Target
            true,  // Check GUID
            true, // Fly if applicable
            2500,  // Wait 2.5s for window to open
            MOUSE_RIGHT,
            false, // NPC is likely stationary
            ws.inTunnel
        );

        // Visualization
        ws.globalState.activePath = ws.repairState.path;
        ws.globalState.activeIndex = ws.repairState.index;

        // 2. Perform Repair Logic
        if (complete) {
            // Send Lua command to repair all items
            // "RepairAllItems()" is the standard WoW API, passed via ConsoleInput
            input.SendDataRobust(std::wstring(L"/run RepairAllItems()"));

            // Wait a moment for the transaction (optional, helps prevent instant state flip)
            Sleep(500);

            // Mark repair as done
            ws.repairState.repairNeeded = false;

            // Cleanup
            interact.Reset();

            // Close the window (Optional: prevents clutter)
            // input.SendDataRobust(std::wstring(L"/run CloseGossip() CloseMerchant()"));

            return true;
        }

        return false;
    }
};

class ActionUnstuck : public GoapAction {
private:
    SimpleKeyboardClient& keyboard;
    MovementController& pilot;
    DWORD actionStartTime = 0;
    bool turnLeft = false; // Random decision for direction

    enum UnstuckStep {
        STEP_DISENGAGE, // Back + Jump/Ascend
        STEP_REDIRECT,  // Turn + Strafe (ONE WAY ONLY)
        STEP_ESCAPE,    // Move Forward in new direction
        FINISHED
    };
    UnstuckStep currentStep = STEP_DISENGAGE;

public:
    ActionUnstuck(SimpleKeyboardClient& k, MovementController& p)
        : keyboard(k), pilot(p) {
    }

    int GetPriority() override { return 900; }
    std::string GetName() override { return "Unstuck Maneuver"; }
    ActionState* GetState(WorldState& ws) override { return &ws.stuckState; }

    bool CanExecute(const WorldState& ws) override {
        return ws.stuckState.isStuck;
    }

    void ResetState() override {
        actionStartTime = GetTickCount();
        currentStep = STEP_DISENGAGE;

        // Randomly pick Left or Right to avoid predictable loops
        turnLeft = (GetTickCount() % 2 == 0);
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        DWORD now = GetTickCount();
        DWORD elapsed = now - actionStartTime;

        // Timeout Safety
        if (elapsed > 6000) {
            ws.stuckState.isStuck = false;
            ws.stuckState.lastPosition = ws.player.position;
            return true;
        }

        bool isFlying = ws.player.isFlying || ws.player.flyingMounted;

        switch (currentStep) {

            // 1. DISENGAGE: Back up and gain height
        case STEP_DISENGAGE:
            if (elapsed < 1500) {
                keyboard.SendKey('S', 0, true);
                // Try to ascend if flying, jump if ground
                if (isFlying) keyboard.SendKey(VK_SPACE, 0, true);
                else if (elapsed < 500) keyboard.SendKey(VK_SPACE, 0, true); // Short Jump tap
                else keyboard.SendKey(VK_SPACE, 0, false);
            }
            else {
                // Stop Moving Back
                keyboard.SendKey('S', 0, false);
                keyboard.SendKey(VK_SPACE, 0, false);

                // Reset timer implies 'phase time', but we are using absolute elapsed
                // simplified: just fall through to next logic based on time
                currentStep = STEP_REDIRECT;
            }
            break;

            // 2. REDIRECT: Turn AND Strafe to the side (One direction only!)
        case STEP_REDIRECT:
            // Run this from 1.5s to 2.5s
            if (elapsed < 2500) {
                if (turnLeft) {
                    //keyboard.SendKey('A', 0, true); // Turn Left
                    keyboard.SendKey('Q', 0, true); // Strafe Left
                }
                else {
                    //keyboard.SendKey('D', 0, true); // Turn Right
                    keyboard.SendKey('E', 0, true); // Strafe Right
                }
            }
            else {
                // Release Keys
                keyboard.SendKey('A', 0, false);
                keyboard.SendKey('Q', 0, false);
                keyboard.SendKey('D', 0, false);
                keyboard.SendKey('E', 0, false);
                currentStep = STEP_ESCAPE;
            }
            break;

            // 3. ESCAPE: Move Forward in the NEW direction
        case STEP_ESCAPE:
            // Run this from 2.5s to 4.0s
            if (elapsed < 4000) {
                keyboard.SendKey('W', 0, true);
            }
            else {
                keyboard.SendKey('W', 0, false);
                currentStep = FINISHED;
            }
            break;

        case FINISHED:
            std::cout << "[GOAP] Unstuck Complete. Resuming." << std::endl;
            ws.stuckState.isStuck = false;
            ws.stuckState.lastPosition = ws.player.position;
            return true;
        }

        return false;
    }
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
        pilot.SteerTowards(ws.player.position, ws.player.rotation, safeSpot, false, ws.player, ws.inTunnel);

        // If we are far enough away, we consider this action "Complete" (for this tick)
        // But for continuous evasion, return false so we keep running until state changes.
        return false;
    }
};

class ActionCombat : public GoapAction {
private:
    InteractionController& interact;
    CombatController& combatController;
    bool targetSelected = false;
    //SimpleKeyboardClient& keyboard;
    //SimpleMouseClient& mouse;
    //Camera& camera;

    const float MELEE_RANGE = 3.5f; // Adjust based on class (e.g., 30.0f for casters)

public:
    ActionCombat(InteractionController& ic, CombatController& cc)
        : interact(ic), combatController(cc) {
    }

    bool CanExecute(const WorldState& ws) override {
        // Execute if the feature is enabled AND we are either in combat or being attacked
        return ws.combatState.enabled && (ws.combatState.inCombat || ws.combatState.underAttack || ws.combatState.hasTarget);
    }

    // Priority 200: Higher than Loot(60)/Gather(100), but Lower than EscapeDanger(500)
    int GetPriority() override { return 200; }

    std::string GetName() override { return "Combat Routine"; }

    ActionState* GetState(WorldState& ws) override {
        return &ws.combatState;
    }
    
    void ResetState() override {
        combatController.ResetCombatState();
        interact.Reset();
        targetSelected = false;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);

        bool lowHp = false;

        if ((ws.player.position.Dist3D(ws.combatState.enemyPosition) > 4.0f) && (ws.combatState.inCombat)){
            ws.combatState.inCombat = false;
            interact.Reset();
        }
        // Logic to check target health
		const auto& entity = ws.entities[ws.combatState.entityIndex];
        if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(entity.info)) {
            if ((ws.combatState.targetGuidLow == entity.guidLow) && (ws.combatState.targetGuidHigh == entity.guidHigh)) {
                logFile << npc->health << std::endl;
                if (npc->health == 0) {
                    ResetState();
                    ws.combatState.hasTarget = false;
                    ws.combatState.underAttack = false;
                    ws.combatState.inCombat = false;
                    return true;
                }
                if ((static_cast<float>(npc->health) / npc->maxHealth) < 0.2f) {
                    lowHp = true;
                }
            }
        }
        
        // 1. Target Selection Phase
        if ((!targetSelected) && (!ws.combatState.inCombat)) {
            // Range 4.0f (Melee), No Flying, 100ms wait after click
            logFile << ws.player.isFlying << " " << interact.GetState() << std::endl;
            if (interact.EngageTarget(ws.combatState.enemyPosition, ws.combatState.targetGuidLow, ws.combatState.targetGuidHigh, ws.player, ws.combatState.path, ws.combatState.index, 2.0f, 10.0f, true, true, ws.player.isFlying, 100, MOUSE_RIGHT, true, ws.inTunnel)) {
                targetSelected = true;
                ws.combatState.inCombat = true;
            }
        }
        ws.globalState.activePath = ws.combatState.path;
        ws.globalState.activeIndex = ws.combatState.index;

        // 2. Combat Phase (Rotation)
        // Ensure we stay facing the target while fighting
        // Note: We might want a dedicated 'FaceTarget' method in InteractController that doesn't click
        // But for now, just running rotation:
        if (ws.combatState.inCombat) {
            combatController.UpdateRotation(lowHp);
        }

        // If mob dies (check logic not shown), return true
        return false;
    }


        ///////////////////////////
        // OLD CODE
        // ////////////////////
        // 1. Identify Target & Update Info
        //Vector3 targetPos = ws.combatState.enemyPosition;
        //int targetHealth = ws.combatState.targetHealth; // Default to 100% if unknown
        //bool targetFound = false;

        //// Try to find the live entity data for our target to get accurate position/health
        //if (ws.combatState.targetGuidLow != 0) {
        //    for (const auto& ent : ws.entities) {
        //        if (ent.guidLow == ws.combatState.targetGuidLow) {
        //            if (auto enemy = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
        //                targetPos = enemy->position;
        //                targetHealth = enemy->health;
        //                targetFound = true;
        //            }
        //            break;
        //        }
        //    }
        //}

        //if (targetHealth == 0) {
        //    ws.combatState.inCombat = false;
        //    return true;
        //}

        //// If we have no target GUID but are in combat, we might want to target nearest (Logic not shown here, assuming state is populated)

        //// 2. Check Distance
        //float dist = ws.player.position.Dist3D(targetPos);

        //// 3. Movement Logic
        //if (dist > MELEE_RANGE) {
        //    // Move towards target
        //    // Note: passing 'false' for flying to force ground combat movement
        //    pilot.SteerTowards(ws.player.position, ws.player.rotation, targetPos, false, ws.player);
        //}
        //else {
        //    if (ws.combatState.inCombat != true) {
        //        int sx, sy;
        //        if (camera.WorldToScreen(ws.combatState.enemyPosition, sx, sy, &mouse))
        //            mouse.MoveAbsolute(sx + p.x, sy + p.y);
        //    }
        //    // In range: Stop moving so we can cast
        //    pilot.Stop();

        //    // Optional: Explicitly face target if SteerTowards doesn't handle static rotation

        //    // Target nearest enemy
        //    keyboard.TypeKey(VK_TAB);
        //}

        //// 4. Rotation Logic
        //// Determine if target is in execute range (< 20% hp) for Hammer of Wrath
        //bool isLowHealth = (targetHealth < 20);

        //// Execute the rotation step
        //combatController.UpdateRotation(isLowHealth);

        //// Combat actions return false (not complete) until the WorldState updates to say we are out of combat
        //return false;
    //}
};

class ActionLoot : public GoapAction {
private:
    InteractionController& interact;

public:
    ActionLoot(InteractionController& ic) : interact(ic) {}

    bool CanExecute(const WorldState& ws) override {
        return ((ws.lootState.enabled) && (ws.lootState.hasLoot));
    }

    ActionState* GetState(WorldState& ws) override {
        return &ws.lootState;
    }

    int GetPriority() override { return 60; }
    std::string GetName() override { return "Loot Corpse"; }

    // Reset state if we are switching to this action fresh
    void ResetState() override {
        interact.Reset();
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);

        if (ws.gatherState.nodeActive == false) {
            interact.SetState(InteractionController::STATE_COMPLETE);
        }

        // Set wait time to 1500ms (1.5s loot window)
        // Range 3.0f, Flying Allowed
        bool complete = interact.EngageTarget(
            ws.lootState.position,
            ws.lootState.guidLow,
            ws.lootState.guidHigh,
            ws.player,
            ws.lootState.path,
            ws.lootState.index,
            6.0f,
            6.0f,
            false,
            false,
            true,
            1500,
            MOUSE_RIGHT,
            false,
            ws.inTunnel
        );
        ws.globalState.activePath = ws.lootState.path;
        ws.globalState.activeIndex = ws.lootState.index;

        if (complete) {
            // Cleanup
            interact.Reset();
            ws.lootState.hasLoot = false;
            return true;
        }
        return false;
    }
};

// --- CONCRETE ACTION: GATHER NODE (NEW) ---
class ActionGather : public GoapAction {
private:
    InteractionController& interact;

public:
    ActionGather(InteractionController& ic) : interact(ic) {}

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
        interact.Reset();
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);

        if (ws.gatherState.nodeActive == false) {
            interact.SetState(InteractionController::STATE_COMPLETE);
        }

        // Set wait time to 4500ms (3.5s cast + 1s loot window)
        // Range 3.0f, Flying Allowed
        bool complete = interact.EngageTarget(
            ws.gatherState.position,
            ws.gatherState.guidLow,
            ws.gatherState.guidHigh,
            ws.player,
            ws.gatherState.path,
            ws.gatherState.index,
            2.8f,
            2.8f,
            false,
            false,
            true,
            4500,
            MOUSE_RIGHT,
            false,
            ws.inTunnel
        );
        ws.globalState.activePath = ws.gatherState.path;
        ws.globalState.activeIndex = ws.gatherState.index;

        if (complete) {
            // Cleanup
            interact.Reset();
            ws.gatherState.hasNode = false;
            ws.gatherState.nodeActive = false;
            // Reset camera or other post-action stuff
            return true;
        }
        return false;
    }
};

// --- Return to a saved waypoint after performing a different action --- //
class ActionReturnWaypoint : public GoapAction {
private:
    const float ACCEPTANCE_RADIUS = 3.0f;
    const float SAFE_FLIGHT_HEIGHT = 50.0f;
    const float GROUND_HEIGHT_THRESHOLD = 15.0f; // If target is this close to ground, use ground path

    enum ReturnPhase {
        PHASE_CHECK_DIRECT,    // First check if we can go direct
        PHASE_MOUNT,           // Mount up if needed
        PHASE_ASCEND,          // Climb to safe height
        PHASE_NAVIGATE,        // Follow path back
        PHASE_COMPLETE
    };

    ReturnPhase currentPhase = PHASE_CHECK_DIRECT;
    Vector3 ascentTarget;
    bool ascentTargetSet = false;
    bool checkedDirect = false;
    DWORD mountTimer = 0;
    SimpleKeyboardClient* keyboard = nullptr;
    ConsoleInput* consoleInput = nullptr;

public:
    void SetKeyboard(SimpleKeyboardClient* kb, ConsoleInput* ci) {
        keyboard = kb;
        consoleInput = ci;
    }

    bool CanExecute(const WorldState& ws) override {
        return (ws.waypointReturnState.enabled && ws.waypointReturnState.hasTarget);
    }

    int GetPriority() override { return 70; }
    std::string GetName() override { return "Return to previous waypoint"; }

    ActionState* GetState(WorldState& ws) override {
        return &ws.waypointReturnState;
    }

    void ResetState() override {
        currentPhase = PHASE_CHECK_DIRECT;
        ascentTargetSet = false;
        checkedDirect = false;
        mountTimer = 0;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);

        switch (currentPhase) {
        case PHASE_CHECK_DIRECT:
        {
            if (checkedDirect) {
                currentPhase = PHASE_MOUNT;
                break;
            }

            checkedDirect = true;
            Vector3 returnTarget = ws.waypointReturnState.savedPath[ws.waypointReturnState.savedIndex];

            logFile << "[RETURN] Checking for direct path..." << std::endl;
            logFile << "  From: (" << ws.player.position.x << "," << ws.player.position.y << "," << ws.player.position.z << ")" << std::endl;
            logFile << "  To: (" << returnTarget.x << "," << returnTarget.y << "," << returnTarget.z << ")" << std::endl;

            // Check if target is at ground level
            float targetGroundZ = globalNavMesh.GetLocalGroundHeight(returnTarget);
            bool targetOnGround = (targetGroundZ > -90000.0f && std::abs(returnTarget.z - targetGroundZ) < GROUND_HEIGHT_THRESHOLD);

            // Check if we're at ground level
            float currentGroundZ = globalNavMesh.GetLocalGroundHeight(ws.player.position);
            bool currentOnGround = (!ws.player.isFlying && !ws.player.flyingMounted) ||
                (currentGroundZ > -90000.0f && std::abs(ws.player.position.z - currentGroundZ) < GROUND_HEIGHT_THRESHOLD);

            logFile << "  Current on ground: " << currentOnGround << " (Z=" << ws.player.position.z << ", groundZ=" << currentGroundZ << ")" << std::endl;
            logFile << "  Target on ground: " << targetOnGround << " (Z=" << returnTarget.z << ", groundZ=" << targetGroundZ << ")" << std::endl;

            // Try direct path check
            bool directClear = false;
            if (currentOnGround && targetOnGround) {
                // Both on ground - check 2D ground path
                logFile << "  Checking ground direct path (2D)..." << std::endl;
                Vector3 testStart = ws.player.position;
                testStart.z = currentGroundZ + 1.0f;
                Vector3 testEnd = returnTarget;
                testEnd.z = targetGroundZ + 1.0f;
                directClear = globalNavMesh.CheckFlightSegment(testStart, testEnd, 530, false);
            }
            else {
                // At least one is in air - check 3D flight path
                logFile << "  Checking flight direct path (3D)..." << std::endl;
                directClear = globalNavMesh.CheckFlightSegment(ws.player.position, returnTarget, 530, true);
            }

            if (directClear) {
                logFile << "  ✓ Direct path is clear! Using simple 2-point path." << std::endl;
                ws.waypointReturnState.path = { ws.player.position, returnTarget };
                ws.waypointReturnState.hasPath = true;
                ws.waypointReturnState.index = 0;

                // Decide if we should fly or walk
                if (currentOnGround && targetOnGround) {
                    ws.waypointReturnState.flyingPath = false;
                    logFile << "  Using ground path (both positions on ground)" << std::endl;
                }
                else {
                    ws.waypointReturnState.flyingPath = true;
                    logFile << "  Using flight path" << std::endl;
                }

                currentPhase = PHASE_NAVIGATE;
                break;
            }
            else {
                logFile << "  ✗ Direct path blocked, need complex pathfinding" << std::endl;
                currentPhase = PHASE_MOUNT;
            }

            break;
        }

        case PHASE_MOUNT:
        {
            // CRITICAL: Check if we're in a no-fly zone (tunnel/indoor)
            bool inNoFlyZone = !CanFlyAt(530, ws.player.position.x, ws.player.position.y, ws.player.position.z);
            if (ws.inTunnel) {
                logFile << "[RETURN] In no-fly zone (tunnel/indoor), cannot mount - using ground pathfinding" << std::endl;
                ws.waypointReturnState.flyingPath = false;
                currentPhase = PHASE_NAVIGATE;
                break;
            }

            Vector3 returnTarget = ws.waypointReturnState.savedPath[ws.waypointReturnState.savedIndex];
            float targetGroundZ = globalNavMesh.GetLocalGroundHeight(returnTarget);
            bool targetOnGround = (targetGroundZ > -90000.0f && std::abs(returnTarget.z - targetGroundZ) < GROUND_HEIGHT_THRESHOLD);

            // If target is on ground, use ground pathfinding
            if (targetOnGround) {
                logFile << "[RETURN] Target is on ground, using ground pathfinding" << std::endl;
                ws.waypointReturnState.flyingPath = false;
                currentPhase = PHASE_NAVIGATE;
                break;
            }

            // Target is in air, need to fly
            if (ws.player.isFlying || ws.player.flyingMounted || ws.player.groundMounted) {
                logFile << "[RETURN] Already mounted" << std::endl;
                ws.waypointReturnState.flyingPath = true;
                currentPhase = PHASE_ASCEND;
                break;
            }

            // Mount up
            if ((mountTimer == 0) && (!ws.inTunnel)) {
                logFile << "[RETURN] Mounting for flight..." << std::endl;
                if (keyboard && consoleInput) {
                    consoleInput->SendDataRobust(std::wstring(L"/run if not(IsFlyableArea()and IsMounted())then CallCompanion(\"Mount\", 1) end "));
                }
                mountTimer = GetTickCount();
            }

            if (GetTickCount() - mountTimer > 2500) {
                if (ws.player.isFlying || ws.player.flyingMounted || ws.player.groundMounted) {
                    logFile << "[RETURN] Mount successful" << std::endl;
                    ws.waypointReturnState.flyingPath = true;
                    currentPhase = PHASE_ASCEND;
                    mountTimer = 0;
                }
                else {
                    logFile << "[RETURN] Mount failed, retrying..." << std::endl;
                    mountTimer = 0;
                }
            }

            return false;
        }

        case PHASE_ASCEND:
        {
            // Find safe ascent target using mini A*
            if (!ascentTargetSet) {
                float groundZ = globalNavMesh.GetLocalGroundHeight(ws.player.position);
                float targetZ = (groundZ > -90000.0f)
                    ? groundZ + SAFE_FLIGHT_HEIGHT
                    : ws.player.position.z + SAFE_FLIGHT_HEIGHT;

                Vector3 straightUp = Vector3(ws.player.position.x, ws.player.position.y, targetZ);

                if (globalNavMesh.CheckFlightSegment(ws.player.position, straightUp, 530, true)) {
                    ascentTarget = straightUp;
                    ascentTargetSet = true;
                    logFile << "[RETURN] Ascending straight up to Z=" << targetZ << std::endl;
                }
                else {
                    logFile << "[RETURN] Blocked above, mini A* search..." << std::endl;

                    // Mini 2D A* (same as before)
                    struct EscapeNode {
                        Vector3 pos;
                        float gScore = 1e9f;
                        float hScore = 0.0f;
                        int parentIdx = -1;
                        float fScore() const { return gScore + hScore; }
                    };

                    std::vector<EscapeNode> escapeNodes;
                    escapeNodes.reserve(200);
                    std::unordered_map<int64_t, int> gridMap;
                    std::unordered_set<int> closedSet;

                    auto GridHash = [](float x, float y, float gridSize) -> int64_t {
                        int gx = (int)std::floor(x / gridSize);
                        int gy = (int)std::floor(y / gridSize);
                        return ((int64_t)gx << 32) | (int64_t)gy;
                        };

                    const float ESCAPE_GRID_SIZE = 10.0f;
                    const int MAX_ESCAPE_ITERATIONS = 150;
                    const float MAX_ESCAPE_RADIUS = 60.0f;

                    Vector3 returnTarget = ws.waypointReturnState.savedPath[ws.waypointReturnState.savedIndex];
                    Vector3 searchCenter = ws.player.position;

                    int startIdx = 0;
                    escapeNodes.emplace_back();
                    escapeNodes[startIdx].pos = ws.player.position;
                    escapeNodes[startIdx].gScore = 0.0f;
                    escapeNodes[startIdx].hScore = ws.player.position.Dist2D(returnTarget);
                    gridMap[GridHash(ws.player.position.x, ws.player.position.y, ESCAPE_GRID_SIZE)] = startIdx;

                    std::vector<int> openList = { startIdx };
                    int goalIdx = -1;
                    int iterations = 0;

                    const int dirs[][2] = {
                        {1,0}, {-1,0}, {0,1}, {0,-1},
                        {1,1}, {1,-1}, {-1,1}, {-1,-1}
                    };

                    while (!openList.empty() && iterations < MAX_ESCAPE_ITERATIONS) {
                        int currentIdx = openList[0];
                        int listIdx = 0;
                        for (size_t i = 1; i < openList.size(); ++i) {
                            if (escapeNodes[openList[i]].fScore() < escapeNodes[currentIdx].fScore()) {
                                currentIdx = openList[i];
                                listIdx = i;
                            }
                        }
                        openList.erase(openList.begin() + listIdx);

                        if (closedSet.count(currentIdx)) continue;
                        closedSet.insert(currentIdx);
                        iterations++;

                        EscapeNode& current = escapeNodes[currentIdx];

                        Vector3 testAscent = Vector3(current.pos.x, current.pos.y, targetZ);
                        if (globalNavMesh.CheckFlightSegment(current.pos, testAscent, 530, true)) {
                            goalIdx = currentIdx;
                            logFile << "[RETURN] Escape found after " << iterations << " iterations" << std::endl;
                            break;
                        }

                        for (int i = 0; i < 8; ++i) {
                            Vector3 neighborPos = current.pos + Vector3(
                                dirs[i][0] * ESCAPE_GRID_SIZE,
                                dirs[i][1] * ESCAPE_GRID_SIZE,
                                0.0f
                            );
                            neighborPos.z = ws.player.position.z;

                            if (searchCenter.Dist2D(neighborPos) > MAX_ESCAPE_RADIUS) continue;

                            int64_t hash = GridHash(neighborPos.x, neighborPos.y, ESCAPE_GRID_SIZE);
                            auto it = gridMap.find(hash);

                            int neighborIdx;
                            if (it == gridMap.end()) {
                                neighborIdx = escapeNodes.size();
                                escapeNodes.emplace_back();
                                escapeNodes[neighborIdx].pos = neighborPos;
                                escapeNodes[neighborIdx].hScore = neighborPos.Dist2D(returnTarget);
                                gridMap[hash] = neighborIdx;
                            }
                            else {
                                neighborIdx = it->second;
                            }

                            if (closedSet.count(neighborIdx)) continue;

                            if (!globalNavMesh.CheckFlightSegment(current.pos, neighborPos, 530, true)) {
                                continue;
                            }

                            float tentativeG = current.gScore + current.pos.Dist2D(neighborPos);
                            if (tentativeG < escapeNodes[neighborIdx].gScore) {
                                escapeNodes[neighborIdx].gScore = tentativeG;
                                escapeNodes[neighborIdx].parentIdx = currentIdx;

                                if (std::find(openList.begin(), openList.end(), neighborIdx) == openList.end()) {
                                    openList.push_back(neighborIdx);
                                }
                            }
                        }
                    }

                    if (goalIdx >= 0) {
                        std::vector<Vector3> escapePath;
                        int idx = goalIdx;
                        while (idx >= 0) {
                            escapePath.push_back(escapeNodes[idx].pos);
                            idx = escapeNodes[idx].parentIdx;
                        }
                        std::reverse(escapePath.begin(), escapePath.end());

                        Vector3 escapePoint = escapeNodes[goalIdx].pos;
                        ascentTarget = Vector3(escapePoint.x, escapePoint.y, targetZ);
                        ascentTargetSet = true;

                        escapePath.push_back(ascentTarget);
                        ws.globalState.activePath = escapePath;
                        ws.globalState.activeIndex = 0;
                    }
                    else {
                        logFile << "[RETURN] Mini A* failed, extreme height" << std::endl;
                        ascentTarget = Vector3(ws.player.position.x, ws.player.position.y, targetZ + 60.0f);
                        ascentTargetSet = true;
                    }
                }
            }

            // Navigate escape path
            if (!ws.globalState.activePath.empty() && ws.globalState.activeIndex < ws.globalState.activePath.size()) {
                Vector3 nextWP = ws.globalState.activePath[ws.globalState.activeIndex];
                float dist = ws.player.position.Dist3D(nextWP);

                if (dist < ACCEPTANCE_RADIUS) {
                    ws.globalState.activeIndex++;
                }

                if (ws.globalState.activeIndex >= ws.globalState.activePath.size()) {
                    logFile << "[RETURN] Reached safe height Z=" << ws.player.position.z << std::endl;
                    currentPhase = PHASE_NAVIGATE;
                    ws.globalState.activePath.clear();
                    break;
                }

                pilot.SteerTowards(ws.player.position, ws.player.rotation, nextWP, true, ws.player, ws.inTunnel);
            }
            else {
                float dist = ws.player.position.Dist3D(ascentTarget);
                if (dist < ACCEPTANCE_RADIUS || ws.player.position.z >= ascentTarget.z - 2.0f) {
                    logFile << "[RETURN] Reached safe height Z=" << ws.player.position.z << std::endl;
                    currentPhase = PHASE_NAVIGATE;
                    break;
                }

                pilot.SteerTowards(ws.player.position, ws.player.rotation, ascentTarget, true, ws.player, ws.inTunnel);
            }

            return false;
        }

        case PHASE_NAVIGATE:
        {
            if (!ws.waypointReturnState.hasPath) {
                Vector3 returnTarget = ws.waypointReturnState.savedPath[ws.waypointReturnState.savedIndex];

                logFile << "[RETURN] Calculating " << (ws.waypointReturnState.flyingPath ? "FLIGHT" : "GROUND") << " path" << std::endl;
                logFile << "  From: (" << ws.player.position.x << "," << ws.player.position.y << "," << ws.player.position.z << ")" << std::endl;
                logFile << "  To: (" << returnTarget.x << "," << returnTarget.y << "," << returnTarget.z << ")" << std::endl;

                ws.waypointReturnState.path = CalculatePath(
                    { returnTarget },
                    ws.player.position,
                    0,
                    ws.waypointReturnState.flyingPath,
                    530,
                    false
                );

                if (ws.waypointReturnState.path.empty()) {
                    logFile << "[RETURN] Path calculation FAILED!" << std::endl;
                    ResetState();
                    return false;
                }

                ws.waypointReturnState.hasPath = true;
                ws.waypointReturnState.index = 0;

                logFile << "[RETURN] Path has " << ws.waypointReturnState.path.size() << " waypoints" << std::endl;
            }

            ws.globalState.activePath = ws.waypointReturnState.path;
            ws.globalState.activeIndex = ws.waypointReturnState.index;

            if (ws.waypointReturnState.index >= ws.waypointReturnState.path.size()) {
                currentPhase = PHASE_COMPLETE;
                break;
            }

            Vector3 target = ws.waypointReturnState.path[ws.waypointReturnState.index];
            float dist = ws.waypointReturnState.flyingPath
                ? ws.player.position.Dist3D(target)
                : ws.player.position.Dist2D(target);

            if (dist < ACCEPTANCE_RADIUS) {
                ws.waypointReturnState.index++;
                return false;
            }

            pilot.SteerTowards(ws.player.position, ws.player.rotation, target, ws.waypointReturnState.flyingPath, ws.player, ws.inTunnel);
            return false;
        }

        case PHASE_COMPLETE:
        {
            pilot.Stop();
            ws.waypointReturnState.path = { { -1000.0f, -1000.0f, -1000.0f } };
            ws.waypointReturnState.savedPath = { { -1000.0f, -1000.0f, -1000.0f } };
            ws.waypointReturnState.hasTarget = false;
            ws.waypointReturnState.hasPath = false;
            ResetState();
            logFile << "[RETURN] Complete!" << std::endl;
            return true;
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
        pilot.SteerTowards(ws.player.position, ws.player.rotation, target, ws.pathFollowState.flyingPath, ws.player, ws.inTunnel);
        return false; // Still running
    }
};

// --- THE PLANNER ---
class GoapAgent {
private:
    MovementController& pilot;
    std::vector<GoapAction*> availableActions;
    CombatController combatController;
    GoapAction* currentAction = nullptr;
    InteractionController interact;
    ConsoleInput consoleInput;  // Add this member

public:
    WorldState state;

    GoapAgent(MovementController& mc, SimpleMouseClient& mouse, SimpleKeyboardClient& keyboard, Camera& cam, MemoryAnalyzer& mem, DWORD pid, ULONG_PTR base, HWND hGameWindow)
        : pilot(mc), interact(mc, mouse, keyboard, cam, mem, pid, base, hGameWindow), combatController(keyboard), consoleInput(keyboard) {
        // Register Actions
        availableActions.push_back(new ActionEscapeDanger());
        availableActions.push_back(new ActionLoot(interact));

        ActionReturnWaypoint* returnAction = new ActionReturnWaypoint();
        returnAction->SetKeyboard(&keyboard, &consoleInput);
        availableActions.push_back(returnAction);

        availableActions.push_back(new ActionCombat(interact, combatController));
        availableActions.push_back(new ActionGather(interact));
        availableActions.push_back(new ActionFollowPath());
        availableActions.push_back(new ActionUnstuck(keyboard, mc));
        availableActions.push_back(new ActionRepair(interact, keyboard));
    }
    
    ~GoapAgent() {
        for (auto a : availableActions) delete a;
    }

    void Tick() {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        if (!logFile.is_open()) {
            logFile.open("SMM_Debug.log", std::ios::app);  // fallback to current dir
        }

        if (CheckIfStuck()) {
            pilot.Stop();
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

        if (bestAction != currentAction) {
            if (currentAction) {
                // FIXED: Save from globalState, not from action's state
                // Only save if we're interrupting PathFollowState
                if (currentAction->GetName() == "Follow Path") {
                    state.waypointReturnState.savedPath = state.pathFollowState.path;  // Use .path not .activePath
                    state.waypointReturnState.savedIndex = state.pathFollowState.index;  // Use .index not .activeIndex

                    logFile << "[GOAP] Saving path position: waypoint " << state.pathFollowState.index
                        << " of " << state.pathFollowState.path.size() << std::endl;

                    // Enable return after interruption
                    Vector3 temp = { -1000.0f, -1000.0f, -1000.0f };
                    if (state.pathFollowState.path.size() > 0 && state.pathFollowState.path[0] != temp) {
                        state.waypointReturnState.hasTarget = true;
                    }
                }

                logFile << "[GOAP] Stopping action: " << currentAction->GetName() << std::endl;
                pilot.Stop();
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

    // Helper to get current running state
    ActionState* GetCurrentActionState() {
        if (currentAction) return currentAction->GetState(state);
        return nullptr;
    }

private:
    bool CheckIfStuck() {
        // 1. CRITICAL: If we are already flagged as stuck, DO NOT check position.
        // We must rely on ActionUnstuck to clear this flag when it is finished.
        return false;
        if (state.stuckState.isStuck) {
            return false;
        }

        // 2. Only check if we are actually trying to go somewhere (active path)
        if (state.globalState.activePath.empty()) {
            return false;
        }

        DWORD now = GetTickCount();
        if (now - state.stuckState.lastCheckTime > 1000) { // Check every 1 second
            float distMoved = state.player.position.Dist3D(state.stuckState.lastPosition);

            // 3. Distance Check
            if (distMoved < 1.0f) {
                // We haven't moved enough
                if (state.stuckState.stuckStartTime == 0) {
                    state.stuckState.stuckStartTime = now;
                }

                // If stuck for > 2 seconds, SET the flag
                if (now - state.stuckState.stuckStartTime > 2000) {
                    std::cout << "[GOAP] Stuck Detected! Engaging Unstuck Maneuver." << std::endl;
                    state.stuckState.isStuck = true;
                    state.stuckState.stuckStartTime = 0;
                    return true;
                }
            }
            else {
                // We are moving fine. Reset the detection timer.
                // NOTE: We do NOT set isStuck = false here. 
                // Only ActionUnstuck clears the flag.
                state.stuckState.stuckStartTime = 0;
            }

            state.stuckState.lastPosition = state.player.position;
            state.stuckState.lastCheckTime = now;
        }
    }
};