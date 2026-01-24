#pragma once
#include <string>
#include <vector>
#include <fstream>
#include "json.hpp"
#include "Logger.h"
#include "Vector.h"
#include "Profile.h"

using json = nlohmann::json;

class MovementController;
struct WorldState;

// --- The Profile System ---
class ProfileSystem {
private:
    ProfileSettings settings;
    std::vector<AvoidMob> avoidList;
    std::vector<Blackspot> blackspots;
    std::vector<Vector3> path;

    int currentWaypoint = 0;
    bool isActive = false;

public:
    // 1. LOAD: Parses the JSON string into C++ structs
    bool LoadProfile(const std::string& jsonContent, std::string& outError) {
        try {
            auto j = json::parse(jsonContent);

            // Settings
            if (j.contains("settings")) {
                settings.sellGrey = j["settings"].value("sell_grey", false);
                settings.sellWhite = j["settings"].value("sell_white", false);
                settings.gatherEnabled = j["settings"].value("gather_enabled", false);
            }

            // Avoid Mobs
            avoidList.clear();
            if (j.contains("avoid_mobs")) {
                for (const auto& item : j["avoid_mobs"]) {
                    avoidList.push_back({ item["id"], item.value("name", "Unknown") });
                }
            }

            // Blackspots
            blackspots.clear();
            if (j.contains("blackspots")) {
                for (const auto& item : j["blackspots"]) {
                    blackspots.push_back({
                        Vector3(item["x"], item["y"], item["z"]),
                        item["radius"]
                        });
                }
            }

            // Path
            path.clear();
            if (j.contains("path")) {
                for (const auto& item : j["path"]) {
                    path.push_back(Vector3(item["x"], item["y"], item["z"]));
                }
            }

            // Reset State
            currentWaypoint = 0;

            // Logic: Find closest waypoint to start from (like your C# example)
            if (!path.empty()) {
                currentWaypoint = GetClosestWaypointIndex();
            }

            isActive = true;
            return true;
        }
        catch (json::parse_error& e) {
            outError = e.what();
            return false;
        }
    }

    // 2. TICK: Called every frame to execute behavior
    template <typename T>
    void Tick(MovementController* pilot, T* gameState) {
        if (!isActive || path.empty()) return;

        if (gameState) {
            // This line is now checked at link time, not compile time!
            gameState->gatherState.enabled = settings.gatherEnabled;
        }
    }

    // Helper to find closest point
    int GetClosestWaypointIndex() {
        // You need access to player position here. 
        // Assuming global 'g_GameState' or similar exists, or pass it in.
        return 0; // Placeholder: Implement your distance check logic here
    }
};