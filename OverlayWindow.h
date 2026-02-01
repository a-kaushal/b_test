#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <random>

#include "WebServer.h" 
extern std::atomic<bool> g_IsPaused;

class OverlayWindow {
private:
    struct OverlayPoint {
        int x, y;
        COLORREF color;
    };

    // Shared Data
    std::vector<OverlayPoint> bufferPoints; // Points waiting to be drawn
    std::vector<OverlayPoint> renderPoints; // Points currently being drawn
    std::mutex m_mutex;
    std::atomic<bool> m_running = false;
    std::thread m_thread;

    HWND hOverlay = NULL;
    HWND hGame = NULL;
    int width = 0, height = 0;
    std::string className;
    std::string windowTitle;

    // Helper: Random String Generator
    std::string RandomString(size_t length) {
        const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        std::string str(length, 0);
        for (size_t i = 0; i < length; ++i) str[i] = charset[rand() % max_index];
        return str;
    }

    // Window Procedure (Standard)
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_ERASEBKGND:
            return 1; // Prevent flickering
        case WM_CLOSE:
            return 0;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    // --- WORKER THREAD ---
    // This runs in the background and keeps the window alive.
    void OverlayThread() {
        // 1. Register Class
        srand(GetTickCount());
        className = RandomString(10 + (rand() % 10));
        windowTitle = RandomString(10 + (rand() % 10));

        WNDCLASSEXA wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = className.c_str();
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
        RegisterClassExA(&wc);

        // 2. Create Window
        hOverlay = CreateWindowExA(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            className.c_str(), windowTitle.c_str(),
            WS_POPUP | WS_VISIBLE,
            0, 0, 100, 100,
            NULL, NULL, GetModuleHandle(NULL), NULL
        );

        if (!hOverlay) return;

        SetLayeredWindowAttributes(hOverlay, RGB(0, 0, 0), 0, LWA_COLORKEY);

        // 3. Render Loop
        while (m_running) {
            // A. Process Windows Messages (Keeps window responsive)
            MSG msg;
            while (PeekMessageA(&msg, hOverlay, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }

            // B. Sync Position with Game
            if (hGame) {
                RECT rect;
                if (GetClientRect(hGame, &rect)) {
                    POINT pt = { 0, 0 };
                    ClientToScreen(hGame, &pt);
                    width = rect.right - rect.left;
                    height = rect.bottom - rect.top;

                    // Only move if changed (Performance optimization)
                    static int lastX = 0, lastY = 0, lastW = 0, lastH = 0;
                    if (pt.x != lastX || pt.y != lastY || width != lastW || height != lastH) {
                        MoveWindow(hOverlay, pt.x, pt.y, width, height, TRUE);
                        lastX = pt.x; lastY = pt.y; lastW = width; lastH = height;
                    }
                }
            }

            // C. Copy Data for Rendering (Thread Safe)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                renderPoints = bufferPoints;
            }

            // D. Render
            RenderFrame();

            // E. Frame Cap (~60 FPS)
            Sleep(16);
        }

        // Cleanup
        DestroyWindow(hOverlay);
        UnregisterClassA(className.c_str(), GetModuleHandle(NULL));
    }

    void RenderFrame() {
        if (!hOverlay || width <= 0 || height <= 0) return;

        HDC hdc = GetDC(hOverlay);
        if (hdc) {
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

            // Clear Background
            HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
            RECT r = { 0, 0, width, height };
            FillRect(memDC, &r, blackBrush);
            DeleteObject(blackBrush);

            // Draw Points
            for (const auto& pt : renderPoints) {
                if (pt.x >= 0 && pt.x < width && pt.y >= 0 && pt.y < height) {
                    HBRUSH targetBrush = CreateSolidBrush(pt.color);
                    RECT dotRect = { pt.x - 4, pt.y - 4, pt.x + 4, pt.y + 4 };
                    FillRect(memDC, &dotRect, targetBrush);
                    DeleteObject(targetBrush);
                }
            }

            // DRAW STATUS INDICATOR (Top-Left)
            COLORREF statusColor;
            if (g_IsPaused) {
                statusColor = RGB(255, 255, 0); // Yellow (Paused)
            }
            else if (!WebServer::IsBotActive()) {
                statusColor = RGB(255, 0, 0);   // Red (Stopped/Inactive)
            }
            else {
                statusColor = RGB(0, 255, 0);   // Green (Running)
            }

            HBRUSH statusBrush = CreateSolidBrush(statusColor);
            // Draw a 10x10 square at (10,10)
            RECT statusRect = { 10, 10, 20, 20 };
            FillRect(memDC, &statusRect, statusBrush);

            // Optional: Border for visibility
            FrameRect(memDC, &statusRect, (HBRUSH)GetStockObject(WHITE_BRUSH));

            DeleteObject(statusBrush);

            // Blit
            BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
            ReleaseDC(hOverlay, hdc);
        }
    }

public:
    OverlayWindow() {}
    ~OverlayWindow() {
        m_running = false;
        if (m_thread.joinable()) m_thread.join();
    }

    bool Setup(HWND hGameWindow) {
        if (m_running) return true;
        hGame = hGameWindow;
        m_running = true;
        // Start the background thread
        m_thread = std::thread(&OverlayWindow::OverlayThread, this);
        return true;
    }

    // This is now purely for compatibility - the thread handles messages automatically
    void ProcessMessages() {
        // Do nothing! The thread handles it.
    }

    // Called by Logic Thread
    void DrawFrame(int x, int y, COLORREF color, bool accumulate = false) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!accumulate) {
            bufferPoints.clear();
        }
        // Only add valid points to keep buffer small
        if (x != -100) {
            bufferPoints.push_back({ x, y, color });
        }
    }

    // Manual UpdatePosition is no longer needed (Thread handles it), but kept for API compatibility
    void UpdatePosition() {}
};