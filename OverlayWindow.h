#pragma once
#include <windows.h>
#include <string>
#include <random>
#include <algorithm>
#include <vector>

class OverlayWindow {
private:
    struct OverlayPoint {
        int x, y;
        COLORREF color;
    };

    HWND hOverlay = NULL;
    HWND hGame = NULL;
    int width = 0, height = 0;
    std::string className;
    std::string windowTitle;
    std::vector<OverlayPoint> activePoints; // Stores points for the current frame

    // Generate a random string of length N
    std::string RandomString(size_t length) {
        const char charset[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);

        std::string str(length, 0);
        for (size_t i = 0; i < length; ++i) {
            str[i] = charset[rand() % max_index];
        }
        return str;
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

public:
    ~OverlayWindow() {
        if (hOverlay) DestroyWindow(hOverlay);
        if (!className.empty()) UnregisterClassA(className.c_str(), GetModuleHandle(NULL));
    }

    bool Setup(HWND hGameWindow) {
        hGame = hGameWindow;

        // 1. RANDOMIZATION: Generate random names to evade string scanning
        srand(GetTickCount());
        className = RandomString(10 + (rand() % 10));   // e.g., "Xj92BzL1q"
        windowTitle = RandomString(10 + (rand() % 10)); // e.g., "mK8vP4dZ2"

        WNDCLASSEXA wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = className.c_str(); // Use random class name
        wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
        RegisterClassExA(&wc);

        hOverlay = CreateWindowExA(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            className.c_str(),
            windowTitle.c_str(), // Use random title
            WS_POPUP | WS_VISIBLE,
            0, 0, 100, 100,
            NULL, NULL, GetModuleHandle(NULL), NULL
        );

        if (!hOverlay) return false;

        SetLayeredWindowAttributes(hOverlay, RGB(0, 0, 0), 0, LWA_COLORKEY);

        return true;
    }

    void UpdatePosition() {
        if (!hGame || !hOverlay) return;
        RECT rect;
        GetClientRect(hGame, &rect);
        POINT pt = { 0, 0 };
        ClientToScreen(hGame, &pt);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        MoveWindow(hOverlay, pt.x, pt.y, width, height, TRUE);
    }

    // UPDATED: Now supports accumulating points
    // accumulate = false: Clears screen, adds this point, draws. (Default/Standard behavior)
    // accumulate = true:  Keeps existing points, adds this point, draws everything.
    void DrawFrame(int x, int y, COLORREF color, bool accumulate = false) {
        if (!hOverlay) return;

        // 1. Manage Points
        if (!accumulate) {
            activePoints.clear();
        }
        activePoints.push_back({ x, y, color });

        HDC hdc = GetDC(hOverlay);
        if (hdc) {
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

            // 2. Clear Background (Black = Transparent)
            HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
            RECT r = { 0, 0, width, height };
            FillRect(memDC, &r, blackBrush);
            DeleteObject(blackBrush);

            // 3. Draw All Points
            for (const auto& pt : activePoints) {
                // Bounds check for each point
                if (pt.x >= 0 && pt.x < width && pt.y >= 0 && pt.y < height) {
                    HBRUSH targetBrush = CreateSolidBrush(pt.color);
                    RECT dotRect = { pt.x - 4, pt.y - 4, pt.x + 4, pt.y + 4 };
                    FillRect(memDC, &dotRect, targetBrush);
                    DeleteObject(targetBrush);
                }
            }

            // 4. Blit to Screen
            BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
            ReleaseDC(hOverlay, hdc);
        }
    }
};