#pragma once
#include <windows.h>
#include <vector>
#include <mutex>
#include <iostream>

#include "Vector.h"

struct InputRegister {
	uint32_t movementRegister[8]; // 0 = none, 1 = forward, 2 = backward, 4 = left, 8 = right, 16 = strafe left, 32 = strafe right, 64 = rotate anticlockwise, 128 = rotate clockwise
	int movementDuration[8]; // Duration for each movement key
	bool followingPath;
    bool grindingEnemy;
	bool inCombat;
    Vector3 currentDestination;
    std::vector<Vector3> path;
    size_t pathIndex;
	std::mutex mtx; //Not sure what this is for, but keeping it here
};

// Function to convert radians to degrees
inline float RadToDeg(float radians) {
    return radians * (180.0f / 3.14159265f);
}

inline uint32_t ManipulateBit(int bitPosition, uint32_t originalValue, bool setBit) {
    if (setBit) {
        return originalValue | (1 << bitPosition);
    } else {
        return originalValue & ~(1 << bitPosition);
    }
}

inline int Char_Rotate_To(float current_rotation, float target_rotation, WORD& key, float target_dist = 20) {
    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318530f;

    bool clockwise;

    // 1. Calculate the raw difference
    float diff = target_rotation - current_rotation;

    // 2. Normalize the difference to the range (-PI, PI]
    // This handles the "wrap around" (e.g. going from 6.0 rads to 0.1 rads)
    while (diff <= -PI) diff += TWO_PI;
    while (diff > PI) diff -= TWO_PI;
    std::cout << diff << std::endl;
    std::cout << current_rotation << std::endl;

    // 3. Determine direction based on the sign of the difference
    // NOTE: This depends on your game's coordinate system.
    // If Positive rotation is Clockwise in your game, use (diff > 0).
    // If Positive rotation is Counter-Clockwise, use (diff < 0).
    // I have assumed standard math (CCW is positive), so negative diff means CW.
    clockwise = (diff < 0);

    // 4. Calculate duration based on the absolute distance of the shortest path
    // We use std::abs(diff) so the duration is always positive and correct
    int duration = int((std::abs(diff) / PI) * 1000);

    // Safety check: Don't rotate if the difference is negligible
    if (std::abs(diff) < 0.2f) {
        return 0;
    }

    if (clockwise == true) {
        key = 'D';
    }
    else if (clockwise == false) {
        key = 'A';
    }
	std::cout << key << std::endl;

    return duration;
};

inline float TurnCharacter(float currentAngle, float targetAngle, float turnSpeed) {
    float angleDiff = targetAngle - currentAngle;

    currentAngle = RadToDeg(currentAngle);
    targetAngle = RadToDeg(targetAngle);
    turnSpeed = turnSpeed * (180.0f / 3.14159265f); // Convert to degrees per second

    // Normalize angleDiff to the range [-180, 180]
    while (angleDiff > 180.0f) angleDiff -= 360.0f;
    while (angleDiff < -180.0f) angleDiff += 360.0f;

    // Determine turn time
    float turnTime = angleDiff / turnSpeed;
    return turnTime;
}