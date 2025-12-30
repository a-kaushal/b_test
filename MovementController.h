#pragma once
#include "Vector.h"
#include "SimpleKeyboardClient.h"
#include "Profile.h"
#include <cmath>
#include <string>
#include <locale>
#include <codecvt>

class MovementController {
private:
    SimpleKeyboardClient& kbd;
    const float TURN_THRESHOLD = 0.1f;      // 0.1 rad ~= 5.6 degrees

    // Loosened for flight to allow "Bank Turns" (turning while moving)
    const float GROUND_STOP_THRESHOLD = 1.0f;     // Stop moving if target is > 57 degrees off center (Ground)
    const float FLIGHT_STOP_THRESHOLD = 2.0f;     // Stop moving if target is > 115 degrees off center (Flight)

    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318530f;

    // User specified: 1.0 radian per second
    // ADJUST THIS if the bot consistently undershoots (increase value) or overshoots (decrease value)
    const float TURN_SPEED_RAD_SEC = PI;

    // Vertical control constants
    const float VERTICAL_DEADZONE = 1.5f; // Reduced slightly for tighter control
    const WORD KEY_ASCEND = VK_SPACE;
    const WORD KEY_DESCEND = 'X';

    ConsoleInput inputCommand;

    float NormalizeAngle(float angle) {
        while (angle <= -PI) angle += TWO_PI;
        while (angle > PI) angle -= TWO_PI;
        return angle;
    }

public:
    MovementController(SimpleKeyboardClient& keyboard) : kbd(keyboard), inputCommand(keyboard) {}

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
    }

    void SteerTowards(Vector3 currentPos, float currentRot, Vector3 targetPos, bool isFlying, PlayerInfo& player) {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        float dx = targetPos.x - currentPos.x;
        float dy = targetPos.y - currentPos.y;
        float dist2D = std::sqrt(dx * dx + dy * dy);
        // Check if mount is currently equipped. If it isn't then equip it
        if ((isFlying == true) && (player.flyingMounted == false)) {
            inputCommand.SendData(std::wstring(L"run if not(IsFlyableArea()and IsMounted())then CallCompanion(\"Mount\", 1) end "));
            return;
        }
        if (player.flyingMounted == true) {
            inputCommand.Reset();
        }
        
        // --- 1. Horizontal Steering (Yaw) ---
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
                durationMs -= 20;

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
				durationMs -= 20;

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
        }
    }
};