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
#include <fstream> // Required for std::ofstream

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

// We do NOT include Database.h, MemoryRead.h, or dllmain.h to avoid Linker Errors.

// Global Game State Pointer (Extern)
// This is usually declared in WorldState.h or MovementController.h
// If WorldState.h declares it, this is redundant but harmless. 
// If not, this is required.
extern WorldState* g_GameState;

std::atomic<bool> WebServer::running = false;
std::thread WebServer::serverThread;

void WebServer::Start(int port) {
    if (running) return;
    running = true;
    serverThread = std::thread(ServerThread, port);
}

void WebServer::Stop() {
    running = false;
    if (serverThread.joinable()) serverThread.detach();
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

void WebServer::HandleClient(unsigned __int64 clientSocketRaw) {
    SOCKET clientSocket = (SOCKET)clientSocketRaw;
    char buffer[4096];
    int bytesRecv = recv(clientSocket, buffer, 4096, 0);

    if (bytesRecv > 0) {
        std::string request(buffer, bytesRecv);
        std::string response;

        if (request.find("GET /data ") != std::string::npos) {
            std::string json = BuildJSON();
            std::stringstream ss;
            ss << "HTTP/1.1 200 OK\r\n"
                << "Content-Type: application/json\r\n"
                << "Access-Control-Allow-Origin: *\r\n"
                << "Content-Length: " << json.length() << "\r\n"
                << "Connection: close\r\n"
                << "\r\n"
                << json;
            response = ss.str();
        }
        else {
            std::string html = GetHTML();
            std::stringstream ss;
            ss << "HTTP/1.1 200 OK\r\n"
                << "Content-Type: text/html\r\n"
                << "Content-Length: " << html.length() << "\r\n"
                << "Connection: close\r\n"
                << "\r\n"
                << html;
            response = ss.str();
        }

        send(clientSocket, response.c_str(), (int)response.length(), 0);
    }

    shutdown(clientSocket, SD_SEND);
    closesocket(clientSocket);
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
        // Access path directly. We assume path is safe or accept minor tearing for visualization.
        // pathMutex is not accessible here because we removed MovementController.h/changed header logic
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

std::string WebServer::GetHTML() {
    return R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WoW Bot Dashboard</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #1e1e1e; color: #c0c0c0; margin: 0; display: flex; height: 100vh; }
        #sidebar { width: 350px; background-color: #252526; display: flex; flex-direction: column; border-right: 1px solid #333; }
        #content { flex-grow: 1; display: flex; flex-direction: column; padding: 10px; }
        .panel { padding: 15px; border-bottom: 1px solid #333; }
        .panel h2 { margin-top: 0; color: #fff; font-size: 16px; text-transform: uppercase; letter-spacing: 1px; }
        .stat-row { display: flex; justify-content: space-between; margin-bottom: 5px; font-size: 14px; }
        .stat-label { color: #888; }
        .stat-value { color: #fff; font-weight: bold; }
        #map-container { flex-grow: 1; background-color: #000; position: relative; overflow: hidden; border: 1px solid #333; }
        canvas { display: block; width: 100%; height: 100%; }
        table { width: 100%; border-collapse: collapse; font-size: 12px; }
        th { text-align: left; background: #333; padding: 5px; color: #fff; }
        td { padding: 4px 5px; border-bottom: 1px solid #2d2d2d; }
        tr:hover { background-color: #2d2d2d; }
        .tab-buttons { display: flex; background: #2d2d2d; }
        .tab-btn { flex: 1; padding: 10px; background: none; border: none; color: #888; cursor: pointer; }
        .tab-btn.active { background: #3e3e42; color: #fff; }
        .list-container { overflow-y: auto; flex-grow: 1; }
        .hp-bar-bg { width: 100%; height: 6px; background: #444; margin-top: 10px; }
        .hp-bar-fill { height: 100%; background: #4caf50; width: 0%; transition: width 0.3s; }
    </style>
</head>
<body>
<div id="sidebar">
    <div class="panel">
        <h2>Player Status</h2>
        <div class="stat-row"><span class="stat-label">Health</span><span class="stat-value" id="p-hp">0 / 0</span></div>
        <div class="hp-bar-bg"><div class="hp-bar-fill" id="p-hp-bar"></div></div>
        <br>
        <div class="stat-row"><span class="stat-label">Position</span><span class="stat-value" id="p-pos">0, 0, 0</span></div>
        <div class="stat-row"><span class="stat-label">State</span><span class="stat-value" id="p-state">Idle</span></div>
    </div>
    <div class="tab-buttons">
        <button class="tab-btn active" onclick="showTab('enemies')">Enemies</button>
        <button class="tab-btn" onclick="showTab('objects')">Objects</button>
    </div>
    <div class="list-container">
        <div id="tab-enemies">
            <table id="enemy-table"><thead><tr><th>Name</th><th>HP</th><th>Dist</th></tr></thead><tbody></tbody></table>
        </div>
        <div id="tab-objects" style="display:none">
            <table id="object-table"><thead><tr><th>Name</th><th>Dist</th></tr></thead><tbody></tbody></table>
        </div>
    </div>
</div>
<div id="content">
    <div id="map-container">
        <canvas id="mapCanvas"></canvas>
    </div>
</div>
<script>
    const canvas = document.getElementById('mapCanvas');
    const ctx = canvas.getContext('2d');
    let gameData = null;
    function resize() {
        canvas.width = canvas.parentElement.clientWidth;
        canvas.height = canvas.parentElement.clientHeight;
    }
    window.addEventListener('resize', resize);
    resize();
    function showTab(name) {
        document.getElementById('tab-enemies').style.display = name === 'enemies' ? 'block' : 'none';
        document.getElementById('tab-objects').style.display = name === 'objects' ? 'block' : 'none';
        document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
        event.target.classList.add('active');
    }
    async function update() {
        try {
            const res = await fetch('/data');
            gameData = await res.json();
            render();
            updateSidebar();
        } catch (e) { console.error(e); }
    }
    function updateSidebar() {
        if (!gameData || !gameData.player) return;
        const p = gameData.player;
        document.getElementById('p-hp').innerText = `${p.health} / ${p.maxHealth}`;
        const hpPct = p.maxHealth > 0 ? (p.health / p.maxHealth) * 100 : 0;
        document.getElementById('p-hp-bar').style.width = hpPct + '%';
        document.getElementById('p-pos').innerText = `${p.x.toFixed(1)}, ${p.y.toFixed(1)}, ${p.z.toFixed(1)}`;
        document.getElementById('p-state').innerText = p.state;
        const eBody = document.querySelector('#enemy-table tbody');
        const oBody = document.querySelector('#object-table tbody');
        eBody.innerHTML = '';
        oBody.innerHTML = '';
        gameData.entities.forEach(e => {
            const row = document.createElement('tr');
            if (e.type === 'Enemy') {
                row.innerHTML = `<td>${e.name}</td><td>${e.hp}</td><td>${e.dist.toFixed(1)}</td>`;
                eBody.appendChild(row);
            } else if (e.type === 'Object') {
                row.innerHTML = `<td>${e.name}</td><td>${e.dist.toFixed(1)}</td>`;
                oBody.appendChild(row);
            }
        });
    }
    function render() {
        if (!gameData || !gameData.player) return;
        ctx.fillStyle = '#000';
        ctx.fillRect(0, 0, canvas.width, canvas.height);
        const scale = 3.0; 
        const cx = canvas.width / 2;
        const cy = canvas.height / 2;
        const px = gameData.player.x;
        const py = gameData.player.y;
        function toScreen(gx, gy) {
            return {
                x: cx + (py - gy) * scale, 
                y: cy - (px - gx) * scale
            };
        }
        if (gameData.path && gameData.path.length > 0) {
            ctx.beginPath();
            ctx.strokeStyle = '#00ff00';
            ctx.lineWidth = 2;
            gameData.path.forEach((node, i) => {
                const pos = toScreen(node.x, node.y);
                if (i === 0) ctx.moveTo(pos.x, pos.y);
                else ctx.lineTo(pos.x, pos.y);
            });
            ctx.stroke();
        }
        ctx.beginPath();
        ctx.fillStyle = '#0088ff';
        ctx.arc(cx, cy, 6, 0, Math.PI * 2);
        ctx.fill();
        const rot = gameData.player.rotation; 
        ctx.beginPath();
        ctx.strokeStyle = '#fff';
        ctx.moveTo(cx, cy);
        ctx.lineTo(cx + Math.cos(rot + Math.PI/2) * 20, cy + Math.sin(rot + Math.PI/2) * -20);
        ctx.stroke();
        gameData.entities.forEach(e => {
            const pos = toScreen(e.x, e.y);
            if (pos.x < 0 || pos.x > canvas.width || pos.y < 0 || pos.y > canvas.height) return;
            ctx.beginPath();
            if (e.type === 'Enemy') ctx.fillStyle = '#ff4444';
            else if (e.type === 'Object') ctx.fillStyle = '#ffeb3b';
            else ctx.fillStyle = '#aaa';
            ctx.arc(pos.x, pos.y, 4, 0, Math.PI * 2);
            ctx.fill();
        });
    }
    setInterval(update, 500);
</script>
</body>
</html>
    )HTML";
}