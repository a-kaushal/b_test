#pragma once
#include <vector>
#include <string>
#include "Vector.h"

struct AvoidMob {
    int id;
    std::string name;
};

struct Blackspot {
    Vector3 pos;
    float radius;
};

struct ProfileSettings {
    // Toggles
    bool gatherEnabled = true;
    bool miningEnabled = true;
    bool herbalismEnabled = false;
    bool skinningEnabled = false;
    bool lootMobsEnabled = true;
    bool sellGrey = true;
    bool sellWhite = false;
    bool mailBlue = true;
    bool mailPurple = true;

    // Thresholds
    float combatRange = 5.0f;
    float lootRange = 50.0f;
    float gatherRange = 300.0f;
    int minFreeSlots = 2;

    // Lists
    std::vector<int> blacklistedItems;
    std::vector<int> blacklistedNodes;
    std::vector<Blackspot> blackspots;
    std::vector<AvoidMob> avoidMobs;

    std::string mountName = "Blue Wind Rider";
};

// Global Declaration (So everyone knows this variable exists)
extern ProfileSettings g_ProfileSettings;