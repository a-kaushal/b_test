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
#include <mutex>
#include <algorithm> // Required for std::min

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

// [FIX] Now a member function: WebServer::ReadLogFileTail
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

std::string WebServer::GenerateJSONState() {
    if (!g_GameState) return "{}";

    // Lock to prevent reading while GameLoop is writing
    std::lock_guard<std::mutex> lock(g_EntityMutex);

    std::stringstream ss;
    ss << "{";

    // --- 1. BOT STATUS ---
    // We can access private members directly now
    ss << "\"running\": " << (botActive ? "true" : "false") << ",";

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
    ss << "\"target\": \"0x" << std::hex << p.inCombatGuidLow << std::dec << "\",";
    ss << "\"selected\": \"0x" << std::hex << p.targetGuidLow << std::dec << "\"";
    ss << "},";

    // --- 3. PATH ---
    ss << "\"path\": [";
    for (size_t i = 0; i < g_GameState->globalState.activePath.size(); ++i) {
        auto& node = g_GameState->globalState.activePath[i];
        ss << "{\"x\":" << node.pos.x << ",\"y\":" << node.pos.y << "}";
        if (i < g_GameState->globalState.activePath.size() - 1) ss << ",";
    }
    ss << "],";

    // --- 4. ENTITIES ---
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
    ss << "]"; // End Entities

    ss << "}"; // End Root
    return ss.str();
}

// =============================================================
// SERVER CLASS MEMBERS
// =============================================================

void WebServer::Start(int port) {
    if (running) return;
    running = true;
    startTime = GetTickCount64();
    serverThread = std::thread(ServerThread, port);
    serverThread.detach();
}

void WebServer::Stop() {
    running = false;
}

// Getters/Setters for Bot State
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

void WebServer::HandleClient(unsigned __int64 clientSocketRaw) {
    SOCKET clientSocket = (SOCKET)clientSocketRaw;

    const int BUFFER_SIZE = 4096; // 4KB chunks are sufficient for reading
    std::vector<char> buffer(BUFFER_SIZE);
    std::string requestData;

    // 1. Read Headers
    int bytesRecv;
    size_t headerEndPos = std::string::npos;

    while ((bytesRecv = recv(clientSocket, buffer.data(), BUFFER_SIZE, 0)) > 0) {
        requestData.append(buffer.data(), bytesRecv);
        headerEndPos = requestData.find("\r\n\r\n");
        if (headerEndPos != std::string::npos) {
            break; // Headers fully received
        }
    }

    if (headerEndPos == std::string::npos) {
        // Malformed request or connection closed early
        closesocket(clientSocket);
        return;
    }

    // 2. Parse Content-Length to handle the Body
    size_t contentLength = 0;
    size_t clPos = requestData.find("Content-Length: ");
    if (clPos != std::string::npos) {
        size_t endOfLine = requestData.find("\r\n", clPos);
        std::string val = requestData.substr(clPos + 16, endOfLine - (clPos + 16));
        try { contentLength = std::stoull(val); }
        catch (...) { contentLength = 0; }
    }

    // 3. Read Remaining Body (if necessary)
    size_t bodyStart = headerEndPos + 4;
    size_t currentBodySize = requestData.size() - bodyStart;

    while (currentBodySize < contentLength) {
        bytesRecv = recv(clientSocket, buffer.data(), BUFFER_SIZE, 0);
        if (bytesRecv <= 0) break; // Error or closed
        requestData.append(buffer.data(), bytesRecv);
        currentBodySize += bytesRecv;
    }

    std::string responseBody;
    std::string contentType = "text/html";
    int statusCode = 200;

    // --- ROUTING LOGIC ---

    // 1. POST /api/start
    if (requestData.find("POST /api/start ") != std::string::npos) {
        botActive = true;
        responseBody = "{\"status\":\"ok\"}";
        contentType = "application/json";
    }
    // 2. POST /api/stop
    else if (requestData.find("POST /api/stop ") != std::string::npos) {
        botActive = false;
        responseBody = "{\"status\":\"ok\"}";
        contentType = "application/json";
    }
    // 3. POST /api/upload_profile
    else if (requestData.find("POST /api/upload_profile ") != std::string::npos) {
        size_t bodyPos = requestData.find("\r\n\r\n");
        if (bodyPos != std::string::npos) {
            std::string body = requestData.substr(bodyPos + 4);
            std::string error;
            if (g_ProfileLoader.CompileAndLoad(body, error)) {
                responseBody = "{\"status\":\"success\", \"message\":\"Profile Loaded!\"}";
                currentProfile = "Uploaded Profile";
            }
            else {
                responseBody = "{\"status\":\"error\", \"message\":\"" + EscapeJSON(error) + "\"}";
            }
            contentType = "application/json";
        }
    }
    // NEW ENDPOINT: POST /api/load_last
    else if (requestData.find("POST /api/load_last ") != std::string::npos) {
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
    // 4. GET /api/state (Complete State for Dashboard)
    else if (requestData.find("GET /api/state ") != std::string::npos) {
        responseBody = GenerateJSONState(); // Calls member function
        contentType = "application/json";
    }
    // 5. GET /api/logs
    else if (requestData.find("GET /api/logs ") != std::string::npos) {
        responseBody = ReadLogFileTail(50000); // Calls member function
        contentType = "text/plain";
    }
    // 6. GET / (Dashboard HTML)
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

    shutdown(clientSocket, SD_SEND);
    closesocket(clientSocket);
}

// THIS IS THE NEW HTML DASHBOARD
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
        :root { --bg: #1e1e1e; --panel: #252526; --accent: #007acc; --text: #d4d4d4; --border: #333; }
        body { background-color: var(--bg); color: var(--text); font-family: 'Segoe UI', Tahoma, sans-serif; margin: 0; display: flex; flex-direction: column; height: 100vh; overflow: hidden; }
        
        /* HEADER */
        header { background-color: #2d2d30; padding: 10px 20px; display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); }
        .title { font-weight: bold; font-size: 18px; color: #fff; }
        .controls { display: flex; gap: 10px; }
        
        /* BUTTONS */
        button { padding: 8px 16px; border: none; border-radius: 4px; font-weight: bold; cursor: pointer; color: white; transition: opacity 0.2s; }
        button:hover { opacity: 0.8; }
        .btn-start { background-color: #28a745; }
        .btn-stop { background-color: #dc3545; }
        .btn-blue { background-color: var(--accent); }
        .btn-file { background-color: #444; position: relative; overflow: hidden; }
        .btn-file input[type=file] { position: absolute; top: 0; right: 0; min-width: 100%; min-height: 100%; opacity: 0; cursor: pointer; }

        /* TABS */
        .tabs { display: flex; background: var(--panel); border-bottom: 1px solid var(--border); }
        .tab { padding: 10px 20px; cursor: pointer; background: var(--panel); color: #888; }
        .tab.active { background: var(--bg); color: #fff; border-top: 2px solid var(--accent); font-weight: bold; }
        .tab:hover { color: #fff; }

        /* CONTENT AREA */
        .content { flex: 1; position: relative; overflow: hidden; }
        .view { display: none; height: 100%; width: 100%; }
        .view.active { display: block; }

        /* CONSOLE VIEW */
        #log-container { height: 100%; overflow-y: scroll; font-family: 'Consolas', monospace; font-size: 12px; padding: 10px; white-space: pre-wrap; color: #ccc; }

        /* MAP VIEW */
        #map-wrapper { height: 100%; width: 100%; position: relative; background: #000; display: flex; justify-content: center; align-items: center; }
        canvas { display: block; }
        .legend { position: absolute; bottom: 20px; left: 20px; background: rgba(0,0,0,0.7); padding: 10px; border-radius: 4px; pointer-events: none; }
        .dot { display: inline-block; width: 10px; height: 10px; border-radius: 50%; margin-right: 5px; }

        /* TABLE VIEW */
        #table-wrapper { height: 100%; overflow: auto; padding: 20px; }
        table { width: 100%; border-collapse: collapse; font-size: 13px; }
        th { text-align: left; background: var(--panel); padding: 8px; position: sticky; top: 0; }
        /* Copyable Cells */
        td { border-bottom: 1px solid var(--border); padding: 6px; cursor: pointer; transition: background 0.1s; }
        td:hover { background: #3e3e42; }
        tr:hover { background: #2a2d2e; }

        /* FOOTER STATUS */
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
                    <div><span class="dot" style="background:#ffff00"></span> Neutral NPC</div>
                    <div><span class="dot" style="background:#00ff00"></span> Friendly NPC</div>
                    <div><span class="dot" style="background:#d02090"></span> Object</div>
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
        // --- GLOBAL STATE ---
        let gameState = {};
        const canvas = document.getElementById('mapCanvas');
        const ctx = canvas.getContext('2d');

        // --- CONTROLS ---
        async function sendCommand(cmd) {
            try { await fetch('/api/' + cmd, { method: 'POST' }); } 
            catch(e) { console.error(e); }
        }

        async function loadLastProfile() {
            const status = document.getElementById('uploadStatus');
            status.innerText = "Reloading...";
            try {
                const res = await fetch('/api/load_last', { method: 'POST' });
                const data = await res.json();
                if(data.status === 'success') {
                    status.innerText = "Reloaded!";
                    status.style.color = "lime";
                    setTimeout(() => status.innerText = "", 3000);
                } else {
                    status.innerText = "Error: " + data.message;
                    status.style.color = "red";
                }
            } catch(err) {
                status.innerText = "Request Failed";
            }
        }

        async function uploadProfile() {
            const input = document.getElementById('profileFile');
            const status = document.getElementById('uploadStatus');
            if (input.files.length === 0) return;

            const file = input.files[0];
            status.innerText = "Reading...";
            
            const reader = new FileReader();
            reader.onload = async function(e) {
                status.innerText = "Uploading...";
                try {
                    const res = await fetch('/api/upload_profile', {
                        method: 'POST',
                        body: e.target.result
                    });
                    const data = await res.json();
                    if(data.status === 'success') {
                        status.innerText = "Success!";
                        status.style.color = "lime";
                        setTimeout(() => status.innerText = "", 3000);
                    } else {
                        status.innerText = "Error: " + data.message;
                        status.style.color = "red";
                    }
                } catch(err) {
                    status.innerText = "Upload Failed";
                }
            };
            reader.readAsText(file);
        }

        // --- CLIPBOARD HELPER ---
        function copyToClipboard(text, el) {
            // Robust fallback for non-secure contexts (HTTP)
            const textArea = document.createElement("textarea");
            textArea.value = text;
            textArea.style.position = "fixed";
            document.body.appendChild(textArea);
            textArea.focus();
            textArea.select();
            try {
                document.execCommand('copy');
                // Visual feedback
                const prev = el.style.background;
                el.style.background = '#666';
                setTimeout(() => el.style.background = "", 150);
            } catch (err) {
                console.error('Copy failed', err);
            }
            document.body.removeChild(textArea);
        }

        // --- UI LOGIC ---
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

        // --- DATA POLLING ---
        async function poll() {
            try {
                // 1. Logs
                if(document.getElementById('console').classList.contains('active')) {
                    const res = await fetch('/api/logs');
                    const text = await res.text();
                    const container = document.getElementById('log-container');
                    const isAtBottom = container.scrollHeight - container.scrollTop === container.clientHeight;
                    container.innerText = text;
                    if(isAtBottom) container.scrollTop = container.scrollHeight;
                }

                // 2. State
                const res = await fetch('/api/state');
                gameState = await res.json();

                document.getElementById('stat-running').innerText = "Status: " + (gameState.running ? "RUNNING" : "STOPPED");
                document.getElementById('stat-profile').innerText = "Profile: " + (gameState.profile || "None");
                document.getElementById('stat-uptime').innerText = "Uptime: " + gameState.uptime;

                if(document.getElementById('map').classList.contains('active')) drawMap();
                if(document.getElementById('entities').classList.contains('active')) updateTable();

            } catch(e) { console.log("Poll error", e); }
        }

        // --- MAP DRAWING ---
        function drawMap() {
            if(!gameState.player) return;
            const px = gameState.player.x;
            const py = gameState.player.y;
            const scale = 4.0;
            const cx = canvas.width / 2;
            const cy = canvas.height / 2;

            ctx.fillStyle = "#000";
            ctx.fillRect(0, 0, canvas.width, canvas.height);

            const toScreen = (x, y) => ({
                x: cx + (x - px) * scale,
                y: cy - (y - py) * scale
            });

            // Path
            if(gameState.path && gameState.path.length > 0) {
                ctx.beginPath();
                ctx.strokeStyle = "lime";
                ctx.lineWidth = 2;
                gameState.path.forEach((p, i) => {
                    const s = toScreen(p.x, p.y);
                    if(i===0) ctx.moveTo(s.x, s.y); else ctx.lineTo(s.x, s.y);
                });
                ctx.stroke();
            }

            // Entities
            if(gameState.entities) {
                gameState.entities.forEach(ent => {
                    const s = toScreen(ent.x, ent.y);
                    if(s.x < -10 || s.x > canvas.width+10 || s.y < -10 || s.y > canvas.height+10) return;

                    ctx.beginPath();
                    ctx.arc(s.x, s.y, 4, 0, Math.PI*2);
                    
                    if(ent.type === 'Enemy') {
                        if (ent.reaction === 2) ctx.fillStyle = '#00ff00';      // Friendly
                        else if (ent.reaction === 1) ctx.fillStyle = '#ffff00'; // Neutral
                        else ctx.fillStyle = '#ff0000';                         // Hostile
                    }
                    else if(ent.type === 'Object') ctx.fillStyle = '#d02090';   // Purple
                    else if(ent.type === 'Player') ctx.fillStyle = 'blue';
                    else ctx.fillStyle = 'gray';
                    
                    ctx.fill();
                });
            }

            // Player Arrow
            const rot = gameState.player.rot; 
            ctx.save();
            ctx.translate(cx, cy);
            ctx.rotate(rot + Math.PI); 
            ctx.beginPath();
            ctx.moveTo(0, -8);
            ctx.lineTo(-6, 8);
            ctx.lineTo(6, 8);
            ctx.closePath();
            ctx.fillStyle = "cyan";
            ctx.fill();
            ctx.restore();
        }

        function updateTable() {
            const tbody = document.querySelector('#entTable tbody');
            const thead = document.querySelector('#tableHeader');
            const filter = document.getElementById('typeFilter').value;
            tbody.innerHTML = '';
            
            // 1. Player Table
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
                    ["Flags", `Mounted: ${p.isMounted}, Ground Mounted: ${p.groundMounted}, Flying Mounted: ${p.flyingMounted}, Flying: ${p.isFlying}, Indoor: ${p.isIndoor}`],
                    ["Target GUID", p.target],
                    ["Selected GUID", p.selected]
                ];
                rows.forEach(r => {
                    const color = (r[0] === "Need Repair" && p.needRepair) ? "color:red;font-weight:bold" : "";
                    tbody.innerHTML += `<tr><td><b>${r[0]}</b></td><td style="${color}" onclick="copyToClipboard(this.innerText, this)" title="Click to copy">${r[1]}</td></tr>`;
                });
                return;
            }

            // 2. Entity Table
            thead.innerHTML = `<th>Type</th><th>Name</th><th>Dist</th><th>Pos (X,Y,Z)</th><th>Health</th><th>Combat?</th><th>Target GUID</th>`;
            if (!gameState.entities) return;

            const filtered = gameState.entities
                .filter(e => filter === 'All' || e.type === filter)
                .sort((a, b) => a.dist - b.dist);

            filtered.forEach(ent => {
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