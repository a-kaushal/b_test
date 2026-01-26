#pragma once
#include <vector>
#include <string>
#include "Vector.h"

// --- Enums ---
enum class PlayerFactions {
    Neutral = 0,
    Alliance = 1,
    Horde = 2
};

enum class VendorType {
    Food,
    Repair,
    General
};

// --- Structs ---

struct Mob {
    int Id;
    int MapId;
    std::string Name;
};

struct Blackspot {
    Vector3 Position;     // Renamed from 'pos' to match your request
    int MapID;            // New
    float Radius;
    PlayerFactions Faction; // New

    // Constructors for easier initialization
    Blackspot() : Position(0, 0, 0), MapID(0), Radius(0), Faction(PlayerFactions::Neutral) {}
    Blackspot(Vector3 p, int map, float r, PlayerFactions f)
        : Position(p), MapID(map), Radius(r), Faction(f) {
    }
};

struct MailBox {
    int Id; // Optional identifier
    int MapId;
    Vector3 Position;
    std::string Name;
};

struct Vendor {
    int Id;
    std::string Name;
    int MapId;
    Vector3 Position;
    VendorType Type;
};

// --- Settings ---
struct ProfileSettings {
    // Toggles
    bool gatherEnabled = true;
    bool miningEnabled = true;
    bool herbalismEnabled = true;
    bool skinningEnabled = false;
    bool lootMobsEnabled = true;
    bool lootingEnabled = true;
    bool mailingEnabled = false;
    bool sellGrey = true;
    bool sellWhite = false;
    bool mailBlue = true;
    bool mailPurple = true;

    // Thresholds
    float combatRange = 5.0f;
    float lootRange = 50.0f;
    float gatherRange = 300.0f;
    int minFreeSlots = 2;

    // Lists (Updated types)
    std::vector<int> blacklistedItems;
    std::vector<int> blacklistedNodes;

    std::vector<Mob> avoidMobs;
    std::vector<Blackspot> blackspots;
    std::vector<Blackspot> ignoredAreas;
    std::vector<int> wantedObjects;
    std::vector<MailBox> mailboxes;
    std::vector<Vendor> vendors;

    std::string mountName = "Blue Wind Rider";
};

// Global Declaration (So everyone knows this variable exists)
extern ProfileSettings g_ProfileSettings;