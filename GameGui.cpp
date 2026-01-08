#include "GameGui.h"
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

// LINK TO MAIN: Access the global flag defined in your main.cpp
extern std::atomic<bool> g_IsRunning;

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

            // 4. MAP ID (Column 3)
            if (ent.info) {
                if (auto enemy = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                    std::snprintf(buffer, sizeof(buffer), "%s", enemy->name.c_str());
                    SetItemText(hListView, i, 3, buffer);
                }
                else if (auto object = std::dynamic_pointer_cast<ObjectInfo>(ent.info)) {
                    std::snprintf(buffer, sizeof(buffer), "%s", object->name.c_str());
                    SetItemText(hListView, i, 3, buffer);
                }
                else {
                    SetItemText(hListView, i, 3, "N/A");
                }
            }
            else {
                SetItemText(hListView, i, 3, "N/A");
            }

            // 4. Entity Pointer (Column 4)
            sprintf_s(buffer, "%02X", ent.entityPtr);
            SetItemText(hListView, i, 4, buffer);

            // 4. Entity Pointer (Column 4)
            if (ent.info) {
                if (auto enemy = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                    if (enemy->reaction == 0) {
                        sprintf_s(buffer, sizeof(buffer), "Enemy");
                    }
                    if (enemy->reaction == 1) {
                        sprintf_s(buffer, sizeof(buffer), "Neutral");
                    }
                    if (enemy->reaction == 2) {
                        sprintf_s(buffer, sizeof(buffer), "Friendly");
                    }
                    SetItemText(hListView, i, 5, buffer);
                }
                else {
                    SetItemText(hListView, i, 5, "N/A");
                }
            }
            else {
                SetItemText(hListView, i, 5, "N/A");
            }

            // 6. TYPE ID (Column 6)
            sprintf_s(buffer, "%d", ent.type);
            SetItemText(hListView, i, 6, buffer);

            // 7. DISTANCE TO PLAYER (Column 7)
            if (ent.info) {
                if (auto enemy = std::dynamic_pointer_cast<EnemyInfo>(ent.info)) {
                    sprintf_s(buffer, sizeof(buffer), "%.2f", enemy->distance);
                    SetItemText(hListView, i, 7, buffer);
                }
                else if (auto object = std::dynamic_pointer_cast<ObjectInfo>(ent.info)) {
                    sprintf_s(buffer, sizeof(buffer), "%.2f", object->distance);
                    SetItemText(hListView, i, 7, buffer);
                }
                else {
                    SetItemText(hListView, i, 7, "N/A");
                }
            }
            else {
                SetItemText(hListView, i, 7, "N/A");
            }

            // 8. AGRO RANGE (Column 8)
            if (ent.info) {
                if (auto object = std::dynamic_pointer_cast<ObjectInfo>(ent.info)) {
                    sprintf_s(buffer, sizeof(buffer), "%d", object->nodeActive);
                    SetItemText(hListView, i, 8, buffer);
                }
                else {
                    SetItemText(hListView, i, 8, "N/A");
                }
            }
            else {
                SetItemText(hListView, i, 8, "N/A");
            }
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

void UpdateGuiData(const std::vector<GameEntity>& newData) {
    std::lock_guard<std::mutex> lock(guiMutex);

    // Copy the new data into the global list
    globalEntities = newData;

    SortEntitiesByDistance(globalEntities);
}