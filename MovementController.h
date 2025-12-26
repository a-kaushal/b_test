#pragma once
#include "Vector.h"
#include "SimpleKeyboardClient.h"
#include <cmath>

class MovementController {
private:
    SimpleKeyboardClient& kbd;
    const float TURN_THRESHOLD = 0.05f;      // 0.05 rad ~= 2.8 degrees
    const float STOP_MOVE_THRESHOLD = 1.0f;
    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318530f;

    // User specified: 1.0 radian per second
    // ADJUST THIS if the bot consistently undershoots (increase value) or overshoots (decrease value)
    const float TURN_SPEED_RAD_SEC = PI;

    // Vertical control constants
    const float VERTICAL_DEADZONE = 2.0f;
    const WORD KEY_ASCEND = VK_SPACE;
    const WORD KEY_DESCEND = 'X';

    float NormalizeAngle(float angle) {
        while (angle <= -PI) angle += TWO_PI;
        while (angle > PI) angle -= TWO_PI;
        return angle;
    }

public:
    MovementController(SimpleKeyboardClient& keyboard) : kbd(keyboard) {}

    // Emergency Stop
    void Stop() {
        // Must cancel async threads first
        kbd.StopHold('A');
        kbd.StopHold('D');
        kbd.StopHold(KEY_ASCEND);
        kbd.StopHold(KEY_DESCEND);

        kbd.SendKey('W', 0, false);
        kbd.SendKey('A', 0, false);
        kbd.SendKey('D', 0, false);
        kbd.SendKey(KEY_ASCEND, 0, false);
        kbd.SendKey(KEY_DESCEND, 0, false);
    }

    void SteerTowards(Vector3 currentPos, float currentRot, Vector3 targetPos, bool isFlying) {
        float dx = targetPos.x - currentPos.x;
        float dy = targetPos.y - currentPos.y;
        float dist2D = std::sqrt(dx * dx + dy * dy);

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
        bool isVerticalMove = false;

        // Loosen forward constraint slightly while flying
        float forwardThreshold = isFlying ? STOP_MOVE_THRESHOLD * 1.5f : STOP_MOVE_THRESHOLD;
        bool wantForward = (std::abs(angleDiff) < forwardThreshold);

        if (isFlying) {
            float dz = targetPos.z - currentPos.z;

            // If target is significantly above us
            if (dz > VERTICAL_DEADZONE) {
                kbd.SendKey(KEY_ASCEND, 0, true);   // Hold Space
                kbd.SendKey(KEY_DESCEND, 0, false);
                isVerticalMove = true;
            }
            // If target is significantly below us
            else if (dz < -VERTICAL_DEADZONE) {
                kbd.SendKey(KEY_DESCEND, 0, true);  // Hold X
                kbd.SendKey(KEY_ASCEND, 0, false);
                isVerticalMove = true;
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
        // Stop moving forward if we are just adjusting altitude (Vertical Takeoff/Landing)
        if (isFlying && dist2D < 1.0f && isVerticalMove) {
            kbd.SendKey('W', 0, false); // Hover mode
        }
        else if (wantForward) {
            kbd.SendKey('W', 0, true);
        }
        else {
            kbd.SendKey('W', 0, false);
        }
    }
};