#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>

int ENEMY_NAME_COLUMN_INDEX = 10;
int OBJ_NAME_COLUMN_INDEX = 3;

using namespace std;

class WoWDataTool {
private:
    // Map: ID -> Full Raw Line
    unordered_map<long long, string> database;

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