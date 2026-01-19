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

// --- FORWARD DECLARATIONS TO PREVENT LINKER ERRORS ---
// We define these externs manually to avoid including headers like Database.h
// which cause LNK2005 errors due to double definitions.

// 1. LogFile (Used by Pathfinding2.h)
extern std::ofstream g_LogFile;

// 2. Mutex (Used by WebServer logic)
extern std::mutex g_EntityMutex;

// --- INCLUDES ---
// Order matters here due to dependencies in your headers
#include "Vector.h"
#include "Entity.h"

// Pathfinding2.h is needed for PathNode definition.
// It might include MovementController.h internally, but we avoid including it directly.
#include "Pathfinding2.h" 

// WorldState defines the main data structure
#include "WorldState.h"

#include "ProfileLoader.h"
extern ProfileLoader g_ProfileLoader; // Global instance declared in dllmain

// We do NOT include Database.h, MemoryRead.h, or dllmain.h to avoid Linker Errors.

// Global Game State Pointer (Extern)
// This is usually declared in WorldState.h or MovementController.h
// If WorldState.h declares it, this is redundant but harmless. 
// If not, this is required.
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

// --- Interface Methods for Main Thread ---
bool WebServer::IsBotActive() {
    return botActive;
}

void WebServer::SetBotActive(bool active) {
    botActive = active;
}

bool WebServer::IsProfileLoadRequested() {
    return profileLoadReq;
}

std::string WebServer::GetRequestedProfile() {
    return pendingProfile;
}

void WebServer::ConfirmProfileLoaded(const std::string& profileName) {
    currentProfile = profileName;
    profileLoadReq = false;
    pendingProfile = "";
}

void WebServer::ServerThread(int port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        WSACleanup();
        return;
    }

    sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = INADDR_ANY;
    service.sin_port = htons(port);

    if (bind(listenSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        closesocket(listenSocket);
        WSACleanup();
        return;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenSocket);
        WSACleanup();
        return;
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
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

void WebServer::HandleClient(unsigned __int64 clientSocketRaw) {
    SOCKET clientSocket = (SOCKET)clientSocketRaw;
    char buffer[4096];
    int bytesRecv = recv(clientSocket, buffer, 4096, 0);

    if (bytesRecv > 0) {
        std::string request(buffer, bytesRecv);
        std::string response;
        std::string responseBody;
        std::string contentType = "text/html";

        // --- API ROUTING ---

        // 1. GET /api/status (For new Dashboard)
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
        // 4. POST /api/load_profile
        else if (request.find("POST /api/load_profile ") != std::string::npos) {
            // Very basic JSON body parsing
            size_t bodyPos = request.find("\r\n\r\n");
            if (bodyPos != std::string::npos) {
                std::string body = request.substr(bodyPos + 4);
                size_t keyPos = body.find("\"profile\"");
                if (keyPos != std::string::npos) {
                    size_t valStart = body.find(":", keyPos) + 1;
                    size_t quoteStart = body.find("\"", valStart);
                    size_t quoteEnd = body.find("\"", quoteStart + 1);
                    if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                        pendingProfile = body.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                        profileLoadReq = true;
                    }
                }
            }
            responseBody = "{\"status\":\"ok\"}";
            contentType = "application/json";
        }
        // 5. GET /data (Legacy Support)
        else if (request.find("GET /data ") != std::string::npos) {
            responseBody = BuildJSON();
            contentType = "application/json";
        }
        // 6. GET /api/upload_profile
        else if (request.find("POST /api/upload_profile ") != std::string::npos) {
            // 1. Extract Body (The C++ Code)
            size_t bodyPos = request.find("\r\n\r\n");
            if (bodyPos != std::string::npos) {
                std::string body = request.substr(bodyPos + 4);

                // 2. Trigger Compile/Load
                // NOTE: This blocks the web thread during compilation (a few seconds)
                std::string error;
                if (g_ProfileLoader.CompileAndLoad(body, error)) {
                    responseBody = "{\"status\":\"success\", \"message\":\"Profile Compiled and Loaded\"}";
                }
                else {
                    responseBody = "{\"status\":\"error\", \"message\":\"" + EscapeJSON(error) + "\"}";
                }
                contentType = "application/json";
            }
        }
        // 7. GET / (Main Page)
        else {
            responseBody = GetHTML();
        }

        // Send Response
        std::stringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
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

    // Running State
    ss << "\"running\": " << (botActive ? "true" : "false") << ",";

    // Profile
    ss << "\"profile\": \"" << currentProfile << "\",";

    // Uptime (HH:MM:SS)
    unsigned long long now = GetTickCount64();
    unsigned long long elapsedSeconds = (now - startTime) / 1000;
    unsigned long long hours = elapsedSeconds / 3600;
    unsigned long long minutes = (elapsedSeconds % 3600) / 60;
    unsigned long long seconds = elapsedSeconds % 60;

    ss << "\"uptime\": \""
        << std::setfill('0') << std::setw(2) << hours << ":"
        << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setfill('0') << std::setw(2) << seconds << "\",";

    // XP Per Hour (Placeholder - WorldState needs an xp tracker to support this)
    ss << "\"xp_per_hour\": \"0\",";

    // Current Target Resolution
    std::string targetName = "None";
    uint64_t targetGuidLow = g_GameState->player.targetGuidLow;
    if (targetGuidLow != 0) {
        std::lock_guard<std::mutex> lock(g_EntityMutex);
        for (const auto& ent : g_GameState->entities) {
            if (ent.guidLow == targetGuidLow) {
                if (ent.info) {
                    if (ent.type == 33) { // Enemy
                        if (auto npc = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) targetName = npc->name;
                    }
                    else if (ent.type == 257) { // Object
                        if (auto obj = std::dynamic_pointer_cast<ObjectInfo>(ent.info)) targetName = obj->name;
                    }
                }
                break;
            }
        }
    }
    ss << "\"current_target\": \"" << targetName << "\",";

    // Action State Logic
    std::string actionState = "Idle";
    if (g_GameState->player.isDead) actionState = "Dead / Ghost";
    else if (g_GameState->combatState.inCombat) actionState = "Combat";
    else if (g_GameState->player.state & (1 << 24)) actionState = "Flying"; // Basic guess based on flags
    else if (botActive) actionState = "Patrolling";

    ss << "\"action_state\": \"" << actionState << "\"";

    ss << "}";
    return ss.str();
}

std::string WebServer::GetHTML() {
    // Note: Standard spaces used to ensure C++ string literal parsing compatibility
    return R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Shadow Bot Controller</title>
    <script src="https://cdn.tailwindcss.com"></script>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Cinzel:wght@400;700&family=Roboto:wght@300;400;500&display=swap');

        body {
            font-family: 'Roboto', sans-serif;
            background-color: #050505;
            color: #e0e0e0;
            background-image: radial-gradient(circle at 50% 50%, #1a1a1a 0%, #000000 100%);
        }

        h1, h2, h3, .wow-font {
            font-family: 'Cinzel', serif;
            color: #f8b700;
            text-shadow: 0px 0px 5px rgba(248, 183, 0, 0.4);
        }

        .panel {
            background: rgba(20, 20, 20, 0.9);
            border: 1px solid #333;
            box-shadow: 0 0 15px rgba(0, 0, 0, 0.8);
            border-radius: 4px;
        }

        .wow-btn {
            background: linear-gradient(180deg, #5c1818 0%, #360a0a 100%);
            border: 1px solid #7a5050;
            color: #ffcccc;
            transition: all 0.2s;
            text-transform: uppercase;
            letter-spacing: 1px;
            font-weight: bold;
        }

        .wow-btn:hover {
            background: linear-gradient(180deg, #7a2020 0%, #4a0e0e 100%);
            border-color: #ff9999;
            box-shadow: 0 0 10px rgba(255, 50, 50, 0.3);
        }

        .wow-btn:active {
            transform: translateY(1px);
        }

        .wow-btn-primary {
            background: linear-gradient(180deg, #18385c 0%, #0a1a36 100%);
            border: 1px solid #4a6fa5;
            color: #d0e0ff;
        }

        .wow-btn-primary:hover {
            background: linear-gradient(180deg, #204a7a 0%, #0e204a 100%);
            border-color: #99bbff;
            box-shadow: 0 0 10px rgba(50, 100, 255, 0.3);
        }

        .wow-btn-success {
            background: linear-gradient(180deg, #1f5c18 0%, #0f360a 100%);
            border: 1px solid #507a4a;
            color: #d0ffd0;
        }

        .wow-btn-success:hover {
            background: linear-gradient(180deg, #297a20 0%, #144a0e 100%);
            border-color: #99ff99;
            box-shadow: 0 0 10px rgba(50, 255, 50, 0.3);
        }

        .wow-input {
            background: #111;
            border: 1px solid #444;
            color: #ddd;
        }
        
        .wow-input:focus {
            border-color: #f8b700;
            outline: none;
            box-shadow: 0 0 5px rgba(248, 183, 0, 0.3);
        }

        .status-dot {
            height: 12px;
            width: 12px;
            border-radius: 50%;
            display: inline-block;
            margin-right: 8px;
        }

        .status-running {
            background-color: #4ade80;
            box-shadow: 0 0 8px #4ade80;
        }

        .status-stopped {
            background-color: #ef4444;
            box-shadow: 0 0 8px #ef4444;
        }
        
        /* Custom Scrollbar */
        ::-webkit-scrollbar {
            width: 8px;
        }
        ::-webkit-scrollbar-track {
            background: #111; 
        }
        ::-webkit-scrollbar-thumb {
            background: #444; 
            border-radius: 4px;
        }
        ::-webkit-scrollbar-thumb:hover {
            background: #555; 
        }
    </style>
</head>
<body class="min-h-screen p-4 md:p-8">

    <div class="max-w-4xl mx-auto">
        <header class="mb-8 text-center border-b border-gray-800 pb-6">
            <h1 class="text-4xl md:text-5xl font-bold mb-2">Shadow Bot <span class="text-gray-500 text-2xl">Controller</span></h1>
            <div id="connection-status" class="text-red-500 text-sm font-mono tracking-wider">DISCONNECTED</div>
        </header>

        <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
            
            <div class="panel p-6 flex flex-col gap-6">
                <div class="flex justify-between items-center border-b border-gray-800 pb-4">
                    <h2 class="text-xl">Bot State</h2>
                    <div class="flex items-center bg-black px-4 py-2 rounded border border-gray-800">
                        <span id="bot-state-indicator" class="status-dot status-stopped"></span>
                        <span id="bot-state-text" class="font-mono text-gray-300">UNKNOWN</span>
                    </div>
                </div>

                <div class="grid grid-cols-2 gap-4">
                    <button onclick="sendCommand('start')" class="wow-btn wow-btn-success py-4 rounded text-lg font-bold">
                        Start Bot
                    </button>
                    <button onclick="sendCommand('stop')" class="wow-btn py-4 rounded text-lg font-bold">
                        Stop Bot
                    </button>
                </div>

                <div class="mt-2">
                    <h3 class="text-lg mb-3 border-b border-gray-800 pb-2">Profile Management</h3>
                    <div class="flex gap-2">
                        <input type="text" id="profile-input" placeholder="Enter profile name (e.g. grinding_mulgore)" class="wow-input w-full px-4 py-2 rounded">
                        <button onclick="loadProfile()" class="wow-btn wow-btn-primary px-6 py-2 rounded whitespace-nowrap">
                            Load
                        </button>
                    </div>
                    <p class="text-xs text-gray-500 mt-2 italic">Ensure the profile file exists in your bot's profile directory.</p>
                </div>
            </div>

            <div class="panel p-6">
                <h2 class="text-xl mb-4 border-b border-gray-800 pb-2">Active Statistics</h2>
                <div class="space-y-4 font-mono text-sm">
                    <div class="flex justify-between">
                        <span class="text-gray-400">Current Profile:</span>
                        <span id="stat-profile" class="text-yellow-500">None</span>
                    </div>
                    <div class="flex justify-between">
                        <span class="text-gray-400">Uptime:</span>
                        <span id="stat-uptime" class="text-blue-400">00:00:00</span>
                    </div>
                    <div class="flex justify-between">
                        <span class="text-gray-400">XP / Hour:</span>
                        <span id="stat-xp" class="text-green-400">0</span>
                    </div>
                    <div class="flex justify-between">
                        <span class="text-gray-400">Target:</span>
                        <span id="stat-target" class="text-red-400">None</span>
                    </div>
                    <div class="flex justify-between">
                        <span class="text-gray-400">State:</span>
                        <span id="stat-action" class="text-gray-300">Idle</span>
                    </div>
                </div>
            </div>
        </div>

        <div class="panel mt-6 p-4">
            <h2 class="text-xl mb-2">System Logs</h2>
            <div id="log-container" class="bg-black border border-gray-800 rounded h-64 overflow-y-auto p-4 font-mono text-xs md:text-sm text-gray-400">
                <div class="mb-1"><span class="text-gray-600">[SYSTEM]</span> Waiting for bot connection...</div>
            </div>
            <div class="flex justify-end mt-2">
                <button onclick="clearLogs()" class="text-xs text-gray-500 hover:text-white underline">Clear Logs</button>
            </div>
        </div>
    </div>

    <div id="config-url" style="display:none;">http://localhost:8080</div>

    <script>
        // Configuration
        const API_BASE = 'http://localhost:8080'; // Change this if your C++ server runs on a different port
        
        let isConnected = false;

        // --- Core Functions ---

        async function sendCommand(action) {
            log(`Sending command: ${action}...`);
            try {
                const response = await fetch(`${API_BASE}/api/${action}`, {
                    method: 'POST'
                });
                
                if (response.ok) {
                    log(`Success: ${action} executed.`);
                    fetchStatus(); // Update status immediately
                } else {
                    log(`Error: Server returned ${response.status}`);
                }
            } catch (error) {
                log(`Failed to send command: ${error.message}`);
            }
        }

        async function loadProfile() {
            const profileName = document.getElementById('profile-input').value;
            if (!profileName) {
                alert("Please enter a profile name");
                return;
            }

            log(`Loading profile: ${profileName}...`);
            try {
                const response = await fetch(`${API_BASE}/api/load_profile`, {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json'
                    },
                    body: JSON.stringify({ profile: profileName })
                });

                if (response.ok) {
                    log("Profile loaded successfully.");
                } else {
                    log("Failed to load profile.");
                }
            } catch (error) {
                log(`Network error: ${error.message}`);
            }
        }

        // --- Polling & Updates ---

        async function fetchStatus() {
            try {
                // We assume the C++ server exposes /api/status
                const response = await fetch(`${API_BASE}/api/status`);
                if (response.ok) {
                    const data = await response.json();
                    updateUI(data);
                    
                    if (!isConnected) {
                        isConnected = true;
                        document.getElementById('connection-status').innerText = "CONNECTED";
                        document.getElementById('connection-status').className = "text-green-500 text-sm font-mono tracking-wider";
                        log("Connected to Bot Server.");
                    }
                } else {
                    throw new Error("Server error");
                }
            } catch (error) {
                if (isConnected) {
                    isConnected = false;
                    document.getElementById('connection-status').innerText = "DISCONNECTED";
                    document.getElementById('connection-status').className = "text-red-500 text-sm font-mono tracking-wider";
                    log("Lost connection to server.");
                }
            }
        }

        function updateUI(data) {
            // Update State Indicator
            const stateText = document.getElementById('bot-state-text');
            const stateDot = document.getElementById('bot-state-indicator');
            
            stateText.innerText = data.running ? "RUNNING" : "STOPPED";
            
            if (data.running) {
                stateDot.className = "status-dot status-running";
            } else {
                stateDot.className = "status-dot status-stopped";
            }

            // Update Stats
            setText('stat-profile', data.profile || "None");
            setText('stat-uptime', data.uptime || "00:00:00");
            setText('stat-xp', data.xp_per_hour || "0");
            setText('stat-target', data.current_target || "None");
            setText('stat-action', data.action_state || "Idle");
        }

        function setText(id, text) {
            const el = document.getElementById(id);
            if(el) el.innerText = text;
        }

        // --- Logging System ---

        function log(message) {
            const container = document.getElementById('log-container');
            const entry = document.createElement('div');
            entry.className = "mb-1 border-b border-gray-900 pb-1";
            
            const time = new Date().toLocaleTimeString();
            entry.innerHTML = `<span class="text-gray-600">[${time}]</span> ${message}`;
            
            container.appendChild(entry);
            container.scrollTop = container.scrollHeight;
        }

        function clearLogs() {
            document.getElementById('log-container').innerHTML = '';
        }

        // --- Initialization ---

        // Poll status every 1 second
        setInterval(fetchStatus, 1000);
        
        // Initial check
        fetchStatus();

    </script>
</body>
</html>
    )HTML";
}

// Preserve existing BuildJSON for /data endpoint
std::string WebServer::BuildJSON() {
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