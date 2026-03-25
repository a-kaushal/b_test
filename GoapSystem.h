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
#include "WorldState.h"
#include "Logger.h"
#include "dllmain.h"
#include "Misc.h"
#include "Mailing.h"
#include "ProfileLoader.h"

// Returns true if an NPC is a valid attack target based on unit flags, creature type,
// and pet ownership. Does NOT check reaction, level, mob ID lists, or grind-specific
// rules — callers add those on top.
inline bool IsBasicAttackable(const EnemyInfo& npc,
                               const std::vector<GameEntity>& entities,
                               ULONG_PTR playerGuidLow) {
    if (npc.health <= 0)                      return false;
    // Live unit flags (read from game memory each tick)
    if (npc.liveUnitFlags & 0x01000000)       return false; // PLAYER_CONTROLLED (vehicle/possess)
    if (npc.liveUnitFlags & 0x02000000)       return false; // NOT_SELECTABLE
    if (npc.liveUnitFlags & 0x2)              return false; // NON_ATTACKABLE
    // Template flags (from creature_template DB)
    if (npc.unitFlags & 0x2)                  return false; // NON_ATTACKABLE
    if (npc.unitFlags & 0x100)                return false; // IMMUNE_TO_PC
    if (npc.unitFlags & 0x02000000)           return false; // NOT_SELECTABLE
    // Extra flags
    if (npc.flagsExtra & 0x2)                 return false; // CIVILIAN
    if (npc.flagsExtra & 0x80)                return false; // TRIGGER
    if (npc.flagsExtra & 0x8000)              return false; // GUARD
    // Creature type
    switch (npc.creatureType) {
        case 8:  return false; // CRITTER
        case 11: return false; // TOTEM
        case 12: return false; // NON_COMBAT_PET
        case 13: return false; // GAS_CLOUD
        case 14: return false; // WILD_PET
        default: break;
    }
    // Player pet/summon check via UNIT_FIELD_SUMMONED_BY
    if (npc.summonedByGuidLow != 0) {
        if (npc.summonedByGuidLow == playerGuidLow) return false;
        for (const auto& ent : entities) {
            if (ent.guidLow == npc.summonedByGuidLow) {
                if (std::dynamic_pointer_cast<OtherPlayerInfo>(ent.info)) return false;
                break;
            }
        }
    }
    return true;
}

// ACTION PRIORITIES
/*
Action Unstuck - 10000
Action Respawn - 1000
Action Escape Danger - 500 (Unused)
ActionCombat - 200
ActionGather - 100
ActionLoot - 70 (Unused)
ActionReturnWaypoint - 60
ActionEscapeWater - 51
Action Interact - 50
Follow Path - 20
ActionProfileExecutor - 10
*/

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

    bool groundOverride = false;
    bool randomClick = false;
    bool recalculatedPath = false;

    bool failedClick = false;
    int interactAttempt = 0;
    bool windowsCleaned = false; // tracks whether we sent CloseAllWindows for this interaction

    int sx, sy;

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
        groundOverride = false;
        currentPath.clear();
        randomClick = false;
        recalculatedPath = false;
        interactAttempt = 0;
        failedClick = false;
        windowsCleaned = false;
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
    bool EngageTarget(Vector3 targetPos, ULONG_PTR targetGuidLow, ULONG_PTR targetGuidHigh, PlayerInfo& player, std::vector<PathNode>& currentPath, int& pathIndex, int mapId, float approachDist, float interactDist, float finalDist,
        bool checkTarget, bool targetGuid, bool canFly, int postClickWaitMs, MouseButton click, bool movingTarget, bool& failedPath, int targetId, bool& failedInteract, bool mountDisable = false, int waitTime = 1500, bool groundInteract = true,
        Vector3 updatedPos = { -1, -1, -1 }, bool checkGoal = true, bool airTarget = false) {
        bool fly_entry_state = canFly;
        if (updatedPos.x == -1) updatedPos = targetPos;
        // If we are stabilizing, aligning camera, or scanning for the mouse, we are "close enough".
        // Running CalculatePath now will reset variables and lag the mouse loop.
        bool isInteracting = (currentState == STATE_STABILIZE ||
            currentState == STATE_ALIGN_CAMERA ||
            currentState == STATE_SCAN_MOUSE ||
            currentState == STATE_WAIT_HOVER ||
            currentState == STATE_CLICK);

        // Only recalculate path if:
        // 1. We are NOT currently interacting (isInteracting == false)
        // 2. We are in a state that allows movement (not CreatePath)
        // 3. The target is moving (movingTarget == true)
        // 4. Enough time has passed (200ms)
        if (!isInteracting && (currentState != STATE_CREATE_PATH) && (movingTarget == true) && (updatedPos.Dist3D(targetPos) > interactDist) && (pathCalcTimer != 0) && (GetTickCount() - pathCalcTimer > 200)) {
            // Recalculate
            currentPath = CalculatePath({ targetPos }, player.position, 0, canFly, mapId, player.isFlying, g_GameState->globalState.ignoreUnderWater, false, 25.0f, true, 5.0f, checkGoal, airTarget);
            pathIndex = 0;
            recalculatedPath = true;
            pathCalcTimer = GetTickCount();
        }

        if (((GetState() == "STATE_APPROACH") || (GetState() == "STATE_APPROACH_POST_INTERACT")) && (GetTickCount() < pilot.m_MountDisabledUntil) && (groundOverride == false) && (canFly == true)) {
            currentState = STATE_CREATE_PATH;
            g_LogFile << "Recalculating ground path for tunnel" << std::endl;
            canFly = false;
            groundOverride = true;
        }
        if ((player.flyingMounted) && (pilot.m_IsMounting = false)) {
            groundOverride = false;
        }

        switch (currentState) {
        case STATE_IDLE:
            currentState = STATE_CREATE_PATH;
            return false;

        case STATE_CREATE_PATH:
            // Assuming CalculatePath is available globally or via included header
            g_LogFile << "Target Pos x: " << targetPos.x << " | Target Pos y: " << targetPos.y << " | Target Pos z: " << targetPos.z << std::endl;
            currentPath = CalculatePath({ targetPos }, player.position, 0, canFly, mapId, player.isFlying, g_GameState->globalState.ignoreUnderWater, false, 25.0f, true, 5.0f, checkGoal, airTarget);
            //if (currentPath.empty()) EndScript(pilot, (canFly && g_GameState->player.areaMountable) ? 2 : 1);

            if (currentPath.empty()) {
                g_LogFile << "Path is empty" << std::endl;
                failedPath = true;

                // --- CRITICAL FIX START ---
                // Do NOT switch to STATE_APPROACH. Reset and bail out.
                currentState = STATE_IDLE;
                return false;
                // --- CRITICAL FIX END ---
            }
            /*for (int i = 0; i < currentPath.size(); i++) {
                g_LogFile << "Path: " << i << " | X coord: " << currentPath[i].pos.x << " | Y coord: " << currentPath[i].pos.y << " | Z coord: " << currentPath[i].pos.z << std::endl;
            }*/
            pathIndex = 0;
            pathCalcTimer = GetTickCount();
            currentState = STATE_APPROACH;
            return false;

        case STATE_APPROACH:
            if ((MoveToTargetLogic(targetPos, currentPath, pathIndex, player, stateTimer, approachDist, interactDist, finalDist, fly_entry_state, mountDisable)) && !player.isMounted) {
                stateTimer = GetTickCount(); // Start stabilization timer
                currentState = STATE_STABILIZE;
            }
            return false;

        case STATE_STABILIZE:
            // Wait 1500ms after arriving/dismounting before moving mouse, or 300ms if not mounted
            if (groundInteract) {
                if (player.onGround) {
                    currentState = STATE_ALIGN_CAMERA;
                }
            }
            else if (GetTickCount() - stateTimer > waitTime) {
                currentState = STATE_ALIGN_CAMERA;
            }
            return false;

        case STATE_ALIGN_CAMERA:
            // Close any open UI frames once before starting camera alignment.
            // Only send the command when a UI frame is actually blocking — sending it
            // unconditionally wastes one full tick per engagement even when nothing is open.
            if (!windowsCleaned) {
                windowsCleaned = true;
                if (g_GameState->globalState.uiBlocking) {
                    inputCommand.SendDataRobust(L"/run CloseAllWindows()", g_GameState->globalState.chatOpen);
                    return false;
                }
            }
            if (AlignCameraLogic(targetPos)) {
                currentState = STATE_SCAN_MOUSE;
                offsetIndex = 0;
                windowsCleaned = false; // reset for the next interaction cycle
            }
            return false;

        case STATE_SCAN_MOUSE:
            if (offsetIndex > 0) {
                if (pilot.GetSteering()) {
                    mouse.ReleaseButton(MOUSE_RIGHT);
                    pilot.ChangeSteering(false);
                }
                g_LogFile << "Mouse Random Click " << click << std::endl;

                mouse.Click(click);
            }
            if (offsetIndex >= searchOffsets.size()) {
                //if (GetTickCount() - stateTimer >= postClickWaitMs) {
                    // Failed to find target after checking all offsets
                    g_LogFile << "[INTERACT] Failed to find target GUID of " << targetGuidLow << " " << targetGuidHigh << std::endl;

                    if (!failedClick) {
                        if (interactAttempt == 0) {
                            keyboard.TypeKey(VK_HOME);
                            keyboard.TypeKey(VK_HOME);
                            keyboard.TypeKey(VK_HOME);
                            keyboard.TypeKey(VK_HOME);
                            Sleep(500);
                            failedClick = true;
                        }
                        interactAttempt += 1;
                    }
                    if (interactAttempt <= 1) {
                        if (pilot.faceTarget(g_GameState->player.position, Vector3{ targetPos.x, targetPos.y, targetPos.z - 1.0f }, g_GameState->player.rotation, 0.2f, g_GameState->player.vertRotation, true)) {
                            return false;
                        }
                    }

                    int interactAttemptTemp = interactAttempt;
                    Reset(); // Reset to try again? Or fail?
                    failedInteract = true;
                    interactAttempt = interactAttemptTemp;

                    keyboard.TypeKey(VK_HOME);
                    keyboard.TypeKey(VK_HOME);
                    keyboard.TypeKey(VK_HOME);
                    keyboard.TypeKey(VK_HOME);
                    keyboard.TypeKey(VK_END);
                    keyboard.TypeKey(VK_END);
                    keyboard.TypeKey(VK_END);
                    keyboard.TypeKey(VK_END);

                    // Move forward for 1 second
                    keyboard.SendKey('W', 0, true);
                    Sleep(100);
                    keyboard.SendKey(VK_SPACE, 0, true);
                    Sleep(200);
                    keyboard.SendKey(VK_SPACE, 0, false);
                    Sleep(700);
                    keyboard.SendKey('W', 0, false);

                    g_LogFile << "Player Moved" << std::endl;

                    return false;
                //}
                //else {
                //    return false;
                //}
            }

            // Move Mouse Logic
            {
                POINT p = searchOffsets[offsetIndex];
                ClientToScreen(hGameWindow, &p);

                if (camera.WorldToScreen(targetPos, sx, sy)) {
                    g_LogFile << sx << " " << p.x << " " << sy << " " << p.y << std::endl;
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

                uint32_t low_counter, type_field, instance, id, map_id, server_id;
                GUIDBreakdown(low_counter, type_field, instance, id, map_id, server_id, currentLow, currentHigh);                
                if ((targetGuidLow == 0) && (targetGuidHigh == 0) && (targetId != -1) && (id == targetId)) {
                    currentState = STATE_CLICK;
                }
                else if ((targetGuidLow != 0) && (targetGuidHigh != 0) && (currentLow == targetGuidLow && currentHigh == targetGuidHigh)) {
                    currentState = STATE_CLICK;
                }
                else {
                    offsetIndex++; // Try next offset
                    currentState = STATE_SCAN_MOUSE;
                    //currentState = STATE_CLICK;
                    //randomClick = true;
                }
            }
            return false;

        case STATE_CLICK:
            if (pilot.GetSteering()) {
                mouse.ReleaseButton(MOUSE_RIGHT);
                pilot.ChangeSteering(false);
            }

            // If a UI frame opened between alignment and click, close it and
            // redo alignment rather than clicking through the UI.
            if (g_GameState->globalState.uiBlocking) {
                g_LogFile << "[Interact] UI blocking click — closing frames and retrying." << std::endl;
                inputCommand.SendDataRobust(L"/run CloseAllWindows()", g_GameState->globalState.chatOpen);
                windowsCleaned = false;
                offsetIndex = 0;
                currentState = STATE_ALIGN_CAMERA;
                return false;
            }

            g_LogFile << "Mouse Click " << click << std::endl;
            mouse.Click(click);

            stateTimer = GetTickCount();
            if (randomClick) {
                currentState = STATE_SCAN_MOUSE;
                randomClick = false;
            }
            else currentState = STATE_POST_INTERACT_WAIT;
            return false;

        case STATE_POST_INTERACT_WAIT:
            if (GetTickCount() - stateTimer >= postClickWaitMs) {
                if (approachDist == interactDist) currentState = STATE_COMPLETE;
                else currentState = STATE_APPROACH_POST_INTERACT;
            }
            return false;

        case STATE_APPROACH_POST_INTERACT:
            if (MoveToTargetLogic(targetPos, currentPath, pathIndex, player, stateTimer, approachDist, interactDist, finalDist, fly_entry_state, mountDisable)) {
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
    bool MoveToTargetLogic(Vector3 targetPos, std::vector<PathNode>& path, int& index, PlayerInfo& player, DWORD& timer, float approachDist, float interactDist,
        float finalDist, bool fly, bool mountDisable = false) {
        static const std::wstring DISMOUNT_CMD = L"/run if IsMounted() then Dismount()end";
        if (path.empty()) {
            return true; // Treat as "Arrived" to prevent crash
        }
        //if ((index >= path.size()) || (path.back().pos.Dist3D(player.position) < finalDist) || (targetPos.Dist3D(player.position) < finalDist)) {
        if ((index >= path.size()) || ((path.back().pos.Dist2D(player.position) < 0.5f) && (fabs(path.back().pos.z - player.position.z) < 10.0f)) || ((targetPos.Dist2D(player.position) < 0.5f) && (fabs(targetPos.z - player.position.z) < 10.0f))) {
            pilot.Stop();
            g_LogFile << "Reached Goal " << index << " " << path.back().pos.Dist3D(player.position) << " " << targetPos.Dist3D(player.position) << " " << finalDist << std::endl;
            // Dismount Logic
            if (player.flyingMounted || player.groundMounted || player.inWater) {
                if (player.inWater) {
                    if (timer != 0) {
                        if (GetTickCount() - timer > 1500) {
                            keyboard.SendKey('X', 0, false);
                            if (GetTickCount() - timer > 1700) {
                                if (inputCommand.SendDataRobust(DISMOUNT_CMD, g_GameState->globalState.chatOpen)) {
                                    inputCommand.Reset();
                                    return true;
                                }
                            }
                        }
                        return false;
                    }

                    if (timer == 0) { timer = GetTickCount(); keyboard.SendKey('X', 0, true); }
                    return false;
                }
                //else if (path.back().pos.Dist3D(player.position) < (finalDist + 1.0f) || targetPos.Dist3D(player.position) < (finalDist + 1.0f)) {
                else if (inputCommand.SendDataRobust(DISMOUNT_CMD, g_GameState->globalState.chatOpen)) {
                    inputCommand.Reset();
                    return true;
                }
                //    return false;
                //}
                else {
                    index--;
                }
            }
            else {
                inputCommand.Reset();
                return true;
            }
            return false;
        }

        PathNode& node = path[index];
        Vector3 wp = node.pos;
        int wpType = node.type;

        float dist;
        // Ground Nodes: 2D Check (Ignore Z) to prevent getting stuck on points slightly below ground
        // Air Nodes: 3D Check
        if (wpType == PATH_GROUND) {
            dist = player.position.Dist2D(wp);
        }
        else {
            dist = player.position.Dist3D(wp);
        }

        bool transitionPoint = false;

        //if (wpType == PATH_GROUND && dist > )
        if (index == (path.size() - 1)) {
            if ((wpType == PATH_AIR && dist < finalDist) || (wpType == PATH_GROUND && dist < finalDist)) {
                index++;
                return false;
            }
        }
        else if ((index < path.size() - 2) && (wpType != path[index + 1].type) && (dist < 1.0f)) {
            index++;
            return false;
        }
        else if (player.position.Dist3D(targetPos) < finalDist) {
            index = path.size();
            return false;
        }
        else if ((wpType == PATH_AIR && dist < interactDist) || (wpType == PATH_GROUND && dist < interactDist)) {
            index++;
            return false;
        }

        float goalDist = 0.0f;
        if (!player.groundMounted && wpType == PATH_GROUND) {
            goalDist += player.position.Dist3D(path[index].pos);
            for (int k = index; k + 1 < path.size(); k++) {
                if (path[k].type == PATH_GROUND) {
                    goalDist += path[k].pos.Dist3D(path[k + 1].pos);
                }
                else break;
            }
        }
        else if (!player.flyingMounted && wpType == PATH_AIR) {
            goalDist += player.position.Dist3D(path[index].pos);
            for (int k = index; k + 1 < path.size(); k++) {
                if (path[k].type == PATH_AIR) {
                    goalDist += path[k].pos.Dist3D(path[k + 1].pos);
                }
                else break;
            }
        }
        else {
            goalDist = 10000.0f;
        }

        //g_LogFile << wp.z << " " << wpType << " " << goalDist << std::endl;
        pilot.SteerTowards(player.position, player.rotation, wp, wpType, player, goalDist, mountDisable);
        return false;
    }

    // Helper: Logic to rotate camera
    bool AlignCameraLogic(Vector3 targetPos) {
        
        Vector3 camFwd = camera.GetForward();
        Vector3 camPos = camera.GetPosition();

        // 1. Calculate Yaw Angles (Top-Down view)
        // atan2(y, x) gives angle from X-axis in radians
        float camYaw = std::atan2(camFwd.y, camFwd.x);
        Vector3 toTarget = targetPos - camPos;
        float targetYaw = std::atan2(toTarget.y, toTarget.x);

        // 2. Calculate Difference
        float diff = targetYaw - camYaw;

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

// Executes the next action in the profile queue
class ActionProfileExecutor : public GoapAction {
private:
    InteractionController& interact;

public:
    ActionProfileExecutor(InteractionController& ic) : interact(ic) {}

    // Priority 10: Runs when nothing more important (Combat/Loot) is happening
    int GetPriority() override { return 10; }

    std::string GetName() override {
        auto* profile = g_ProfileLoader.GetActiveProfile();
        if (profile && !profile->taskQueue.empty()) {
            return "Profile: " + profile->taskQueue.front()->GetName();
        }
        return "Profile: Idle";
    }

    ActionState* GetState(WorldState& ws) override { return nullptr; }

    bool CanExecute(const WorldState& ws) override {
        auto* profile = g_ProfileLoader.GetActiveProfile();
        return (profile && !profile->taskQueue.empty());
    }

    // If Combat/Loot takes over, this function runs. We notify the task it was interrupted.
    void ResetState() override {
        auto* profile = g_ProfileLoader.GetActiveProfile();
        if (profile && !profile->taskQueue.empty()) {
            profile->taskQueue.front()->OnInterrupt();
        }
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        auto* profile = g_ProfileLoader.GetActiveProfile();
        if (!profile || profile->taskQueue.empty()) return true;

        // 1. Get the current command object
        auto currentTask = profile->taskQueue.front();

        // 2. Run it! 
        // The task returns TRUE if it is finished.
        if (currentTask->Execute(&ws)) {
            // Task is done, remove it.
            g_LogFile << "[Profile] Finished: " << currentTask->GetName() << std::endl;
            profile->taskQueue.pop_front();

            // Return false so we immediately pick up the next task in the next tick
            return false;
        }

        // Task is still running
        return false;
    }
};

class ActionInteract : public GoapAction {
private:
    InteractionController& interact;
    SimpleKeyboardClient& keyboard;
    SimpleMouseClient& mouse;
    ConsoleInput input; // Used to send the Lua command

    DWORD interactPause = 0;

    bool failedPath = false;
    // Track the last ID to detect overrides
    int lastInteractId = -1;
    bool reloadedGame = 0;

public:
    ActionInteract(InteractionController& ic, SimpleKeyboardClient& k, SimpleMouseClient& m)
        : interact(ic), keyboard(k), mouse(m), input(k) {
    }

    // Priority 150: Higher than Gathering(100)/Looting(60), Lower than Combat(200)
    // If equipment is broken, we should prioritize this over farming, but defend ourselves if attacked.
    int GetPriority() override { return 50; }

    std::string GetName() override { return "Interact With Target"; }

    ActionState* GetState(WorldState& ws) override { return &ws.interactState; }

    bool CanExecute(const WorldState& ws) override {
        // Execute if enabled AND we actually need repairs
        return ws.interactState.enabled && ws.interactState.interactActive;
    }

    void ResetState() override {
        interact.Reset();
        g_GameState->interactState.path = {};
        g_GameState->interactState.index = 0;
        g_GameState->interactState.repairDone = false;
        g_GameState->interactState.vendorDone = false;
        g_GameState->interactState.locationChangeTime = -1;
        g_GameState->interactState.locationChange = false;
        reloadedGame = 0;
        lastInteractId = -1;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        static const std::wstring CMD_GOSSIP_REPAIR = L"/run local o=C_GossipInfo.GetOptions() if o then for _,v in ipairs(o) do if v.icon==132060 then C_GossipInfo.SelectOption(v.gossipOptionID) return end end end";
        static const std::wstring CMD_REPAIR_ALL    = L"/run RepairAllItems()";
        static const std::wstring CMD_CLOSE_MAIL    = L"/run ToggleBackpack() CloseMail()";
        static const std::wstring CMD_CLOSE_MERCHANT = L"/run CloseMerchant()";
        static const std::wstring CMD_RELOAD        = L"/reload";
        bool failedInteract;
        // Check for Override
        // If the ID in the global state is different from what we processed last tick,
        // it means dllmain switched targets on us. Force a hard reset.
        if (ws.interactState.interactId != lastInteractId) {
            g_LogFile << "[ActionInteract] Target changed! Resetting controller." << std::endl;
            interact.Reset();
            ws.interactState.path.clear(); // Ensure path is empty so CalculatePath triggers
            lastInteractId = ws.interactState.interactId;
        }
        
        // 1. Move to NPC and Interact
        // approachDist: 3.0f, interactDist: 5.0f
        // postClickWaitMs: 2500 (Give the vendor window time to open)
        bool complete = interact.EngageTarget(
            ws.interactState.location,
            ws.interactState.targetGuidLow,
            ws.interactState.targetGuidHigh,
            ws.player,
            ws.interactState.path,
            ws.interactState.index,
            ws.interactState.mapId,
            5.0f,  // Approach Distance
            3.0f,  // Interact Distance
            3.0f,  // Final Distance
            true,  // Check Target
            true,  // Check GUID
            ws.interactState.flyingPath, // Fly if applicable
            1000,  // Wait 2.5s for window to open
            MOUSE_RIGHT,
            g_GameState->interactState.locationChange, // NPC is likely stationary
            failedPath,
            ws.interactState.interactId,
            failedInteract,
            false,
            1500,
            true,
            g_GameState->interactState.inGameLocation,
            true,
            false
        );

        if (failedPath) {
            ws.stuckState.isStuck = true;
            failedPath = false;
        }

        // Visualization
        ws.globalState.activePath = ws.interactState.path;
        ws.globalState.activeIndex = ws.interactState.index;

        // 2. Perform Repair Logic
        if (complete) {

            if (ws.interactState.repair && !ws.interactState.repairDone) {
                // Send Lua command to repair all items
                // "RepairAllItems()" is the standard WoW API, passed via ConsoleInput
                input.SendDataRobust(CMD_GOSSIP_REPAIR, g_GameState->globalState.chatOpen);
                Sleep(20);
                input.SendDataRobust(CMD_REPAIR_ALL, g_GameState->globalState.chatOpen);
                ws.interactState.repairDone = true;
                interactPause = GetTickCount();
            }
            if (ws.interactState.vendorSell && !ws.interactState.vendorDone) {
                // Selects the vendor shop gossip action
                input.SendDataRobust(CMD_GOSSIP_REPAIR, g_GameState->globalState.chatOpen);
                ws.interactState.vendorDone = true;
                interactPause = GetTickCount();
            }
            if (ws.interactState.mailing) {
                if (!reloadedGame) {
                    Sleep(5000);
                    input.SendDataRobust(CMD_RELOAD, g_GameState->globalState.chatOpen);
                    g_GameState->globalState.reloaded = true;
                    reloadedGame = 1;
                    interact.Reset();
                    Sleep(10000);
                    return false;
                }
                if (PerformMailing(mouse)) {
                    Sleep(100);
                    input.SendDataRobust(CMD_CLOSE_MAIL, g_GameState->globalState.chatOpen);
                    g_GameState->interactState.targetGuidHigh = 0;
                    g_GameState->interactState.targetGuidLow = 0;
                    g_GameState->interactState.mailing = false;
                    g_GameState->interactState.interactActive = false;
                    ResetState();
                    return true;
                }
            }

            // Wait a moment for the transaction (optional, helps prevent instant state flip)
            if ((GetTickCount() - interactPause > 2500) && ((ws.interactState.vendorSell && ws.interactState.sellComplete) || !ws.interactState.vendorSell)) {
                if (ws.globalState.vendorOpen) {
                    input.SendDataRobust(CMD_CLOSE_MERCHANT, g_GameState->globalState.chatOpen);
                }
                else {
                    g_GameState->globalState.bagEmptyTime = GetTickCount();
                    ResetState();
                    failedPath = false;
                    g_GameState->interactState.targetGuidHigh = 0;
                    g_GameState->interactState.targetGuidLow = 0;
                    g_GameState->interactState.repair = false;
                    g_GameState->interactState.vendorSell = false;
                    g_GameState->interactState.resupply = false;
                    g_GameState->interactState.mailing = false;
                    g_GameState->interactState.interactActive = false;
                    // Close the window (Optional: prevents clutter)
                    // input.SendDataRobust(std::wstring(L"/run CloseGossip() CloseMerchant()"), g_GameState->globalState.chatOpen);
                    return true;
                }

            }
        }

        return false;
    }
};

class ActionUnstuck : public GoapAction {
private:
    SimpleKeyboardClient& keyboard;
    MovementController& pilot;

    DWORD actionStartTime  = 0;
    bool  turningPhase     = true;   // true = still rotating to escape angle
    DWORD movingStartTime  = 0;      // when we started moving after the turn
    DWORD turningStartTime = 0;      // when the current turn phase started (for timeout)

    static constexpr float PI = 3.14159265f;

    // Returns a point 20 yards away from the player in the direction
    // (stuckAngle + offset), used as a faceTarget destination.
    Vector3 EscapeTarget(const WorldState& ws, float angleOffset) const {
        float angle = ws.stuckState.stuckAngle + angleOffset;
        return ws.player.position + Vector3(std::cos(angle), std::sin(angle), 0.0f) * 20.0f;
    }

    // Turn to escapeTarget, then move forward for moveDuration ms.
    // Returns true when the movement phase is complete.
    bool TurnThenMove(WorldState& ws, Vector3 escapeTarget, DWORD moveDuration, bool jumpWhileMoving = false) {
        DWORD now = GetTickCount();
        if (turningPhase) {
            if (turningStartTime == 0) turningStartTime = now;

            // Proceed when faceTarget aligns OR the turn has taken longer than 3s
            // (e.g. game window lost mouse focus and rotation stalled).
            bool aligned  = pilot.faceTarget(ws.player.position, escapeTarget, ws.player.rotation);
            bool timedOut = (now - turningStartTime) > 3000;
            if (aligned || timedOut) {
                if (timedOut && !aligned)
                    g_LogFile << "[UNSTUCK] Turn timed out — proceeding to move phase." << std::endl;
                pilot.Stop(); // release right-mouse before switching to W key
                turningPhase     = false;
                turningStartTime = 0;
                movingStartTime  = now;
            }
            return false;
        }
        DWORD moveElapsed = now - movingStartTime;
        if (moveElapsed < moveDuration) {
            keyboard.SendKey('W', 0, true);
            if (jumpWhileMoving) {
                bool jumpWindow = (moveElapsed % 600) < 300;
                keyboard.SendKey(VK_SPACE, 0, jumpWindow);
            }
            return false;
        }
        keyboard.SendKey('W', 0, false);
        keyboard.SendKey(VK_SPACE, 0, false);
        return true;
    }

public:
    ActionUnstuck(SimpleKeyboardClient& k, MovementController& p)
        : keyboard(k), pilot(p) {}

    int GetPriority() override { return 10000; }
    std::string GetName() override { return "Unstuck Maneuver"; }
    ActionState* GetState(WorldState& ws) override { return &ws.stuckState; }

    bool CanExecute(const WorldState& ws) override {
        return ws.stuckState.isStuck;
    }

    void ResetState() override {
        actionStartTime  = 0;
        turningPhase     = true;
        movingStartTime  = 0;
        turningStartTime = 0;
        g_GameState->stuckState.attemptCount++;
        g_GameState->stuckState.isStuck        = false;
        g_GameState->stuckState.lastUnstuckTime = GetTickCount();
        g_GameState->waypointReturnState.waitingForUnstuck = false;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        DWORD now = GetTickCount();

        if (actionStartTime == 0) {
            actionStartTime  = now;
            turningPhase     = true;
            movingStartTime  = 0;
            turningStartTime = 0;
            if (ws.stuckState.lastUnstuckTime > 0 && (now - ws.stuckState.lastUnstuckTime > 120000)) {
                g_LogFile << "[UNSTUCK] 2+ minutes since last stuck. Resetting stages." << std::endl;
                ws.stuckState.attemptCount = 0;
            }
            g_LogFile << "[UNSTUCK] Stage " << ws.stuckState.attemptCount
                      << " | obstacle angle: " << ws.stuckState.stuckAngle << " rad" << std::endl;
        }

        DWORD elapsed   = now - actionStartTime;
        bool  stageComplete = false;

        switch (ws.stuckState.attemptCount) {

        // Stage 0: Jump forward — clears small ledges and floor bumps
        case 0:
            if (elapsed < 600) {
                keyboard.SendKey('W', 0, true);
                keyboard.SendKey(VK_SPACE, 0, elapsed > 100 && elapsed < 400);
            } else {
                keyboard.SendKey('W', 0, false);
                keyboard.SendKey(VK_SPACE, 0, false);
                stageComplete = true;
            }
            break;

        // Stage 1: Face 90° left of blocked direction, move forward
        case 1:
            stageComplete = TurnThenMove(ws, EscapeTarget(ws, PI / 2.0f), 1500);
            break;

        // Stage 2: Face 90° right of blocked direction, move forward
        case 2:
            stageComplete = TurnThenMove(ws, EscapeTarget(ws, -PI / 2.0f), 1500);
            break;

        // Stage 3: Back up (no turn needed), then force path recalculation
        case 3:
            if (elapsed < 1000) {
                keyboard.SendKey('S', 0, true);
            } else {
                keyboard.SendKey('S', 0, false);
                stageComplete = true;
            }
            break;

        // Stage 4: 45° left + jump
        case 4:
            stageComplete = TurnThenMove(ws, EscapeTarget(ws, PI / 4.0f), 1000, true);
            break;

        // Stage 5: 45° right + jump
        case 5:
            stageComplete = TurnThenMove(ws, EscapeTarget(ws, -PI / 4.0f), 1000, true);
            break;

        // Stage 6: Ascend if flying; otherwise repeated jumps forward
        case 6:
            if (ws.player.isFlying || ws.player.flyingMounted) {
                if (elapsed < 2500) {
                    keyboard.SendKey(VK_SPACE, 0, true);
                } else {
                    keyboard.SendKey(VK_SPACE, 0, false);
                    stageComplete = true;
                }
            } else {
                stageComplete = TurnThenMove(ws, EscapeTarget(ws, 0.0f), 1500, true);
            }
            break;

        // Stage 7: Full 180° — run directly away from the obstacle
        case 7:
            stageComplete = TurnThenMove(ws, EscapeTarget(ws, PI), 2000);
            break;

        default:
            ws.stuckState.attemptCount = 8;
            stageComplete = true;
            break;
        }

        if (stageComplete) {
            pilot.Stop();

            // After unstuck, seek to the closest node in the existing calculated path.
            // Do NOT clear the path — it is a fixed navmesh path from the profile and
            // should only be calculated once. Clearing it forces an expensive recalculation
            // every time the bot gets stuck.
            if (!ws.pathFollowState.path.empty()) {
                std::vector<Vector3> empty = {};
                ws.pathFollowState.index = FindClosestWaypoint(empty, ws.pathFollowState.path, ws.player.position);
                g_LogFile << "[UNSTUCK] Resumed path at closest node: " << ws.pathFollowState.index << std::endl;
            }

            ResetState();
            return true;
        }

        return false;
    }
};

// --- CONCRETE ACTION: ESCAPE DANGER ---
// Triggered when a hostile enemy significantly above the player's level is nearby.
// Finds a safe destination away from all threats, marks navmesh polygons inside every
// threat's aggro radius with AREA_DANGER, pathfinds around those polys, then restores them.
class ActionEscapeDanger : public GoapAction {
private:
    const int   FLEE_LEVEL_DIFF   = 5;     // Flee if enemy is this many levels above player
    const float FLEE_DETECT_RANGE = 40.0f; // Only react to threats within this range (yards)
    const float FLEE_DEST_DIST    = 50.0f; // How far ahead to project the flee destination
    const float SAFE_MARGIN       = 10.0f; // Extra buffer added beyond enemy aggro range

    std::vector<PathNode> fleePath;
    int fleeIndex = 0;

    struct Threat {
        Vector3 position;
        float   dangerRadius; // aggroRange + SAFE_MARGIN
    };

    // Returns all hostile enemies FLEE_LEVEL_DIFF+ levels above the player within their aggro range.
    std::vector<Threat> GetThreats(const WorldState& ws) const {
        std::vector<Threat> threats;
        for (const auto& ent : ws.entities) {
            if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                if (npc->reaction != 0) continue;                          // not hostile
                if (npc->npcFlag != 0) continue;                           // vendor/repair/trainer — never a threat
                if (npc->level <= 0 || npc->level > 95) continue;         // sanity: filter garbage reads
                if ((npc->level - ws.player.level) < FLEE_LEVEL_DIFF) continue;
                float detectRange = (std::max)(npc->agroRange, 5.0f) + 5.0f;
                if (ws.player.position.Dist2D(npc->position) > detectRange) continue;
                float radius = (std::max)(npc->agroRange, 15.0f) + SAFE_MARGIN;
                threats.push_back({ npc->position, radius });
            }
        }
        return threats;
    }

    bool IsPositionSafe(const Vector3& pos, const std::vector<Threat>& threats) const {
        for (const auto& t : threats) {
            if (pos.Dist2D(t.position) < t.dangerRadius) return false;
        }
        return true;
    }

    // Projects a flee destination away from the threat centroid.
    // Tries 8 angle offsets, picks the first candidate that is outside all danger zones.
    // Falls back to the raw opposite direction if all candidates are still inside a zone.
    Vector3 FindFleeDestination(const WorldState& ws, const std::vector<Threat>& threats) const {
        // Average threat direction (from player toward threat centroid)
        float sumX = 0, sumY = 0;
        for (const auto& t : threats) {
            float dx = t.position.x - ws.player.position.x;
            float dy = t.position.y - ws.player.position.y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 0.01f) { sumX += dx / len; sumY += dy / len; }
        }
        float fleeAngle = atan2f(-sumY, -sumX); // opposite of threat centroid

        // Try 8 angles: straight away, then ±45°, ±90°, ±135°, 180°
        const float offsets[] = { 0.0f, 0.785f, -0.785f, 1.571f, -1.571f, 2.356f, -2.356f, 3.14159f };
        for (float offset : offsets) {
            float angle = fleeAngle + offset;
            Vector3 candidate = {
                ws.player.position.x + cosf(angle) * FLEE_DEST_DIST,
                ws.player.position.y + sinf(angle) * FLEE_DEST_DIST,
                ws.player.position.z
            };
            if (IsPositionSafe(candidate, threats)) return candidate;
        }

        // All candidates still inside a zone — just go straight away
        return {
            ws.player.position.x + cosf(fleeAngle) * FLEE_DEST_DIST,
            ws.player.position.y + sinf(fleeAngle) * FLEE_DEST_DIST,
            ws.player.position.z
        };
    }

public:
    bool CanExecute(const WorldState& ws) override {
        if (ws.player.isDead || ws.player.isGhost) return false;
        for (const auto& ent : ws.entities) {
            if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                if (npc->reaction != 0) continue;
                if (npc->npcFlag != 0) continue;   // vendor/repair/trainer — never a threat
                if (npc->level <= 0 || npc->level > 95) continue; // sanity: filter garbage reads (WoW 5.4.8 max ~93)
                if ((npc->level - ws.player.level) < FLEE_LEVEL_DIFF) continue;

                // Actively attacking the player → always flee
                if (npc->inCombat &&
                    npc->targetGuidLow  == ws.player.playerGuidLow &&
                    npc->targetGuidHigh == ws.player.playerGuidHigh)
                    return true;

                // Within this mob's actual aggro radius → flee preemptively
                float detectRange = (std::max)(npc->agroRange, 5.0f) + 5.0f;
                if (ws.player.position.Dist2D(npc->position) <= detectRange) return true;
            }
        }
        return false;
    }

    int GetPriority() override { return 500; }
    std::string GetName() override { return "Escape Danger"; }
    ActionState* GetState(WorldState& ws) override { return &ws.fleeState; }

    void ResetState() override {
        fleePath.clear();
        fleeIndex = 0;
        g_GameState->fleeState.fleeActive  = false;
        g_GameState->fleeState.activePath  = {};
        g_GameState->fleeState.activeIndex = 0;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        auto threats = GetThreats(ws);

        if (threats.empty()) {
            g_LogFile << "[Flee] Threat gone. Flee complete." << std::endl;
            return true;
        }
        if (IsPositionSafe(ws.player.position, threats)) {
            g_LogFile << "[Flee] Reached safe distance. Flee complete." << std::endl;
            return true;
        }

        ws.fleeState.fleeActive = true;

        // (Re)calculate flee path when we don't have one or have exhausted it
        if (fleePath.empty() || fleeIndex >= (int)fleePath.size()) {
            Vector3 dest = FindFleeDestination(ws, threats);
            ws.fleeState.destination = dest;

            // Mark all danger-zone polygons so the pathfinder routes around them
            std::vector<std::pair<dtPolyRef, unsigned short>> savedFlags;
            for (const auto& t : threats) {
                globalNavMesh.MarkPolysInRadius(t.position, t.dangerRadius, AREA_DANGER, savedFlags);
            }

            fleePath = FindPath(ws.player.position, dest, false, true, true, 5.0f, AREA_DANGER);
            fleeIndex = 0;

            // Restore poly flags immediately — danger marking must not persist
            globalNavMesh.RestorePolyFlags(savedFlags);

            if (fleePath.empty()) {
                g_LogFile << "[Flee] Navmesh path failed. Steering directly away." << std::endl;
                pilot.SteerTowards(ws.player.position, ws.player.rotation,
                    dest, false, ws.player, FLEE_DEST_DIST);
                return false;
            }
            g_LogFile << "[Flee] Path calculated (" << fleePath.size() << " nodes) toward "
                      << dest.x << ", " << dest.y << std::endl;
        }

        // Update overlay path
        ws.fleeState.activePath  = fleePath;
        ws.fleeState.activeIndex = fleeIndex;

        // Advance to next waypoint when close enough
        Vector3 waypoint = fleePath[fleeIndex].pos;
        if (ws.player.position.Dist2D(waypoint) < 3.0f) {
            fleeIndex++;
            return false;
        }

        float goalDist = 0;
        for (int k = fleeIndex; k < (int)fleePath.size() - 1; k++) {
            goalDist += fleePath[k].pos.Dist3D(fleePath[k + 1].pos);
        }
        pilot.SteerTowards(ws.player.position, ws.player.rotation,
            waypoint, false, ws.player, goalDist);
        return false;
    }
};

class ActionRespawn : public GoapAction {
public:
    // Priority 10000: Highest priority. If dead, nothing else matters.
    int GetPriority() override { return 1000; }

    std::string GetName() override { return "Respawn"; }

    ActionState* GetState(WorldState& ws) override {
        return &ws.respawnState;
    }

    bool CanExecute(const WorldState& ws) override {
        // Run this action if the player is dead or a ghost
        return ws.respawnState.isDead;
    }

    void ResetState() override {
        // Reset the search logic when the action starts/stops
        g_GameState->respawnState.possibleZLayers.clear();
        g_GameState->respawnState.currentLayerIndex = -1;
        g_GameState->respawnState.isPathingToCorpse = false;
        g_GameState->respawnState.currentTargetPos = { 0, 0, 0 };
        g_GameState->respawnState.path = {};
        g_GameState->respawnState.index = 0;
        g_GameState->respawnState.hasPath = false;
        g_GameState->respawnState.isPathingToCorpse = false;
        g_GameState->respawnState.isDead = false;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        // 1. Completion Check: If we are alive, we are done.
        if (!ws.player.isDead && !ws.player.isGhost) {
            ResetState();
            return true; // Action Complete
        }

        // 2. Release Spirit (if not yet a ghost)
        if (!ws.player.isGhost) {
            pilot.ExecuteLua(L"/run RepopMe()");
            Sleep(1000);
            return false;
        }

        // 3. Attempt Resurrection (if in range)
        if (ws.player.canRespawn) {
            pilot.Stop();
            pilot.ExecuteLua(L"/run RetrieveCorpse()");
            Sleep(1000);
            return false;
        }

        // 4. Initialize Z-Layer Search (Run once when we first start pathing)
        if (ws.respawnState.possibleZLayers.empty()) {
            float cx = ws.player.corpseX;
            float cy = ws.player.corpseY;
            g_LogFile << cx << " " << cy << std::endl;

            // Use the helper from Pathfinding2.h
            std::vector<float> layers = GetPossibleZLayers(ws.player.mapId, cx, cy);

            if (layers.empty()) {
                g_LogFile << "[Respawn] No valid ground found at corpse location! Using player Z." << std::endl;
                layers.push_back(ws.player.position.z);
            }

            // Sort layers by vertical distance to our CURRENT position
            std::sort(layers.begin(), layers.end(), [&](float a, float b) {
                return std::abs(a - ws.player.position.z) < std::abs(b - ws.player.position.z);
                });

            ws.respawnState.possibleZLayers = layers;
            ws.respawnState.currentLayerIndex = 0;
            ws.respawnState.isPathingToCorpse = false;

            g_LogFile << "[Respawn] Found " << layers.size() << " possible layers." << std::endl;
        }

        // 5. Navigation Logic
        if (!ws.respawnState.isPathingToCorpse) {

            // --- FAIL CONDITION: Exhausted all layers ---
            if (ws.respawnState.currentLayerIndex >= ws.respawnState.possibleZLayers.size()) {
                g_LogFile << "[Respawn] CRITICAL: Exhausted all Z-layers. Corpse unreachable." << std::endl;
                EndScript(pilot, 3); // Fail Code 3
                return false;
            }

            // Pick the next target Z
            float targetZ = ws.respawnState.possibleZLayers[ws.respawnState.currentLayerIndex];
            Vector3 targetPos = { ws.player.corpseX, ws.player.corpseY, targetZ };

            ws.respawnState.currentTargetPos = targetPos;

            // Use a flying path if the ghost has a flying mount, fall back to ground if it fails.
            bool ghostCanFly = ws.player.flyingMounted;
            std::vector<Vector3> singlePoint = { targetPos };

            std::vector<PathNode> path = CalculatePath(
                singlePoint,
                ws.player.position,
                0,
                ghostCanFly,
                ws.player.corpseMapId,
                ghostCanFly,
                false,
                false,
                20.0f,
                false,
                20.0f,
                false,
                false
            );

            if (path.empty() && ghostCanFly) {
                g_LogFile << "[Respawn] Flying path failed, retrying with ground path." << std::endl;
                ghostCanFly = false;
                path = CalculatePath(
                    singlePoint,
                    ws.player.position,
                    0,
                    false,
                    ws.player.corpseMapId,
                    false,
                    false,
                    false,
                    20.0f,
                    false,
                    20.0f,
                    false,
                    false
                );
            }

            if (!path.empty()) {
                ws.respawnState.path = path;
                ws.respawnState.index = 0;
                ws.respawnState.hasPath = true;
                ws.respawnState.isPathingToCorpse = true;

                g_LogFile << "[Respawn] Pathing to Layer " << (ws.respawnState.currentLayerIndex + 1)
                    << "/" << ws.respawnState.possibleZLayers.size()
                    << " (Z=" << targetZ << ")" << std::endl;
            }
            else {
                g_LogFile << "[Respawn] Failed to path to Z=" << targetZ << ". Skipping layer." << std::endl;
                ws.respawnState.currentLayerIndex++;
            }
        }
        else {
            // We are actively pathing...
            // Visualize path (optional)
            ws.globalState.activePath = ws.respawnState.path;
            ws.globalState.activeIndex = ws.respawnState.index;

            float distToTarget = ws.player.position.Dist3D(ws.respawnState.currentTargetPos);

            // Check if we reached the destination BUT didn't trigger the 'canRespawn' check above
            bool pathFinished = (ws.respawnState.index >= ws.respawnState.path.size());

            if (pathFinished || distToTarget < 5.0f) {
                // If we are here, Step 3 (Resurrection) didn't return true, meaning we are NOT in range.
                // This implies this Z-layer was wrong.
                if (!ws.player.canRespawn) {
                    g_LogFile << "[Respawn] Arrived at Z=" << ws.respawnState.currentTargetPos.z << " but cannot res. Trying next layer." << std::endl;
                    pilot.Stop();
                    ws.respawnState.isPathingToCorpse = false; // Force re-path
                    ws.respawnState.currentLayerIndex++;
                }
            }

            // Continue steering if path not finished.
            // If mount state changed (ghost mount appeared/disappeared), force a repath.
            bool ghostCanFlyNow = ws.player.flyingMounted;
            bool pathIsFlyingPath = !ws.respawnState.path.empty() && ws.respawnState.path[0].type == PATH_AIR;
            if (ghostCanFlyNow != pathIsFlyingPath) {
                ws.respawnState.isPathingToCorpse = false; // trigger repath with correct type
            }
            else if (!pathFinished) {
                PathNode& targetNode = ws.respawnState.path[ws.respawnState.index];
                if (ws.player.position.Dist2D(targetNode.pos) < 1.0f) {
                    ws.respawnState.index++;
                }
                else {
                    pilot.SteerTowards(ws.player.position, ws.player.rotation, targetNode.pos, ghostCanFlyNow, ws.player, 0.0f, true);
                }
            }
        }

        return false; // Continue executing until alive
    }
};

class ActionCombat : public GoapAction {
private:
    InteractionController& interact;
    CombatController& combatController;
    SimpleKeyboardClient& keyboard;
    bool targetSelected = false;

    // Config
    const float MELEE_RANGE = 4.0f;    // Max distance to stop and attack
    const float CHASE_RANGE = 30.0f;   // Max distance to start pathfinding
    const int REPATH_DELAY = 500;      // Ms between path recalculations (normal)
    const int REPATH_DELAY_FLEE = 200; // Ms between recalculations when target is fleeing
    const float REPATH_DRIFT = 4.0f;   // Force repath if target drifts this many yards from last calc
    const int   PATH_FAIL_LIMIT = 3;   // Give up after this many consecutive paths that end too far from enemy
    const float PATH_FAIL_DIST  = 15.0f; // Path endpoint must reach within this many yards of enemy

    // State
    bool failedPath = false;
    DWORD pathTimer = 0;       // When the last path was calculated
    DWORD moveTimer = 0;       // Separate timer for MoveToTargetLogic (dismount/water sequences)
    DWORD clickCooldown = 0;
    Vector3 lastPathTarget = { 0, 0, 0 }; // Enemy position when path was last calculated

    bool inRoutine = false;

    bool combatStuck = false;
    int32_t prevHealth = -1;
    DWORD healthCheck = 0;

    DWORD retargetTimer = 0;
    const int   RETARGET_INTERVAL  = 2000;  // ms between re-evaluations

    // Tracks how long we've been stuck in PHASE 1 AFTER previously being inRoutine.
    // Catches defeated-but-not-dead enemies (e.g. mechanical robots that go inactive).
    DWORD postCombatPhase1Timer = 0;

    // Counts consecutive EngageTarget interaction failures — kept for ResetState bookkeeping.
    int failedInteractCount = 0;

    // Pull-extra-mobs config / state
    const float EXTRA_MOB_RANGE = 20.0f; // yards from player to scan for extra targets
    DWORD extraPullTimer = 0;            // rate-limits extra-pull checks

    // Enemy position recorded when the current EngageTarget approach was started.
    // Used to detect target drift during PHASE 1 so the approach can be reset.
    Vector3 lastEngagePos = { 0, 0, 0 };

    // Counts consecutive CalculatePath calls whose endpoint lands too far from the enemy.
    // When this reaches PATH_FAIL_LIMIT the target is considered unreachable and combat ends.
    int pathFailCount = 0;

    const float RETARGET_THRESHOLD = 0.25f; // minimum score improvement required to switch

    // Scan for the nearest valid grind target and, if found, reset internal combat state
    // and redirect to that target without returning true to the GOAP loop.
    // Returns true if a new target was found and ActionCombat should continue.
    bool TryChainNextGrindTarget(WorldState& ws) {
        if (!ws.grindState.enabled) return false;
        if (!ws.grindState.inLoop && !ws.grindState.engageAlways) return false;

        int pLevel = ws.player.level;
        float bestDist = ws.grindState.pullRange;
        ULONG_PTR nextGuidLow = 0, nextGuidHigh = 0;
        Vector3 nextPos = {};

        for (const auto& ent : ws.entities) {
            if (ent.guidLow == ws.combatState.targetGuidLow &&
                ent.guidHigh == ws.combatState.targetGuidHigh) continue; // skip just-killed mob
            if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                if (!IsBasicAttackable(*npc, ws.entities, g_GameState->player.playerGuidLow)) continue;
                if (npc->reaction == 2) continue;
                if ((npc->unitFlags & 0x8000) && npc->reaction == 1) continue;
                if (npc->npcFlag != 0) continue;
                { bool avoided = false;
                  for (const auto& avoid : g_ProfileSettings.avoidMobs)
                      if ((uint32_t)avoid.Id == npc->id) { avoided = true; break; }
                  if (avoided) continue; }
                if (npc->inCombat && npc->targetGuidLow != 0 &&
                    (npc->targetGuidLow  != g_GameState->player.playerGuidLow ||
                     npc->targetGuidHigh != g_GameState->player.playerGuidHigh)) {
                    bool targetIsPlayer = false;
                    for (const auto& e : ws.entities) {
                        if (e.guidLow == npc->targetGuidLow && e.guidHigh == npc->targetGuidHigh) {
                            if (std::dynamic_pointer_cast<OtherPlayerInfo>(e.info)) targetIsPlayer = true;
                            break;
                        }
                    }
                    if (targetIsPlayer) continue;
                }
                if (!ws.grindState.killAllMobs && !ws.grindState.mobIds.empty()) {
                    bool idMatch = false;
                    for (int id : ws.grindState.mobIds)
                        if (npc->id == (uint32_t)id) { idMatch = true; break; }
                    if (!idMatch) continue;
                }
                if (npc->level > pLevel + ws.grindState.maxLevelMod) continue;
                if (npc->level < pLevel - ws.grindState.minLevelMod) continue;
                // Check blacklist
                bool blacklisted = false;
                for (size_t i = 0; i < ws.grindState.blacklistGuidLow.size(); ++i) {
                    if (ws.grindState.blacklistGuidLow[i] == ent.guidLow &&
                        ws.grindState.blacklistGuidHigh[i] == ent.guidHigh) { blacklisted = true; break; }
                }
                if (blacklisted) continue;
                // Only chain to mobs within pullRange of at least one hotspot.
                if (!ws.grindState.hotspots.empty()) {
                    bool nearHotspot = false;
                    for (const auto& hs : ws.grindState.hotspots) {
                        if (npc->position.Dist2D(hs) <= ws.grindState.pullRange) {
                            nearHotspot = true;
                            break;
                        }
                    }
                    if (!nearHotspot) continue;
                }
                float d = ws.player.position.Dist2D(npc->position);
                if (d < bestDist) {
                    bestDist = d;
                    nextGuidLow  = ent.guidLow;
                    nextGuidHigh = ent.guidHigh;
                    nextPos      = npc->position;
                }
            }
        }

        if (nextGuidLow == 0) return false;

        // Found next target — reset combat state and redirect without GOAP transition.
        g_LogFile << "[Combat] Chain-kill: next target at dist=" << bestDist << std::endl;
        combatController.ResetCombatState();
        interact.Reset();
        targetSelected = false;
        inRoutine      = false;
        failedPath     = false;
        clickCooldown  = 0;
        pathTimer      = 0;
        moveTimer      = 0;
        lastPathTarget    = { 0, 0, 0 };
        lastEngagePos     = { 0, 0, 0 };
        postCombatPhase1Timer = 0;
        failedInteractCount   = 0;
        pathFailCount         = 0;
        combatStuck    = false;
        prevHealth     = -1;
        healthCheck    = 0;
        g_GameState->combatState.path.clear();
        g_GameState->combatState.index      = 0;
        g_GameState->combatState.inCombat   = false;
        g_GameState->combatState.underAttack = false;
        g_GameState->combatState.hasTarget  = true;
        g_GameState->combatState.enemyPosition  = nextPos;
        g_GameState->combatState.targetGuidLow  = nextGuidLow;
        g_GameState->combatState.targetGuidHigh = nextGuidHigh;
        return true;
    }

    // Lower score = higher priority target.
    // Combines normalised distance (40%) and remaining health fraction (60%).
    float ScoreTarget(const EnemyInfo& npc, const WorldState& ws) const {
        float dist      = ws.player.position.Dist2D(npc.position);
        float healthPct = (npc.maxHealth > 0) ? (float)npc.health / npc.maxHealth : 1.0f;
        return (dist / CHASE_RANGE) * 0.4f + healthPct * 0.6f;
    }

public:
    ActionCombat(InteractionController& ic, CombatController& cc, SimpleKeyboardClient& kbd)
        : interact(ic), combatController(cc), keyboard(kbd) {
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
        g_GameState->combatState.path = {};
        g_GameState->combatState.index = 0;
        g_GameState->combatState.hasTarget = false;
        g_GameState->combatState.underAttack = false;
        g_GameState->combatState.inCombat = false;
        failedPath = false;
        inRoutine = false;
        g_GameState->combatState.reset = false;
        clickCooldown = 0;
        combatStuck = false;
        prevHealth = -1;
        healthCheck = 0;
        pathTimer = 0;
        moveTimer = 0;
        lastPathTarget = { 0, 0, 0 };
        retargetTimer = 0;
        postCombatPhase1Timer = 0;
        failedInteractCount = 0;
        lastEngagePos = { 0, 0, 0 };
        pathFailCount = 0;
        extraPullTimer = 0;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        bool lowHp = false;
        bool failedInteract = false;
        
        // --- LOGGING: Entry ---
        // Rate limit log to avoid spamming every single frame (e.g. every 500ms)
        static DWORD lastLog = 0;
        bool doLog = (GetTickCount() - lastLog > 500);
        if (doLog) lastLog = GetTickCount();

        // Logic to check target health
        if (ws.combatState.reset) {
            g_LogFile << "[Combat] Reset Requested. Exiting routine." << std::endl;
            ResetState();
            return true;
        }

        // --- 3. CRITICAL FIX: DYNAMIC POSITION UPDATE ---
        // Search for the target in the current entity list to get its LIVE position.
        // Do not rely on 'entityIndex' which can become stale.
        bool foundLiveTarget = false;
        for (const auto& ent : ws.entities) {
            if (ent.guidLow == ws.combatState.targetGuidLow && ent.guidHigh == ws.combatState.targetGuidHigh) {
                // Check Health
                if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                    // Only update position if the value from memory is valid (not NaN/Inf).
                    // Underground or despawning entities can briefly emit garbage coordinates.
                    if (!std::isnan(npc->position.x) && !std::isnan(npc->position.y) && !std::isnan(npc->position.z) &&
                        !std::isinf(npc->position.x) && !std::isinf(npc->position.y) && !std::isinf(npc->position.z)) {
                        ws.combatState.enemyPosition = npc->position;
                    }
                    if ((prevHealth == -1 || (GetTickCount() - healthCheck > 8000)) && inRoutine) {
                        if (prevHealth == npc->health) {
                            combatStuck = true;
                        }
                        prevHealth = npc->health;
                        healthCheck = GetTickCount();
                    }
                    if (npc->health <= 0) {
                        g_LogFile << "[Combat] Target Dead (Found in list)." << std::endl;
                        if (ws.grindState.lootMobs) {
                            //g_GameState->lootState.hasLoot  = true;
                            g_GameState->lootState.position = npc->position;
                            g_GameState->lootState.guidLow  = ws.combatState.targetGuidLow;
                            g_GameState->lootState.guidHigh = ws.combatState.targetGuidHigh;
                            g_GameState->lootState.mapId    = ws.player.mapId;
                            g_LogFile << "[Loot] Corpse queued at "
                                      << npc->position.x << ", " << npc->position.y << std::endl;
                            ResetState();
                            return true;
                        }
                        // No looting — chain to next grind target (grind behavior only).
                        if (ws.grindState.enabled && TryChainNextGrindTarget(ws)) return false;
                        ResetState();
                        return true;
                    }
                    if (npc->maxHealth > 0 && (static_cast<float>(npc->health) / npc->maxHealth) < 0.2f) {
                        lowHp = true;
                    }
                    if (doLog) {
                       // g_LogFile << "[Combat] Target Active @ " << npc->position.x << "," << npc->position.y << "," << npc->position.z
                       //     << " | Dist: " << npc->distance << " | HP: " << npc->health << std::endl;
                    }
                    foundLiveTarget = true;  // only set when entity is confirmed alive as EnemyInfo
                }
                // If entity exists by GUID but cannot be cast to EnemyInfo, it changed type
                // (e.g. a mechanical NPC became a corpse/inactive object). Treat as despawned.
                break;
            }
        }

        if (!foundLiveTarget) {
            g_LogFile << "[Combat] Target no longer in entity list — despawned/dead. Ending combat." << std::endl;
            if (ws.grindState.lootMobs) {
                //g_GameState->lootState.hasLoot  = true;
                g_GameState->lootState.position = ws.combatState.enemyPosition;
                g_GameState->lootState.guidLow  = ws.combatState.targetGuidLow;
                g_GameState->lootState.guidHigh = ws.combatState.targetGuidHigh;
                g_GameState->lootState.mapId    = ws.player.mapId;
                ResetState();
                return true;
            }
            // No looting — chain to next grind target (grind behavior only).
            if (ws.grindState.enabled && TryChainNextGrindTarget(ws)) return false;
            ResetState();
            return true;
        }

        if (combatStuck) {
            // Move forward for 1 second
            keyboard.SendKey('W', 0, true);
            Sleep(100);
            keyboard.SendKey(VK_SPACE, 0, true);
            Sleep(200);
            keyboard.SendKey(VK_SPACE, 0, false);
            Sleep(700);
            keyboard.SendKey('W', 0, false);
            combatStuck = false;
        }
        
        targetSelected = (ws.player.targetGuidLow == ws.combatState.targetGuidLow) && (ws.player.targetGuidHigh == ws.combatState.targetGuidHigh);

        std::string stateName = interact.GetState();
        bool isMoving = (stateName == "STATE_APPROACH" || stateName == "STATE_APPROACH_POST_INTERACT" || !targetSelected);

        // If we are trying to select the target (NOT selected) and NOT moving (Aligning/Clicking), hide the path.
        if (!targetSelected && !isMoving) {
            ws.globalState.activePath = {};
            ws.globalState.activeIndex = 0;
        }
        else {
            ws.globalState.activePath = ws.combatState.path;
            ws.globalState.activeIndex = ws.combatState.index;
        }

        // Short-Circuit Interaction
        if (targetSelected) {
            ws.combatState.inCombat = true;
                if (interact.GetState() != "STATE_IDLE") {
                // g_LogFile << "Interaction Reset" << std::endl;
                interact.Reset();
            }
        }

        // --- PHASE 1: ACQUIRE TARGET (If not selected) ---
        if (!targetSelected) {
            ws.combatState.inCombat = false;

            // If we were previously in melee (inRoutine) and can no longer select the
            // target, the mob is defeated-but-not-dead (e.g. mechanical robots that go
            // inactive with health > 0). Give up after 4 seconds.
            if (inRoutine) {
                if (postCombatPhase1Timer == 0) postCombatPhase1Timer = GetTickCount();
                if (GetTickCount() - postCombatPhase1Timer > 4000) {
                    g_LogFile << "[Combat] Target unselectable after combat — likely defeated (no-death mob). Ending." << std::endl;
                    ResetState();
                    return true;
                }
            }

            // Use Dist3D when there is a large vertical gap, matching the Phase 2 distance check.
            // Without this, an enemy directly above reads as Dist2D≈0 and movement stops
            // even though the bot still needs to navigate the path up.
            float zGapP1 = std::abs(ws.player.position.z - ws.combatState.enemyPosition.z);
            float distToEnemy = (zGapP1 > MELEE_RANGE)
                ? ws.player.position.Dist3D(ws.combatState.enemyPosition)
                : ws.player.position.Dist2D(ws.combatState.enemyPosition);

            // Keep approaching to melee range while TAB-targeting.
            // The health check at the top of Execute() already bails out for dead targets
            // before reaching here, so there is no risk of walking up to a corpse.
            if (distToEnemy > MELEE_RANGE) {
                bool pathExhausted = !ws.combatState.path.empty() && ws.combatState.index >= (int)ws.combatState.path.size();
                bool timerExpired  = (GetTickCount() - pathTimer) > (DWORD)REPATH_DELAY;
                bool targetDrifted = ws.combatState.enemyPosition.Dist2D(lastPathTarget) > REPATH_DRIFT;
                // Recalculate when: path gone, path consumed, or enemy moved AND timer expired.
                // Avoid recalculating on timer alone — the new path would start from the
                // player's feet and replay the same nearby waypoints on every tick.
                if (ws.combatState.path.empty() || pathExhausted || (timerExpired && targetDrifted)) {
                    ws.combatState.path = CalculatePath({ ws.combatState.enemyPosition }, ws.player.position, 0, false, ws.player.mapId, false, g_GameState->globalState.ignoreUnderWater, false, 25.0f, true, 5.0f, false, false);
                    ws.combatState.index = 0;
                    // Skip nodes the player is already standing on so recalculation
                    // doesn't loop through the same close waypoints again.
                    while (ws.combatState.index + 1 < (int)ws.combatState.path.size()) {
                        if (ws.player.position.Dist2D(ws.combatState.path[ws.combatState.index].pos) < MELEE_RANGE)
                            ws.combatState.index++;
                        else
                            break;
                    }
                    pathTimer = GetTickCount();
                    lastPathTarget = ws.combatState.enemyPosition;
                }
                if (ws.combatState.path.empty()) {
                    pilot.SteerTowards(ws.player.position, ws.player.rotation, ws.combatState.enemyPosition, false, ws.player, distToEnemy, true);
                } else {
                    interact.MoveToTargetLogic(ws.combatState.enemyPosition, ws.combatState.path, ws.combatState.index, ws.player, moveTimer, MELEE_RANGE, MELEE_RANGE, MELEE_RANGE / 2, false, false);
                }
            }

            // Press TAB every 500ms to cycle to the target.
            // If the mob is dead the health check at the top of Execute() catches it
            // on the next tick — no counting needed here.
            if (clickCooldown == 0 || GetTickCount() - clickCooldown > 500) {
                keyboard.SendKey(VK_TAB, 0, true);
                Sleep(50);
                keyboard.SendKey(VK_TAB, 0, false);
                clickCooldown = GetTickCount();
            }
            return false; // Still trying to select
        }

        // --- PHASE 2: CHASE & DESTROY (Target IS selected) ---
        // Use Dist2D normally, but fall back to Dist3D when there is a large vertical gap.
        // Without this, an enemy directly above/below reads as Dist2D≈0 and the bot
        // thinks it's already in melee range and stops navigating the ground path up.
        float zGap = std::abs(ws.player.position.z - ws.combatState.enemyPosition.z);
        float distToTarget = (zGap > MELEE_RANGE)
            ? ws.player.position.Dist3D(ws.combatState.enemyPosition)
            : ws.player.position.Dist2D(ws.combatState.enemyPosition);

        // A. MOVEMENT
        if (distToTarget > MELEE_RANGE) {
            inRoutine = false;

            // Decide how stale the current path is
            float targetDrift = ws.combatState.enemyPosition.Dist2D(lastPathTarget);
            int repathInterval = lowHp ? REPATH_DELAY_FLEE : REPATH_DELAY;
            bool pathExhausted = !ws.combatState.path.empty() && ws.combatState.index >= (int)ws.combatState.path.size();
            bool timerExpired  = (GetTickCount() - pathTimer) > (DWORD)repathInterval;
            bool targetDrifted = targetDrift > REPATH_DRIFT;

            if (ws.combatState.path.empty() || pathExhausted || (timerExpired && targetDrifted)) {
                ws.combatState.path = CalculatePath({ ws.combatState.enemyPosition }, ws.player.position, 0, false, ws.player.mapId, false, g_GameState->globalState.ignoreUnderWater, false, 25.0f, true, 5.0f, false, false);
                ws.combatState.index = 0;
                // Skip nodes the player is already standing on so recalculation
                // doesn't loop through the same close waypoints again.
                while (ws.combatState.index + 1 < (int)ws.combatState.path.size()) {
                    if (ws.player.position.Dist2D(ws.combatState.path[ws.combatState.index].pos) < MELEE_RANGE)
                        ws.combatState.index++;
                    else
                        break;
                }
                pathTimer = GetTickCount();
                lastPathTarget = ws.combatState.enemyPosition;

                // Check if the path actually reaches the enemy. When the enemy is below the NavMesh
                // (underground / invalid Z), the path ends many yards short and repeatedly
                // exhausting then recalculating it creates an infinite loop. Give up if this
                // happens PATH_FAIL_LIMIT times in a row.
                if (!ws.combatState.path.empty()) {
                    const Vector3& endPt = ws.combatState.path.back().pos;
                    float endDist = endPt.Dist2D(ws.combatState.enemyPosition);
                    if (endDist > PATH_FAIL_DIST) {
                        pathFailCount++;
                        g_LogFile << "[Combat] Path endpoint " << endDist
                                  << " yds from enemy (attempt " << pathFailCount << "/" << PATH_FAIL_LIMIT << ")" << std::endl;
                        if (pathFailCount >= PATH_FAIL_LIMIT) {
                            g_LogFile << "[Combat] Enemy unreachable after " << pathFailCount
                                      << " path attempts — invalid position? Ending combat." << std::endl;
                            ResetState();
                            return true;
                        }
                    }
                    else {
                        pathFailCount = 0; // path reaches the target — reset counter
                    }
                }
            }

            // If pathfinding failed, fall back to direct steering so movement never stops
            if (ws.combatState.path.empty()) {
                pilot.SteerTowards(ws.player.position, ws.player.rotation, ws.combatState.enemyPosition, false, ws.player, distToTarget, true);
            }
            else {
                interact.MoveToTargetLogic(ws.combatState.enemyPosition, ws.combatState.path, ws.combatState.index, ws.player, moveTimer, MELEE_RANGE, MELEE_RANGE, MELEE_RANGE / 2, false, false);
            }
        }
        else {
            // B. COMBAT (In Range)
            if (inRoutine == false) {
                //g_LogFile << "[Combat] In Range (" << distToTarget << "). Stopping to Rotate." << std::endl;
                pilot.Stop();
            }
            inRoutine = true;

            // B0. PULL EXTRA MOBS
            // When pullExtraMobs is enabled and fewer than EXTRA_MOB_LIMIT enemies are already
            // targeting the player, scan for a valid additional enemy within EXTRA_MOB_RANGE and
            // briefly tab-target + start-attack it to pull it into the fight. The bot then falls
            // back to PHASE 1 for one cycle to re-click the primary target via EngageTarget.
            if (ws.combatState.pullExtraMobs &&
                ws.combatState.attackerCount < ws.combatState.extraMobLimit &&
                GetTickCount() - extraPullTimer > 5000) {

                for (const auto& ent : ws.entities) {
                    if (ent.guidLow == ws.combatState.targetGuidLow &&
                        ent.guidHigh == ws.combatState.targetGuidHigh) continue; // skip current target
                    if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                        if (!IsBasicAttackable(*npc, ws.entities, g_GameState->player.playerGuidLow)) continue;
                        if (npc->reaction != 0 || npc->npcFlag != 0) continue;
                        // Skip if already attacking us
                        if (npc->inCombat &&
                            npc->targetGuidLow  == ws.player.playerGuidLow &&
                            npc->targetGuidHigh == ws.player.playerGuidHigh) continue;
                        float d = ws.player.position.Dist2D(npc->position);
                        if (d <= EXTRA_MOB_RANGE) {
                            g_LogFile << "[Combat] Pulling extra mob at dist=" << d << std::endl;
                            pilot.ExecuteLua(L"/targetenemy [harm,nodead]");
                            Sleep(100);
                            pilot.ExecuteLua(L"/startattack");
                            extraPullTimer = GetTickCount();
                            break; // pull one at a time; let the next timer cycle pull another
                        }
                    }
                }
            }

            // B1. TARGET RE-EVALUATION
            // Periodically scan nearby enemies and switch to a higher-priority target
            // if one exists. Only runs while in melee range to avoid disrupting chases.
            if (GetTickCount() - retargetTimer > (DWORD)RETARGET_INTERVAL) {
                retargetTimer = GetTickCount();

                // Score the current target
                float currentScore = 9999.0f;
                for (const auto& ent : ws.entities) {
                    if (ent.guidLow == ws.combatState.targetGuidLow &&
                        ent.guidHigh == ws.combatState.targetGuidHigh) {
                        if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                            // Dead current target scores worst so any live mob will beat it
                            currentScore = (npc->health <= 0) ? 9999.0f : ScoreTarget(*npc, ws);
                        }
                        break;
                    }
                }

                // Find the best-scoring alternative within chase range
                float     bestScore     = currentScore;
                ULONG_PTR bestGuidLow   = ws.combatState.targetGuidLow;
                ULONG_PTR bestGuidHigh  = ws.combatState.targetGuidHigh;
                Vector3   bestPos       = ws.combatState.enemyPosition;

                for (const auto& ent : ws.entities) {
                    if (ent.guidLow  == ws.combatState.targetGuidLow &&
                        ent.guidHigh == ws.combatState.targetGuidHigh) continue;
                    if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                        if (npc->health <= 0) continue;
                        if (!npc->inCombat) continue;
                        if (npc->targetGuidLow  != ws.player.playerGuidLow ||
                            npc->targetGuidHigh != ws.player.playerGuidHigh) continue;
                        bool avoided = false;
                        for (const auto& avoid : g_ProfileSettings.avoidMobs)
                            if ((uint32_t)avoid.Id == npc->id) { avoided = true; break; }
                        if (avoided) continue;
                        float score = ScoreTarget(*npc, ws);
                        if (score < bestScore) {
                            bestScore    = score;
                            bestGuidLow  = ent.guidLow;
                            bestGuidHigh = ent.guidHigh;
                            bestPos      = npc->position;
                        }
                    }
                }

                // Switch only if the improvement clears the threshold
                if (bestGuidLow != ws.combatState.targetGuidLow &&
                    (currentScore - bestScore) >= RETARGET_THRESHOLD) {
                    g_LogFile << "[Combat] Retargeting — score improved from "
                              << currentScore << " to " << bestScore << std::endl;
                    ws.combatState.targetGuidLow  = bestGuidLow;
                    ws.combatState.targetGuidHigh = bestGuidHigh;
                    ws.combatState.enemyPosition  = bestPos;
                    targetSelected = false;
                    inRoutine      = false;
                    prevHealth     = -1;
                    healthCheck    = 0;
                    interact.Reset();
                }
            }

            // Face the target (Critical for casting)
            if (pilot.faceTarget(ws.player.position, ws.combatState.enemyPosition, ws.player.rotation)) {
                float playerHealthPct = (ws.player.maxHealth > 0)
                    ? (float)ws.player.health / ws.player.maxHealth : 1.0f;
                combatController.UpdateRotation(ws.player.position, ws.combatState.enemyPosition,
                    ws.player.rotation, lowHp, ws.player.level, playerHealthPct);
            }
        }

        return false;
    }
};

// Out-of-combat self-healing.  Stops the bot and casts the best available
// heal until the player is back to full (or near-full) HP.
class ActionHeal : public GoapAction {
private:
    SimpleKeyboardClient& keyboard;
    ConsoleInput console;

    const float HEAL_BELOW_PCT = 0.50f; // Start healing below 50%
    const float FULL_THRESHOLD = 0.99f; // Stop when at 99%
    const int   CAST_INTERVAL  = 2500;  // ms between casts (covers Holy Light cast time)

    DWORD lastCastTime = 0;

public:
    ActionHeal(SimpleKeyboardClient& kbd) : keyboard(kbd), console(kbd) {}

    int GetPriority() override { return 150; }
    std::string GetName() override { return "Heal Self"; }
    ActionState* GetState(WorldState& ws) override { return &ws.healState; }

    bool CanExecute(const WorldState& ws) override {
        if (ws.combatState.inCombat || ws.combatState.underAttack) return false;
        if (ws.player.isDead || ws.player.isGhost) return false;
        if (ws.player.maxHealth == 0) return false;
        return ((float)ws.player.health / ws.player.maxHealth) < HEAL_BELOW_PCT;
    }

    void ResetState() override {
        // Do NOT reset lastCastTime here: if the action is briefly preempted and
        // re-selected within CAST_INTERVAL, preserving the timer prevents a
        // double-cast. If the action was interrupted long enough for the interval
        // to have naturally expired, the next Execute will cast immediately anyway.
        g_GameState->healState.isHealing = false;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        if (ws.player.maxHealth == 0) return true;

        float hpPct = (float)ws.player.health / ws.player.maxHealth;
        if (hpPct >= FULL_THRESHOLD) return true;

        pilot.Stop();
        g_GameState->healState.isHealing = true;

        DWORD now = GetTickCount();
        if (now - lastCastTime < (DWORD)CAST_INTERVAL) return false;

        // Use the fastest available heal for the player's level
        if (ws.player.level >= 5) {
            console.SendDataRobust(L"/cast Flash of Light", ws.globalState.chatOpen);
        } else {
            console.SendDataRobust(L"/cast Holy Light", ws.globalState.chatOpen);
        }
        lastCastTime = now;
        return false;
    }
};

class ActionLoot : public GoapAction {
private:
    InteractionController& interact;

    bool failedPath = false;

public:
    ActionLoot(InteractionController& ic) : interact(ic) {}

    bool CanExecute(const WorldState& ws) override {
        return ((ws.lootState.enabled) && (ws.lootState.hasLoot));
    }

    ActionState* GetState(WorldState& ws) override {
        return &ws.lootState;
    }

    int GetPriority() override { return 70; }
    std::string GetName() override { return "Loot Corpse"; }

    // Reset state if we are switching to this action fresh
    void ResetState() override {
        interact.Reset();
        g_GameState->lootState.path = {};
        g_GameState->lootState.index = 0;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        bool failedInteract;

        // Set wait time to 1500ms (1.5s loot window)
        // Range 3.0f, Flying Allowed
        bool complete = interact.EngageTarget(
            ws.lootState.position,
            ws.lootState.guidLow,
            ws.lootState.guidHigh,
            ws.player,
            ws.lootState.path,
            ws.lootState.index,
            ws.lootState.mapId,
            3.0f,
            2.5f,
            2.5f,
            false,
            false,
            ws.lootState.flyingPath,
            1500,
            MOUSE_RIGHT,
            false,
            failedPath,
            -1,
            failedInteract,
            false,
            1500,
            true,
            { -1, -1, -1 },
            false,
            false
        );
        ws.globalState.activePath = ws.lootState.path;
        ws.globalState.activeIndex = ws.lootState.index;

        if (complete) {
            // Cleanup
            interact.Reset();
            ws.lootState.hasLoot = false;
            failedPath = false;
            return true;
        }
        return false;
    }
};

// --- CONCRETE ACTION: GATHER NODE (NEW) ---
class ActionGather : public GoapAction {
private:
    InteractionController& interact;

    float approachDist = 3.5f;
    float finalDist = 2.8f;

    int failCount = 0;
    bool failedPath = false;
    bool failedInteract = false;
    int failInteractCount = 0;

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
        //g_GameState->gatherState.path = {};
        //g_GameState->gatherState.index = 0;
        g_GameState->gatherState.hasNode = false;
        g_GameState->gatherState.nodeActive = false;

        approachDist = 3.5f;
        finalDist = 2.8f;
        failCount = 0;
        failInteractCount = 0;
        failedPath = false;
        failedInteract = false;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {  
        if (ws.gatherState.nodeActive == false) {
            interact.SetState(InteractionController::STATE_COMPLETE);
        }

        // Handle Path Failure (Rotation First)
        // If pathfinding failed, we force the camera to face the problematic node 
        // BEFORE we blacklist it and hand control over to the Unstuck/Return logic.
        if (failedPath) {
            // AlignCameraLogic returns true when facing the target
            if (interact.AlignCameraLogic(ws.gatherState.position)) {
                g_LogFile << "Failed to create path to node (Camera Aligned), adding to blacklist" << std::endl;

                // Now that we are looking at it, blacklist it
                ws.gatherState.blacklistNodesGuidLow.push_back(ws.gatherState.guidLow);
                ws.gatherState.blacklistNodesGuidHigh.push_back(ws.gatherState.guidHigh);
                ws.gatherState.blacklistTime.push_back(GetTickCount());

                ws.gatherState.nodeActive = false;
                ResetState();
                return true; // Action complete
            }
            return false; // Still rotating
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
            ws.gatherState.mapId,
            approachDist,
            2.8f,
            finalDist,
            false,
            false,
            ws.gatherState.flyingPath,
            4500,
            MOUSE_RIGHT,
            false,
            failedPath,
            -1,
            failedInteract,
            false,
            1500,
            true,
            {},
            true,
            false
        );
        ws.globalState.activePath = ws.gatherState.path;
        ws.globalState.activeIndex = ws.gatherState.index;

        if (failedInteract) {
            failInteractCount++;
            failedInteract = false;
            if (failInteractCount > 1) {
                g_LogFile << "Failed to interact 3 times, adding to blacklist" << std::endl;
                ws.gatherState.blacklistNodesGuidLow.push_back(ws.gatherState.guidLow);
                ws.gatherState.blacklistNodesGuidHigh.push_back(ws.gatherState.guidHigh);
                ws.gatherState.blacklistTime.push_back(GetTickCount());
                ResetState();
                return true;
            }
        }

        /*if (failedPath) {
            g_LogFile << "Failed to create path to node, adding to blacklist" << std::endl;
            ws.gatherState.blacklistNodesGuidLow.push_back(ws.gatherState.guidLow);
            ws.gatherState.blacklistNodesGuidHigh.push_back(ws.gatherState.guidHigh);
            ws.gatherState.blacklistTime.push_back(GetTickCount());
            complete = true;
            ws.gatherState.nodeActive = false;
        }*/

        if (complete) {
            if (failCount >= 3) {
                g_LogFile << "Failed to gather 3 times, adding to blacklist" << std::endl;
                ws.gatherState.blacklistNodesGuidLow.push_back(ws.gatherState.guidLow);
                ws.gatherState.blacklistNodesGuidHigh.push_back(ws.gatherState.guidHigh);
                ws.gatherState.blacklistTime.push_back(GetTickCount());
            }
            else if (ws.gatherState.nodeActive) {
                finalDist *= 0.7f;
                g_LogFile << "Gathering Failed Node Still Active, retrying at distance of " << finalDist << std::endl;
                failCount++;
                interact.Reset();
                return false;
            }

            // Cleanup
            ResetState();
            // Reset camera or other post-action stuff
            return true;
        }
        return false;
    }
};

class ActionEscapeWater : public GoapAction {
private:
    DWORD mountSentTime = 0;
    DWORD outOfWaterTime = 0;

public:
    bool CanExecute(const WorldState& ws) override {
        if (!ws.pathFollowState.flyingPath) return false;
        return ws.player.inWater || outOfWaterTime != 0;
    }

    int GetPriority() override { return 51; }
    std::string GetName() override { return "Escape Water"; }
    ActionState* GetState(WorldState& ws) override { return nullptr; }

    void ResetState() override {
        mountSentTime = 0;
        outOfWaterTime = 0;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        DWORD now = GetTickCount();

        // Send /mountfly once on entry, then retry every 3 s if still not mounted.
        if (mountSentTime == 0 || (!ws.player.flyingMounted && now - mountSentTime >= 3000)) {
            pilot.ExecuteLua(L"/mountfly");
            mountSentTime = now;
        }

        if (!ws.player.flyingMounted) {
            // Waiting for mount cast — press nothing so the cast isn't cancelled.
            pilot.Stop();
            outOfWaterTime = 0;
            return false;
        }

        // Mounted: hold space to fly up and away.
        pilot.HoldAscend();

        if (ws.player.inWater) {
            outOfWaterTime = 0;
        } else {
            if (outOfWaterTime == 0)
                outOfWaterTime = now;
            if (now - outOfWaterTime >= 3000)
                return true;
        }

        return false;
    }
};

// --- Return to a saved waypoint after performing a different action --- //
class ActionReturnWaypoint : public GoapAction {
private:
    const float ACCEPTANCE_RADIUS = 3.0f;
    const float GROUND_ACCEPTANCE_RADIUS = 0.5f;
    const float GROUND_HEIGHT_THRESHOLD = 7.0f; // If target is this close to ground, use ground path

    const int MAX_PATHFINDING_ATTEMPTS = 8;

    enum ReturnPhase {
        PHASE_CHECK_DIRECT,    // First check if we can go direct
        PHASE_MOUNT,           // Mount up if needed
        PHASE_NAVIGATE,        // Follow path back
        PHASE_COMPLETE,
        PHASE_FAILED
    };

    ReturnPhase currentPhase = PHASE_CHECK_DIRECT;
    Vector3 ascentTarget;
    bool ascentTargetSet = false;
    bool checkedDirect = false;
    bool escapeWater = false;
    DWORD mountTimer = 0;
    SimpleKeyboardClient* keyboard = nullptr;
    ConsoleInput* consoleInput = nullptr;

public:
    void SetKeyboard(SimpleKeyboardClient* kb, ConsoleInput* ci) {
        keyboard = kb;
        consoleInput = ci;
    }

    bool CanExecute(const WorldState& ws) override {
        // Don't execute if we're waiting for unstuck to complete
        if (ws.waypointReturnState.waitingForUnstuck) {
            return false;
        }
        return (ws.waypointReturnState.enabled && ws.waypointReturnState.hasTarget);
    }

    int GetPriority() override { return 60; }
    std::string GetName() override { return "Return to previous waypoint"; }

    ActionState* GetState(WorldState& ws) override {
        return &ws.waypointReturnState;
    }

    void ResetState() override {
        // If we yielded to ActionUnstuck for a path-failure retry, preserve the
        // target and attempt counter so the action can resume after unstuck finishes.
        if (g_GameState->waypointReturnState.waitingForUnstuck) {
            g_GameState->waypointReturnState.path  = {};
            g_GameState->waypointReturnState.index = 0;
            g_GameState->waypointReturnState.hasPath = false;
            currentPhase    = PHASE_CHECK_DIRECT;
            ascentTargetSet = false;
            checkedDirect   = false;
            mountTimer      = 0;
            return;
        }

        // Full reset (action truly finished or cancelled)
        g_GameState->waypointReturnState.path = {};
        g_GameState->waypointReturnState.index = 0;
        g_GameState->waypointReturnState.pathfindingAttempts = 0;
        g_GameState->waypointReturnState.waitingForUnstuck = false;

        g_GameState->waypointReturnState.savedPath = {};
        g_GameState->waypointReturnState.hasTarget = false;
        g_GameState->waypointReturnState.hasPath = false;

        currentPhase = PHASE_CHECK_DIRECT;
        ascentTargetSet = false;
        checkedDirect = false;
        mountTimer = 0;
    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        ws.globalState.activePath = ws.waypointReturnState.path;
        ws.globalState.activeIndex = ws.waypointReturnState.index;
        DWORD now = GetTickCount();

        switch (currentPhase) {
        case PHASE_CHECK_DIRECT:
        {
            if (checkedDirect) {
                currentPhase = PHASE_MOUNT;
                break;
            }

            // Inherit the grind profile's fly/ground preference before any path logic.
            // flyingPath defaults to true in ActionState, so without this a ground-only
            // grind would fly back to the waypoint when the direct path check fails.
            ws.waypointReturnState.flyingPath = ws.waypointReturnState.flyingTarget;

            checkedDirect = true;
            Vector3 returnTarget = ws.waypointReturnState.savedPath[ws.waypointReturnState.savedIndex].pos;

            g_LogFile << "[RETURN] Checking for direct path..." << std::endl;
            g_LogFile << "  From: (" << ws.player.position.x << "," << ws.player.position.y << "," << ws.player.position.z << ")" << std::endl;
            g_LogFile << "  To: (" << returnTarget.x << "," << returnTarget.y << "," << returnTarget.z << ")" << std::endl;

            // Check if target is at ground level
            float targetGroundZ = globalNavMesh.GetLocalGroundHeight(returnTarget);
            bool targetOnGround = (targetGroundZ > -90000.0f && std::abs(returnTarget.z - targetGroundZ) < GROUND_HEIGHT_THRESHOLD);

            // Check if we're at ground level
            float currentGroundZ = globalNavMesh.GetLocalGroundHeight(ws.player.position);
            bool currentOnGround = (!ws.player.isFlying) ||
                (currentGroundZ > -90000.0f && std::abs(ws.player.position.z - currentGroundZ) < GROUND_HEIGHT_THRESHOLD);

            g_LogFile << "  Current on ground: " << currentOnGround << " (Z=" << ws.player.position.z << ", groundZ=" << currentGroundZ << ")" << std::endl;
            g_LogFile << "  Target on ground: " << targetOnGround << " (Z=" << returnTarget.z << ", groundZ=" << targetGroundZ << ")" << std::endl;

            float horizontalDist = ws.player.position.Dist2D(returnTarget);
            float verticalDist = std::abs(ws.player.position.z - returnTarget.z);

            if (horizontalDist > 100.0f || verticalDist > 50.0f) {
                g_LogFile << "  ✗ Distance too great for direct path (hDist=" << horizontalDist << ", vDist=" << verticalDist << ")" << std::endl;
                currentPhase = PHASE_MOUNT;
                break;
            }
            Vector3 adjustedPlayer = ws.player.position;

            // Try direct path check - ONLY for short distances
            bool directClear = false;
            if (currentOnGround && targetOnGround) {
                // Both on ground - check 2D ground path
                g_LogFile << "  Checking ground direct path (2D)..." << std::endl;
                Vector3 testStart = ws.player.position;
                testStart.z = currentGroundZ + 1.0f;
                Vector3 testEnd = returnTarget;
                testEnd.z = targetGroundZ + 1.0f;
                directClear = globalNavMesh.CheckFlightSegment(testStart, testEnd, ws.player.mapId, ws.player.isFlying, true);
                // Additional validation: sample midpoint
                if (directClear) {
                    Vector3 midpoint = (testStart + testEnd) * 0.5f;
                    float midGroundZ = globalNavMesh.GetLocalGroundHeight(midpoint);
                    if (midGroundZ > -90000.0f && midpoint.z < midGroundZ + MIN_CLEARANCE) {
                        g_LogFile << "  ✗ Midpoint too close to ground" << std::endl;
                        directClear = false;
                    }
                }
            }
            else {
                // At least one is in air - check 3D flight path
                g_LogFile << "  Checking flight direct path (3D)..." << std::endl;

                /*if (currentOnGround) {
                    adjustedPlayer.z += 20.0f;
                    g_LogFile << "Player on ground, raising player position z by 20.0 for flight path" << std::endl;
                }*/

                directClear = globalNavMesh.CheckFlightSegment(adjustedPlayer, returnTarget, ws.player.mapId, ws.player.isFlying, true);
                // NEW: Additional validation for flight paths
                if (directClear) {
                    // Check multiple points along the path
                    Vector3 dir = returnTarget - adjustedPlayer;
                    float dist = dir.Length();
                    dir = dir / dist;

                    for (int sample = 1; sample <= 3; sample++) {
                        float t = (float)sample / 1.0f;
                        Vector3 testPoint = adjustedPlayer + (dir * (dist * t));

                        // Verify flyability
                        if (!CanFlyAt(ws.player.mapId, testPoint.x, testPoint.y, testPoint.z)) {
                            g_LogFile << "  ✗ Sample point " << sample << " not flyable" << std::endl;
                            directClear = false;
                            break;
                        }

                        // Verify clearance
                        float sampleGroundZ = globalNavMesh.GetLocalGroundHeight(testPoint);
                        if (sampleGroundZ > -90000.0f && testPoint.z < sampleGroundZ + MIN_CLEARANCE + 5.0f) {
                            g_LogFile << "  ✗ Sample point " << sample << " too close to ground" << std::endl;
                            directClear = false;
                            break;
                        }
                    }
                }
            }

            if (directClear) {
                if (currentOnGround) {
                    g_LogFile << "  ✓ Direct path is clear! Using simple 3-point path." << std::endl;
                    ws.waypointReturnState.path = {
                        PathNode(ws.player.position, PATH_AIR),
                        PathNode(adjustedPlayer, PATH_AIR),
                        PathNode(returnTarget, targetOnGround ? PATH_GROUND : PATH_AIR)
                    };
                }
                else {
                    g_LogFile << "  ✓ Direct path is clear! Using simple 2-point path." << std::endl;
                    ws.waypointReturnState.path = {
                        PathNode(ws.player.position, PATH_AIR),
                        PathNode(returnTarget, targetOnGround ? PATH_GROUND : PATH_AIR)
                    };
                }
                // Store PathNodes
                ws.waypointReturnState.hasPath = true;
                ws.waypointReturnState.index = 0;

                // Decide if we should fly or walk
                if (currentOnGround && targetOnGround) {
                    ws.waypointReturnState.flyingPath = false;
                    g_LogFile << "  Using ground path (both positions on ground)" << std::endl;
                }
                else {
                    ws.waypointReturnState.flyingPath = true;
                    g_LogFile << "  Using flight path" << std::endl;
                }

                for (int i = 0; i < ws.waypointReturnState.path.size(); i++) {
                    g_LogFile << ws.waypointReturnState.path[i].pos.x << " " << ws.waypointReturnState.path[i].pos.y << " " << ws.waypointReturnState.path[i].pos.z << " " << ws.waypointReturnState.path[i].type << std::endl;
                }

                currentPhase = PHASE_NAVIGATE;
                break;
            }
            else {
                g_LogFile << "  ✗ Direct path blocked, need complex pathfinding" << std::endl;
                currentPhase = PHASE_MOUNT;
            }

            break;
        }

        case PHASE_MOUNT:
        {
            // For flight paths, SteerTowards handles mounting automatically.
            if (ws.waypointReturnState.flyingPath) {
                currentPhase = PHASE_NAVIGATE;
                break;
            }

            // Already ground-mounted — go straight to navigation.
            if (ws.player.groundMounted) {
                currentPhase = PHASE_NAVIGATE;
                break;
            }

            // Initiate a ground mount attempt.
            if (mountTimer == 0) {
                g_LogFile << "[RETURN] Mounting up for ground return..." << std::endl;
                if (consoleInput) {
                    consoleInput->SendDataRobust(L"/mountground", g_GameState->globalState.chatOpen);
                }
                mountTimer = now;
                break;
            }

            // Wait for the mount cast to complete.
            if (ws.player.groundMounted) {
                mountTimer = 0;
                currentPhase = PHASE_NAVIGATE;
                break;
            }

            // Timeout — player may be in combat or mount unavailable; proceed on foot.
            if (now - mountTimer > 4000) {
                g_LogFile << "[RETURN] Mount timed out, proceeding on foot." << std::endl;
                mountTimer = 0;
                currentPhase = PHASE_NAVIGATE;
            }

            break;
        }

        case PHASE_NAVIGATE:
        {
            if (!ws.waypointReturnState.hasPath) {
                Vector3 returnTarget = ws.waypointReturnState.savedPath[ws.waypointReturnState.savedIndex].pos;
                int mapId = ws.player.mapId;
                Vector3 playerPos = ws.player.position;
                std::vector<PathNode> constructedPath;

                // Track attempt
                ws.waypointReturnState.pathfindingAttempts++;
                ws.waypointReturnState.lastPathAttemptTime = now;
                g_LogFile << "[RETURN] Calculating " << (ws.waypointReturnState.flyingPath ? "FLIGHT" : "GROUND") << " path" << std::endl;
                g_LogFile << "  From: (" << ws.player.position.x << "," << ws.player.position.y << "," << ws.player.position.z << ")" << std::endl;
                g_LogFile << "  To: (" << returnTarget.x << "," << returnTarget.y << "," << returnTarget.z << ")" << std::endl;

                constructedPath = CalculatePath(
                    { returnTarget },
                    ws.player.position,
                    0,
                    ws.waypointReturnState.flyingPath,
                    ws.player.mapId,
                    ws.player.isFlying,
                    g_GameState->globalState.ignoreUnderWater,
                    false,
                    25.0f,
                    true,
                    5.0f,
                    false,
                    ws.waypointReturnState.flyingTarget
                );

                if (constructedPath.size() > 0) {
                    ws.waypointReturnState.path = constructedPath;
                }

                if ((ws.waypointReturnState.path.empty()) || (constructedPath.size() == 0)) {
                    g_LogFile << "[RETURN] ✗ Path calculation FAILED! (Attempt "
                        << ws.waypointReturnState.pathfindingAttempts << "/"
                        << MAX_PATHFINDING_ATTEMPTS << ")" << std::endl;

                    if (ws.waypointReturnState.pathfindingAttempts >= MAX_PATHFINDING_ATTEMPTS) {
                        g_LogFile << "[RETURN] !!! CRITICAL: Maximum pathfinding attempts reached." << std::endl;
                        currentPhase = PHASE_FAILED;
                        break;
                    }

                    if (ws.stuckState.attemptCount >= 8) {
                        g_LogFile << "[RETURN] Stuck attempts exhausted. Quitting." << std::endl;
                        EndScript(pilot, -1);
                    }

                    // NEW: Trigger ActionUnstuck by setting stuck flag
                    g_LogFile << "[RETURN] Triggering ActionUnstuck before retry..." << std::endl;
                    ws.stuckState.isStuck = true;
                    ws.waypointReturnState.waitingForUnstuck = true;

                    // Reset phases so we retry from the beginning after unstuck
                    checkedDirect = false;
                    ascentTargetSet = false;
                    currentPhase = PHASE_CHECK_DIRECT;


                    return false; // ActionUnstuck will take over
                }

                ws.waypointReturnState.hasPath = true;
                ws.waypointReturnState.index = 0;

                g_LogFile << "[RETURN] Path has " << ws.waypointReturnState.path.size() << " waypoints" << std::endl;
            }

            if (ws.waypointReturnState.index >= ws.waypointReturnState.path.size()) {
                currentPhase = PHASE_COMPLETE;
                break;
            }

            PathNode& targetNode = ws.waypointReturnState.path[ws.waypointReturnState.index];
            Vector3 target = targetNode.pos;
            int targetType = targetNode.type;
            float dist;
            if (targetType == PATH_GROUND) {
                dist = ws.player.position.Dist2D(target);
            }
            else {
                dist = ws.player.position.Dist3D(target);
            }
            //g_LogFile << "Debug 1 - Distance to target: " << dist << std::endl;

            if (((targetType == PATH_AIR) && (dist < ACCEPTANCE_RADIUS)) || ((targetType == PATH_GROUND) && (dist < GROUND_ACCEPTANCE_RADIUS))) {
                //g_LogFile << "Debug 3" << std::endl;
                ws.waypointReturnState.index++;
                return false;
            }

            float goalDist = 0.0f;
            if (!g_GameState->player.groundMounted && targetType == PATH_GROUND) {
                //g_LogFile << "Debug 3.5" << std::endl;
                goalDist += g_GameState->player.position.Dist3D(ws.waypointReturnState.path[ws.waypointReturnState.index].pos);
                for (int k = ws.waypointReturnState.index; k + 1 < ws.waypointReturnState.path.size(); k++) {
                    if (ws.waypointReturnState.path[k].type == PATH_GROUND) {
                        goalDist += ws.waypointReturnState.path[k].pos.Dist3D(ws.waypointReturnState.path[k + 1].pos);
                    }
                    else break;
                }
            }
            else if (!g_GameState->player.flyingMounted && targetType == PATH_AIR) {
                //g_LogFile << "Debug 4" << std::endl;
                goalDist += g_GameState->player.position.Dist3D(ws.waypointReturnState.path[ws.waypointReturnState.index].pos);
                for (int k = ws.waypointReturnState.index; k + 1 < ws.waypointReturnState.path.size(); k++) {
                    if (ws.waypointReturnState.path[k].type == PATH_AIR) {
                        goalDist += ws.waypointReturnState.path[k].pos.Dist3D(ws.waypointReturnState.path[k + 1].pos);
                    }
                    else break;
                }
            }
            else {
                //g_LogFile << "Debug 5" << std::endl;
                goalDist = 10000.0f;
            }
            //g_LogFile << "Debug 6" << std::endl;

            /*for (int i = 0; i < ws.waypointReturnState.path.size(); i++) {
                g_LogFile << ws.waypointReturnState.path[i].pos.x << " " << ws.waypointReturnState.path[i].pos.y << " " << ws.waypointReturnState.path[i].pos.z << " " << ws.waypointReturnState.path[i].type << std::endl;
            }
            g_LogFile << ws.waypointReturnState.index << std::endl;
            g_LogFile << target.z << " " << targetType << " " << goalDist << std::endl;*/
            pilot.SteerTowards(ws.player.position, ws.player.rotation, target, targetType, ws.player, goalDist);
            return false;
        }        

        case PHASE_COMPLETE:
        {            
            pilot.Stop();
            ResetState();
            g_LogFile << "[RETURN] ✓ Complete!" << std::endl;
            return true;
        }

        case PHASE_FAILED:
        {
            pilot.Stop();

            g_LogFile << "!!! CRITICAL: ActionReturnWaypoint failed after "
                << MAX_PATHFINDING_ATTEMPTS << " attempts. Stopping program." << std::endl;
            g_LogFile.close();

            Sleep(500000000);
            return false;
        }
        }

        return false;
    }
};

// --- CONCRETE ACTION: GRIND MOBS ---
class ActionGrind : public GoapAction {
private:
    const float ACCEPTANCE_RADIUS        = 3.0f;
    const float GROUND_ACCEPTANCE_RADIUS = 1.0f;
    const int   PATH_CALC_INTERVAL       = 3000; // ms between CalculatePath attempts when path is failing
    DWORD       pathCalcTimer            = 0;

    bool IsValidTarget(const EnemyInfo& npc, const WorldState& ws,
                       ULONG_PTR guidLow = 0, ULONG_PTR guidHigh = 0) const {
        // Helper lambda: log rejection and return false.
        auto reject = [&](const char* reason) -> bool {
            g_LogFile << "[IsValidTarget] SKIP \"" << npc.name
                << "\" (id=" << npc.id << " lvl=" << npc.level
                << " pos=(" << npc.position.x << "," << npc.position.y << "," << npc.position.z << ")"
                << " hp=" << npc.health << " reaction=" << npc.reaction
                << ") reason=" << reason << std::endl;
            return false;
        };

        if (!IsBasicAttackable(npc, ws.entities, g_GameState->player.playerGuidLow))
            return reject("not_basic_attackable");
        if (npc.reaction == 2) return reject("friendly(reaction=2)");
        if ((npc.unitFlags & 0x8000) && npc.reaction == 1) return reject("pvp_attackable_neutral");
        if (npc.npcFlag != 0)  return reject("npcFlag!=0");
        if (guidLow != 0) {
            for (size_t i = 0; i < ws.grindState.blacklistGuidLow.size(); ++i) {
                if (ws.grindState.blacklistGuidLow[i] == guidLow &&
                    ws.grindState.blacklistGuidHigh[i] == guidHigh) return reject("blacklisted");
            }
        }
        int pLevel = ws.player.level;
        if (!ws.grindState.killAllMobs && !ws.grindState.mobIds.empty()) {
            bool idMatch = false;
            for (int id : ws.grindState.mobIds) {
                if (npc.id == (uint32_t)id) { idMatch = true; break; }
            }
            if (!idMatch) return reject("id_not_in_moblist");
        }
        if (npc.level > pLevel + ws.grindState.maxLevelMod) return reject("level_too_high");
        if (npc.level < pLevel - ws.grindState.minLevelMod) return reject("level_too_low");
        for (const auto& avoid : g_ProfileSettings.avoidMobs) {
            if ((uint32_t)avoid.Id == npc.id) {
                static DWORD lastAvoidLog = 0;
                if (GetTickCount() - lastAvoidLog > 5000) {
                    lastAvoidLog = GetTickCount();
                    g_LogFile << "[IsValidTarget] Avoided mob \"" << npc.name << "\" (id=" << npc.id << ") skipped" << std::endl;
                }
                return reject("avoidMobs_list");
            }
        }
        // Skip air-only creatures — they are always flying and unreachable on foot
        if (npc.inhabitType == 4) return reject("air_only(InhabitType=4)");
        if (npc.inCombat && npc.targetGuidLow != 0 &&
            (npc.targetGuidLow  != g_GameState->player.playerGuidLow ||
             npc.targetGuidHigh != g_GameState->player.playerGuidHigh)) {
            for (const auto& ent : ws.entities) {
                if (ent.guidLow == npc.targetGuidLow && ent.guidHigh == npc.targetGuidHigh) {
                    if (std::dynamic_pointer_cast<OtherPlayerInfo>(ent.info)) return reject("being_killed_by_other_player");
                    break;
                }
            }
        }
        return true;
    }

public:
    bool CanExecute(const WorldState& ws) override {
        return ws.grindState.enabled && ws.grindState.hasPath;
    }

    int GetPriority() override { return 25; }
    std::string GetName() override { return "Grind"; }
    ActionState* GetState(WorldState& ws) override { return &ws.grindState; }
    void ResetState() override { }

    bool Execute(WorldState& ws, MovementController& pilot) override {

        // --- 1. SCAN FOR VALID GRIND TARGET ---
        // Only when ActionCombat is not already active — it has priority 200 and will
        // take over automatically the tick after we set combatState.
        // Stop condition: player reached target level
        if (ws.grindState.targetLevel > 0 && ws.player.level >= ws.grindState.targetLevel) {
            pilot.Stop();
            ws.grindState.hasPath = false;
            ws.grindState.path.clear();
            g_LogFile << "[Grind] Target level " << ws.grindState.targetLevel << " reached — task complete." << std::endl;
            return true;
        }

        // Mark inLoop the first time the player arrives within pullRange yards of any hotspot.
        if (!ws.grindState.inLoop && !ws.grindState.hotspots.empty()) {
            for (const auto& hs : ws.grindState.hotspots) {
                if (ws.player.position.Dist2D(hs) <= ws.grindState.pullRange) {
                    ws.grindState.inLoop = true;
                    g_LogFile << "[Grind] Entered grind loop." << std::endl;
                    break;
                }
            }
        }

        if (!ws.combatState.inCombat && !ws.combatState.hasTarget && !ws.combatState.underAttack) {
            // Expire blacklist entries older than 5 minutes
            const DWORD BLACKLIST_DURATION = 5 * 60 * 1000;
            DWORD now = GetTickCount();
            for (int i = (int)ws.grindState.blacklistTime.size() - 1; i >= 0; --i) {
                if (now - ws.grindState.blacklistTime[i] >= BLACKLIST_DURATION) {
                    ws.grindState.blacklistGuidLow.erase(ws.grindState.blacklistGuidLow.begin() + i);
                    ws.grindState.blacklistGuidHigh.erase(ws.grindState.blacklistGuidHigh.begin() + i);
                    ws.grindState.blacklistTime.erase(ws.grindState.blacklistTime.begin() + i);
                }
            }

            // Only scan for targets once in the loop, or when engageAlways is set.
            if (ws.grindState.inLoop || ws.grindState.engageAlways) {
            float     bestDist     = ws.grindState.pullRange;
            ULONG_PTR bestGuidLow  = 0;
            ULONG_PTR bestGuidHigh = 0;
            Vector3   bestPos      = {};

            for (const auto& ent : ws.entities) {
                if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                    if (!IsValidTarget(*npc, ws, ent.guidLow, ent.guidHigh)) continue;
                    // Initial scan: only check distance from the player (who is on the route).
                    // Hotspot proximity is enforced in TryChainNextGrindTarget to prevent
                    // post-combat chaining from drifting far off the route.
                    float d = ws.player.position.Dist2D(npc->position);
                    if (d < bestDist) {
                        bestDist     = d;
                        bestGuidLow  = ent.guidLow;
                        bestGuidHigh = ent.guidHigh;
                        bestPos      = npc->position;
                    }
                }
            }

            if (bestGuidLow != 0) {
                g_GameState->combatState.hasTarget     = true;
                g_GameState->combatState.underAttack   = false;
                g_GameState->combatState.enemyPosition = bestPos;
                g_GameState->combatState.targetGuidLow  = bestGuidLow;
                g_GameState->combatState.targetGuidHigh = bestGuidHigh;
                g_LogFile << "[Grind] Target acquired, dist=" << bestDist << std::endl;
                return false; // ActionCombat (priority 200) wins next tick
            }
            } // end if (inLoop || engageAlways)
        }

        // --- 2. NO TARGET — FOLLOW HOTSPOT ROUTE ---
        // After combat the player may have moved significantly. If the current path node
        // is more than 30 yards from the player it was calculated from a stale position —
        // clear it and snap hotspotIndex to the nearest hotspot so the next recalculation
        // heads forward rather than back to a waypoint the player already passed.
        if (!ws.grindState.path.empty() &&
            ws.grindState.index < (int)ws.grindState.path.size() &&
            ws.player.position.Dist2D(ws.grindState.path[ws.grindState.index].pos) > 30.0f) {
            ws.grindState.path.clear();
            ws.grindState.index = 0;
            if (!ws.grindState.hotspots.empty()) {
                float bestHsDist = FLT_MAX;
                for (int i = 0; i < (int)ws.grindState.hotspots.size(); ++i) {
                    float d = ws.player.position.Dist2D(ws.grindState.hotspots[i]);
                    if (d < bestHsDist) { bestHsDist = d; ws.grindState.hotspotIndex = i; }
                }
            }
        }

        if (ws.grindState.path.empty()) {
            // Rate-limit path calculation to avoid hammering the NavMesh every tick on failure.
            if (GetTickCount() - pathCalcTimer > (DWORD)PATH_CALC_INTERVAL) {
                pathCalcTimer = GetTickCount();
                ws.grindState.path = CalculatePath(
                    ws.grindState.hotspots,
                    ws.player.position,
                    ws.grindState.hotspotIndex,
                    ws.grindState.canFly,
                    ws.grindState.mapId,
                    ws.player.isFlying,     // actual flight state, not profile flag
                    g_GameState->globalState.ignoreUnderWater,
                    ws.grindState.loop,
                    25.0f,
                    true,
                    5.0f,
                    false,
                    ws.grindState.canFly
                );
                ws.grindState.index = 0;

                if (!ws.grindState.path.empty()) {
                    ws.waypointReturnState.savedPath    = ws.grindState.path;
                    ws.waypointReturnState.savedIndex   = ws.grindState.index;
                    ws.waypointReturnState.flyingTarget = ws.grindState.canFly;
                    // Do NOT set hasTarget here — ActionGrind navigates its own path
                    // directly. hasTarget is only set by the GOAP transition code after
                    // combat/looting displaces the bot, and ActionGrind's stale-path
                    // check (>30 yards) already handles post-combat re-routing.
                }
            }

            // While path is unavailable, steer directly toward the current hotspot so
            // the bot keeps moving. Walking into a better-covered NavMesh area usually
            // lets the next CalculatePath attempt succeed.
            // Do NOT suppress mounting (mountDisable=false): if the bot is on water or
            // elevated terrain, it needs to mount up to reach air navmesh coverage.
            if (ws.grindState.path.empty() && !ws.grindState.hotspots.empty()) {
                const Vector3& dest = ws.grindState.hotspots[ws.grindState.hotspotIndex];
                float d = ws.player.position.Dist2D(dest);
                pilot.SteerTowards(ws.player.position, ws.player.rotation, dest, ws.grindState.canFly, ws.player, d, false);
            }

            return false;
        }

        // Share active path with the overlay
        ws.globalState.activePath  = ws.grindState.path;
        ws.globalState.activeIndex = ws.grindState.index;

        // --- 3. CHECK ROUTE END ---
        if (ws.grindState.index >= (int)ws.grindState.path.size()) {
            if (ws.grindState.loop) {
                // Recalculate from current position so the bot naturally flows back
                ws.grindState.path.clear();
                ws.grindState.index = 0;
                g_LogFile << "[Grind] Route complete, looping." << std::endl;
                return false;
            }
            pilot.Stop();
            ws.grindState.hasPath = false;
            ws.grindState.path.clear();
            return true;
        }

        // --- 4. STEER TO CURRENT WAYPOINT ---
        PathNode& node       = ws.grindState.path[ws.grindState.index];
        Vector3   target     = node.pos;
        int       targetType = node.type;

        float dist = (targetType == PATH_GROUND)
            ? ws.player.position.Dist2D(target)
            : ws.player.position.Dist3D(target);

        // Overshoot detection — mirrors ActionFollowPath
        if (targetType == PATH_GROUND && dist < 3.0f) {
            Vector3 heading    = { std::cos(ws.player.rotation), std::sin(ws.player.rotation), 0.0f };
            Vector3 toWaypoint = (target - ws.player.position).Normalize();
            if (heading.Dot(toWaypoint) < 0.0f) {
                ws.grindState.index++;
                ws.waypointReturnState.savedIndex = ws.grindState.index;
                return false;
            }
        }

        float acceptRadius = (targetType == PATH_AIR) ? ACCEPTANCE_RADIUS : GROUND_ACCEPTANCE_RADIUS;
        if (dist < acceptRadius) {
            ws.grindState.index++;
            // Advance hotspot index when the nav node aligns with the next hotspot.
            if (ws.grindState.hotspotIndex + 1 < (int)ws.grindState.hotspots.size()) {
                if (node.pos.Dist3D(ws.grindState.hotspots[ws.grindState.hotspotIndex + 1]) < 10.0f)
                    ws.grindState.hotspotIndex++;
            }
            ws.waypointReturnState.savedIndex = ws.grindState.index;
            return false;
        }

        // Remaining-distance calculation for mount decision (mirrors ActionFollowPath)
        float goalDist = 0.0f;
        if (!g_GameState->player.groundMounted && targetType == PATH_GROUND) {
            goalDist += ws.player.position.Dist3D(node.pos);
            for (int k = ws.grindState.index; k + 1 < (int)ws.grindState.path.size(); k++) {
                if (ws.grindState.path[k].type == PATH_GROUND)
                    goalDist += ws.grindState.path[k].pos.Dist3D(ws.grindState.path[k + 1].pos);
                else break;
            }
        }
        else if (!g_GameState->player.flyingMounted && targetType == PATH_AIR) {
            goalDist += ws.player.position.Dist3D(node.pos);
            for (int k = ws.grindState.index; k + 1 < (int)ws.grindState.path.size(); k++) {
                if (ws.grindState.path[k].type == PATH_AIR)
                    goalDist += ws.grindState.path[k].pos.Dist3D(ws.grindState.path[k + 1].pos);
                else break;
            }
        }
        else {
            goalDist = 10000.0f;
        }

        pilot.SteerTowards(ws.player.position, ws.player.rotation, target, targetType, ws.player, goalDist);

        // Keep WaypointReturn up-to-date so the bot can return after combat displacement
        ws.waypointReturnState.savedPath    = ws.grindState.path;
        ws.waypointReturnState.savedIndex   = ws.grindState.index;
        ws.waypointReturnState.flyingTarget = ws.grindState.canFly;
        return false;
    }
};

// --- CONCRETE ACTION: FOLLOW PATH ---
class ActionFollowPath : public GoapAction {
private:
    const float ACCEPTANCE_RADIUS = 3.0f;
    const float GROUND_ACCEPTANCE_RADIUS = 1.0f;

public:
    bool CanExecute(const WorldState& ws) override {
        return ws.pathFollowState.enabled && ws.pathFollowState.hasPath;
    }

    int GetPriority() override { return 20; } // STANDARD PRIORITY
    std::string GetName() override { return "Follow Path"; }

    ActionState* GetState(WorldState& ws) override {
        return &ws.pathFollowState;
    }

    void ResetState() override {

    }

    bool Execute(WorldState& ws, MovementController& pilot) override {
        if (ws.pathFollowState.path.empty()) {
            ws.pathFollowState.path = CalculatePath(
                g_GameState->pathFollowState.presetPath,
                g_GameState->player.position,
                g_GameState->pathFollowState.presetIndex,
                ws.pathFollowState.flyingPath,
                ws.pathFollowState.mapId,
                ws.player.isFlying,             // actual flight state, not profile flag
                g_GameState->globalState.ignoreUnderWater,
                g_GameState->pathFollowState.looping,
                25.0f,
                true,
                5.0f,
                false,
                ws.pathFollowState.flyingPath
            );
            ws.pathFollowState.index = 0;

            if (ws.pathFollowState.path.size() > 0) {
                ws.waypointReturnState.savedPath = ws.pathFollowState.path;
                ws.waypointReturnState.savedIndex = ws.pathFollowState.index;
                ws.waypointReturnState.flyingTarget = ws.pathFollowState.flyingPath;
                if ((ws.waypointReturnState.savedPath.size() > 0) &&
                    (ws.player.position.Dist3D(ws.waypointReturnState.savedPath[ws.waypointReturnState.savedIndex].pos) > 20.0f)) {
                    ws.waypointReturnState.hasTarget = true;
                }
                return false;
            }
        }

        ws.globalState.activePath = ws.pathFollowState.path;
        ws.globalState.activeIndex = ws.pathFollowState.index;
        // 1. Check if we finished the path
        if (ws.pathFollowState.index >= ws.pathFollowState.path.size()) {
            if (ws.pathFollowState.looping) {
                ws.pathFollowState.index = 0;
                g_LogFile << "Reached Path End, Looping to start" << std::endl;
                return false;
            }
            pilot.Stop();
            g_GameState->pathFollowState.hasPath = false; // Clear state
            g_GameState->pathFollowState.path.clear();
            return true; // Action Complete
        }

        // 2. Get current waypoint
        PathNode& targetNode = ws.pathFollowState.path[ws.pathFollowState.index];
        Vector3 target = targetNode.pos;
        int targetType = targetNode.type;

        float dist;
        // Check based on the specific node type, not just the global state
        if (targetType == PATH_GROUND) {
            dist = ws.player.position.Dist2D(target);
        }
        else {
            dist = ws.player.position.Dist3D(target);
        }

        // Overshoot detection for ground waypoints:
        // SteerTowards stops correcting yaw at 2.5 yards but the acceptance radius is only 1.0 yard.
        // If the bot coasts through the waypoint at a slight angle it will circle forever.
        // Fix: if the waypoint has crossed behind the player, treat it as reached.
        if (targetType == PATH_GROUND && dist < 3.0f) {
            Vector3 heading = { std::cos(ws.player.rotation), std::sin(ws.player.rotation), 0.0f };
            Vector3 toWaypoint = (target - ws.player.position).Normalize();
            if (heading.Dot(toWaypoint) < 0.0f) {
                ws.pathFollowState.index++;
                ws.pathFollowState.pathIndexChange = true;
                return false;
            }
        }

        if (((targetType == PATH_AIR) && (dist < ACCEPTANCE_RADIUS)) || ((targetType == PATH_GROUND) && (dist < GROUND_ACCEPTANCE_RADIUS))) {
            ws.pathFollowState.index++; // Advance state
            ws.pathFollowState.pathIndexChange = true;
            if (g_GameState->pathFollowState.presetIndex + 1 < g_GameState->pathFollowState.presetPath.size()) {

                // Check if the node we just reached corresponds to the NEXT preset waypoint
                Vector3 nextPreset = g_GameState->pathFollowState.presetPath[g_GameState->pathFollowState.presetIndex + 1];
                if (targetNode.pos.Dist3D(nextPreset) < 10.0f) {
                    g_GameState->pathFollowState.presetIndex++;
                    //g_LogFile << "[Profile] Advancing Preset Index to " << g_GameState->pathFollowState.presetIndex << std::endl;
                }
            }
            std::cout << "[GOAP] Waypoint " << ws.pathFollowState.index << " Reached." << std::endl;
            return false; // Not done with action, just sub-step
        }

        float goalDist = 0.0f;
        if (!g_GameState->player.groundMounted && targetType == PATH_GROUND) {
            goalDist += g_GameState->player.position.Dist3D(ws.pathFollowState.path[ws.pathFollowState.index].pos);
            for (int k = ws.pathFollowState.index; k + 1 < ws.pathFollowState.path.size(); k++) {
                if (ws.pathFollowState.path[k].type == PATH_GROUND) {
                    goalDist += ws.pathFollowState.path[k].pos.Dist3D(ws.pathFollowState.path[k + 1].pos);
                }
                else break;
            }
        }
        else if (!g_GameState->player.flyingMounted && targetType == PATH_AIR) {
            goalDist += g_GameState->player.position.Dist3D(ws.pathFollowState.path[ws.pathFollowState.index].pos);
            for (int k = ws.pathFollowState.index; k + 1 < ws.pathFollowState.path.size(); k++) {
                if (ws.pathFollowState.path[k].type == PATH_AIR) {
                    goalDist += ws.pathFollowState.path[k].pos.Dist3D(ws.pathFollowState.path[k + 1].pos);
                }
                else break;
            }
        }
        else {
            goalDist = 10000.0f;
        }

        //g_LogFile << goalDist << std::endl;
        // 4. Steer
        pilot.SteerTowards(ws.player.position, ws.player.rotation, target, targetType, ws.player, goalDist);
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
    WorldState& state;

public:
    GoapAgent(WorldState& worldState, MovementController& mc, SimpleMouseClient& mouse, SimpleKeyboardClient& keyboard, Camera& cam, MemoryAnalyzer& mem, DWORD pid, ULONG_PTR base, HWND hGameWindow)
        : state(worldState), pilot(mc), interact(mc, mouse, keyboard, cam, mem, pid, base, hGameWindow), combatController(keyboard, pilot), consoleInput(keyboard) {
        // Register Actions
        availableActions.push_back(new ActionEscapeDanger());
        availableActions.push_back(new ActionLoot(interact));

        ActionReturnWaypoint* returnAction = new ActionReturnWaypoint();
        returnAction->SetKeyboard(&keyboard, &consoleInput);
        availableActions.push_back(returnAction);

        availableActions.push_back(new ActionCombat(interact, combatController, keyboard));
        availableActions.push_back(new ActionHeal(keyboard));
        availableActions.push_back(new ActionGather(interact));
        availableActions.push_back(new ActionGrind());
        availableActions.push_back(new ActionFollowPath());
        availableActions.push_back(new ActionUnstuck(keyboard, mc));
        availableActions.push_back(new ActionInteract(interact, keyboard, mouse));
        availableActions.push_back(new ActionRespawn());
        availableActions.push_back(new ActionProfileExecutor(interact));
        availableActions.push_back(new ActionEscapeWater());
    }
    
    ~GoapAgent() {
        for (auto a : availableActions) delete a;
    }

    void Tick() {
        
        if (!g_LogFile.is_open()) {
            g_LogFile.open("SMM_Debug.log", std::ios::app);  // fallback to current dir
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

        if (bestAction) {
            if (bestAction != currentAction) {
                g_LogFile << bestAction->GetName() << std::endl;
                if (bestAction->GetName() == "Follow Path") {
                    std::vector<Vector3> empty = {};
                    if (state.pathFollowState.path.size() > 0) {
                        state.waypointReturnState.savedPath = state.pathFollowState.path;
                        //state.waypointReturnState.savedIndex = FindClosestWaypoint(empty, state.pathFollowState.path, state.player.position);
                        //g_LogFile << "Prev Index: " << state.pathFollowState.index << " | New Index: " << state.waypointReturnState.savedIndex << " | Size: " << state.pathFollowState.path.size() << std::endl;
                        //state.pathFollowState.index = state.waypointReturnState.savedIndex;
                        state.waypointReturnState.savedIndex = state.pathFollowState.index;
                        state.waypointReturnState.flyingTarget = state.pathFollowState.flyingPath;
                        if (state.pathFollowState.index >= state.pathFollowState.path.size()) {
                            state.waypointReturnState.index = 0;
                        }
                        //g_LogFile << state.player.position.Dist3D(state.waypointReturnState.savedPath[state.waypointReturnState.savedIndex].pos) << std::endl;
                    }
                }
                else if (bestAction->GetName() == "Repair Equipment") {
                    std::vector<Vector3> empty = {};
                    if (state.interactState.path.size() > 0) {
                        state.waypointReturnState.savedPath = state.interactState.path;
                        //state.waypointReturnState.savedIndex = FindClosestWaypoint(empty, state.interactState.path, state.player.position);
                        //state.interactState.index = state.waypointReturnState.savedIndex;
                        state.waypointReturnState.flyingTarget = false;
                        state.waypointReturnState.savedIndex = state.interactState.index;
                    }
                }
                else if (bestAction->GetName() == "Grind") {
                    if (state.grindState.path.size() > 0) {
                        state.waypointReturnState.savedPath    = state.grindState.path;
                        state.waypointReturnState.savedIndex   = state.grindState.index;
                        state.waypointReturnState.flyingTarget = state.grindState.canFly;
                        if (state.grindState.index >= (int)state.grindState.path.size())
                            state.waypointReturnState.index = 0;
                    }
                }
                /*if (bestAction->GetName() == "Gather Node") {
                    std::vector<Vector3> empty = {};
                    state.waypointReturnState.savedPath = state.gatherState.path;
                    state.waypointReturnState.savedIndex = FindClosestWaypoint(empty, state.gatherState.path, state.player.position);
                }*/
                if (state.waypointReturnState.savedPath.size() > 0 && state.waypointReturnState.savedIndex >= state.waypointReturnState.savedPath.size()) {
                    state.waypointReturnState.savedIndex = state.waypointReturnState.savedPath.size() - 1;
                }
                // For Grind: if there are still valid targets within pullRange, let ActionGrind
                // chain-kill them instead of immediately returning to the path.
                bool grindHasChainTarget = false;
                if (bestAction->GetName() == "Grind" && state.grindState.enabled
                    && (state.grindState.inLoop || state.grindState.engageAlways)) {
                    for (const auto& ent : state.entities) {
                        if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                            if (npc->health <= 0 || npc->reaction == 2 || npc->npcFlag != 0) continue;
                            if (state.player.position.Dist2D(npc->position) <= state.grindState.pullRange) {
                                grindHasChainTarget = true;
                                break;
                            }
                        }
                    }
                }

                // ActionGrind does NOT use WaypointReturn for post-combat recovery.
                // It navigates its own hotspot path directly, and its internal stale-path
                // check (>30 yards from current waypoint) handles any combat displacement
                // by clearing the path and snapping hotspotIndex to the nearest hotspot.
                // Adding Grind here would cause an infinite loop:
                //   Grind recalculates path → transition triggers WaypointReturn →
                //   WaypointReturn clears savedPath → Grind recalculates → repeat.
                if (!grindHasChainTarget &&
                    (state.waypointReturnState.savedPath.size() > 0) &&
                    ((bestAction->GetName() == "Follow Path") || (bestAction->GetName() == "Repair Equipment")) &&
                    (state.player.position.Dist3D(state.waypointReturnState.savedPath[state.waypointReturnState.savedIndex].pos) > 10.0f)) {
                    state.waypointReturnState.hasTarget = true;
                    for (auto action : availableActions) {
                        if (action->CanExecute(state)) {
                            int p = action->GetPriority();
                            if (p > highestPriority) {
                                highestPriority = p;
                                bestAction = action;
                            }
                        }
                    }
                }

                //if (currentAction->GetName() == "Return to previous waypoint") currentAction->ResetState();
                if (currentAction) {
                    currentAction->ResetState();
                    g_LogFile << "[GOAP] Stopping action: " << currentAction->GetName() << std::endl;
                    state.globalState.activePath = {};
                    state.globalState.activeIndex = 0;
                    pilot.Stop();
                }

                if (bestAction) {
                    g_LogFile << "[GOAP] Starting action: " << bestAction->GetName() << std::endl;

                    // Set starting state for new action
                    if (bestAction->GetName() == "Escape Danger") {
                    }
                    if (bestAction->GetName() == "Loot Corpse") {
                        g_GameState->lootState.flyingPath = g_ProfileSettings.canFlyLoot;
                    }
                    if (bestAction->GetName() == "Return to previous waypoint") {
                        g_GameState->waypointReturnState.flyingPath = g_ProfileSettings.canFlyWaypointReturn;
                    }
                    if (bestAction->GetName() == "Combat Routine") {
                        g_GameState->combatState.flyingPath = false; // combat always uses ground pathing
                    }
                    if (bestAction->GetName() == "Gather Node") {
                        g_GameState->gatherState.flyingPath = g_ProfileSettings.canFlyGather;
                    }
                    if (bestAction->GetName() == "Follow Path") {
                    }
                    if (bestAction->GetName() == "Unstuck Maneuver") {
                        g_GameState->stuckState.flyingPath = false; // unstuck always uses ground
                    }
                    if (bestAction->GetName() == "Interact With Target") {
                        g_GameState->interactState.flyingPath = g_ProfileSettings.canFlyVendor;
                    }
                }

                currentAction = bestAction;
            }
        }

        // 3. Execute
        if (currentAction) {
            bool complete = currentAction->Execute(state, pilot);
            // If action finished itself (like reaching end of path), clear current
            if (complete) {
                g_LogFile << "[GOAP] Action Complete: " << currentAction->GetName() << std::endl;
                state.globalState.activePath = {};
                state.globalState.activeIndex = 0;
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

        // 1. Already handling stuck — ActionUnstuck clears this
        if (state.stuckState.isStuck) {
            return false;
        }

        // 2. Debounce + timing state (all statics declared together so the pause
        //    guard below can reset them before any branch reads them).
        static DWORD   lastTimeMoving    = GetTickCount();
        static Vector3 netAnchorPos      = {};
        static DWORD   netAnchorTime     = 0;
        static Vector3 approachAnchorPos = {};
        static DWORD   approachAnchorTime = 0;
        DWORD now = GetTickCount();

        const float NET_PROGRESS_MIN     = 4.0f;   // yards
        const DWORD NET_PROGRESS_TIMEOUT = 5000;   // ms

        // If the script is paused or a UI frame is blocking input (vendor, chat),
        // the player is intentionally stationary — reset all timing so we don't
        // accumulate toward a stuck trigger while nothing is expected to move.
        if (g_IsPaused || state.globalState.uiBlocking || state.globalState.chatOpen) {
            lastTimeMoving     = 0;
            netAnchorTime      = 0;
            approachAnchorTime = 0;
            state.stuckState.lastCheckTime  = 0;
            state.stuckState.stuckStartTime = 0;
            return false;
        }

        // 3a-combat. Combat-approach stuck check.
        // When the bot has a target but pathfinding fails (startRef=0), no movement
        // keys are pressed during EngageTarget's click-cooldown. The IsMoving guard
        // below would suppress all stuck detection in this state.  Use separate
        // anchor variables scoped to hasTarget && !inCombat so this check never
        // fires during mounting, post-unpause delays, or any other intentionally
        // stationary situation outside of a combat approach.
        if (state.combatState.hasTarget && !state.combatState.inCombat) {
            if (approachAnchorTime == 0) {
                approachAnchorPos  = state.player.position;
                approachAnchorTime = now;
            } else if (state.player.position.Dist2D(approachAnchorPos) >= NET_PROGRESS_MIN) {
                approachAnchorPos  = state.player.position;
                approachAnchorTime = now;
            } else if (now - approachAnchorTime > NET_PROGRESS_TIMEOUT) {
                g_LogFile << "[GOAP] Combat approach stuck (< " << NET_PROGRESS_MIN
                          << " yds in 5s). Triggering unstuck." << std::endl;
                state.stuckState.stuckAngle     = state.player.rotation;
                state.stuckState.isStuck        = true;
                state.stuckState.stuckStartTime = 0;
                state.stuckState.lastStuckTime  = now;
                approachAnchorTime = 0;
                return true;
            }
        } else {
            approachAnchorTime = 0; // reset when not approaching
        }

        // Only count actual movement keys (W/S/Q/E/Ascend/Descend) as "bot is
        // navigating."  GetSteering() (right-mouse held) can be true during camera
        // adjustments, combat rotation, and the mount-cast window where W is
        // intentionally released — using it here caused false stuck triggers.
        if (pilot.IsMoving()) {
            lastTimeMoving = now;
        }

        if (now - lastTimeMoving > 600) {
            state.stuckState.stuckStartTime = 0;
            state.stuckState.lastCheckTime  = 0;
            netAnchorTime = 0; // reset so net-progress timer starts fresh when movement resumes
            return false;
        }

        // 3a. Net-progress check: catches oscillatory stuck (e.g. jumping on a slope).
        // The per-interval efficiency check misses this because individual 250ms windows
        // show good movement (bot goes up the slope / slides back), but the bot never
        // escapes the area. This check triggers if net 2D displacement stays under
        // NET_PROGRESS_MIN yards for NET_PROGRESS_TIMEOUT ms while actively moving.
        // Skipped during combat: faceTarget() keeps isSteering=true while the bot stands
        // in melee range, which would cause false positives after 5 seconds of rotation.
        if (state.combatState.inCombat || state.combatState.hasTarget) {
            netAnchorTime = 0; // reset so the timer starts fresh once combat ends
        } else if (netAnchorTime == 0) {
            netAnchorPos  = state.player.position;
            netAnchorTime = now;
        } else if (state.player.position.Dist2D(netAnchorPos) >= NET_PROGRESS_MIN) {
            netAnchorPos  = state.player.position;
            netAnchorTime = now;
        } else if (now - netAnchorTime > NET_PROGRESS_TIMEOUT) {
            g_LogFile << "[GOAP] Net-progress stuck (< " << NET_PROGRESS_MIN
                      << " yds in 5s). Triggering unstuck." << std::endl;
            state.stuckState.stuckAngle     = state.player.rotation;
            state.stuckState.isStuck        = true;
            state.stuckState.stuckStartTime = 0;
            state.stuckState.lastStuckTime  = now;
            netAnchorTime = 0;
            return true;
        }

        // 3b. Seed on first movement tick
        if (state.stuckState.lastCheckTime == 0) {
            state.stuckState.lastCheckTime = now;
            state.stuckState.lastPosition = state.player.position;
            return false;
        }

        if (state.stuckState.attemptCount >= 9) {
            EndScript(pilot, 4);
        }

        // 4. Predictive check every 250ms
        const DWORD CHECK_INTERVAL_MS = 250;
        if (now - state.stuckState.lastCheckTime >= CHECK_INTERVAL_MS) {
            float dt = (now - state.stuckState.lastCheckTime) / 1000.0f;

            // Expected speed per terrain type (yards/sec)
            float expectedSpeed;
            float actualDist;
            if (!g_GameState->player.inWater && (g_GameState->player.isFlying || g_GameState->player.flyingMounted)) {
                expectedSpeed = 14.0f;
                actualDist = state.player.position.Dist3D(state.stuckState.lastPosition);
            }
            else if (g_GameState->player.inWater) {
                expectedSpeed = 4.0f;
                actualDist = state.player.position.Dist3D(state.stuckState.lastPosition);
            }
            else {
                expectedSpeed = 7.0f;
                // Ignore Z so jumps and ramps don't mask ground stuck
                actualDist = state.player.position.Dist2D(state.stuckState.lastPosition);
            }

            float expectedDist = expectedSpeed * dt;
            // Efficiency: ratio of actual to expected movement
            float efficiency = (expectedDist > 0.01f) ? (actualDist / expectedDist) : 1.0f;

            // Advance the anchor every interval so we always measure the last window
            state.stuckState.lastPosition = state.player.position;
            state.stuckState.lastCheckTime = now;

            const float STUCK_EFFICIENCY_THRESHOLD = 0.25f; // < 25% of expected speed = stuck
            if (efficiency < STUCK_EFFICIENCY_THRESHOLD) {
                if (state.stuckState.stuckStartTime == 0) {
                    state.stuckState.stuckStartTime = now;
                }

                // Require 1 second of consistent low efficiency before triggering
                if (now - state.stuckState.stuckStartTime > 1000) {
                    g_LogFile << "[GOAP] Stuck Detected! Efficiency: " << (int)(efficiency * 100)
                        << "% (expected " << expectedDist << " yds, moved " << actualDist
                        << " yds). Attempt " << (state.stuckState.attemptCount + 1) << std::endl;

                    if (state.stuckState.attemptCount >= 8) {
                        g_LogFile << "[GOAP] Unstuck attempts exhausted! Quitting Script." << std::endl;
                        EndScript(pilot, -1);

                        state.pathFollowState.hasPath = false;
                        state.waypointReturnState.hasPath = false;
                        state.waypointReturnState.hasTarget = false;
                        state.globalState.activePath.clear();

                        state.stuckState.isStuck = false;
                        state.stuckState.attemptCount = 0;
                        state.stuckState.stuckStartTime = 0;
                        state.stuckState.lastUnstuckTime = 0;

                        pilot.Stop();
                        return false;
                    }

                    state.stuckState.stuckAngle  = state.player.rotation;
                    state.stuckState.isStuck     = true;
                    state.stuckState.stuckStartTime = 0;
                    state.stuckState.lastStuckTime  = now;
                    netAnchorTime = 0;
                    return true;
                }
            }
            else {
                // Moving well — clear stuck timer
                state.stuckState.stuckStartTime = 0;

                // Reset attempt count after 2 minutes of uninterrupted movement
                if (state.stuckState.lastUnstuckTime > 0 &&
                    now - state.stuckState.lastUnstuckTime > 120000) {
                    state.stuckState.attemptCount = 0;
                    state.stuckState.lastUnstuckTime = 0;
                }
            }
        }

        return false;
    }
};