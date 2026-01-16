#pragma once
#include <cmath>
#include <string>
#include <locale>
#include <codecvt>

#include "Vector.h"
#include "SimpleKeyboardClient.h"
#include "SimpleMouseClient.h"
#include "Profile.h"
#include "WorldState.h"

extern WorldState* g_GameState;

class MovementController {
private:
    SimpleKeyboardClient& kbd;
    SimpleMouseClient& mouse;
    HWND hGameWindow;

    int check = 0;
    DWORD lastSteerTime = 0;

    // --- FLIGHT CONTROL CONSTANTS ---
    const float ALIGNMENT_DEADZONE = 0.05f;      // ~3 degrees: Considered "Facing Target"
    const float BANKING_ANGLE = 0.78f;           // ~45 degrees: Max angle to hold 'W' while turning (Prevents wide drifting)
    const float STEEP_CLIMB_THRESHOLD = 1.4f;    //  degrees: Use Jump/Sit keys for vertical limits

    // Distances
    const float PRECISION_DIST = 5.0f;           // Yards: Below this, we prioritize aiming over moving
    const float COAST_DISTANCE = 2.0f;           // Yards: Stop steering, just coast through the point

    // Loosened for flight to allow "Bank Turns" (turning while moving)
    const float GROUND_STOP_THRESHOLD = 1.0f;     // Stop moving if target is > 57 degrees off center (Ground)
    const float FLIGHT_STOP_THRESHOLD = 2.0f;     // Stop moving if target is > 115 degrees off center (Flight)

    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318530f;
    
    const float PIXELS_PER_RADIAN_YAW = 200.0f;
    const float PIXELS_PER_RADIAN_PITCH = 150.0f;

    const float TURN_THRESHOLD = 0.1f;

    // User specified: 1.0 radian per second
    // ADJUST THIS if the bot consistently undershoots (increase value) or overshoots (decrease value)
    const float TURN_SPEED_RAD_SEC = PI;

    // Vertical control constants
    const float VERTICAL_DEADZONE = 1.5f; // Reduced slightly for tighter control

    // Vertical control keys
    const WORD KEY_ASCEND = VK_SPACE;
    const WORD KEY_DESCEND = 'X';

    ConsoleInput inputCommand;
    bool isSteering = false;
    
    // Obstacle Detection Variables ---
    Vector3 lastPosCheck;
    DWORD lastPosTime = 0;

    // --- CALIBRATION VARIABLES ---
    bool m_IsCalibrated = false;       // Final flag
    bool m_IsCalibrating = false;      // In-progress flag
    int m_CalibrationStep = 0;         // 0-4 = Yaw, 5-9 = Pitch
    int m_CalibrationPhase = 0;        // 0=Prep, 1=Measure
    bool m_RetryInvert = false; // <--- NEW: Toggle to flip direction on failure

    // Accumulators
    float m_AccumulatedYawK = 0.0f;
    float m_AccumulatedPitchK = 0.0f;

    DWORD m_CalibrationLastTime = 0;
    float m_StartRot = 0.0f;           // Yaw start
    float m_StartPitch = 0.0f;         // Pitch start

    // RESULTS
    float m_PixelsPerRadianYaw = 0.0f;
    float m_PixelsPerRadianPitch = 0.0f;

public:
    // --- MOUNTING LOGIC VARIABLES ---
    DWORD m_MountAttemptStart = 0;
    bool m_IsMounting = false;
    DWORD m_MountDisabledUntil = 0; // Timestamp when mounting is re-enabled

private:
    float NormalizeAngle(float angle) {
        while (angle <= -PI) angle += TWO_PI;
        while (angle > PI) angle -= TWO_PI;
        return angle;
    }

public:
    MovementController(SimpleKeyboardClient& keyboard, SimpleMouseClient& mouseClient, HWND hWin) : kbd(keyboard), mouse(mouseClient), inputCommand(keyboard), hGameWindow(hWin) {}

    void ChangeSteering(bool steer) {
        isSteering = steer;
    }
    bool GetSteering() {
        return isSteering;
    }

    // FORCE reset the mount cooldown (Call this only when safe to mount)
    void ForceResetMountCooldown() {
        m_MountDisabledUntil = 0;
        // Optionally clear any "failed attempt" counters if you have them
        g_LogFile << "[Movement] Mount cooldown forcibly reset for hybrid path." << std::endl;
    }

    // Emergency Stop
    void Stop() {
        // Must cancel async threads first
        kbd.StopHold('W');
        kbd.StopHold('S');
        kbd.StopHold('A');
        kbd.StopHold('D');
        kbd.StopHold(KEY_ASCEND);
        kbd.StopHold(KEY_DESCEND);

        kbd.SendKey('W', 0, false);
        kbd.SendKey('S', 0, false);
        kbd.SendKey('A', 0, false);
        kbd.SendKey('D', 0, false);
        kbd.SendKey(KEY_ASCEND, 0, false);
        kbd.SendKey(KEY_DESCEND, 0, false);

        mouse.ReleaseButton(MOUSE_LEFT);
        mouse.ReleaseButton(MOUSE_RIGHT);
        
        // Release Mouse if we were steering
        if (isSteering) {
            mouse.ReleaseButton(MOUSE_RIGHT);
            isSteering = false;
        }
    }

    // Check if any movement keys are currently being pressed
    bool IsMoving() {
        return kbd.IsHolding('W') || kbd.IsHolding('S') ||
            kbd.IsHolding('Q') || kbd.IsHolding('E') ||
            kbd.IsHolding(KEY_ASCEND) || kbd.IsHolding(KEY_DESCEND) ||
            mouse.IsButtonDown(MOUSE_RIGHT) || mouse.IsButtonDown(MOUSE_LEFT);
    }

    // --- NEW: CALIBRATION FUNCTION ---
    // Returns TRUE if calibration is complete/valid.
    // Returns FALSE if it is currently working (caller should return and wait).
   // --- UPDATED CALIBRATION FUNCTION ---
    bool Calibrate(float currentYaw, float currentPitch) {
        
        if (m_IsCalibrated) return true;

        DWORD now = GetTickCount();

        // 1. INITIALIZE CALIBRATION
        if (!m_IsCalibrating) {
            std::cout << "[CALIBRATION] Starting 2-Axis Calibration..." << std::endl;
            mouse.ReleaseButton(MOUSE_RIGHT); // Safety Release
            Sleep(50);

            m_IsCalibrating = true;
            m_CalibrationStep = 0;
            m_AccumulatedYawK = 0.0f;
            m_AccumulatedPitchK = 0.0f;
            m_CalibrationPhase = 0;
            m_RetryInvert = false;
            m_CalibrationLastTime = now;
            return false;
        }

        // 2. PHASE 0: PREPARE AND MOVE
        if (m_CalibrationPhase == 0) {
            // Wait for 1 second interval between steps (except first step)
            if (m_CalibrationStep > 0 && (now - m_CalibrationLastTime < 1000)) {
                return false;
            }
            // --- A. YAW CALIBRATION (Steps 0 to 4) ---
            if (m_CalibrationStep < 5) {
                // Bounds Check & Recenter (Horizontal Safe Zone)
                POINT cursor;
                GetCursorPos(&cursor);
                RECT rect; GetClientRect(hGameWindow, &rect);
                POINT topLeft = { rect.left, rect.top };
                POINT bottomRight = { rect.right, rect.bottom };
                ClientToScreen(hGameWindow, &topLeft);
                ClientToScreen(hGameWindow, &bottomRight);

                // Define Safe Zone (e.g., 200 pixels from right edge)
                long safeMaxX = bottomRight.x - 150;
                long safeMinX = topLeft.x + 150;

                // If moving +50 would hit the edge, OR if we are just too close to any edge
                if (cursor.x > safeMaxX || cursor.x < safeMinX) {
                    g_LogFile << "[CALIBRATION] Cursor near edge. Recentering..." << std::endl;

                    // Release button if held (safety)
                    mouse.ReleaseButton(MOUSE_RIGHT);
                    Sleep(20);

                    // Move to absolute center
                    POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
                    ClientToScreen(hGameWindow, &center);
                    mouse.MoveAbsolute(center.x, center.y);

                    // Wait for mouse to settle
                    Sleep(50);
                }

                m_StartRot = currentYaw;

                mouse.PressButton(MOUSE_RIGHT);
                Sleep(50);
                mouse.Move(100, 0); // Move RIGHT

                m_CalibrationLastTime = GetTickCount();
                m_CalibrationPhase = 1;
                return false;
            }

            // --- B. PITCH CALIBRATION (Steps 5 to 9) ---
            else if (m_CalibrationStep < 10) {
                m_StartPitch = currentPitch;

                // INTELLIGENT DIRECTION:
                // If looking Up (positive pitch), look Down. 
                // If looking Down (negative pitch), look Up.
                // This prevents hitting the hard clamp limits.
                int moveY = 100;
                if (currentPitch > 0.1f) moveY = 100;   // Look Down (Positive Y moves down usually)
                else if (currentPitch < -0.1f) moveY = -100; // Look Up
                else {
                    // If neutral, Alternate: Even steps down, Odd steps up
                    moveY = (m_CalibrationStep % 2 == 0) ? 100 : -100;
                }
                // 2. APPLY RETRY FIX
                // If the previous attempt failed, FLIP the direction
                if (m_RetryInvert) {
                    moveY = -moveY;
                    g_LogFile << "[CALIB PITCH] Retrying with INVERTED direction: " << moveY << std::endl;
                }

                mouse.PressButton(MOUSE_RIGHT);
                Sleep(50);
                mouse.Move(0, moveY);

                // Store the move we actually made so we can calculate K correctly
                // (We store it in the time variable or a temp variable? 
                //  Let's just remember logic: K = Pixels / Delta)
                //  We need to know 'moveY' in Phase 1. 
                //  Hack: Store it in m_StartRot temporarily since we aren't using Yaw
                m_StartRot = (float)moveY;

                m_CalibrationLastTime = GetTickCount();
                m_CalibrationPhase = 1;
                return false;
            }
        }

        // 3. PHASE 1: MEASURE RESULT
        if (m_CalibrationPhase == 1) {
            // Wait 200ms for game to update rotation in memory
            if (now - m_CalibrationLastTime < 200) {
                return false;
            }

            // --- MEASURE YAW (Steps 0-4) ---
            if (m_CalibrationStep < 5) {
                float delta = NormalizeAngle(currentYaw - m_StartRot);

                mouse.ReleaseButton(MOUSE_RIGHT);

                if (std::abs(delta) > 0.01f) {
                    float k = 100.0f / delta;
                    m_AccumulatedYawK += k;
                    g_LogFile << "[CALIB YAW] Step " << m_CalibrationStep << " | K: " << k << std::endl;
                    m_CalibrationStep++;
                }
                else {
                    g_LogFile << "[CALIB YAW] Failed (No delta). Retrying..." << std::endl;
                }
            }
            // --- MEASURE PITCH (Steps 5-9) ---
            else {
                // Read current Pitch (Assuming 'player.vertRotation' is passed as 'currentPitch')
                float delta = currentPitch - m_StartPitch; // No Normalize needed for pitch (clamped)
                float pixelsMoved = m_StartRot; // Retrieved from our hack storage

                mouse.ReleaseButton(MOUSE_RIGHT);

                g_LogFile << "[CALIB DEBUG] StartPitch: " << m_StartPitch << " EndPitch: " << currentPitch << " Delta: " << delta << std::endl;

                if (std::abs(delta) > 0.01f) {
                    float k = pixelsMoved / delta;
                    m_AccumulatedPitchK += k;
                    g_LogFile << "[CALIB PITCH] Step " << m_CalibrationStep << " | Pixels: " << pixelsMoved << " | Delta: " << delta << " | K: " << k << std::endl;
                    m_CalibrationStep++;
                }
                else {
                    g_LogFile << "[CALIB PITCH] Failed (Hit Limit?). Retrying..." << std::endl;
                    m_RetryInvert = !m_RetryInvert; // FLIP DIRECTION
                }
            }


            // --- CHECK COMPLETION ---
            if (m_CalibrationStep >= 10) {
                m_PixelsPerRadianYaw = m_AccumulatedYawK / 5.0f;
                m_PixelsPerRadianPitch = m_AccumulatedPitchK / 5.0f;

                m_IsCalibrated = true;
                m_IsCalibrating = false;

                g_LogFile << "------------------------------------------------" << std::endl;
                g_LogFile << "CALIBRATION COMPLETE" << std::endl;
                g_LogFile << "YAW K:   " << m_PixelsPerRadianYaw << std::endl;
                g_LogFile << "PITCH K: " << m_PixelsPerRadianPitch << std::endl;
                g_LogFile << "------------------------------------------------" << std::endl;
                return true;
            }

            m_CalibrationLastTime = GetTickCount();
            m_CalibrationPhase = 0;
            return false;
        }

        return false;
    }

    void SteerTowards(Vector3 currentPos, float currentRot, Vector3 targetPos, bool flyingPath, PlayerInfo& player) {

        float dx = targetPos.x - currentPos.x;
        float dy = targetPos.y - currentPos.y;
        float dz = targetPos.z - currentPos.z;
        float dist2D = std::sqrt(dx * dx + dy * dy);
        float dist3D = std::sqrt(dx * dx + dy * dy + dz * dz);

        DWORD now = GetTickCount();

        // --- 0. MOUNTING LOGIC REPLACEMENT ---
        // Verify state: If we are mounted, reset our internal flags
        if (player.flyingMounted) {
            m_IsMounting = false;
        }

        // Attempt to mount if: Requested (isFlying), Not Mounted, Not In Tunnel
        if (flyingPath && !player.flyingMounted && !player.inWater) {

            // Check 1: Are we on a cooldown from a previous failure?
            if (now < m_MountDisabledUntil) {
                // If disabled, we do nothing here and fall through to standard movement (walking).
                // Effectively ignoring the "isFlying" request for the purpose of mounting.
            }
            // Check 2: Are we currently waiting for a mount attempt to finish?
            else if (m_IsMounting) {
                // Wait for 3.8 seconds (3800ms) before verifying
                if (now - m_MountAttemptStart > 3800) {
                    if (player.areaFlyable) {
                        m_IsMounting = false;
                    }
                    // Check if it succeeded or if failed due to being under attack
                    else if ((player.flyingMounted) || (g_GameState->combatState.underAttack)) {
                        m_IsMounting = false; // Success, proceed to fly
                    }
                    else {
                        // FAILED: Set cooldown for 2 minutes (120,000 ms)
                        g_LogFile << "[Movement] Mount failed (Tunnel/Indoor?). Disabling mounting for 2 minutes." << std::endl;
                        m_MountDisabledUntil = now + 120000;
                        m_IsMounting = false;
                        // Fall through to walk logic
                    }
                }
                else {
                    // Still waiting (Casting time / Latency). Stop moving.
                    return;
                }
            }
            // Check 3: Start a new attempt
            else {
                // Send command
                inputCommand.SendDataRobust(std::wstring(L"/run if not(IsFlyableArea()and IsMounted())then CallCompanion(\"Mount\", 1) end "));
                m_MountAttemptStart = now;
                m_IsMounting = true;
                return; // Stop moving to allow cast
            }
        }

        // --- 0b. Reset Input if mounted ---
        if (player.flyingMounted == true) {
            inputCommand.Reset();
        }

        // --- 1. CALCULATE ANGLES ---
        float targetYaw = std::atan2(dy, dx);
        float yawDiff = NormalizeAngle(targetYaw - currentRot);

        // Pitch calculation (Target Pitch)
        float targetPitch = std::atan2(dz, dist2D);
        float pitchDiff = targetPitch - player.vertRotation;

        // --- 2. START STEERING INPUT ---
        if (!isSteering) {
            mouse.PressButton(MOUSE_RIGHT);
            isSteering = true;
            Sleep(10);
        }

        // --- 3. MOUSE CONTROL (YAW & PITCH) ---
        int pixelsYaw = 0;
        int pixelsPitch = 0;

        // Coasting Logic: If very close, stop twitching the mouse to avoid 180 spins
        bool isCoasting = (dist3D < COAST_DISTANCE);

        // Determine if we need "Elevator Mode" (Space/X) for extreme verticality
        bool useElevator = (std::abs(targetPitch) > STEEP_CLIMB_THRESHOLD);

        if (!isCoasting) {
            // YAW
            if (std::abs(yawDiff) > ALIGNMENT_DEADZONE) {
                pixelsYaw = (int)(yawDiff * -PIXELS_PER_RADIAN_YAW);
            }

            // PITCH (Only if flying)
            if (flyingPath) {

                // TAKEOFF LOGIC: If we want to fly, are mounted, but currently grounded -> Press Space
                bool needTakeoff = (player.flyingMounted && !player.isFlying && !player.inWater);

                if (player.flyingMounted) {
                    if (needTakeoff) {
                        kbd.SendKey(KEY_ASCEND, 0, true);   // Force Jump/Ascend
                        kbd.SendKey(KEY_DESCEND, 0, false);

                        // Still allow mouse pitch alignment so we look where we are going
                        if (std::abs(pitchDiff) > ALIGNMENT_DEADZONE) {
                            pixelsPitch = (int)(pitchDiff * -PIXELS_PER_RADIAN_PITCH);
                        }
                    }
                    else if (!useElevator) {
                        // Standard Flight
                        if (std::abs(pitchDiff) > ALIGNMENT_DEADZONE) {
                            pixelsPitch = (int)(pitchDiff * -PIXELS_PER_RADIAN_PITCH);
                        }

                        // Release Elevator Keys if we aren't using them
                        kbd.SendKey(KEY_ASCEND, 0, false);
                        kbd.SendKey(KEY_DESCEND, 0, false);
                    }
                    else {
                        // Steep Vertical (Hovering up/down)
                        if (targetPitch > 0) {
                            kbd.SendKey(KEY_ASCEND, 0, true);
                            kbd.SendKey(KEY_DESCEND, 0, false);
                        }
                        else {
                            kbd.SendKey(KEY_DESCEND, 0, true);
                            kbd.SendKey(KEY_ASCEND, 0, false);
                        }
                        // Don't fight the keys with mouse pitch
                        pixelsPitch = 0;
                        pixelsYaw = 0;
                    }
                }
            }
        }

        // Clamp Mouse Speed to prevent camera snapping
        pixelsYaw = std::clamp(pixelsYaw, -60, 60);
        pixelsPitch = std::clamp(pixelsPitch, -30, 30);

        // Apply Mouse Move
        if (pixelsYaw != 0 || pixelsPitch != 0) {
            // Recenter Cursor if it drifts too far
            RECT rect; GetClientRect(hGameWindow, &rect);
            POINT cursor; GetCursorPos(&cursor);
            POINT topLeft = { rect.left, rect.top }; ClientToScreen(hGameWindow, &topLeft);

            if (cursor.x < topLeft.x + 50 || cursor.x > topLeft.x + (rect.right - rect.left) - 50 ||
                cursor.y < topLeft.y + 50 || cursor.y > topLeft.y + (rect.bottom - rect.top) - 50) {

                POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
                ClientToScreen(hGameWindow, &center);
                mouse.MoveAbsolute(center.x, center.y);
            }
            mouse.Move(pixelsYaw, pixelsPitch);
        }

        // --- 4. THROTTLE CONTROL (SMOOTH BANKING) ---
        bool shouldMoveForward = false;

        if (isCoasting) {
            // Just push through the waypoint
            shouldMoveForward = true;
        }
        else {
            // DYNAMIC THROTTLE LOGIC:
            // 1. If we are far away (> 5y), allow wider turns (Banking).
            // 2. If we are close (< 5y), require strict alignment (Precision).

            float allowedError = (dist3D > PRECISION_DIST) ? BANKING_ANGLE : ALIGNMENT_DEADZONE * 2.0f;

            // "Arcing Safety": 
            // If the turn is sharp (> 45 deg), STOP moving to pivot.
            // This prevents "Drifting" into walls.
            if (std::abs(yawDiff) < allowedError) {
                shouldMoveForward = true;
            }
        }

        // Apply Throttle
        if ((shouldMoveForward) && (!useElevator)) {
            kbd.SendKey('W', 0, true);
        }
        else {
            kbd.SendKey('W', 0, false);
        }

        // --- 5. GROUND UNSTUCK (Spacebar Tap) ---
        if (!flyingPath && !player.flyingMounted && !isCoasting) {
            DWORD now = GetTickCount();
            if (now - lastPosTime > 1000) {
                if (currentPos.Dist2D(lastPosCheck) < 0.5f && shouldMoveForward) {
                    // We are trying to move but stuck -> Jump
                    kbd.SendKey(VK_SPACE, 0, true);
                    Sleep(50);
                    kbd.SendKey(VK_SPACE, 0, false);
                }
                lastPosCheck = currentPos;
                lastPosTime = now;
            }
        }

        //    // --- 1. Horizontal Steering (Yaw) --- (A and D old)
        //    float targetAngle = std::atan2(dy, dx);
        //    float angleDiff = NormalizeAngle(targetAngle - currentRot);

        //    bool isTurningLeft = kbd.IsHolding('A');
        //    bool isTurningRight = kbd.IsHolding('D');

        //    // Check if we need to turn
        //    if (angleDiff > TURN_THRESHOLD) {
        //        // WE WANT TO GO LEFT
        //        if (isTurningRight) kbd.StopHold('D'); // Cancel opposite turn

        //        // Only start a new turn if we aren't already holding the key
        //        if (!isTurningLeft) {
        //            // Calculate precise duration
        //            // Time = Distance / Speed
        //            float durationSeconds = std::abs(angleDiff) / TURN_SPEED_RAD_SEC;
        //            int durationMs = static_cast<int>(durationSeconds * 1000.0f);
        //            durationMs *= 0.5;

        //            // Min duration to avoid micro-presses
        //            if (durationMs > 20) {
        //                kbd.HoldKeyAsync('A', durationMs);
        //            }
        //        }
        //    }
        //    else if (angleDiff < -TURN_THRESHOLD) {
        //        // WE WANT TO GO RIGHT
        //        if (isTurningLeft) kbd.StopHold('A');

        //        if (!isTurningRight) {
        //            float durationSeconds = std::abs(angleDiff) / TURN_SPEED_RAD_SEC;
        //            int durationMs = static_cast<int>(durationSeconds * 1000.0f);
        //            durationMs *= 0.5;

        //            if (durationMs > 20) {
        //                kbd.HoldKeyAsync('D', durationMs);
        //            }
        //        }
        //    }
        //    // DEADZONE (Aligned)
        //    else {
        //        // FIX: Force stop if we are aligned, even if the timer is still running.
        //        // This prevents the bot from continuing to turn if it reached the target faster than calculated.
        //        if (isTurningLeft) kbd.StopHold('A');
        //        if (isTurningRight) kbd.StopHold('D');
        //    }
        //}

        /*// --- 2. Vertical Steering (Altitude) ---
        // Just manage the keys; don't block other movement


        if (isFlying) {
            float dz = targetPos.z - currentPos.z;

            // If target is significantly above us
            if (dz > VERTICAL_DEADZONE) {
                kbd.SendKey(KEY_ASCEND, 0, true);   // Ascend
                kbd.SendKey(KEY_DESCEND, 0, false);
            }
            // If target is significantly below us
            else if (dz < -VERTICAL_DEADZONE) {
                kbd.SendKey(KEY_DESCEND, 0, true);  // Descend
                kbd.SendKey(KEY_ASCEND, 0, false);
            }
            else {
                kbd.SendKey(KEY_ASCEND, 0, false);
                kbd.SendKey(KEY_DESCEND, 0, false);
            }
        }
        else {
            kbd.SendKey(KEY_ASCEND, 0, false);
            kbd.SendKey(KEY_DESCEND, 0, false);
        }

        // --- 3. Throttle (W) ---

        // Determine if we are facing the target enough to move forward
        // While flying, we allow a much wider angle (FLIGHT_STOP_THRESHOLD) so we curve/bank towards the target
        // instead of stopping to rotate in place.
        float stopThreshold = isFlying ? FLIGHT_STOP_THRESHOLD : GROUND_STOP_THRESHOLD;
        bool facingCorrectly = (std::abs(angleDiff) < stopThreshold);
        if ((dist2D < 10) && (std::abs(angleDiff) > (TURN_THRESHOLD*2)) && (facingCorrectly)) {
            facingCorrectly = false;
        }

        // DIAGONAL MOVEMENT FIX:
        // We removed the (dist2D < 1.0f) check here.
        // Previously, if the bot was under the target, it would stop 'W' and only use 'Space' (Elevator).
        // Now, we generally allow 'W' unless we are significantly off-angle.
        // We rely on the pathfinding logic to switch waypoints when we get close enough in 3D space.

        if (facingCorrectly) {
            kbd.SendKey('W', 0, true);
        }
        else {
            kbd.SendKey('W', 0, false);
        }*/
    }

    bool faceTarget(Vector3 currentPos, Vector3 targetPos, float currentRot) {
        float dx = targetPos.x - currentPos.x;
        float dy = targetPos.y - currentPos.y;
        float dz = targetPos.z - currentPos.z;
        float dist2D = std::sqrt(dx * dx + dy * dy);
        float dist3D = std::sqrt(dx * dx + dy * dy + dz * dz);

        DWORD now = GetTickCount();
        // --- 1. Horizontal Steering (Yaw) --- (A and D old)
        float targetAngle = std::atan2(dy, dx);
        float angleDiff = NormalizeAngle(targetAngle - currentRot);

        bool isTurningLeft = kbd.IsHolding('A');
        bool isTurningRight = kbd.IsHolding('D');

        // Check if we need to turn
        if (angleDiff > TURN_THRESHOLD) {
            // WE WANT TO GO LEFT
            if (isTurningRight) kbd.StopHold('D'); // Cancel opposite turn

            // Only start a new turn if we aren't already holding the key
            if (!isTurningLeft) {
                // Calculate precise duration
                // Time = Distance / Speed
                float durationSeconds = std::abs(angleDiff) / TURN_SPEED_RAD_SEC;
                int durationMs = static_cast<int>(durationSeconds * 1000.0f);
                durationMs *= 0.5;

                // Min duration to avoid micro-presses
                if (durationMs > 20) {
                    kbd.HoldKeyAsync('A', durationMs);
                }
            }
        }
        else if (angleDiff < -TURN_THRESHOLD) {
            // WE WANT TO GO RIGHT
            if (isTurningLeft) kbd.StopHold('A');

            if (!isTurningRight) {
                float durationSeconds = std::abs(angleDiff) / TURN_SPEED_RAD_SEC;
                int durationMs = static_cast<int>(durationSeconds * 1000.0f);
                durationMs *= 0.5;

                if (durationMs > 20) {
                    kbd.HoldKeyAsync('D', durationMs);
                }
            }
        }
        // DEADZONE (Aligned)
        else {
            // FIX: Force stop if we are aligned, even if the timer is still running.
            // This prevents the bot from continuing to turn if it reached the target faster than calculated.
            if (isTurningLeft) kbd.StopHold('A');
            if (isTurningRight) kbd.StopHold('D');
            return true;
        }
        return false;
    }
};