#pragma once
#include "SimpleKeyboardClient.h"
#include "MovementController.h"
#include <chrono>
#include <map>
#include <string>
#include <iostream>

// MoP 5.4.8 Retribution Paladin rotation.
//
// Single-target priority:
//   1. Seal of Truth (once per session)
//   2. Avenging Wrath (off-GCD, use on CD)
//   3. Lay on Hands / Word of Glory / Flash of Light (emergency healing)
//   4. Inquisition (maintain ~100% uptime from level 81; spend 3 HP)
//   5. Hammer of Wrath (execute, target <20% HP; also usable during Avenging Wrath)
//   6. Templar's Verdict at 5 HP (or 3+ HP during Avenging Wrath)
//   7. Crusader Strike / Hammer of the Righteous (HP generator)
//   8. Judgment (HP generator + debuff)
//   9. Exorcism (HP generator, instant via Art of War proc)
//  10. Holy Wrath (AoE filler)
//
// AoE (2+ enemies): replace TV with Divine Storm, CS with HotR; add Holy Wrath.

class CombatController {
private:
    SimpleKeyboardClient& kbd;
    ConsoleInput console;
    MovementController pilot;

    // Cooldown tracking (last-cast timestamps)
    std::map<std::string, std::chrono::steady_clock::time_point> lastCastTime;

    // Holy Power (0-5, estimated from cast history)
    int estimatedHolyPower = 0;

    // Combat session flags
    bool hasStartedAttack  = false;
    bool hasSeal           = false;

    // Inquisition buff tracking
    bool inquisitionActive = false;
    std::chrono::steady_clock::time_point inquisitionExpiry{};

    // Avenging Wrath active window tracking (off-GCD, 20s duration)
    bool avengingWrathActive = false;
    std::chrono::steady_clock::time_point avengingWrathExpiry{};

    // ── Cooldowns (ms) ────────────────────────────────────────────
    static constexpr int CD_CRUSADER_STRIKE         = 4500;
    static constexpr int CD_HAMMER_OF_THE_RIGHTEOUS = 4500;  // shares CD with CS
    static constexpr int CD_JUDGMENT                = 6000;
    static constexpr int CD_EXORCISM                = 15000;
    static constexpr int CD_HAMMER_OF_WRATH         = 6000;
    static constexpr int CD_HOLY_WRATH              = 9000;
    static constexpr int CD_AVENGING_WRATH          = 120000; // 2-min CD
    static constexpr int CD_LAY_ON_HANDS            = 600000; // 10-min CD

    // Inquisition: 20s per HP spent (60s at 3 HP); refresh when <5s remain
    static constexpr int INQUISITION_HP_COST        = 3;
    static constexpr int INQUISITION_DURATION_MS    = 60000;  // 60s at 3 HP
    static constexpr int INQUISITION_REFRESH_MS     = 5000;   // refresh when <5s left

    // Avenging Wrath window: 20s
    static constexpr int AVENGING_WRATH_DURATION_MS = 20000;

    // ── Ability unlock levels (MoP 5.4.8) ────────────────────────
    // Adjust these if your server unlocks abilities at different levels.
    static constexpr int LVL_CRUSADER_STRIKE         = 3;
    static constexpr int LVL_FLASH_OF_LIGHT          = 5;
    static constexpr int LVL_JUDGMENT                = 7;
    static constexpr int LVL_LAY_ON_HANDS            = 8;
    static constexpr int LVL_TEMPLAR_VERDICT         = 10;
    static constexpr int LVL_WORD_OF_GLORY           = 10;
    static constexpr int LVL_EXORCISM                = 12;
    static constexpr int LVL_HAMMER_OF_WRATH         = 16;
    static constexpr int LVL_HOLY_WRATH              = 20;   // AoE, stuns undead/demons
    static constexpr int LVL_HAMMER_OF_THE_RIGHTEOUS = 20;   // Prot/Ret AoE generator
    static constexpr int LVL_AVENGING_WRATH          = 20;   // Major damage CD
    static constexpr int LVL_DIVINE_STORM            = 26;   // AoE HP spender
    static constexpr int LVL_SEAL_OF_TRUTH           = 24;   // Single-target DPS seal
    static constexpr int LVL_INQUISITION             = 81;   // Sustained damage buff

    // ── Helpers ───────────────────────────────────────────────────

    bool IsReady(const std::string& spell, int cooldownMs) const {
        auto it = lastCastTime.find(spell);
        if (it == lastCastTime.end()) return true;
        auto now  = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        return diff >= cooldownMs;
    }

    // Send /cast <spell>, record cast time, apply Holy Power delta.
    void Cast(const std::wstring& spell, int hpChange = 0) {
        console.SendDataRobust(std::wstring(L"/cast ") + spell, g_GameState->globalState.chatOpen);
        auto now = std::chrono::steady_clock::now();
        lastCastTime[std::string(spell.begin(), spell.end())] = now;
        estimatedHolyPower = std::clamp(estimatedHolyPower + hpChange, 0, 5);
    }

    void Exec(const std::wstring& cmd) {
        console.SendDataRobust(cmd, g_GameState->globalState.chatOpen);
    }

    // True if Inquisition buff is active with >REFRESH_MS remaining.
    bool InquisitionIsHealthy() const {
        if (!inquisitionActive) return false;
        auto now       = std::chrono::steady_clock::now();
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                             inquisitionExpiry - now).count();
        return remaining > INQUISITION_REFRESH_MS;
    }

    // Cast Inquisition and record expiry.
    void CastInquisition() {
        Cast(L"Inquisition", -INQUISITION_HP_COST);
        inquisitionActive = true;
        inquisitionExpiry = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(INQUISITION_DURATION_MS);
    }

    // Cast Avenging Wrath (off-GCD) and record active window.
    void CastAvengingWrath() {
        Cast(L"Avenging Wrath");
        avengingWrathActive = true;
        avengingWrathExpiry = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(AVENGING_WRATH_DURATION_MS);
    }

    bool IsAvengingWrathActive() const {
        if (!avengingWrathActive) return false;
        return std::chrono::steady_clock::now() < avengingWrathExpiry;
    }

public:
    CombatController(SimpleKeyboardClient& keyboard, MovementController p)
        : kbd(keyboard), console(keyboard), pilot(p) {}

    // Call once per combat tick.
    //   targetIsLowHealth — true when target HP < ~20% (execute phase)
    //   playerLevel       — gates ability availability
    //   playerHealthPct   — 0.0–1.0
    void UpdateRotation(Vector3 playerPos, Vector3 targetPos, float playerRot,
                        bool  targetIsLowHealth = false,
                        int   playerLevel       = 90,
                        float playerHealthPct   = 1.0f) {

        int  attackerCount = g_GameState->combatState.attackerCount;
        bool useAoE        = (attackerCount >= 2);  // AoE rotation at 2+ enemies
        bool inAW          = IsAvengingWrathActive();

        // ── 0. START ATTACK ──────────────────────────────────────────
        if (!hasStartedAttack) {
            Exec(L"/startattack");
            hasStartedAttack = true;
        }

        // ── 1. SEAL MAINTENANCE (once per session) ───────────────────
        // Seal of Truth for single-target (Censure DoT stacks).
        // Falls back to Seal of Righteousness before level 24.
        if (!hasSeal) {
            if (playerLevel >= LVL_SEAL_OF_TRUTH) {
                Cast(L"Seal of Truth");
            } else {
                Cast(L"Seal of Righteousness");
            }
            hasSeal = true;
            return;
        }

        // ── 2. AVENGING WRATH (off-GCD major damage CD) ──────────────
        if (playerLevel >= LVL_AVENGING_WRATH &&
            IsReady("Avenging Wrath", CD_AVENGING_WRATH)) {
            CastAvengingWrath();
            // Off-GCD: do NOT return; continue rotation on the same tick.
        }

        // ── 3. EMERGENCY HEALING ─────────────────────────────────────

        // Lay on Hands — 10-min CD, use only at very low HP
        if (playerHealthPct < 0.20f &&
            playerLevel >= LVL_LAY_ON_HANDS &&
            IsReady("Lay on Hands", CD_LAY_ON_HANDS)) {
            Cast(L"Lay on Hands");
            return;
        }

        // Word of Glory — HP-free instant heal; use when not in Inquisition window
        // (spending HP on WoG while Inquisition needs refresh delays the buff)
        if (playerHealthPct < 0.30f &&
            playerLevel >= LVL_WORD_OF_GLORY &&
            estimatedHolyPower >= 3 &&
            !InquisitionIsHealthy()) {
            Cast(L"Word of Glory", -3);
            return;
        }

        // Flash of Light — fast heal, costs mana
        if (playerHealthPct < 0.45f &&
            playerLevel >= LVL_FLASH_OF_LIGHT &&
            IsReady("Flash of Light", 1500)) {
            Cast(L"Flash of Light");
            return;
        }

        // ── 4. INQUISITION (maintain ~100% uptime from level 81) ─────
        if (playerLevel >= LVL_INQUISITION &&
            estimatedHolyPower >= INQUISITION_HP_COST &&
            !InquisitionIsHealthy()) {
            CastInquisition();
            return;
        }

        // ── 5. HAMMER OF WRATH (execute / Avenging Wrath bonus) ──────
        // Normally usable at target <20% HP; during Avenging Wrath it's
        // usable at any target health.
        bool howAvailable = (targetIsLowHealth || inAW) &&
                             playerLevel >= LVL_HAMMER_OF_WRATH &&
                             IsReady("Hammer of Wrath", CD_HAMMER_OF_WRATH);
        if (howAvailable) {
            Cast(L"Hammer of Wrath", 1);
            return;
        }

        // ── 6. HOLY POWER DUMP ───────────────────────────────────────
        // Spend at 5 HP normally; spend at 3+ HP during Avenging Wrath to
        // maximise the damage window.
        int spendThreshold = inAW ? 3 : 5;

        if (estimatedHolyPower >= spendThreshold) {
            // AoE: Divine Storm (2+ targets) — replaces Templar's Verdict
            if (useAoE && playerLevel >= LVL_DIVINE_STORM) {
                Cast(L"Divine Storm", -3);
                return;
            }
            // Single target: Templar's Verdict
            if (playerLevel >= LVL_TEMPLAR_VERDICT) {
                Cast(L"Templar's Verdict", -3);
                return;
            }
        }

        // ── 7. HP GENERATORS ─────────────────────────────────────────

        // Hammer of the Righteous — AoE splash, shares CD with Crusader Strike.
        // Preferred in AoE; also fine single-target when HotR is available.
        if (playerLevel >= LVL_HAMMER_OF_THE_RIGHTEOUS &&
            IsReady("Hammer of the Righteous", CD_HAMMER_OF_THE_RIGHTEOUS) &&
            (useAoE || !IsReady("Crusader Strike", CD_CRUSADER_STRIKE))) {
            Cast(L"Hammer of the Righteous", 1);
            lastCastTime["Crusader Strike"] = std::chrono::steady_clock::now();
            return;
        }

        // Crusader Strike — primary single-target HP generator
        if (playerLevel >= LVL_CRUSADER_STRIKE &&
            IsReady("Crusader Strike", CD_CRUSADER_STRIKE)) {
            Cast(L"Crusader Strike", 1);
            lastCastTime["Hammer of the Righteous"] = std::chrono::steady_clock::now();
            return;
        }

        // ── 8. JUDGMENT (ranged HP gen, applies debuff) ───────────────
        if (playerLevel >= LVL_JUDGMENT &&
            IsReady("Judgment", CD_JUDGMENT)) {
            Cast(L"Judgment", 1);
            return;
        }

        // ── 9. EXORCISM (instant filler via Art of War proc, +1 HP) ───
        if (playerLevel >= LVL_EXORCISM &&
            IsReady("Exorcism", CD_EXORCISM)) {
            Cast(L"Exorcism", 1);
            return;
        }

        // ── 10. HOLY WRATH (AoE filler, stuns undead/demons) ─────────
        if (playerLevel >= LVL_HOLY_WRATH &&
            IsReady("Holy Wrath", CD_HOLY_WRATH)) {
            Cast(L"Holy Wrath");
            return;
        }

        // Nothing to cast — auto-attack from /startattack handles the rest.
    }

    void ResetCombatState() {
        estimatedHolyPower = 0;
        hasStartedAttack   = false;
        // hasSeal persists across fights (seals are permanent until death/relog).
        // inquisitionActive/expiry persists — the buff can carry between pulls.
        // avengingWrathActive/expiry persists — it may still be running between pulls.
    }
};
