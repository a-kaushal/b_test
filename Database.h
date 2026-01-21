#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>

int ENEMY_NAME_COLUMN_INDEX = 10;

// Constants for Creature Template Columns
const int COL_CREATURE_FACTION_A = 18;
const int COL_CREATURE_FACTION_H = 19;
int NPC_FLAG_COLUMN_INDEX = 20;
// Column index for 'npc_rank' (0=Normal, 1=Elite, 2=Rare Elite, 3=Boss)
const int COL_NPC_RANK = 24;

// Constants for Reaction Results
const int REACTION_HOSTILE = 0; // Red
const int REACTION_NEUTRAL = 1; // Yellow
const int REACTION_FRIENDLY = 2; // Green

// Standard Player Faction Template IDs (from DBC)
const uint32_t FACTION_TEMPLATE_ALLIANCE_PLAYER = 1;
const uint32_t FACTION_TEMPLATE_HORDE_PLAYER = 2;

int ITEM_NAME_COLUMN_INDEX = 4;

int OBJ_NAME_COLUMN_INDEX = 3;

// Column indices for Game Objects (from your schema)
const int COL_OBJ_TYPE = 1;
const int COL_OBJ_LOCK_ID = 16; // data0

// Column indices for Lock.csv
const int CSV_COL_LOCK_ID = 0;
const int CSV_COL_LOCK_REQ_TYPE = 1; // 2 = Skill
const int CSV_COL_LOCK_TYPE = 9;  // 2=Herb, 3=Mine
const int CSV_COL_SKILL_REQ = 17; // Skill Level

using namespace std;

enum GatherType {
    GATHER_NONE = 0,
    GATHER_HERB = 1,
    GATHER_MINE = 2
};

// Represents the herb and mine status of objects from the Lock.dbc file
struct LockInfo {
    int typeIndex; // 2 for Herb, 3 for Mine
    int skillLevel;
};

// Creature spawn locations
struct CreatureSpawn {
    uint32_t entry;
    uint32_t mapId;
    Vector3 position;
};

// Represents one row from FactionTemplate.dbc
struct FactionTemplateEntry {
    uint32_t ID;            // Col 0
    uint32_t Faction;       // Col 1 (Group ID, e.g., Stormwind)
    uint32_t Flags;         // Col 2
    uint32_t FactionFlags;  // Col 3 (My Group Mask - Who am I?)
    uint32_t FriendlyMask;  // Col 4 (Who do I help?)
    uint32_t HostileMask;   // Col 5 (Who do I hate?)
    uint32_t EnemyFactions[4];  // Col 6-9 (Specific enemies)
    uint32_t FriendFactions[4]; // Col 10-13 (Specific friends)
    uint32_t npcRank; // Col
};

// Represents creature data needed for logic checks
struct CreatureTemplateEntry {
    uint32_t Entry;
    string Name;
    uint32_t FactionA;
    uint32_t FactionH;
    uint32_t MinLevel;
    uint32_t MaxLevel;
    uint32_t NpcFlags;   // Interaction flags (Gossip, Vendor)
    uint32_t UnitFlags;  // State flags (Passive, Unattackable)
    uint32_t Rank;       // Elite/Boss status
};

// Represents a row from WorldMapArea.csv
struct WorldMapAreaEntry {
    uint32_t MapID;      // Col 1
    uint32_t AreaID;     // Col 2
    string AreaName;     // Col 3
    float LocLeft;       // Col 4 (World Y Max)
    float LocRight;      // Col 5 (World Y Min)
    float LocTop;        // Col 6 (World X Max)
    float LocBottom;     // Col 7 (World X Min)
};

// Global storage for the DBC data (In a real app, load this from CSV/DBC)
std::unordered_map<uint32_t, FactionTemplateEntry> sFactionTemplateStore;

enum FactionMasks {
    FACTION_MASK_PLAYER = 1,  // Bit 0
    FACTION_MASK_ALLIANCE = 2,  // Bit 1
    FACTION_MASK_HORDE = 4,  // Bit 2
    FACTION_MASK_MONSTER = 8   // Bit 3
};

enum Reaction {
    HOSTILE = 0, // Red
    NEUTRAL = 1, // Yellow
    FRIENDLY = 2 // Green
};

class WoWDataTool {
private:
    // Reads the databases
    unordered_map<long long, string> database;

    // Map: Lock ID -> Lock Info (CSV) (Only for objects)
    unordered_map<int, LockInfo> locks;

    // Global storage for Creatures
    std::unordered_map<uint32_t, CreatureTemplateEntry> sCreatureTemplateStore;

    // Faction info (Only for entities)
    unordered_map<uint32_t, FactionTemplateEntry> sFactionTemplateStore;

    // Map: NPC ID -> List of Spawn Locations
    std::unordered_map<uint32_t, std::vector<CreatureSpawn>> creatureSpawnStore;

    // NEW: Map Zone Name -> Map Area Data
    std::unordered_map<string, WorldMapAreaEntry> sWorldMapAreaStore;

    // REVERSE LOOKUP MAP: Hash -> Zone Name
    std::unordered_map<uint32_t, string> hashToNameStore;

    // REVERSE LOOKUP MAP: Hash -> WorldMapAreaEntry
    std::unordered_map<uint32_t, WorldMapAreaEntry> hashToInfoStore;

    // DJB2 Hash Algorithm (Must match your Lua function exactly)
    uint32_t calculateHash(const string& str) {
        uint32_t hash = 5381;
        for (char c : str) {
            // ((hash << 5) + hash) is the same as (hash * 33)
            hash = ((hash << 5) + hash) + (unsigned char)c;
        }
        return hash;
    }

    // Helper to remove quotes from CSV strings: "Durotar" -> Durotar
    string stripQuotes(string input) {
        if (input.size() >= 2 && input.front() == '"' && input.back() == '"') {
            return input.substr(1, input.size() - 2);
        }
        return input;
    }

    // Helper to parse a specific column from a delimiter-separated string
    string getColumnInternal(const string& line, int index, char delimiter) {
        if (line.empty()) return "";

        int currentCol = 0;
        size_t start = 0;
        size_t end = line.find(delimiter);

        while (end != string::npos) {
            if (currentCol == index) {
                return line.substr(start, end - start);
            }
            currentCol++;
            start = end + 1;
            end = line.find(delimiter, start);
        }

        if (currentCol == index) {
            return line.substr(start);
        }

        return "";
    }
    
    // Core Faction Logic Helper
    int calculateReaction(uint32_t creatureFactionId, uint32_t playerFactionId) {
        if (sFactionTemplateStore.find(creatureFactionId) == sFactionTemplateStore.end() ||
            sFactionTemplateStore.find(playerFactionId) == sFactionTemplateStore.end()) {
            return REACTION_NEUTRAL; // Error fallback
        }

        const FactionTemplateEntry& a = sFactionTemplateStore[creatureFactionId];
        const FactionTemplateEntry& b = sFactionTemplateStore[playerFactionId];

        // 1. Exact Match Check (Always friends with yourself)
        if (a.ID == b.ID) return REACTION_FRIENDLY;

        // 2. Explicit Enemy List Check (Overrides masks)
        for (int i = 0; i < 4; ++i) {
            if (a.EnemyFactions[i] != 0 && a.EnemyFactions[i] == b.Faction)
                return REACTION_HOSTILE;
        }

        // 3. Explicit Friend List Check (Overrides masks)
        for (int i = 0; i < 4; ++i) {
            if (a.FriendFactions[i] != 0 && a.FriendFactions[i] == b.Faction)
                return REACTION_FRIENDLY;
        }

        // 4. Bitmask Check - Hostile (The most common check)
        if (a.HostileMask & b.FactionFlags) return REACTION_HOSTILE;

        // 5. Bitmask Check - Friendly
        if (a.FriendlyMask & b.FactionFlags) return REACTION_FRIENDLY;

        // 6. Default to Neutral
        return REACTION_NEUTRAL;
    }

public:
    CreatureTemplateEntry* getCreatureTemplate(int id) {
        if (sCreatureTemplateStore.find(id) == sCreatureTemplateStore.end()) {
            return nullptr;
        }
        return &sCreatureTemplateStore[id];
    }

    // ---------------------------------------------------------
    // NEW: LOAD CREATURE SPAWN LOCATIONS
    // Parses the TSV file and groups spawns by NPC ID
    // ---------------------------------------------------------
    void loadCreatureSpawnLocations(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "[Error] Could not open Spawn DB: " << filename << endl;
            return;
        }
        cout << "Loading Creature Spawns... ";

        string line;
        getline(file, line); // Skip header row if present

        int count = 0;
        while (getline(file, line)) {
            try {
                // Columns based on your TSV export: 
                // 0:ID, 1:Map, 2:X, 3:Y, 4:Z
                string sID = getColumnInternal(line, 0, '\t');
                string sMap = getColumnInternal(line, 1, '\t');
                string sX = getColumnInternal(line, 2, '\t');
                string sY = getColumnInternal(line, 3, '\t');
                string sZ = getColumnInternal(line, 4, '\t');

                if (!sID.empty() && !sX.empty()) {
                    uint32_t id = stoi(sID);

                    CreatureSpawn spawn;
                    spawn.mapId = stoi(sMap);
                    spawn.position = Vector3(stof(sX), stof(sY), stof(sZ));

                    // Add to the vector for this specific ID
                    creatureSpawnStore[id].push_back(spawn);
                    count++;
                }
            }
            catch (...) { continue; }
        }
        cout << "Done. (" << count << " spawns loaded for " << creatureSpawnStore.size() << " unique IDs)" << endl;
    }

    // ---------------------------------------------------------
    // GET CANDIDATE REPAIR VENDORS (Sorted by Distance)
    // Returns a list of the 'maxCount' closest vendors.
    // ---------------------------------------------------------
    struct VendorCandidate {
        uint32_t id;
        Vector3 position;
        float distance;
    };

    std::vector<VendorCandidate> getRepairVendorsByDistance(const Vector3& playerPos, uint32_t mapId, bool isHorde, int maxCount = 5) {
        std::vector<VendorCandidate> candidates;

        // 1. Collect all valid candidates
        for (const auto& [id, spawnList] : creatureSpawnStore) {

            // Validation Checks (Template, Flag, Reaction)
            if (sCreatureTemplateStore.find(id) == sCreatureTemplateStore.end()) continue;
            const CreatureTemplateEntry& entry = sCreatureTemplateStore[id];

            if (!(entry.NpcFlags & 4096)) continue; // Must have Repair Flag

            // Faction/Reaction Check
            uint32_t mobFactionId = isHorde ? entry.FactionH : entry.FactionA;
            uint32_t playerFactionId = isHorde ? FACTION_TEMPLATE_HORDE_PLAYER : FACTION_TEMPLATE_ALLIANCE_PLAYER;
            if (calculateReaction(mobFactionId, playerFactionId) == REACTION_HOSTILE) continue;

            // Check Spawns
            for (const auto& spawn : spawnList) {
                if (spawn.mapId == mapId) {
                    float dst = playerPos.Dist3D(spawn.position);

                    // Optimization: Don't add if further than 5000 yards (cross-zone)
                    if (dst > 5000.0f) continue;

                    candidates.push_back({ id, spawn.position, dst });
                }
            }
        }

        // 2. Sort by Distance (Ascending)
        std::sort(candidates.begin(), candidates.end(), [](const VendorCandidate& a, const VendorCandidate& b) {
            return a.distance < b.distance;
            });

        // 3. Trim to maxCount
        if (candidates.size() > maxCount) {
            candidates.resize(maxCount);
        }

        return candidates;
    }

    // ---------------------------------------------------------
    // FIND SPECIFIC REPAIR VENDOR BY ID
    // Returns the nearest spawn location for a specific NPC ID.
    // ---------------------------------------------------------
    bool getSpecificRepairVendor(uint32_t targetId, const Vector3& playerPos, uint32_t mapId, Vector3& outPosition) {
        // 1. Validate Template exists
        if (sCreatureTemplateStore.find(targetId) == sCreatureTemplateStore.end()) {
            std::cout << "[Database] Error: Creature ID " << targetId << " not found in Template DB." << std::endl;
            return false;
        }

        // 2. Verify it is actually a Repair Vendor (Flag 4096)
        if (!(sCreatureTemplateStore[targetId].NpcFlags & 4096)) {
            std::cout << "[Database] Error: Creature ID " << targetId << " is not a Repair Vendor." << std::endl;
            return false;
        }

        // 3. Check if we have any spawn data for this ID
        if (creatureSpawnStore.find(targetId) == creatureSpawnStore.end()) {
            std::cout << "[Database] Error: No spawn locations found for Creature ID " << targetId << "." << std::endl;
            return false;
        }

        float minDst = 999999.0f;
        bool found = false;

        // 4. Find the nearest spawn on the CURRENT MAP
        for (const auto& spawn : creatureSpawnStore[targetId]) {
            if (spawn.mapId == mapId) {
                float dst = playerPos.Dist3D(spawn.position);
                if (dst < minDst) {
                    minDst = dst;
                    outPosition = spawn.position;
                    found = true;
                }
            }
        }

        if (!found) {
            std::cout << "[Database] Creature ID " << targetId << " has no spawns on Map " << mapId << "." << std::endl;
        }

        return found;
    }

    void loadDatabase(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "Error: Could not open " << filename << endl;
            return;
        }
        cout << "Loading data into memory... ";

        string line;
        while (getline(file, line)) {
            size_t firstTab = line.find('\t');
            if (firstTab != string::npos) {
                try {
                    // Store ID and Line
                    long long id = stoll(line.substr(0, firstTab));
                    database[id] = line;
                }
                catch (...) {}
            }
        }
        cout << "Done. (" << database.size() << " records)" << endl;
    }

    // Returns the raw line for an ID
    string getRawLine(uint32_t id) {
        if (database.find(id) != database.end()) {
            return database[id];
        }
        return "";
    }

    // 2. LOAD LOCKS (Comma-Separated)
    // Parses Lock.csv and stores Mining/Herb locks in memory.
    void loadLocks(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "[Error] Could not open Lock DB: " << filename << endl;
            return;
        }
        cout << "Loading Locks... ";

        string line;
        // Skip header if your csv has one
        // getline(file, line); 
        while (getline(file, line)) {
            try {
                // Parse necessary columns
                string sID = getColumnInternal(line, CSV_COL_LOCK_ID, ',');
                string sReqType = getColumnInternal(line, CSV_COL_LOCK_REQ_TYPE, ',');
                string sLockType = getColumnInternal(line, CSV_COL_LOCK_TYPE, ',');
                string sSkill = getColumnInternal(line, CSV_COL_SKILL_REQ, ',');

                if (!sID.empty() && !sReqType.empty() && !sLockType.empty()) {
                    int id = stoi(sID);
                    int reqType = stoi(sReqType);
                    int lockType = stoi(sLockType);
                    int skill = sSkill.empty() ? 0 : stoi(sSkill);

                    // Filter: We only care about Skill Requirements (Type 2)
                    // And only Mining (3) or Herbalism (2)
                    if (reqType == 2 && (lockType == 2 || lockType == 3)) {
                        locks[id] = { lockType, skill };
                    }
                }
            }
            catch (...) {}
        }
        cout << "Done. (" << locks.size() << " gatherable locks)" << endl;
    }

    // 3. LOAD FACTION TEMPLATE (Comma-Separated)
    // Parses FactionTemplate.csv and stores faction states in memory.
    void loadFactions(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "[Error] Could not open Faction DB: " << filename << endl;
            return;
        }
        cout << "Loading Factions... ";

        string line;
        getline(file, line); // Skip header

        while (getline(file, line)) {
            try {
                string sID = getColumnInternal(line, 0, ',');

                if (!sID.empty()) {
                    FactionTemplateEntry faction; // Declared strictly inside loop
                    faction.ID = stoi(sID);

                    string sGroup = getColumnInternal(line, 1, ',');
                    string sFlags = getColumnInternal(line, 2, ',');
                    string sMyMask = getColumnInternal(line, 3, ',');
                    string sFriendMask = getColumnInternal(line, 4, ',');
                    string sEnemyMask = getColumnInternal(line, 5, ',');

                    faction.Faction = sGroup.empty() ? 0 : stoi(sGroup);
                    faction.Flags = sFlags.empty() ? 0 : stoi(sFlags);
                    faction.FactionFlags = sMyMask.empty() ? 0 : stoi(sMyMask);
                    faction.FriendlyMask = sFriendMask.empty() ? 0 : stoi(sFriendMask);
                    faction.HostileMask = sEnemyMask.empty() ? 0 : stoi(sEnemyMask);

                    for (int i = 0; i < 4; i++) {
                        string sEnemy = getColumnInternal(line, 6 + i, ',');
                        faction.EnemyFactions[i] = sEnemy.empty() ? 0 : stoi(sEnemy);
                    }

                    for (int i = 0; i < 4; i++) {
                        string sFriend = getColumnInternal(line, 10 + i, ',');
                        faction.FriendFactions[i] = sFriend.empty() ? 0 : stoi(sFriend);
                    }

                    sFactionTemplateStore[faction.ID] = faction;
                }
            }
            catch (...) {}
        }
        cout << "Done. (" << sFactionTemplateStore.size() << " faction templates loaded)" << endl;
    }

    // 4. LOAD CREATURE TEMPLATES (Tab-Separated)
    // Parses the raw strings currently stored in 'database' into structured Creature data
    void parseCreatureTemplates() {
        if (database.empty()) {
            cout << "[Warning] Database is empty. Did you call loadDatabase() first?" << endl;
            return;
        }

        cout << "Parsing " << database.size() << " creatures from memory... ";
        sCreatureTemplateStore.clear();

        // Iterate over the raw data map
        for (auto const& [id, rawLine] : database) {
            try {
                CreatureTemplateEntry c;
                c.Entry = (uint32_t)id;

                // Extract Columns using your existing helper
                // Indices based on the schema provided earlier
                c.Name = getColumnInternal(rawLine, 10, '\t');

                string sMinLvl = getColumnInternal(rawLine, 14, '\t');
                string sMaxLvl = getColumnInternal(rawLine, 15, '\t');
                string sFacA = getColumnInternal(rawLine, 18, '\t');
                string sFacH = getColumnInternal(rawLine, 19, '\t');
                string sNpcFlag = getColumnInternal(rawLine, 20, '\t');
                string sRank = getColumnInternal(rawLine, 24, '\t');
                string sUnitFlag = getColumnInternal(rawLine, 33, '\t');

                // Convert strings to integers with safety checks
                c.MinLevel = sMinLvl.empty() ? 1 : stoul(sMinLvl);
                c.MaxLevel = sMaxLvl.empty() ? 1 : stoul(sMaxLvl);
                c.FactionA = sFacA.empty() ? 0 : stoul(sFacA);
                c.FactionH = sFacH.empty() ? 0 : stoul(sFacH);
                c.NpcFlags = sNpcFlag.empty() ? 0 : stoul(sNpcFlag);
                c.Rank = sRank.empty() ? 0 : stoul(sRank);
                c.UnitFlags = sUnitFlag.empty() ? 0 : stoul(sUnitFlag);

                // Store in the lookup map
                sCreatureTemplateStore[c.Entry] = c;
            }
            catch (...) {
                // Skip lines that fail to parse (malformed integers, etc.)
                continue;
            }
        }

        cout << "Done. (" << sCreatureTemplateStore.size() << " valid templates parsed)" << endl;
    }

    // 3. MAIN IDENTIFICATION FUNCTION
    void getGatherInfo(uint32_t objectId, int& outSkillReq, int& type) {
        outSkillReq = 0;

        // 1. Find Object
        if (database.find(objectId) == database.end()) {
            type = GATHER_NONE; return;
        }
        string rawLine = database[objectId];
        // 2. Check Type (Must be 3 for Chest/Node)
        // Optimization: We could cache parsed objects, but raw string lookup is fine for now
        string sType = getColumnInternal(rawLine, COL_OBJ_TYPE, '\t');
        if (sType != "3") {
            type = GATHER_NONE;
            return;
        }

        // 3. Get Lock ID (data0)
        string sLockId = getColumnInternal(rawLine, COL_OBJ_LOCK_ID, '\t');
        if (sLockId.empty()) {
            type = GATHER_NONE;
            return;
        }

        try {
            int lockId = stoi(sLockId);

            // 4. Look up Lock Info
            if (locks.find(lockId) != locks.end()) {
                LockInfo info = locks[lockId];
                outSkillReq = info.skillLevel;

                if (info.typeIndex == 3) {
                    type = GATHER_MINE;
                    return;
                }
                if (info.typeIndex == 2) {
                    type = GATHER_HERB;
                    return;
                }
            }
        }
        catch (...) {}

        type = GATHER_NONE;
        return;
    }

    void getCreatureReaction(uint32_t creatureEntry, bool isHordePlayer, int& outReaction) {
        outReaction = REACTION_NEUTRAL;

        // Fast Lookup
        if (sCreatureTemplateStore.find(creatureEntry) == sCreatureTemplateStore.end()) {
            return;
        }

        CreatureTemplateEntry& c = sCreatureTemplateStore[creatureEntry];
        uint32_t mobFactionId = isHordePlayer ? c.FactionH : c.FactionA;
        uint32_t playerFactionId = isHordePlayer ? FACTION_TEMPLATE_HORDE_PLAYER : FACTION_TEMPLATE_ALLIANCE_PLAYER;

        // Use the helper logic
        outReaction = calculateReaction(mobFactionId, playerFactionId);
    }

    /**
     * EXTRACT SPECIFIC COLUMN
     * @param rawLine: The full tab-separated string
     * @param index: The column number you want (0 = ID, 1 = Class, etc.)
     */
    string getColumn(string rawLine, int index) {
        if (rawLine.empty()) return "Not Found";

        int currentCol = 0;
        size_t start = 0;
        size_t end = rawLine.find('\t');

        while (end != string::npos) {
            if (currentCol == index) {
                return rawLine.substr(start, end - start);
            }
            currentCol++;
            start = end + 1;
            end = rawLine.find('\t', start);
        }

        // Handle the very last column (which has no tab after it)
        if (currentCol == index) {
            return rawLine.substr(start);
        }

        return "Index Out of Bounds";
    }

    // ---------------------------------------------------------
    // LOAD WORLD MAP AREA (CSV)
    // ---------------------------------------------------------
    void loadWorldMapArea(const string& filename) {
        ifstream file(filename);
        if (!file.is_open()) {
            cerr << "[Error] Could not open WorldMapArea DB: " << filename << endl;
            return;
        }
        cout << "Loading World Map Areas... ";

        string line;
        getline(file, line); // Skip header row

        while (getline(file, line)) {
            try {
                // Parse Name first to see if it's valid
                string rawName = getColumnInternal(line, 3, ',');
                string areaName = stripQuotes(rawName);
                uint32_t nameHash = calculateHash(areaName);

                if (areaName.empty()) continue;

                // Parse IDs
                string sMapID = getColumnInternal(line, 1, ',');
                string sAreaID = getColumnInternal(line, 2, ',');

                // Parse Floats (Coordinates)
                string sLocLeft = getColumnInternal(line, 4, ',');
                string sLocRight = getColumnInternal(line, 5, ',');
                string sLocTop = getColumnInternal(line, 6, ',');
                string sLocBottom = getColumnInternal(line, 7, ',');

                WorldMapAreaEntry entry;
                entry.MapID = stoi(sMapID);
                entry.AreaID = stoi(sAreaID); // usually 0 for the main map
                entry.AreaName = areaName;

                // Note: Standard DB2 format uses floats
                entry.LocLeft = stof(sLocLeft);
                entry.LocRight = stof(sLocRight);
                entry.LocTop = stof(sLocTop);
                entry.LocBottom = stof(sLocBottom);

                // STORE IN REVERSE MAPS
                // We prefer entries where AreaID != 0 (Specific Zones) over MapID (Continents)
                // because usually we are looking for "Zangarmarsh", not "Outland".
                if (hashToInfoStore.find(nameHash) == hashToInfoStore.end() || entry.AreaID != 0) {
                    hashToNameStore[nameHash] = areaName; // Store Name
                    hashToInfoStore[nameHash] = entry;    // Store Data
                }
            }
            catch (...) { continue; }
        }
        cout << "Done. (" << sWorldMapAreaStore.size() << " zones loaded)" << endl;
    }

    // ---------------------------------------------------------
    // GET ZONE INFO BY NAME
    // Returns true if found, fills the references with data
    // ---------------------------------------------------------
    bool getZoneInfoByName(string searchName, int& mapId, float& outTop, float& outBottom, float& outLeft, float& outRight, int& areaId, uint32_t& hash) {

        // 1. Try Exact Match via Hash (Fastest)
        // This simulates exactly what the Lua addon sends
        hash = calculateHash(searchName);
        if (hashToInfoStore.find(hash) != hashToInfoStore.end()) {
            WorldMapAreaEntry& entry = hashToInfoStore[hash];
            outTop = entry.LocTop;
            outBottom = entry.LocBottom;
            outLeft = entry.LocLeft;
            outRight = entry.LocRight;
            areaId = entry.AreaID;
            mapId = entry.MapID;            
            return true;
        }

        // 2. Try Partial Search (Iterate all zones)
        // Useful if you type "Zangar" instead of "Zangarmarsh"
        cout << "[Search] Exact match not found for '" << searchName << "', looking for partials..." << endl;

        for (auto& [h, entry] : hashToInfoStore) {
            string dbName = entry.AreaName;

            // Basic substring search (Case-sensitive in this simple version)
            // You can make this case-insensitive using std::tolower if needed
            if (dbName.find(searchName) != string::npos) {
                outTop = entry.LocTop;
                outBottom = entry.LocBottom;
                outLeft = entry.LocLeft;
                outRight = entry.LocRight;
                cout << "[Search] Found partial match: " << dbName << endl;
                return true;
            }
        }

        cout << "[Search] No zone found matching '" << searchName << "'" << endl;
        return false;
    }

    // ---------------------------------------------------------
    // REVERSE HASH LOOKUP
    // Input: The uint32 hash from Lua
    // Output: The Zone Name (and coordinate limits if needed)
    // ---------------------------------------------------------
    string reverseHash(uint32_t hash, int& mapId, float& outTop, float& outBottom, float& outLeft, float& outRight, int& areaId, string& name) {

        // 1. Check if hash exists in our map
        if (hashToInfoStore.find(hash) != hashToInfoStore.end()) {
            WorldMapAreaEntry& entry = hashToInfoStore[hash];

            // Fill outputs
            outTop = entry.LocTop;
            outBottom = entry.LocBottom;
            outLeft = entry.LocLeft;
            outRight = entry.LocRight;
            areaId = entry.AreaID;
            mapId = entry.MapID;
            name = entry.AreaName;

            // Return the Name (Reversed!)
            return entry.AreaName;
        }

        return "UNKNOWN_ZONE";
    }

    // Helper to calculate world coordinates from normalized (0-1) coordinates
    // Using the data retrieved above
    void convertNormToWorld(float normX, float normY, float top, float bottom, float left, float right, float& outWorldX, float& outWorldY) {
        // Map X (0-1) corresponds to World Y
        // Map Y (0-1) corresponds to World X

        float mapHeight = top - bottom;
        float mapWidth = left - right;

        outWorldX = top - (normY * mapHeight);
        outWorldY = left - (normX * mapWidth);
    }
};

bool hasNpcFlag(uint32_t flags, uint32_t flagToCheck) {
    return (flags & flagToCheck) != 0;
}