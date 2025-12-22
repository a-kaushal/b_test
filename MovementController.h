#pragma once
#include "Vector.h"
#include "SimpleKeyboardClient.h"
#include <cmath>

class MovementController {
private:
    SimpleKeyboardClient& kbd;
    const float TURN_THRESHOLD = 0.2f;      // Widen slightly to prevent jitter
    const float STOP_MOVE_THRESHOLD = 1.0f; // Stop moving if turn is > 57 degrees
    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318530f;

    float NormalizeAngle(float angle) {
        while (angle <= -PI) angle += TWO_PI;
        while (angle > PI) angle -= TWO_PI;
        return angle;
    }

public:
    MovementController(SimpleKeyboardClient& keyboard) : kbd(keyboard) {}

    // Emergency Stop
    void Stop() {
        kbd.SendKey('W', 0, false);
        kbd.SendKey('A', 0, false);
        kbd.SendKey('D', 0, false);
    }

    void SteerTowards(Vector3 currentPos, float currentRot, Vector3 targetPos) {
        float dx = targetPos.x - currentPos.x;
        float dy = targetPos.y - currentPos.y;

        // 1. Calculate Error
        // WoW Heading: 0 = North (+X), PI/2 = West (+Y)
        float targetAngle = std::atan2(dy, dx);
        float angleDiff = NormalizeAngle(targetAngle - currentRot);

        // 2. Determine Desired Key States
        bool wantLeft = (angleDiff > TURN_THRESHOLD);
        bool wantRight = (angleDiff < -TURN_THRESHOLD);

        // Only move forward if we are roughly facing the target
        bool wantForward = (std::abs(angleDiff) < STOP_MOVE_THRESHOLD);

        // 3. Apply Controls (Continuous Assertion)
        // We purposefully re-send 'true' (Down) every tick. 
        // This simulates the natural "Key Repeat" of a held keyboard button
        // and ensures the game doesn't drop the input.

        // -- Turning --
        if (wantLeft) {
            kbd.SendKey('A', 0, true);  // Hold Left
            kbd.SendKey('D', 0, false); // Release Right
        }
        else if (wantRight) {
            kbd.SendKey('D', 0, true);  // Hold Right
            kbd.SendKey('A', 0, false); // Release Left
        }
        else {
            // Deadzone: Release both
            kbd.SendKey('A', 0, false);
            kbd.SendKey('D', 0, false);
        }

        // -- Throttle --
        if (wantForward) {
            kbd.SendKey('W', 0, true);  // Hold Forward
        }
        else {
            kbd.SendKey('W', 0, false); // Stop Forward
        }
    }
};