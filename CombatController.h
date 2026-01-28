#pragma once
#include "SimpleKeyboardClient.h"
#include "MovementController.h"
#include <chrono>
#include <map>
#include <string>
#include <iostream>

class CombatController {
private:
    SimpleKeyboardClient& kbd;
    ConsoleInput console;
    MovementController pilot;

    // Cooldown tracking (in milliseconds)
    std::map<std::string, std::chrono::steady_clock::time_point> lastCastTime;

    // Simulated Holy Power (0-5)
    // NOTE: Without reading memory, we have to estimate. 
    // MOP Ret: CS generates 1, Judgment 1, Exorcism 1.
    int estimatedHolyPower = 0;

    // Ability Cooldowns (MOP 5.4 values approx)
    const int CD_CRUSADER_STRIKE = 4500;
    const int CD_HAMMER_OF_THE_RIGHTEOUS = 4500; // Shares CD with Crusader Strike
    const int CD_JUDGMENT = 6000;
    const int CD_EXORCISM = 15000;
    const int CD_HAMMER_OF_WRATH = 6000;
    const int CD_TEMPLARS_VERDICT = 0; // Spender
    const int CD_DIVINE_STORM = 0;     // AoE Spender
    const int CD_INQUISITION = 30000; // Buff maintenance

    // Helper: Check if spell is ready based on internal timer
    bool IsReady(const std::string& spell, int cooldownMs) {
        auto it = lastCastTime.find(spell);
        if (it == lastCastTime.end()) return true;

        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        return diff >= cooldownMs;
    }

    // Helper: Cast spell and update timer
    void Cast(const std::wstring& spell, int hpChange = 0) {
        std::wstring command = std::wstring(L"/cast ") += spell;
        console.SendDataRobust(command);

        lastCastTime[std::string(spell.begin(), spell.end())] = std::chrono::steady_clock::now();

        // Update estimated Holy Power
        estimatedHolyPower += hpChange;
        if (estimatedHolyPower > 5) estimatedHolyPower = 5;
        if (estimatedHolyPower < 0) estimatedHolyPower = 0;

        g_LogFile << "[COMBAT] Cast " << std::string(spell.begin(), spell.end()) << " (Est HP: " << estimatedHolyPower << ")" << std::endl;
    }

public:
    CombatController(SimpleKeyboardClient& keyboard, MovementController pilot) : kbd(keyboard), console(keyboard), pilot(pilot) {}

    void UpdateRotation(Vector3 playerPos, Vector3 targetPos, float playerRot, bool targetIsLowHealth = false) {
        // MOP RETRIBUTION PALADIN PRIORITY (5.4.8)

        // Retrieve attacker count from WorldState (accessed via pilot's extern if needed, or assume caller context)
        // Since we don't pass WorldState here explicitly, we assume g_GameState is globally available
        // as per other files.
        int attackerCount = g_GameState->combatState.attackerCount;
        bool useAoE = (attackerCount >= 3);

        // 1. Inquisition (Buff)
        // Maintain if missing or < 3 Holy Power and high duration needed.
        // Simplified: Cast if we have 3+ HP and it hasn't been cast in 30s
        if (estimatedHolyPower >= 3 && IsReady("Inquisition", CD_INQUISITION)) {
            Cast(L"Inquisition", -3);
            return;
        }

        // 2. Templar's Verdict (5 HP)
        // If we are capped, spend immediately
        if (estimatedHolyPower >= 5) {
            Cast(L"Templar's Verdict", -3);
            return;
        }

        // 3. Hammer of Wrath (Execute < 20% or during Avenging Wrath)
        if (targetIsLowHealth && IsReady("Hammer of Wrath", CD_HAMMER_OF_WRATH)) {
            Cast(L"Hammer of Wrath", 1);
            return;
        }

        // 4. GENERATORS (AoE vs Single)
        // A. Hammer of the Righteous (AoE Generator)
        // Shares Cooldown with Crusader Strike
        if (useAoE && IsReady("Hammer of the Righteous", CD_HAMMER_OF_THE_RIGHTEOUS)) {
            Cast(L"Hammer of the Righteous", 1);
            // Manually set shared cooldown
            lastCastTime["Crusader Strike"] = std::chrono::steady_clock::now();
            return;
        }

        // B. Crusader Strike (Main Generator)
        if (!useAoE && IsReady("Crusader Strike", CD_CRUSADER_STRIKE)) {
            Cast(L"Crusader Strike", 1);
            // Manually set shared cooldown
            lastCastTime["Hammer of the Righteous"] = std::chrono::steady_clock::now();
            return;
        }

        // 5. Judgment (Generator)
        if (IsReady("Judgment", CD_JUDGMENT)) {
            Cast(L"Judgment", 1);
            return;
        }

        // 6. Exorcism (Generator / Filler)
        if (IsReady("Exorcism", CD_EXORCISM)) {
            Cast(L"Exorcism", 1);
            return;
        }

        // 7. FINISHERS (3-4 HP dump if nothing else ready)
        if (estimatedHolyPower >= 3) {
            if (useAoE) {
                Cast(L"Divine Storm", -3);
            }
            else {
                Cast(L"Templar's Verdict", -3);
            }
            return;
        }

        // 8. Auto-Attack (Ensure we are attacking)
        // Chat command /startattack is good to weave in, but usually handled by spells.
        // We'll skip explicit /startattack to save chat spam time unless idle.
    }

    void ResetCombatState() {
        estimatedHolyPower = 0; // Reset HP out of combat (decays rapidly)
    }
};