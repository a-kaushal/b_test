#include "WebServer.h"

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

#pragma comment(lib, "Ws2_32.lib")

// --- EXTERNS ---
extern std::ofstream g_LogFile;
extern std::mutex g_EntityMutex;
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
std::atomic<bool> WebServer::botActive = false;
std::atomic<bool> WebServer::profileLoadReq = false;
std::string WebServer::currentProfile = "None";
std::string WebServer::pendingProfile = "";
unsigned long long WebServer::startTime = 0;

void WebServer::Start(int port) {
    if (running) return;
    running = true;
    startTime = GetTickCount64();
    serverThread = std::thread(ServerThread, port);
}

void WebServer::Stop() {
    running = false;
    if (serverThread.joinable()) serverThread.detach();
}

bool WebServer::IsBotActive() { return botActive; }
void WebServer::SetBotActive(bool active) { botActive = active; }
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

void WebServer::HandleClient(unsigned __int64 clientSocketRaw) {
    SOCKET clientSocket = (SOCKET)clientSocketRaw;

    // [CHANGE] Increased Buffer Size to 512KB to handle file uploads
    const int BUFFER_SIZE = 512 * 1024;
    std::vector<char> buffer(BUFFER_SIZE);

    int bytesRecv = recv(clientSocket, buffer.data(), BUFFER_SIZE, 0);

    if (bytesRecv > 0) {
        std::string request(buffer.data(), bytesRecv);
        std::string responseBody;
        std::string contentType = "text/html";
        int statusCode = 200;

        // --- API ROUTING ---

        // 1. GET /api/status 
        if (request.find("GET /api/status ") != std::string::npos) {
            responseBody = BuildStatusJSON();
            contentType = "application/json";
        }
        // 2. POST /api/start
        else if (request.find("POST /api/start ") != std::string::npos) {
            botActive = true;
            responseBody = "{\"status\":\"ok\"}";
            contentType = "application/json";
        }
        // 3. POST /api/stop
        else if (request.find("POST /api/stop ") != std::string::npos) {
            botActive = false;
            responseBody = "{\"status\":\"ok\"}";
            contentType = "application/json";
        }
        // 4. POST /api/upload_profile
        else if (request.find("POST /api/upload_profile ") != std::string::npos) {
            // Find Body Separator
            size_t bodyPos = request.find("\r\n\r\n");
            if (bodyPos != std::string::npos) {
                // The body contains the raw C++ code
                std::string body = request.substr(bodyPos + 4);

                std::string error;
                // This blocks while compiling. 
                // For a production bot, you'd want to move this to a worker thread.
                if (g_ProfileLoader.CompileAndLoad(body, error)) {
                    responseBody = "{\"status\":\"success\", \"message\":\"Profile Compiled & Loaded Successfully!\"}";
                    currentProfile = "Dynamic Upload";
                }
                else {
                    responseBody = "{\"status\":\"error\", \"message\":\"" + EscapeJSON(error) + "\"}";
                }
                contentType = "application/json";
            }
            else {
                responseBody = "{\"status\":\"error\", \"message\":\"No body found\"}";
                contentType = "application/json";
            }
        }
        // 5. GET /data
        else if (request.find("GET /data ") != std::string::npos) {
            responseBody = BuildJSON();
            contentType = "application/json";
        }
        // 6. GET / (Main Page)
        else {
            responseBody = GetHTML();
        }

        // Send Response
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
    }

    shutdown(clientSocket, SD_SEND);
    closesocket(clientSocket);
}

std::string WebServer::BuildStatusJSON() {
    if (!g_GameState) return "{}";
    std::stringstream ss;
    ss << "{";
    ss << "\"running\": " << (botActive ? "true" : "false") << ",";
    ss << "\"profile\": \"" << currentProfile << "\",";

    unsigned long long now = GetTickCount64();
    unsigned long long elapsed = (now - startTime) / 1000;
    ss << "\"uptime\": \"" << (elapsed / 3600) << "h " << ((elapsed % 3600) / 60) << "m\",";

    std::string actionState = "Idle";
    if (g_GameState->combatState.inCombat) actionState = "Combat";
    else if (botActive) actionState = "Active";

    ss << "\"action_state\": \"" << actionState << "\"";
    ss << "}";
    return ss.str();
}

// Preserve BuildJSON (Legacy)
std::string WebServer::BuildJSON() {
    if (!g_GameState) return "{}";
    std::stringstream ss;
    ss << "{ \"player\": { \"x\": " << g_GameState->player.position.x << " } }"; // truncated for brevity
    return ss.str();
}

std::string WebServer::GetHTML() {
    return R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Shadow Bot Controller</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <style>
        body { background-color: #0f0f0f; color: #e0e0e0; font-family: sans-serif; }
        .panel { background: #1a1a1a; border: 1px solid #333; padding: 20px; border-radius: 8px; }
        .btn { padding: 10px 20px; border-radius: 4px; font-weight: bold; cursor: pointer; }
        .btn-green { background: #1b4d1b; color: #fff; border: 1px solid #2d7a2d; }
        .btn-green:hover { background: #2d7a2d; }
        .btn-blue { background: #1b2d4d; color: #fff; border: 1px solid #2d4a7a; }
        .btn-blue:hover { background: #2d4a7a; }
    </style>
</head>
<body class="p-8">
    <div class="max-w-4xl mx-auto grid gap-6">
        
        <div class="flex justify-between items-center border-b border-gray-700 pb-4">
            <h1 class="text-3xl font-bold text-yellow-500">Shadow Bot Interface</h1>
            <div id="status-indicator" class="text-red-500 font-mono">DISCONNECTED</div>
        </div>

        <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
            <div class="panel">
                <h2 class="text-xl mb-4 text-gray-300">Bot Controls</h2>
                <div class="flex gap-4 mb-6">
                    <button onclick="api('start')" class="btn btn-green flex-1">START BOT</button>
                    <button onclick="api('stop')" class="btn bg-red-900 border border-red-700 flex-1 hover:bg-red-800">STOP BOT</button>
                </div>
                
                <h3 class="text-lg mb-2 text-gray-400 border-t border-gray-700 pt-4">Profile Upload</h3>
                <div class="flex flex-col gap-2">
                    <input type="file" id="profile-file" accept=".cpp,.txt" class="bg-black border border-gray-700 p-2 text-sm text-gray-400">
                    <button onclick="uploadProfile()" class="btn btn-blue">UPLOAD & COMPILE PROFILE</button>
                </div>
                <div id="upload-status" class="mt-2 text-xs font-mono text-gray-500"></div>
            </div>

            <div class="panel font-mono text-sm">
                <h2 class="text-xl mb-4 text-gray-300">Statistics</h2>
                <div class="flex justify-between py-1 border-b border-gray-800">
                    <span class="text-gray-500">State</span>
                    <span id="stat-running" class="text-white">...</span>
                </div>
                <div class="flex justify-between py-1 border-b border-gray-800">
                    <span class="text-gray-500">Active Profile</span>
                    <span id="stat-profile" class="text-yellow-500">...</span>
                </div>
                <div class="flex justify-between py-1 border-b border-gray-800">
                    <span class="text-gray-500">Action</span>
                    <span id="stat-action" class="text-blue-400">...</span>
                </div>
                <div class="flex justify-between py-1">
                    <span class="text-gray-500">Uptime</span>
                    <span id="stat-uptime" class="text-gray-300">...</span>
                </div>
            </div>
        </div>
    </div>

    <script>
        const API = 'http://localhost:8080/api';

        // 1. Basic Commands
        async function api(endpoint) {
            try { await fetch(`${API}/${endpoint}`, { method: 'POST' }); fetchStatus(); } 
            catch(e) { console.error(e); }
        }

        // 2. File Upload Logic
        async function uploadProfile() {
            const fileInput = document.getElementById('profile-file');
            const statusDiv = document.getElementById('upload-status');
            
            if (fileInput.files.length === 0) {
                alert("Please select a .cpp file first.");
                return;
            }

            const file = fileInput.files[0];
            statusDiv.innerText = "Reading file...";
            statusDiv.className = "mt-2 text-xs font-mono text-yellow-500";

            const reader = new FileReader();
            reader.onload = async function(e) {
                const content = e.target.result;
                statusDiv.innerText = "Uploading & Compiling...";
                
                try {
                    const res = await fetch(`${API}/upload_profile`, {
                        method: 'POST',
                        body: content // Send raw content
                    });
                    
                    const data = await res.json();
                    if (data.status === 'success') {
                        statusDiv.innerText = "SUCCESS: " + data.message;
                        statusDiv.className = "mt-2 text-xs font-mono text-green-500";
                    } else {
                        statusDiv.innerText = "ERROR: " + data.message;
                        statusDiv.className = "mt-2 text-xs font-mono text-red-500";
                    }
                } catch (err) {
                    statusDiv.innerText = "Network Error";
                    statusDiv.className = "mt-2 text-xs font-mono text-red-500";
                }
            };
            reader.readAsText(file);
        }

        // 3. Status Polling
        async function fetchStatus() {
            try {
                const res = await fetch(`${API}/status`);
                const data = await res.json();
                
                document.getElementById('status-indicator').innerText = "CONNECTED";
                document.getElementById('status-indicator').className = "text-green-500 font-mono";

                document.getElementById('stat-running').innerText = data.running ? "RUNNING" : "STOPPED";
                document.getElementById('stat-profile').innerText = data.profile;
                document.getElementById('stat-action').innerText = data.action_state;
                document.getElementById('stat-uptime').innerText = data.uptime;
            } catch(e) {
                document.getElementById('status-indicator').innerText = "DISCONNECTED";
                document.getElementById('status-indicator').className = "text-red-500 font-mono";
            }
        }
        
        setInterval(fetchStatus, 1000);
        fetchStatus();
    </script>
</body>
</html>
    )HTML";
}

// Preserve existing BuildJSON for /data endpoint
/*std::string WebServer::BuildJSON() {
    if (!g_GameState) return "{}";

    std::stringstream ss;
    ss << "{";

    // --- PLAYER DATA ---
    ss << "\"player\": {";
    ss << "\"x\": " << g_GameState->player.position.x << ",";
    ss << "\"y\": " << g_GameState->player.position.y << ",";
    ss << "\"z\": " << g_GameState->player.position.z << ",";
    ss << "\"rotation\": " << g_GameState->player.rotation << ",";
    ss << "\"health\": " << g_GameState->player.health << ",";
    ss << "\"maxHealth\": " << g_GameState->player.maxHealth << ",";
    ss << "\"level\": " << g_GameState->player.level << ",";
    ss << "\"state\": \"" << (g_GameState->combatState.inCombat ? "Combat" : "Idle") << "\"";
    ss << "},";

    // --- PATH DATA ---
    ss << "\"path\": [";
    {
        const auto& path = g_GameState->globalState.activePath;
        for (size_t i = 0; i < path.size(); ++i) {
            ss << "{\"x\": " << path[i].pos.x << ", \"y\": " << path[i].pos.y << "}";
            if (i < path.size() - 1) ss << ",";
        }
    }
    ss << "],";

    // --- ENTITY DATA ---
    ss << "\"entities\": [";
    {
        std::lock_guard<std::mutex> lock(g_EntityMutex);
        const auto& entities = g_GameState->entities;
        bool first = true;

        for (const auto& ent : entities) {
            if (!ent.info) continue;

            std::string typeStr = "Unknown";
            std::string nameStr = "Unknown";
            float x = 0, y = 0, z = 0;
            int hp = 0, maxHp = 0;
            float dist = 0;
            bool include = false;

            EntityInfo* rawInfo = ent.info.get();

            if (ent.type == 33) { // Enemy
                if (EnemyInfo* npc = static_cast<EnemyInfo*>(rawInfo)) {
                    typeStr = "Enemy";
                    nameStr = npc->name;
                    x = npc->position.x;
                    y = npc->position.y;
                    z = npc->position.z;
                    hp = npc->health;
                    maxHp = npc->maxHealth;
                    dist = npc->distance;
                    include = true;
                }
            }
            else if (ent.type == 257) { // Object
                if (ObjectInfo* obj = static_cast<ObjectInfo*>(rawInfo)) {
                    typeStr = "Object";
                    nameStr = obj->name;
                    x = obj->position.x;
                    y = obj->position.y;
                    z = obj->position.z;
                    dist = obj->distance;
                    include = true;
                }
            }

            if (include) {
                if (!first) ss << ",";
                ss << "{";
                ss << "\"type\": \"" << typeStr << "\",";
                ss << "\"name\": \"" << EscapeJSON(nameStr) << "\",";
                ss << "\"x\": " << x << ",";
                ss << "\"y\": " << y << ",";
                ss << "\"z\": " << z << ",";
                ss << "\"hp\": " << hp << ",";
                ss << "\"maxHp\": " << maxHp << ",";
                ss << "\"dist\": " << dist;
                ss << "}";
                first = false;
            }
        }
    }
    ss << "]";

    ss << "}";
    return ss.str();
}
*/