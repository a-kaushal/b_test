#pragma once
#include <vector>
#include <string>
#include <Windows.h>
#include "dllmain.h"
#include "Vector.h"
#include "WorldState.h"
#include "Behaviors.h" // Your existing behaviors

// Abstract Base Class for all Profiles
class BotProfile {
public:
    virtual ~BotProfile() = default;

    // Called once when profile is loaded
    virtual void Setup() = 0;

    // Called every frame (Tick). 
    // Return true to keep running, false if profile is "finished"
    virtual void Tick() = 0;

protected:
    // --- SDK HELPER FUNCTIONS ---

    // Returns true if task is IN PROGRESS, false if COMPLETE
    bool GrindMobsUntil(int mapId, int targetLevel, std::string taskName, std::string hotspots) {
        if (g_GameState->player.level >= targetLevel) {
            // Task Complete
            g_GameState->combatState.enabled = false;
            g_GameState->pathFollowState.enabled = false;
            return false;
        }

        // Setup State
        g_GameState->combatState.enabled = true;

        // Simple Logic: If not fighting and no path, start pathing
        if (!g_GameState->combatState.inCombat && !g_GameState->pathFollowState.hasPath) {
            // Parse hotspots string and start following path
            // You can reuse your existing FollowPath logic here
            FollowPath(mapId, hotspots, true, false);
        }

        return true; // Task in progress
    }

    bool MoveTo(int mapId, std::string hotspots) {
        // Simple distance check to last point
        std::vector<Vector3> path = ParsePathString(hotspots);
        if (path.empty()) return false;

        if (g_GameState->player.position.Dist3D(path.back()) < 5.0f) return false; // Arrived

        if (!g_GameState->pathFollowState.hasPath) {
            FollowPath(mapId, hotspots, false, true); // Non-looping, Flying allowed
        }
        return true;
    }

    // Wrappers for your lists
    void AddVendor(int id, std::string name, Vector3 pos, std::string type) {
        // Add to g_GameState->globalState.vendors (You need to add this vector to WorldState)
    }

    void AddBlackspot(Vector3 pos, float radius) {
        // Add to g_GameState->globalState.blackspots (You need to add this vector to WorldState)
    }
};

// Factory typedefs for DLL loading
typedef BotProfile* (*CreateProfileFn)();
typedef void (*DeleteProfileFn)(BotProfile*);