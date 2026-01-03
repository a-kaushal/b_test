#pragma once
#include "Vector.h"
#include "SimpleKeyboardClient.h"
#include "SimpleMouseClient.h"
#include "Profile.h"
#include <cmath>
#include <string>
#include <locale>
#include <codecvt>

class MovementController {
private:
    SimpleKeyboardClient& kbd;
    SimpleMouseClient& mouse;
    HWND hGameWindow;

    // --- CONFIGURATION CONSTANTS ---
    const float TURN_THRESHOLD = 0.1f;
    const float PITCH_DEADZONE = 0.08f;
    const float STEEP_CLIMB_THRESHOLD = 1.1f;

    // Thresholds for "Coasting" (flying blind through the waypoint)
    const float COAST_DISTANCE = 3.0f; // Yards

    // Loosened for flight to allow "Bank Turns" (turning while moving)
    const float GROUND_STOP_THRESHOLD = 1.0f;     // Stop moving if target is > 57 degrees off center (Ground)
    const float FLIGHT_STOP_THRESHOLD = 2.0f;     // Stop moving if target is > 115 degrees off center (Flight)

    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318530f;
    
    const float PIXELS_PER_RADIAN_YAW = 25.0f;
    const float PIXELS_PER_RADIAN_PITCH = 10.0f;

    // User specified: 1.0 radian per second
    // ADJUST THIS if the bot consistently undershoots (increase value) or overshoots (decrease value)
    const float TURN_SPEED_RAD_SEC = PI;

    // Vertical control constants
    const float VERTICAL_DEADZONE = 1.5f; // Reduced slightly for tighter control
    const WORD KEY_ASCEND = VK_SPACE;
    const WORD KEY_DESCEND = 'X';

    ConsoleInput inputCommand;
    bool isSteering = false;
    
    // Obstacle Detection Variables ---
    Vector3 lastPosCheck;
    DWORD lastPosTime = 0;

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
        
        // Release Mouse if we were steering
        if (isSteering) {
            mouse.ReleaseButton(MOUSE_RIGHT);
            isSteering = false;
        }
    }

    void SteerTowards(Vector3 currentPos, float currentRot, Vector3 targetPos, bool isFlying, PlayerInfo& player) {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        float dx = targetPos.x - currentPos.x;
        float dy = targetPos.y - currentPos.y;
        float dz = targetPos.z - currentPos.z;
        float dist2D = std::sqrt(dx * dx + dy * dy);
        // Check if mount is currently equipped. If it isn't then equip it
        if ((isFlying == true) && (player.flyingMounted == false)) {
            inputCommand.SendDataRobust(std::wstring(L"/run if not(IsFlyableArea()and IsMounted())then CallCompanion(\"Mount\", 1) end "));
            return;
        }
        if (player.flyingMounted == true) {
            inputCommand.Reset();
        }
        
        // WIP MOUSE MOVEMENT
        // --- CHECK IF COASTING ---
        // If we are very close to the waypoint, STOP steering and just drive forward.
        // This prevents the "Jitter" where atan2 flips out at close range.
        bool isCoasting = (dist2D < COAST_DISTANCE);

        // --- 1. CALCULATE YAW ---
        float targetYaw = std::atan2(dy, dx);
        float yawDiff = NormalizeAngle(targetYaw - currentRot);

        // --- 2. CALCULATE PITCH ---
        float pitchDiff = 0.0f;
        float targetPitch = 0.0f;
        bool useElevatorMode = false;

        if (isFlying && !isCoasting) { // Disable elevator logic if coasting
            targetPitch = std::atan2(dz, dist2D);

            if (targetPitch > STEEP_CLIMB_THRESHOLD) {
                useElevatorMode = true;
                kbd.SendKey(KEY_ASCEND, 0, true);
                kbd.SendKey(KEY_DESCEND, 0, false);
            }
            else if (targetPitch < -STEEP_CLIMB_THRESHOLD) {
                useElevatorMode = true;
                kbd.SendKey(KEY_DESCEND, 0, true);
                kbd.SendKey(KEY_ASCEND, 0, false);
            }
            else {
                kbd.SendKey(KEY_ASCEND, 0, false);
                kbd.SendKey(KEY_DESCEND, 0, false);
                pitchDiff = targetPitch - player.vertRotation;
            }
        }
        else {
            kbd.SendKey(KEY_ASCEND, 0, false);
            kbd.SendKey(KEY_DESCEND, 0, false);
        }

        // --- 3. MOUSE STEERING ---
        if (!isSteering) {
            mouse.PressButton(MOUSE_RIGHT);
            isSteering = true;
            Sleep(10);
        }

        int pixelsYaw = 0;
        int pixelsPitch = 0;

        if (!isCoasting) {
            // Yaw
            if (std::abs(yawDiff) > TURN_THRESHOLD) {
                pixelsYaw = (int)(yawDiff * -PIXELS_PER_RADIAN_YAW);
            }

            // Pitch (Only if not using elevator keys)
            if (isFlying && !useElevatorMode) {
                if (std::abs(pitchDiff) > PITCH_DEADZONE) {
                    pixelsPitch = (int)(pitchDiff * -PIXELS_PER_RADIAN_PITCH);
                }
            }

            // Clamp
            pixelsYaw = std::clamp(pixelsYaw, -60, 60);
            pixelsPitch = std::clamp(pixelsPitch, -30, 30);
        }
        else {
            // FORCE STRAIGHT: We are coasting through the waypoint
            pixelsYaw = 0;
            pixelsPitch = 0;
        }

        // Execute Mouse Move
        if (pixelsYaw != 0 || pixelsPitch != 0) {
            // Smart Recenter
            POINT cursor; GetCursorPos(&cursor);
            RECT rect; GetClientRect(hGameWindow, &rect);
            POINT topLeft = { rect.left, rect.top };
            POINT bottomRight = { rect.right, rect.bottom };
            ClientToScreen(hGameWindow, &topLeft);
            ClientToScreen(hGameWindow, &bottomRight);

            long minX = topLeft.x + 20; long maxX = bottomRight.x - 20;
            long minY = topLeft.y + 20; long maxY = bottomRight.y - 20;

            if ((cursor.x + pixelsYaw > maxX) || (cursor.x + pixelsYaw < minX) ||
                (cursor.y + pixelsPitch > maxY) || (cursor.y + pixelsPitch < minY))
            {
                POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
                ClientToScreen(hGameWindow, &center);
                mouse.MoveAbsolute(center.x, center.y);
            }
            mouse.Move(pixelsYaw, pixelsPitch);
        }

        // WIP MOUSE MOVEMENT
        // --- 4. THROTTLE (W) ---
        if (useElevatorMode) {
            kbd.SendKey('W', 0, false);
        }
        else {
            // If coasting, ALWAYS drive forward
            if (isCoasting) {
                kbd.SendKey('W', 0, true);
            }
            else {
                // Standard Steering Throttle Logic
                float stopThreshold = isFlying ? FLIGHT_STOP_THRESHOLD : GROUND_STOP_THRESHOLD;
                bool facingCorrectly = (std::abs(yawDiff) < stopThreshold);

                if (facingCorrectly) {
                    kbd.SendKey('W', 0, true);
                }
                else {
                    kbd.SendKey('W', 0, false);
                }
            }
        }

        // --- 5. OBSTACLE JUMP LOGIC (Ground Only) ---
        if (!isFlying && !player.flyingMounted && !useElevatorMode && !isCoasting) {
            DWORD now = GetTickCount();
            if (now - lastPosTime > 500) {
                if (currentPos.Dist2D(lastPosCheck) < 1.0f) {
                    // Check throttle state via loose logic
                    float stopThreshold = GROUND_STOP_THRESHOLD;
                    if (std::abs(yawDiff) < stopThreshold) {
                        kbd.SendKey(VK_SPACE, 0, true);
                        Sleep(20);
                        kbd.SendKey(VK_SPACE, 0, false);
                    }
                }
                lastPosCheck = currentPos;
                lastPosTime = now;
            }
        }
        
        // --- 1. Horizontal Steering (Yaw) --- (A and D old)
        /*float targetAngle = std::atan2(dy, dx);
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
        }
        
        // --- 2. Vertical Steering (Altitude) ---
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
};