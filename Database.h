#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>

int ENEMY_NAME_COLUMN_INDEX = 10;
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

struct LockInfo {
    int typeIndex; // 2 for Herb, 3 for Mine
    int skillLevel;
};

class WoWDataTool {
private:
    // Map: Object ID -> Full Raw Line (TSV)
    unordered_map<long long, string> database;

    // Map: Lock ID -> Lock Info (CSV)
    unordered_map<int, LockInfo> locks;

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

public:
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
};