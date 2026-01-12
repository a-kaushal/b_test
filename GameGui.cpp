#include "GameGui.h"
#include "dllmain.h"
#include <commctrl.h> // Common Controls (ListView)
#include <cstdio>
#include <string>
#include <atomic> // REQUIRED for flag
#include <thread> // REQUIRED for sleep
#include <chrono>
#include <random>
#include <algorithm>

#pragma comment(lib, "Comctl32.lib")

// --- Global Data ---
std::mutex guiMutex;
std::vector<GameEntity> globalEntities;
HWND hListView = NULL;

// --- Helper Functions ---
void AddColumn(HWND hList, int columnIndex, const char* title, int width) {
    LVCOLUMNA lvCol = { 0 };
    lvCol.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvCol.fmt = LVCFMT_LEFT;
    lvCol.cx = width;
    lvCol.pszText = (LPSTR)title;
    SendMessageA(hList, (0x1000 + 27), (WPARAM)columnIndex, (LPARAM)&lvCol);
}

// Helper to SET text on an existing item (No insertion)
void SetItemText(HWND hList, int row, int col, const std::string& text) {
    LVITEMA lvItem = { 0 };
    lvItem.iItem = row;
    lvItem.iSubItem = col;
    lvItem.pszText = (LPSTR)text.c_str();
    lvItem.mask = LVIF_TEXT;
    SendMessageA(hList, (0x1000 + 6), 0, (LPARAM)&lvItem); // LVM_SETITEMA
}

// Helper to INSERT a new item
void InsertNewItem(HWND hList, int row, const std::string& text) {
    LVITEMA lvItem = { 0 };
    lvItem.iItem = row;
    lvItem.iSubItem = 0;
    lvItem.pszText = (LPSTR)text.c_str();
    lvItem.mask = LVIF_TEXT;
    SendMessageA(hList, (0x1000 + 7), 0, (LPARAM)&lvItem); // LVM_INSERTITEMA
}

// --- Window Procedure ---
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
    {
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        hListView = CreateWindowExA(0, "SysListView32", "",
            WS_CHILD | WS_VISIBLE | LVS_REPORT,
            0, 0, rcClient.right, rcClient.bottom,
            hwnd, NULL, GetModuleHandleA(NULL), NULL);

        // Enable Double Buffering to stop flashing
        ListView_SetExtendedListViewStyle(hListView, 0x00000020 | 0x00000001 | 0x00010000);

        AddColumn(hListView, 0, "Index", 60);
        AddColumn(hListView, 1, "Type", 100);
        AddColumn(hListView, 2, "ID", 100);
        AddColumn(hListView, 3, "Name", 100);
        AddColumn(hListView, 4, "Entity Pointer", 100);
        AddColumn(hListView, 5, "Reaction", 100);
        AddColumn(hListView, 6, "Type ID", 100);
        AddColumn(hListView, 7, "Distance", 100);
        AddColumn(hListView, 8, "Agro Range", 100);

        SetTimer(hwnd, 1, 250, NULL);
    }
    break;

    case WM_SIZE:
        if (hListView) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            MoveWindow(hListView, 0, 0, rc.right, rc.bottom, TRUE);
        }
        break;

    case WM_TIMER:
    {
        // Check if we should stop (Safety check)
        if (!g_IsRunning) break;

        std::lock_guard<std::mutex> lock(guiMutex);

        // Get how many items are currently in the list
        int existingCount = (int)SendMessageA(hListView, (0x1000 + 4), 0, 0); // LVM_GETITEMCOUNT
        int newCount = (int)globalEntities.size();

        // Limit for performance
        if (newCount > 200) newCount = 200;

        char buffer[128];

        for (int i = 0; i < newCount; i++) {
            const auto& ent = globalEntities[i];

            bool isUpdate = (i < existingCount);

            // 1. INDEX (Column 0)
            sprintf_s(buffer, "%d", ent.entityIndex);
            if (isUpdate) SetItemText(hListView, i, 0, buffer);
            else InsertNewItem(hListView, i, buffer);

            // 2. TYPE (Column 1)
            sprintf_s(buffer, "%s", ent.objType.c_str());
            SetItemText(hListView, i, 1, buffer);

            // 3. ID (Column 2)
            sprintf_s(buffer, "%d", ent.id);
            SetItemText(hListView, i, 2, buffer);

            // 4. NAME (Column 3)
            SetItemText(hListView, i, 3, ent.name_safe);

            // 5. POINTER
            sprintf_s(buffer, "%02X", ent.entityPtr);
            SetItemText(hListView, i, 4, buffer);

            // 6. REACTION
            SetItemText(hListView, i, 5, ent.reaction_safe);

            // 7. TYPE ID
            sprintf_s(buffer, "%d", ent.type);
            SetItemText(hListView, i, 6, buffer);

            // 8. DISTANCE (CRITICAL FIX: Use dist_safe)
            sprintf_s(buffer, "%.2f", ent.dist_safe);
            SetItemText(hListView, i, 7, buffer);

            // 9. NODE ACTIVE/AGGRO (Use nodeActive_safe)
            sprintf_s(buffer, "%d", ent.nodeActive_safe);
            SetItemText(hListView, i, 8, buffer);
        }

        // If the new list is smaller than the old list, delete the extra rows
        if (existingCount > newCount) {
            for (int i = existingCount - 1; i >= newCount; i--) {
                SendMessageA(hListView, (0x1000 + 8), (WPARAM)i, 0); // LVM_DELETEITEM
            }
        }
    }
    break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        // CHANGED: Must call PostQuitMessage so the loop knows to exit
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

// --- Thread Entry ---
void StartGuiThread(HMODULE hDllInst) {
    // 1. GENERATE A UNIQUE CLASS NAME
    // This prevents collisions with previous injections or Explorer's own windows.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    // Create a name like "MyCheatGUI_4821"
    std::string classNameStr = "CabinetWClass" + std::to_string(dis(gen));
    const char* className = classNameStr.c_str();

    // 2. REGISTER CLASS
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hDllInst;
    wc.lpszClassName = className; // Use the unique name
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    HMODULE hShell32 = LoadLibraryA("shell32.dll");
    if (hShell32) wc.hIcon = LoadIconA(hShell32, (LPCSTR)4);
    else wc.hIcon = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);

    if (!RegisterClassA(&wc)) {
        // If this fails, we can't create the window.
        // Since the name is random, this should rarely fail.
        MessageBoxA(NULL, "Failed to register GUI Class.", "Error", MB_ICONERROR);
        return;
    }

    // 3. CREATE WINDOW
    HWND hwnd = CreateWindowExA(0, className, "File Explorer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 650, 450,
        NULL, NULL, hDllInst, NULL);

    if (hwnd == NULL) {
        UnregisterClassA(className, hDllInst);
        return;
    }

    ShowWindow(hwnd, SW_SHOW);

    // 4. MESSAGE LOOP
    MSG msg = { 0 };
    while (g_IsRunning) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_IsRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 5. CLEANUP SEQUENCE (CRITICAL)

    // A. Kill the timer we set in WM_CREATE
    KillTimer(hwnd, 1);

    // B. Destroy the window
    if (IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }

    // C. PUMP MESSAGES until the window is truly gone.
    // DestroyWindow() sends messages (WM_DESTROY, WM_NCDESTROY) to the queue.
    // We must process them, or the window handle remains "active" and prevents UnregisterClass.
    MSG cleanupMsg;
    while (PeekMessageA(&cleanupMsg, NULL, 0, 0, PM_REMOVE)) {
        DispatchMessageA(&cleanupMsg);
    }

    // D. Unregister the class
    // Since we used a unique name, this is clean.
    UnregisterClassA(className, hDllInst);
}

// CRITICAL FIX: Populate safe cache here (Main Thread)
void UpdateGuiData(const std::vector<GameEntity>& newData) {
    std::lock_guard<std::mutex> lock(guiMutex);

    // We rebuild the list to ensure the cache is fresh
    globalEntities.clear();
    globalEntities.reserve(newData.size());

    for (const auto& rawEnt : newData) {
        GameEntity safeEnt = rawEnt; // Copy basic fields

        // DEFAULT VALUES
        safeEnt.name_safe = "N/A";
        safeEnt.reaction_safe = "N/A";
        safeEnt.dist_safe = 0.0f;
        safeEnt.nodeActive_safe = 0;

        // RESOLVE POINTERS SAFELY HERE (Main thread owns the memory)
        if (rawEnt.info) {
            if (auto enemy = std::dynamic_pointer_cast<EnemyInfo>(rawEnt.info)) {
                safeEnt.name_safe = enemy->name; // Copy string
                safeEnt.dist_safe = enemy->distance;
                if (enemy->reaction == 0) safeEnt.reaction_safe = "Enemy";
                else if (enemy->reaction == 1) safeEnt.reaction_safe = "Neutral";
                else safeEnt.reaction_safe = "Friendly";
            }
            else if (auto object = std::dynamic_pointer_cast<ObjectInfo>(rawEnt.info)) {
                safeEnt.name_safe = object->name; // Copy string
                safeEnt.dist_safe = object->distance;
                safeEnt.nodeActive_safe = object->nodeActive;
            }
        }
        globalEntities.push_back(safeEnt);
    }

    // Sort logic (optional, keep if you had it)
    SortEntitiesByDistance(globalEntities); 
}