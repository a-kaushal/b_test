#include "WebServer.h"
#include "json.hpp" // Make sure this file is in your project directory
using json = nlohmann::json;

// [CRITICAL] WinSock2 must be included FIRST
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <memory> 
#include <fstream>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <atomic> // Required for externs

#pragma comment(lib, "Ws2_32.lib")

// --- EXTERNS ---
extern std::ofstream g_LogFile;
extern std::mutex g_EntityMutex;

// [FIX] Import Real Global Flags from dllmain.cpp
// This ensures the web server sees the EXACT same state as the game loop
extern std::atomic<bool> g_IsRunning;
extern std::atomic<bool> g_IsPaused;

#include "Vector.h"
#include "Entity.h"
#include "Pathfinding2.h" 
#include "WorldState.h"
#include "ProfileLoader.h"

extern ProfileLoader g_ProfileLoader;
extern WorldState* g_GameState;

// Static Member Initialization
std::atomic<bool> WebServer::running = false;
std::thread WebServer::serverThread;
std::atomic<bool> WebServer::botActive = false; // Kept for internal logic, but we prioritize g_IsPaused
std::atomic<bool> WebServer::profileLoadReq = false;
std::atomic<bool> WebServer::autoLoad = false;
std::string WebServer::currentProfile = "None";
std::string WebServer::pendingProfile = "";
unsigned long long WebServer::startTime = 0;

// =============================================================
// HELPER FUNCTIONS
// =============================================================

std::string EscapeJSON(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

std::string WebServer::ReadLogFileTail(int charsToRead) {
    std::string filepath = "C:\\SMM\\SMM_Debug.log";
    std::ifstream file(filepath, std::ios::binary | std::ios::ate); // Open at end
    if (!file.is_open()) return "Log file not found.";

    std::streamsize size = file.tellg();
    if (size == 0) return "";

    std::streamsize readSize = (std::min)(size, (std::streamsize)charsToRead);

    std::string content;
    content.resize(readSize);

    file.seekg(-readSize, std::ios::end);
    file.read(&content[0], readSize);

    return content;
}

// --- DEBUG SERIALIZATION HELPERS ---

std::string Vec3JSON(const Vector3& v) {
    std::stringstream ss;
    ss << "{\"x\":" << v.x << ",\"y\":" << v.y << ",\"z\":" << v.z << "}";
    return ss.str();
}

// Serializes the base ActionState fields
void SerializeActionBase(std::stringstream& ss, const ActionState& s) {
    ss << "\"activePathCount\":" << s.activePath.size() << ",";
    ss << "\"activeIndex\":" << s.activeIndex << ",";
    ss << "\"actionChange\":" << (s.actionChange ? "true" : "false") << ",";
    ss << "\"flyingPath\":" << (s.flyingPath ? "true" : "false") << ",";
    ss << "\"inMotion\":" << (s.inMotion ? "true" : "false") << ",";
    ss << "\"ignoreUnderWater\":" << (s.ignoreUnderWater ? "true" : "false");
}

std::string WebServer::GenerateJSONState() {
    if (!g_GameState) return "{}";

    // Lock to prevent reading while GameLoop is writing
    std::lock_guard<std::mutex> lock(g_EntityMutex);

    std::stringstream ss;
    ss << "{";

    // --- 1. BOT STATUS [FIXED] ---
    // We explicitly send the 'paused' state derived from the global variable
    ss << "\"running\": " << (g_IsRunning ? "true" : "false") << ",";
    ss << "\"paused\": " << (g_IsPaused ? "true" : "false") << ",";
    ss << "\"autoLoad\": " << (autoLoad ? "true" : "false") << ",";

    std::string profileName = "None";
    if (auto* p = g_ProfileLoader.GetActiveProfile()) profileName = p->profileName;
    ss << "\"profile\": \"" << EscapeJSON(profileName) << "\",";

    unsigned long long now = GetTickCount64();
    unsigned long long elapsed = (now - startTime) / 1000;
    ss << "\"uptime\": \"" << (elapsed / 3600) << "h " << ((elapsed % 3600) / 60) << "m\",";

    // --- 2. PLAYER ---
    auto& p = g_GameState->player;
    ss << "\"player\": {";
    ss << "\"addr\": \"0x" << std::hex << p.playerPtr << std::dec << "\",";
    ss << "\"x\":" << p.position.x << ",\"y\":" << p.position.y << ",\"z\":" << p.position.z << ",";
    ss << "\"hp\":" << p.health << ",\"maxHp\":" << p.maxHealth << ",";
    ss << "\"level\":" << p.level << ",";
    ss << "\"mapId\":" << p.mapId << ",";
    ss << "\"bagFree\":" << p.bagFreeSlots << ",";
    ss << "\"needRepair\":" << (p.needRepair ? "true" : "false") << ",";
    ss << "\"rot\":" << p.rotation << ",";
    ss << "\"isDead\":" << (p.isDead ? "true" : "false") << ",";
    ss << "\"isGhost\":" << (p.isGhost ? "true" : "false") << ",";
    ss << "\"inCombat\":" << (p.inCombatGuidLow > 0 ? "true" : "false") << ",";
    ss << "\"isMounted\":" << (p.isMounted ? "true" : "false") << ",";
    ss << "\"groundMounted\":" << (p.groundMounted ? "true" : "false") << ",";
    ss << "\"flyingMounted\":" << (p.flyingMounted ? "true" : "false") << ",";
    ss << "\"isFlying\":" << (p.isFlying ? "true" : "false") << ",";
    ss << "\"isIndoor\":" << (p.isIndoor ? "true" : "false") << ",";
    ss << "\"onGround\":" << (p.onGround ? "true" : "false") << ",";
    ss << "\"inWater\":" << (p.inWater ? "true" : "false") << ",";
    ss << "\"target\": \"0x" << std::hex << p.inCombatGuidLow << std::dec << "\",";
    ss << "\"selected\": \"0x" << std::hex << p.targetGuidLow << std::dec << "\"";
    ss << "},";

    // --- 3. PATH (Visual Only) ---
    ss << "\"path\": [";
    for (size_t i = 0; i < g_GameState->globalState.activePath.size(); ++i) {
        auto& node = g_GameState->globalState.activePath[i];
        ss << "{\"x\":" << node.pos.x << ",\"y\":" << node.pos.y << "}";
        if (i < g_GameState->globalState.activePath.size() - 1) ss << ",";
    }
    ss << "],";

    // --- 4. DEBUG DUMP ---
    ss << "\"debug\": {";

    // GlobalState
    ss << "\"GlobalState\": {";
    SerializeActionBase(ss, g_GameState->globalState);
    ss << ", \"vendorOpen\":" << (g_GameState->globalState.vendorOpen ? "true" : "false");
    ss << ", \"chatOpen\":" << (g_GameState->globalState.chatOpen ? "true" : "false");
    ss << ", \"mapId\":" << g_GameState->globalState.mapId;
    ss << ", \"areaId\":" << g_GameState->globalState.areaId;
    ss << ", \"mapName\":\"" << EscapeJSON(g_GameState->globalState.mapName) << "\"";
    ss << "},";

    // Looting
    auto& l = g_GameState->lootState;
    ss << "\"Looting\": {";
    SerializeActionBase(ss, l);
    ss << ", \"enabled\":" << (l.enabled ? "true" : "false");
    ss << ", \"hasLoot\":" << (l.hasLoot ? "true" : "false");
    ss << ", \"position\":" << Vec3JSON(l.position);
    ss << ", \"guidLow\":\"0x" << std::hex << l.guidLow << std::dec << "\"";
    ss << "},";

    // Gathering
    auto& g = g_GameState->gatherState;
    ss << "\"Gathering\": {";
    SerializeActionBase(ss, g);
    ss << ", \"enabled\":" << (g.enabled ? "true" : "false");
    ss << ", \"hasNode\":" << (g.hasNode ? "true" : "false");
    ss << ", \"nodeActive\":" << (g.nodeActive ? "true" : "false");
    ss << ", \"position\":" << Vec3JSON(g.position);
    ss << ", \"blacklistCount\":" << g.blacklistNodesGuidLow.size();
    ss << ", \"gatheredCount\":" << g.gatheredNodesGuidLow.size();
    ss << "},";

    // PathFollowing
    auto& pf = g_GameState->pathFollowState;
    ss << "\"PathFollowing\": {";
    SerializeActionBase(ss, pf);
    ss << ", \"enabled\":" << (pf.enabled ? "true" : "false");
    ss << ", \"presetIndex\":" << pf.presetIndex;
    ss << ", \"looping\":" << (pf.looping ? "true" : "false");
    ss << ", \"hasPath\":" << (pf.hasPath ? "true" : "false");
    ss << ", \"pathCount\":" << pf.path.size();
    ss << "},";

    // WaypointReturn
    auto& wr = g_GameState->waypointReturnState;
    ss << "\"WaypointReturn\": {";
    SerializeActionBase(ss, wr);
    ss << ", \"enabled\":" << (wr.enabled ? "true" : "false");
    ss << ", \"hasTarget\":" << (wr.hasTarget ? "true" : "false");
    ss << ", \"hasPath\":" << (wr.hasPath ? "true" : "false");
    ss << ", \"attempts\":" << wr.pathfindingAttempts;
    ss << ", \"waitingForUnstuck\":" << (wr.waitingForUnstuck ? "true" : "false");
    ss << "},";

    // Combat
    auto& c = g_GameState->combatState;
    ss << "\"Combat\": {";
    SerializeActionBase(ss, c);
    ss << ", \"enabled\":" << (c.enabled ? "true" : "false");
    ss << ", \"inCombat\":" << (c.inCombat ? "true" : "false");
    ss << ", \"underAttack\":" << (c.underAttack ? "true" : "false");
    ss << ", \"attackerCount\":" << c.attackerCount;
    ss << ", \"hasTarget\":" << (c.hasTarget ? "true" : "false");
    ss << ", \"targetHealth\":" << c.targetHealth;
    ss << ", \"enemyPos\":" << Vec3JSON(c.enemyPosition);
    ss << ", \"targetGuid\":\"0x" << std::hex << c.targetGuidLow << std::dec << "\"";
    ss << "},";

    // StuckState
    auto& s = g_GameState->stuckState;
    ss << "\"StuckState\": {";
    SerializeActionBase(ss, s);
    ss << ", \"isStuck\":" << (s.isStuck ? "true" : "false");
    ss << ", \"lastPosition\":" << Vec3JSON(s.lastPosition);
    ss << ", \"attemptCount\":" << s.attemptCount;
    ss << "},";

    // InteractState
    auto& i = g_GameState->interactState;
    ss << "\"InteractState\": {";
    SerializeActionBase(ss, i);
    ss << ", \"enabled\":" << (i.enabled ? "true" : "false");
    ss << ", \"interactActive\":" << (i.interactActive ? "true" : "false");
    ss << ", \"interactId\":" << i.interactId;
    ss << ", \"interactTimes\":" << i.interactTimes;
    ss << ", \"vendorSell\":" << (i.vendorSell ? "true" : "false");
    ss << ", \"repair\":" << (i.repair ? "true" : "false");
    ss << ", \"targetGuid\":\"0x" << std::hex << i.targetGuidLow << std::dec << "\"";
    ss << "},";

    // RespawnState
    auto& r = g_GameState->respawnState;
    ss << "\"RespawnState\": {";
    SerializeActionBase(ss, r);
    ss << ", \"enabled\":" << (r.enabled ? "true" : "false");
    ss << ", \"isDead\":" << (r.isDead ? "true" : "false");
    ss << ", \"isPathingToCorpse\":" << (r.isPathingToCorpse ? "true" : "false");
    ss << ", \"currentTargetPos\":" << Vec3JSON(r.currentTargetPos);
    ss << ", \"layerCount\":" << r.possibleZLayers.size();
    ss << "}"; // Last one

    ss << "},"; // End Debug

    // --- 5. ENTITIES ---
    ss << "\"entities\": [";
    bool first = true;
    for (const auto& ent : g_GameState->entities) {
        std::string typeStr = "Unknown";
        std::stringstream extra;

        if (ent.type == 33 && ent.info) { // Enemy
            if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                typeStr = "Enemy";
                extra << ",\"hp\":" << npc->health << ",\"maxHp\":" << npc->maxHealth
                    << ",\"level\":" << npc->level << ",\"reaction\":" << npc->reaction
                    << ",\"inCombat\":" << (npc->inCombat ? "true" : "false")
                    << ",\"targetGuid\":\"0x" << std::hex << npc->targetGuidLow << std::dec << "\""
                    << ",\"target\":" << npc->targetGuidLow;

                if (!first) ss << ",";
                ss << "{\"type\":\"" << typeStr << "\",\"name\":\"" << EscapeJSON(npc->name) << "\","
                    << "\"id\":" << npc->id << ",\"dist\":" << npc->distance
                    << ",\"x\":" << npc->position.x << ",\"y\":" << npc->position.y << ",\"z\":" << npc->position.z << extra.str() << "}";
                first = false;
            }
        }
        else if (ent.type == 257 && ent.info) { // Object
            if (auto obj = std::dynamic_pointer_cast<ObjectInfo>(ent.info)) {
                typeStr = "Object";
                extra << ",\"nodeActive\":" << obj->nodeActive << ",\"skill\":" << obj->skillLevel;
                if (!first) ss << ",";
                ss << "{\"type\":\"" << typeStr << "\",\"name\":\"" << EscapeJSON(obj->name) << "\","
                    << "\"id\":" << obj->id << ",\"dist\":" << obj->distance
                    << ",\"x\":" << obj->position.x << ",\"y\":" << obj->position.y << ",\"z\":" << obj->position.z << extra.str() << "}";
                first = false;
            }
        }
    }
    ss << "]";

    ss << "}";
    return ss.str();
}

// =============================================================
// CONFIGURATION PERSISTENCE
// =============================================================

void WebServer::LoadConfig() {
    std::ifstream f("C:\\SMM\\WebConfig.ini");
    std::string line;
    if (f.is_open()) {
        while (getline(f, line)) {
            if (line.find("AutoLoad=") != std::string::npos) {
                if (line.find("1") != std::string::npos || line.find("true") != std::string::npos) {
                    autoLoad = true;
                }
                else {
                    autoLoad = false;
                }
            }
        }
    }
}

void WebServer::SaveConfig() {
    std::ofstream f("C:\\SMM\\WebConfig.ini");
    f << "AutoLoad=" << (autoLoad ? "1" : "0") << "\n";
}

// =============================================================
// SERVER CLASS MEMBERS
// =============================================================

void WebServer::Start(int port) {
    if (running) return;
    running = true;
    LoadConfig();

    if (autoLoad) {
        std::thread([]() {
            std::string err;
            if (g_ProfileLoader.LoadLastProfile(err)) {
                if (g_LogFile.is_open()) g_LogFile << "[WebServer] Auto-Loaded Last Profile." << std::endl;
            }
            else {
                if (g_LogFile.is_open()) g_LogFile << "[WebServer] Auto-Load Failed: " << err << std::endl;
            }
            }).detach();
    }

    startTime = GetTickCount64();
    serverThread = std::thread(ServerThread, port);
    serverThread.detach();
}

void WebServer::Stop() {
    running = false;
}

bool WebServer::IsBotActive() { return !g_IsPaused; } // Getter reflects global state
void WebServer::SetBotActive(bool active) { g_IsPaused = !active; } // Setter toggles global state
bool WebServer::IsProfileLoadRequested() { return profileLoadReq; }
std::string WebServer::GetRequestedProfile() { return pendingProfile; }
void WebServer::ConfirmProfileLoaded(const std::string& profileName) {
    currentProfile = profileName;
    profileLoadReq = false;
    pendingProfile = "";
}

void WebServer::ServerThread(int port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) { WSACleanup(); return; }

    sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = INADDR_ANY;
    service.sin_port = htons(port);

    if (bind(listenSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        closesocket(listenSocket); WSACleanup(); return;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenSocket); WSACleanup(); return;
    }

    while (running) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket != INVALID_SOCKET) {
            std::thread(HandleClient, clientSocket).detach();
        }
        else {
            Sleep(10);
        }
    }

    closesocket(listenSocket);
    WSACleanup();
}

void WebServer::HandleClient(unsigned __int64 clientSocketRaw) {
    SOCKET clientSocket = (SOCKET)clientSocketRaw;

    const int BUFFER_SIZE = 4096;
    std::vector<char> buffer(BUFFER_SIZE);
    std::string requestData;

    int bytesRecv;
    size_t headerEndPos = std::string::npos;

    while ((bytesRecv = recv(clientSocket, buffer.data(), BUFFER_SIZE, 0)) > 0) {
        requestData.append(buffer.data(), bytesRecv);
        headerEndPos = requestData.find("\r\n\r\n");
        if (headerEndPos != std::string::npos) break;
    }

    if (headerEndPos == std::string::npos) {
        closesocket(clientSocket);
        return;
    }

    size_t contentLength = 0;
    size_t clPos = requestData.find("Content-Length: ");
    if (clPos != std::string::npos) {
        size_t endOfLine = requestData.find("\r\n", clPos);
        std::string val = requestData.substr(clPos + 16, endOfLine - (clPos + 16));
        try { contentLength = std::stoull(val); }
        catch (...) { contentLength = 0; }
    }

    std::string profileNameHeader = "";
    size_t pnPos = requestData.find("Profile-Name: ");
    if (pnPos != std::string::npos) {
        size_t endOfLine = requestData.find("\r\n", pnPos);
        profileNameHeader = requestData.substr(pnPos + 14, endOfLine - (pnPos + 14));
        size_t lastSlash = profileNameHeader.find_last_of("/\\");
        if (lastSlash != std::string::npos) profileNameHeader = profileNameHeader.substr(lastSlash + 1);
    }

    size_t bodyStart = headerEndPos + 4;
    size_t currentBodySize = requestData.size() - bodyStart;

    while (currentBodySize < contentLength) {
        bytesRecv = recv(clientSocket, buffer.data(), BUFFER_SIZE, 0);
        if (bytesRecv <= 0) break;
        requestData.append(buffer.data(), bytesRecv);
        currentBodySize += bytesRecv;
    }

    std::string responseBody;
    std::string contentType = "text/html";
    int statusCode = 200;

    // [FIX] Update Globals directly
    if (requestData.find("POST /api/start") != std::string::npos) {
        g_IsPaused = false; // UNPAUSE
        responseBody = "{\"status\":\"ok\"}";
        contentType = "application/json";
    }
    else if (requestData.find("POST /api/stop") != std::string::npos) {
        g_IsPaused = true; // PAUSE
        responseBody = "{\"status\":\"ok\"}";
        contentType = "application/json";
    }
    else if (requestData.find("POST /api/upload_profile") != std::string::npos) {
        size_t bodyPos = requestData.find("\r\n\r\n");
        if (bodyPos != std::string::npos) {
            std::string body = requestData.substr(bodyPos + 4);
            std::string error;
            if (g_ProfileLoader.CompileAndLoad(body, error, profileNameHeader)) {
                responseBody = "{\"status\":\"success\", \"message\":\"Profile Loaded!\"}";
                currentProfile = profileNameHeader.empty() ? "Uploaded Profile" : profileNameHeader;
            }
            else {
                responseBody = "{\"status\":\"error\", \"message\":\"" + EscapeJSON(error) + "\"}";
            }
            contentType = "application/json";
        }
    }
    else if (requestData.find("POST /api/load_last") != std::string::npos) {
        std::string error;
        if (g_ProfileLoader.LoadLastProfile(error)) {
            responseBody = "{\"status\":\"success\", \"message\":\"Last Profile Reloaded!\"}";
            currentProfile = "Reloaded Profile";
        }
        else {
            responseBody = "{\"status\":\"error\", \"message\":\"" + EscapeJSON(error) + "\"}";
        }
        contentType = "application/json";
    }
    else if (requestData.find("GET /api/state") != std::string::npos) {
        responseBody = GenerateJSONState();
        contentType = "application/json";
    }
    else if (requestData.find("POST /api/set_autoload") != std::string::npos) {
        size_t bodyPos = requestData.find("\r\n\r\n");
        if (bodyPos != std::string::npos) {
            std::string body = requestData.substr(bodyPos + 4);
            if (body.find("true") != std::string::npos || body.find("1") != std::string::npos)
                autoLoad = true;
            else
                autoLoad = false;

            SaveConfig();
            responseBody = "{\"status\":\"success\"}";
            contentType = "application/json";
        }
    }
    else if (requestData.find("GET /api/logs") != std::string::npos) {
        responseBody = ReadLogFileTail(50000);
        contentType = "text/plain";
    }
    // --- PATHFINDING TESTER ENDPOINTS ---
    else if (requestData.find("GET /api/get_player_info") != std::string::npos) {
        if (g_GameState) {
            std::lock_guard<std::mutex> lock(g_EntityMutex);
            auto& p = g_GameState->player;
            std::stringstream ss;
            ss << "{\"x\":" << p.position.x << ",\"y\":" << p.position.y << ",\"z\":" << p.position.z
                << ",\"mapId\":" << p.mapId << "}";
            responseBody = ss.str();
            contentType = "application/json";
        }
        else {
            responseBody = "{}";
        }
    }
    else if (requestData.find("POST /api/test_path") != std::string::npos) {
        size_t bodyPos = requestData.find("\r\n\r\n");
        if (bodyPos != std::string::npos) {
            std::string body = requestData.substr(bodyPos + 4);
            try {
                auto j = json::parse(body);

                Vector3 start(j["start"]["x"], j["start"]["y"], j["start"]["z"]);
                Vector3 end(j["end"]["x"], j["end"]["y"], j["end"]["z"]);
                int mapId = j["mapId"];
                bool canFly = j["canFly"];
                bool strict = j["strict"];

                // Simulate Input Path
                std::vector<Vector3> inputPath = { end };

                // CALL PATHFINDER
                std::vector<PathNode> result = CalculatePath(
                    inputPath, start, 0,
                    canFly, mapId, canFly, // isFlying = canFly for testing
                    false, false, 25.0f, strict
                );

                // UPDATE GLOBAL OVERLAY
                if (g_GameState && !result.empty()) {
                    std::lock_guard<std::mutex> lock(g_EntityMutex);
                    g_GameState->globalState.activePath = result;
                    g_GameState->globalState.activeIndex = 0;
                    //g_GameState->globalState.activePathName = "DEBUG_TEST";
                }

                // RESPONSE
                json resp;
                resp["success"] = !result.empty();
                resp["count"] = result.size();
                std::vector<json> pathArr;
                for (const auto& n : result) {
                    pathArr.push_back({ {"x", n.pos.x}, {"y", n.pos.y}, {"z", n.pos.z}, {"type", n.type} });
                }
                resp["path"] = pathArr;

                responseBody = resp.dump();
                contentType = "application/json";
            }
            catch (const std::exception& e) {
                responseBody = "{\"success\":false, \"error\":\"" + std::string(e.what()) + "\"}";
                contentType = "application/json";
            }
        }
    }
    else {
        responseBody = GetHTML();
    }

    std::stringstream ss;
    ss << "HTTP/1.1 " << statusCode << " OK\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Content-Length: " << responseBody.length() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << responseBody;

    std::string header = ss.str();
    send(clientSocket, header.c_str(), (int)header.length(), 0);

    shutdown(clientSocket, SD_SEND);
    closesocket(clientSocket);
}

std::string WebServer::GetHTML() {
    std::stringstream ss;

    // PART 1: HEADER & STYLES
    ss << R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Shadow Bot Dashboard</title>
    <style>
        * { box-sizing: border-box; }
        :root { --bg: #1e1e1e; --panel: #252526; --accent: #007acc; --text: #d4d4d4; --border: #333; }
        body { background-color: var(--bg); color: var(--text); font-family: 'Segoe UI', Tahoma, sans-serif; margin: 0; display: flex; flex-direction: column; height: 100vh; overflow: hidden; }
        
        header { background-color: #2d2d30; padding: 10px 20px; display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); }
        .title { font-weight: bold; font-size: 18px; color: #fff; }
        .controls { display: flex; gap: 10px; }
        
        button { padding: 8px 16px; border: none; border-radius: 4px; font-weight: bold; cursor: pointer; color: white; transition: opacity 0.2s; }
        button:hover { opacity: 0.8; }
        .btn-start { background-color: #28a745; }
        .btn-stop { background-color: #dc3545; }
        .btn-blue { background-color: var(--accent); }
        .btn-file { background-color: #444; position: relative; overflow: hidden; }
        .btn-file input[type=file] { position: absolute; top: 0; right: 0; min-width: 100%; min-height: 100%; opacity: 0; cursor: pointer; }

        .tabs { display: flex; background: var(--panel); border-bottom: 1px solid var(--border); }
        .tab { padding: 10px 20px; cursor: pointer; background: var(--panel); color: #888; }
        .tab.active { background: var(--bg); color: #fff; border-top: 2px solid var(--accent); font-weight: bold; }
        .tab:hover { color: #fff; }

        .content { flex: 1; position: relative; overflow: hidden; }
        .view { display: none; height: 100%; width: 100%; }
        .view.active { display: block; }

        #log-container { height: 100%; overflow-y: scroll; font-family: 'Consolas', monospace; font-size: 12px; padding: 10px; white-space: pre-wrap; color: #ccc; }

        #map-wrapper { height: 100%; width: 100%; position: relative; background: #000; display: flex; justify-content: center; align-items: center; }
        canvas { display: block; }
        .legend { position: absolute; bottom: 20px; left: 20px; background: rgba(0,0,0,0.7); padding: 10px; border-radius: 4px; pointer-events: none; }
        .dot { display: inline-block; width: 10px; height: 10px; border-radius: 50%; margin-right: 5px; }

        #table-wrapper { height: 100%; overflow: auto; padding: 20px; }
        table { width: 100%; border-collapse: collapse; font-size: 13px; }
        th { text-align: left; background: var(--panel); padding: 8px; position: sticky; top: 0; }
        td { border-bottom: 1px solid var(--border); padding: 6px; cursor: pointer; transition: background 0.1s; }
        td:hover { background: #3e3e42; }
        tr:hover { background: #2a2d2e; }

        #debug-wrapper { height: 100%; overflow: auto; padding: 20px; }
        .debug-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(320px, 1fr)); gap: 20px; }
        
        .debug-card { 
            background: var(--panel); 
            border: 1px solid var(--border); 
            border-radius: 4px; 
            box-shadow: 0 2px 4px rgba(0,0,0,0.3); 
            transition: box-shadow 0.3s, border-color 0.3s;
        }
        
        /* [FIXED] Highlighter Class */
        .active-card {
            border: 2px solid #28a745 !important;
            box-shadow: 0 0 15px rgba(40, 167, 69, 0.6) !important;
        }

        .debug-card h3 { background: #333; margin: 0; padding: 10px 15px; font-size: 14px; border-bottom: 1px solid var(--border); color: #fff; text-transform: uppercase; letter-spacing: 0.5px; }
        .debug-table { width: 100%; border-collapse: collapse; font-family: 'Consolas', monospace; font-size: 12px; table-layout: fixed; }
        .debug-table td { border-bottom: 1px solid #333; padding: 6px 15px; vertical-align: top; word-wrap: break-word; overflow-wrap: break-word; }
        .debug-table tr:last-child td { border-bottom: none; }
        .debug-table tr:nth-child(even) { background: rgba(255,255,255,0.03); }
        .dt-key { color: #aaa; width: 45%; }
        .dt-val { color: #fff; font-weight: bold; text-align: right; width: 55%; }
        .val-true { color: #4caf50; }
        .val-false { color: #f44336; opacity: 0.7; }
        .val-num { color: #64b5f6; }
        .val-obj { color: #e1bee7; font-size: 11px; text-align: left; font-weight: normal; }

        .status-bar { background: var(--accent); color: white; padding: 4px 15px; font-size: 12px; display: flex; gap: 20px; }
    </style>
</head>
)HTML";

    // PART 2: BODY HTML
    ss << R"HTML(
<body>
    <header>
        <div class="title">Shadow Bot Dashboard</div>
        <div class="controls">
            <div class="chk-container">
                <input type="checkbox" id="chkAutoLoad" onchange="toggleAutoLoad(this)">
                <label for="chkAutoLoad">Auto-Load Last</label>
            </div>
            <button class="btn-file">
                Select Profile...
                <input type="file" id="profileFile" onchange="uploadProfile()">
            </button>
            <span id="uploadStatus" style="font-size: 12px; color: #aaa; align-self: center;"></span>
            <button class="btn-blue" onclick="loadLastProfile()">Reload Last</button>
            <div style="width: 1px; background: #555; margin: 0 10px;"></div>
            <button class="btn-start" onclick="sendCommand('start')">START</button>
            <button class="btn-stop" onclick="sendCommand('stop')">STOP</button>
        </div>
    </header>

    <div class="tabs">
        <div class="tab active" onclick="switchTab('console')">Console Log</div>
        <div class="tab" onclick="switchTab('map')">Live Map</div>
        <div class="tab" onclick="switchTab('entities')">Entity Table</div>
        <div class="tab" onclick="switchTab('debug')">Debug State</div>
        <div class="tab" onclick="switchTab('path')">Path Tester</div>
    </div>

    <div class="content">
        <div id="console" class="view active">
            <div id="log-container">Waiting for logs...</div>
        </div>

        <div id="map" class="view">
            <div id="map-wrapper">
                <canvas id="mapCanvas"></canvas>
                <div class="legend">
                    <div><span class="dot" style="background:cyan"></span> Player</div>
                    <div><span class="dot" style="background:lime"></span> Path</div>
                    <div><span class="dot" style="background:#ff0000"></span> Enemy NPC</div>
                    <div><span class="dot" style="background:#00ff00"></span> Friendly NPC</div>
                </div>
            </div>
        </div>

        <div id="entities" class="view">
            <div style="padding: 10px 20px; background: var(--panel); border-bottom: 1px solid var(--border);">
                <label>Filter Type: </label>
                <select id="typeFilter" onchange="updateTable()" style="background:#333; color:white; border:1px solid #555;">
                    <option value="All">All Entities</option>
                    <option value="Enemy">Enemies</option>
                    <option value="Object">Objects</option>
                    <option value="Player">Player Info</option>
                </select>
            </div>
            <div id="table-wrapper">
                <table id="entTable">
                    <thead><tr id="tableHeader"></tr></thead>
                    <tbody></tbody>
                </table>
            </div>
        </div>
        <div id="debug" class="view">
            <div id="debug-wrapper">
                <div id="debug-grid" class="debug-grid"></div>
            </div>
        </div>
        <div id="path" class="view">
            <div style="padding: 20px; color: #ddd;">
                <div style="display: flex; gap: 20px; background: #252526; padding: 15px; border: 1px solid #333;">
                    <div>
                        <h4>Start</h4>
                        <input type="number" id="startX" placeholder="X" step="0.1" style="width:70px">
                        <input type="number" id="startY" placeholder="Y" step="0.1" style="width:70px">
                        <input type="number" id="startZ" placeholder="Z" step="0.1" style="width:70px">
                        <button class="btn-blue" onclick="getPos('start')">Get My Pos</button>
                    </div>
                    <div>
                        <h4>End</h4>
                        <input type="number" id="endX" placeholder="X" step="0.1" style="width:70px">
                        <input type="number" id="endY" placeholder="Y" step="0.1" style="width:70px">
                        <input type="number" id="endZ" placeholder="Z" step="0.1" style="width:70px">
                        <button class="btn-blue" onclick="getPos('end')">Get My Pos</button>
                    </div>
                    <div>
                        <h4>Settings</h4>
                        <input type="number" id="pMapId" placeholder="MapID" style="width:60px">
                        <label><input type="checkbox" id="pCanFly"> Can Fly</label>
                        <label><input type="checkbox" id="pStrict" checked> Strict</label>
                        <br><br>
                        <button class="btn-start" onclick="runPathTest()">Calculate Path</button>
                    </div>
                </div>
                <div id="pathStatus" style="margin: 10px 0; font-weight: bold;"></div>
                <div style="background: #000; height: 500px; position: relative; border: 1px solid #444;">
                    <canvas id="pathCanvas" style="width:100%; height:100%; display:block;"></canvas>
                </div>
            </div>
        </div>
    </div>

    <div class="status-bar">
        <span id="stat-running">Status: Stopped</span>
        <span id="stat-profile">Profile: None</span>
        <span id="stat-uptime">Uptime: 0m</span>
    </div>
)HTML";

    // PART 3: SCRIPT
    ss << R"HTML(
    <script>
        let gameState = {};
        const canvas = document.getElementById('mapCanvas');
        const ctx = canvas.getContext('2d');

        async function sendCommand(cmd) { try { await fetch('/api/' + cmd, { method: 'POST' }); } catch(e) {} }
        async function toggleAutoLoad(el) { await fetch('/api/set_autoload', { method: 'POST', body: el.checked ? "true" : "false" }); }
        
        async function loadLastProfile() {
            const status = document.getElementById('uploadStatus');
            status.innerText = "Reloading...";
            try {
                const res = await fetch('/api/load_last', { method: 'POST' });
                const data = await res.json();
                status.innerText = data.status === 'success' ? "Reloaded!" : "Error";
                status.style.color = data.status === 'success' ? "lime" : "red";
                setTimeout(() => status.innerText = "", 3000);
            } catch(err) { status.innerText = "Failed"; }
        }

        async function uploadProfile() {
            const input = document.getElementById('profileFile');
            const status = document.getElementById('uploadStatus');
            if (input.files.length === 0) return;
            const file = input.files[0];
            const reader = new FileReader();
            reader.onload = async function(e) {
                status.innerText = "Uploading...";
                try {
                    const res = await fetch('/api/upload_profile', {
                        method: 'POST',
                        body: e.target.result,
                        headers: { 'Profile-Name': file.name } 
                    });
                    const data = await res.json();
                    status.innerText = data.status === 'success' ? "Success!" : "Error";
                    status.style.color = data.status === 'success' ? "lime" : "red";
                    setTimeout(() => status.innerText = "", 3000);
                } catch(err) { status.innerText = "Failed"; }
            };
            reader.readAsText(file);
        }

        function copyToClipboard(text, el) {
            navigator.clipboard.writeText(text);
            const prev = el.style.background;
            el.style.background = '#666';
            setTimeout(() => el.style.background = "", 150);
        }

        function switchTab(name) {
            document.querySelectorAll('.view').forEach(el => el.classList.remove('active'));
            document.querySelectorAll('.tab').forEach(el => el.classList.remove('active'));
            document.getElementById(name).classList.add('active');
            event.target.classList.add('active');
            if(name === 'map') resizeCanvas();
        }

        function resizeCanvas() {
            if(canvas.parentElement) {
                canvas.width = canvas.parentElement.clientWidth;
                canvas.height = canvas.parentElement.clientHeight;
            }
        }
        window.onresize = resizeCanvas;

        // --- PATH TESTER LOGIC ---
        async function getPos(target) {
            const res = await fetch('/api/get_player_info');
            const p = await res.json();
            document.getElementById(target+'X').value = p.x.toFixed(2);
            document.getElementById(target+'Y').value = p.y.toFixed(2);
            document.getElementById(target+'Z').value = p.z.toFixed(2);
            if(target === 'start') document.getElementById('pMapId').value = p.mapId;
        }

        async function runPathTest() {
            const status = document.getElementById('pathStatus');
            status.innerText = "Calculating...";
            
            const payload = {
                start: { 
                    x: parseFloat(document.getElementById('startX').value), 
                    y: parseFloat(document.getElementById('startY').value), 
                    z: parseFloat(document.getElementById('startZ').value) 
                },
                end: { 
                    x: parseFloat(document.getElementById('endX').value), 
                    y: parseFloat(document.getElementById('endY').value), 
                    z: parseFloat(document.getElementById('endZ').value) 
                },
                mapId: parseInt(document.getElementById('pMapId').value),
                canFly: document.getElementById('pCanFly').checked,
                strict: document.getElementById('pStrict').checked
            };

            const res = await fetch('/api/test_path', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(payload)
            });
            
            const data = await res.json();
            if(data.success) {
                status.innerText = `Success: ${data.count} points generated.`;
                status.style.color = 'lime';
                drawPathTest(data.path);
            } else {
                status.innerText = `Failed: ${data.error || "Unknown error"}`;
                status.style.color = 'red';
            }
        }

        function drawPathTest(path) {
            const cvs = document.getElementById('pathCanvas');
            // Fix resolution
            cvs.width = cvs.clientWidth;
            cvs.height = cvs.clientHeight;
            const cx = cvs.getContext('2d');
            
            if(!path || path.length === 0) return;

            // Normalize bounds
            let minX=Infinity, maxX=-Infinity, minY=Infinity, maxY=-Infinity;
            path.forEach(p => {
                if(p.x < minX) minX = p.x; if(p.x > maxX) maxX = p.x;
                if(p.y < minY) minY = p.y; if(p.y > maxY) maxY = p.y;
            });
            
            const padding = 40;
            const w = maxX - minX || 1;
            const h = maxY - minY || 1;
            const scale = Math.min((cvs.width - padding*2)/w, (cvs.height - padding*2)/h);

            cx.clearRect(0,0,cvs.width,cvs.height);
            cx.strokeStyle = "#00ff00";
            cx.lineWidth = 2;
            cx.beginPath();

            path.forEach((p, i) => {
                // Invert Y for correct visualization direction
                const x = padding + (p.x - minX) * scale;
                const y = cvs.height - (padding + (p.y - minY) * scale);
                
                if(i===0) {
                    cx.moveTo(x, y);
                    cx.fillStyle = "blue"; cx.fillRect(x-4, y-4, 8, 8); // Start
                } else {
                    cx.lineTo(x, y);
                }
                if(i===path.length-1) {
                    cx.fillStyle = "red"; cx.fillRect(x-4, y-4, 8, 8); // End
                }
            });
            cx.stroke();
        }

        // --- DEBUG GRID RENDERER [FIXED: PRIORITY HIGHLIGHTING] ---
        function renderDebugGrid() {
            const container = document.getElementById('debug-grid');
            if (!gameState.debug) return;
            
            let html = '';
            
            // Visual Order
            const order = ["GlobalState", "Combat", "Looting", "Gathering", "PathFollowing", "InteractState", "StuckState", "RespawnState"];
            const keys = Object.keys(gameState.debug).sort((a,b) => {
                let ia = order.indexOf(a); let ib = order.indexOf(b);
                if(ia === -1) ia = 99; if(ib === -1) ib = 99;
                return ia - ib;
            });

            // --- PRIORITY LOGIC ---
            // Only ONE state can be 'active' at a time.
            let activeStateName = null;
            const d = gameState.debug;

            // 1. Critical
            if (d.StuckState && d.StuckState.isStuck) activeStateName = "StuckState";
            else if (d.Combat && (d.Combat.inCombat || d.Combat.underAttack)) activeStateName = "Combat";
            else if (d.RespawnState && (d.RespawnState.isDead || d.RespawnState.isPathingToCorpse)) activeStateName = "RespawnState";
            
            // 2. High Priority
            else if (d.Looting && d.Looting.hasLoot) activeStateName = "Looting";
            else if (d.InteractState && d.InteractState.interactActive) activeStateName = "InteractState";
            else if (d.Gathering && (d.Gathering.hasNode || d.Gathering.nodeActive)) activeStateName = "Gathering";
            
            // 3. Movement / Idle
            else if (d.PathFollowing && d.PathFollowing.hasPath) activeStateName = "PathFollowing";
            else if (d.WaypointReturn && (d.WaypointReturn.hasPath || d.WaypointReturn.waitingForUnstuck)) activeStateName = "WaypointReturn";
            else if (d.GlobalState && d.GlobalState.inMotion) activeStateName = "GlobalState"; 

            keys.forEach(catName => {
                const data = gameState.debug[catName];
                const cardClass = (catName === activeStateName) ? "debug-card active-card" : "debug-card";

                html += `<div class="${cardClass}"><h3>${catName}</h3><table class="debug-table">`;
                for(let key in data) {
                    let val = data[key];
                    let displayVal = val;
                    let cls = "";
                    if (typeof val === 'boolean') {
                        cls = val ? "val-true" : "val-false";
                        displayVal = val ? "TRUE" : "FALSE";
                    } else if (typeof val === 'number') {
                        cls = "val-num";
                    } else if (typeof val === 'object' && val !== null) {
                        cls = "val-obj";
                        displayVal = JSON.stringify(val);
                    }
                    html += `<tr><td class="dt-key">${key}</td><td class="dt-val ${cls}">${displayVal}</td></tr>`;
                }
                html += `</table></div>`;
            });
            container.innerHTML = html;
        }
)HTML";

        // PART 3: SCRIPT 2
        ss << R"HTML(

        async function poll() {
            try {
                if(document.getElementById('console').classList.contains('active')) {
                    const res = await fetch('/api/logs');
                    const text = await res.text();
                    const container = document.getElementById('log-container');
                    const isAtBottom = container.scrollHeight - container.scrollTop === container.clientHeight;
                    container.innerText = text;
                    if(isAtBottom) container.scrollTop = container.scrollHeight;
                }

                const res = await fetch('/api/state');
                gameState = await res.json();

                // [FIXED] Banner Status Logic with g_IsPaused support
                const bar = document.querySelector('.status-bar');
                const profile = gameState.profile || "None";
                const isPaused = gameState.paused; // from g_IsPaused

                if (profile === "None") {
                    bar.style.background = "#d32f2f"; // RED (No Profile)
                    bar.style.color = "white";
                } else if (isPaused === true || isPaused === "true") {
                    bar.style.background = "#ffc107"; // YELLOW (Paused)
                    bar.style.color = "black";
                } else {
                    bar.style.background = "#28a745"; // GREEN (Running)
                    bar.style.color = "white";
                }

                document.getElementById('stat-running').innerText = "Status: " + (isPaused ? "PAUSED" : "RUNNING");
                document.getElementById('stat-profile').innerText = "Profile: " + profile;
                document.getElementById('stat-uptime').innerText = "Uptime: " + gameState.uptime;

                const chk = document.getElementById('chkAutoLoad');
                if(gameState.autoLoad !== undefined && document.activeElement !== chk) {
                    chk.checked = gameState.autoLoad;
                }

                if(document.getElementById('map').classList.contains('active')) drawMap();
                if(document.getElementById('entities').classList.contains('active')) updateTable();
                if(document.getElementById('debug').classList.contains('active')) renderDebugGrid();

            } catch(e) { console.log(e); }
        }

        function drawMap() {
            if(!gameState.player) return;
            const px = gameState.player.x;
            const py = gameState.player.y;
            const scale = 4.0;
            const cx = canvas.width / 2;
            const cy = canvas.height / 2;
            ctx.fillStyle = "#000";
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            const toScreen = (x, y) => ({ x: cx + (x - px) * scale, y: cy - (y - py) * scale });

            if(gameState.path) {
                ctx.beginPath();
                ctx.strokeStyle = "lime";
                ctx.lineWidth = 2;
                gameState.path.forEach((p, i) => {
                    const s = toScreen(p.x, p.y);
                    if(i===0) ctx.moveTo(s.x, s.y); else ctx.lineTo(s.x, s.y);
                });
                ctx.stroke();
            }
            if(gameState.entities) {
                gameState.entities.forEach(ent => {
                    const s = toScreen(ent.x, ent.y);
                    if(s.x < -10 || s.x > canvas.width+10 || s.y < -10 || s.y > canvas.height+10) return;
                    ctx.beginPath();
                    ctx.arc(s.x, s.y, 4, 0, Math.PI*2);
                    if(ent.type === 'Enemy') ctx.fillStyle = (ent.reaction === 2) ? '#00ff00' : (ent.reaction === 1 ? '#ffff00' : '#ff0000');
                    else if(ent.type === 'Object') ctx.fillStyle = '#d02090';
                    else if(ent.type === 'Player') ctx.fillStyle = 'blue';
                    else ctx.fillStyle = 'gray';
                    ctx.fill();
                });
            }
            const rot = gameState.player.rot; 
            ctx.save();
            ctx.translate(cx, cy);
            ctx.rotate(rot + Math.PI); 
            ctx.beginPath(); ctx.moveTo(0, -8); ctx.lineTo(-6, 8); ctx.lineTo(6, 8); ctx.closePath();
            ctx.fillStyle = "cyan"; ctx.fill();
            ctx.restore();
        }

        function updateTable() {
            const tbody = document.querySelector('#entTable tbody');
            const thead = document.querySelector('#tableHeader');
            const filter = document.getElementById('typeFilter').value;
            tbody.innerHTML = '';
            
            if (filter === 'Player') {
                thead.innerHTML = `<th>Property</th><th>Value</th>`;
                const p = gameState.player;
                if (!p) return;
                const rows = [
                    ["Position", `${p.x.toFixed(1)}, ${p.y.toFixed(1)}, ${p.z.toFixed(1)}`],
                    ["Rotation", p.rot.toFixed(3) + " rad"],
                    ["Health", `${p.hp} / ${p.maxHp}`],
                    ["Level", p.level],
                    ["Map ID", p.mapId],
                    ["Free Bag Slots", p.bagFree],
                    ["Need Repair", p.needRepair ? "YES" : "No"],
                    ["Status", p.isDead ? "DEAD" : (p.inCombat ? "In Combat" : "Idle")],
                    ["Flags", `Mounted: ${p.isMounted}, Ground Mounted: ${p.groundMounted}, Flying Mounted: ${p.flyingMounted}, Flying: ${p.isFlying}, Indoor: ${p.isIndoor}, In Water: ${p.inWater}, On Ground: ${p.onGround}`],
                    ["Target GUID", p.target],
                    ["Selected GUID", p.selected]
                ];
                rows.forEach(r => tbody.innerHTML += `<tr><td><b>${r[0]}</b></td><td onclick="copyToClipboard(this.innerText, this)">${r[1]}</td></tr>`);
                return;
            }

            thead.innerHTML = `<th>Type</th><th>Name</th><th>Dist</th><th>Pos</th><th>Health</th>`;
            if (!gameState.entities) return;

            gameState.entities.filter(e => filter === 'All' || e.type === filter).sort((a, b) => a.dist - b.dist).forEach(ent => {
                let healthInfo = "N/A";
                let combatInfo = "-";
                let targetInfo = "-";
                let color = '#ccc';
                let typeDisplay = ent.type;

                if (ent.type === 'Enemy') {
                    healthInfo = `${ent.hp} / ${ent.maxHp} (Lvl ${ent.level})`;
                    combatInfo = ent.inCombat ? "<span style='color:red;font-weight:bold'>YES</span>" : "No";
                    targetInfo = ent.targetGuid || "None";

                    if (ent.reaction === 2) { color = '#00ff00'; typeDisplay = "Friendly NPC"; }
                    else if (ent.reaction === 1) { color = '#ffff00'; typeDisplay = "Neutral NPC"; }
                    else { color = '#ff0000'; typeDisplay = "Enemy NPC"; }
                } 
                else if (ent.type === 'Object') {
                    healthInfo = ent.nodeActive ? "Active" : "Depleted";
                    combatInfo = "-";
                    targetInfo = "-";
                    color = '#d02090'; // Purple
                }

                const tr = document.createElement('tr');
                tr.innerHTML = `
                    <td style="color:${color}; font-weight:bold;" onclick="copyToClipboard(this.innerText, this)" title="Click to copy">${typeDisplay}</td>
                    <td onclick="copyToClipboard(this.innerText, this)" title="Click to copy">${ent.name} (ID: ${ent.id})</td>
                    <td onclick="copyToClipboard(this.innerText, this)" title="Click to copy">${ent.dist.toFixed(1)}</td>
                    <td onclick="copyToClipboard(this.innerText, this)" title="Click to copy">${ent.x.toFixed(1)}, ${ent.y.toFixed(1)}, ${ent.z.toFixed(1)}</td>
                    <td onclick="copyToClipboard(this.innerText, this)" title="Click to copy">${healthInfo}</td>
                    <td onclick="copyToClipboard(this.innerText, this)" title="Click to copy">${combatInfo}</td>
                    <td style="font-family:monospace; font-size:11px;" onclick="copyToClipboard(this.innerText, this)" title="Click to copy">${targetInfo}</td>
                `;
                tbody.appendChild(tr);
            });
        }

        setInterval(poll, 1000);
        poll();
    </script>
</body>
</html>
    )HTML";

    return ss.str();
}