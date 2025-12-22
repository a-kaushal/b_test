#pragma once

#include <windows.h>
#include <vector>
#include <cmath>
#include <iostream>

// --- MOUSE SPECIFIC DEFINITIONS ---
#define MOUSE_SIGNATURE_MAGIC 0x4D4F5553  // "MOUS"
#define MOUSE_IOCTL_BASE 0x8C4

#define IOCTL_MOUSE_MOVE    CTL_CODE(FILE_DEVICE_UNKNOWN, MOUSE_IOCTL_BASE + 0x01, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MOUSE_BUTTON  CTL_CODE(FILE_DEVICE_UNKNOWN, MOUSE_IOCTL_BASE + 0x02, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MOUSE_WHEEL   CTL_CODE(FILE_DEVICE_UNKNOWN, MOUSE_IOCTL_BASE + 0x03, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MOUSE_CONFIGURE CTL_CODE(FILE_DEVICE_UNKNOWN, MOUSE_IOCTL_BASE + 0x04, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MOUSE_GET_STATUS CTL_CODE(FILE_DEVICE_UNKNOWN, MOUSE_IOCTL_BASE + 0x05, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Obfuscated device path
#define MOUSE_DEVICE_PATH L"\\\\.\\{9A7E1BD3-9D0F-5E1B-AF4C-3F2E6D5B8C0F}"

// Mouse button definitions
enum MouseButton {
    MOUSE_LEFT = 0,
    MOUSE_RIGHT = 1,
    MOUSE_MIDDLE = 2,
    MOUSE_X1 = 3,
    MOUSE_X2 = 4
};

#pragma pack(push, 1)
typedef struct _ENHANCED_MOUSE_MOVE {
    ULONG Signature;
    LONG DeltaX;
    LONG DeltaY;
    BOOLEAN Absolute;
    BOOLEAN SimulateHardware;
    UCHAR Reserved[8];
} ENHANCED_MOUSE_MOVE;

typedef struct _ENHANCED_MOUSE_BUTTON {
    ULONG Signature;
    UCHAR Button;
    BOOLEAN Down;
    BOOLEAN SimulateHardware;
    UCHAR Reserved[8];
} ENHANCED_MOUSE_BUTTON;

typedef struct _ENHANCED_MOUSE_WHEEL {
    ULONG Signature;
    SHORT Delta;
    BOOLEAN Horizontal;
    BOOLEAN SimulateHardware;
    UCHAR Reserved[8];
} ENHANCED_MOUSE_WHEEL;

typedef struct _MOUSE_DRIVER_CONFIG {
    BOOLEAN EnableJitter;
    BOOLEAN EnableHardwareSimulation;
    BOOLEAN EnableSmoothMovement;
    BOOLEAN StealthMode;
    ULONG MovementSmoothness;
    ULONG MinDelayMs;
    ULONG MaxDelayMs;
    UCHAR Reserved[16];
} MOUSE_DRIVER_CONFIG;

typedef struct _MOUSE_DRIVER_STATUS {
    ULONG Version;
    ULONG MovesProcessed;
    ULONG ButtonsProcessed;
    ULONG WheelProcessed;
    ULONG LastActionTime;
    BOOLEAN Active;
    MOUSE_DRIVER_CONFIG CurrentConfig;
    UCHAR Reserved[12];
} MOUSE_DRIVER_STATUS;
#pragma pack(pop)

class SimpleMouseClient {
private:
    HANDLE m_hDevice;
    bool m_Connected;
    MOUSE_DRIVER_CONFIG m_Config;

    // --- SAFETY LOCK VARIABLES ---
    HWND m_LockWindow; // The window we are restricted to
    bool m_SafetyEnabled;

    // Helper: Check if it is safe to click right now
    // Returns TRUE if safe, FALSE if blocked
    bool IsSafeToClick() {
        if (!m_SafetyEnabled || m_LockWindow == NULL) return true; // Safety off

        // 1. Check Foreground Window (Alt-Tab Protection)
        if (GetForegroundWindow() != m_LockWindow) {
            // Optional: Print debug info
            // std::cout << "[MOUSE] Blocked: Game is not focused." << std::endl;
            return false;
        }

        // 2. Check Mouse Position (Bounds Check)
        POINT pt;
        if (GetCursorPos(&pt)) {
            RECT rect;
            GetWindowRect(m_LockWindow, &rect);
            // Verify cursor is strictly inside the client area
            if (pt.x < rect.left || pt.x > rect.right || pt.y < rect.top || pt.y > rect.bottom) {
                // std::cout << "[MOUSE] Blocked: Cursor outside game window." << std::endl;
                return false;
            }
        }
        return true;
    }

    // Inject mouse movement via SendInput
    void InjectMouseMove(LONG dx, LONG dy, bool absolute) {
        INPUT input = { 0 };
        input.type = INPUT_MOUSE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;

        if (absolute) {
            input.mi.dwFlags |= MOUSEEVENTF_ABSOLUTE;
        }

        SendInput(1, &input, sizeof(INPUT));
    }

    // Inject mouse button via SendInput
    void InjectMouseButton(MouseButton button, bool down) {
        INPUT input = { 0 };
        input.type = INPUT_MOUSE;

        switch (button) {
        case MOUSE_LEFT:
            input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;
        case MOUSE_RIGHT:
            input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;
        case MOUSE_MIDDLE:
            input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            break;
        case MOUSE_X1:
            input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = XBUTTON1;
            break;
        case MOUSE_X2:
            input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = XBUTTON2;
            break;
        }

        SendInput(1, &input, sizeof(INPUT));
    }

    // Inject mouse wheel via SendInput
    void InjectMouseWheel(SHORT delta, bool horizontal) {
        INPUT input = { 0 };
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
        input.mi.mouseData = delta;

        SendInput(1, &input, sizeof(INPUT));
    }

public:
    SimpleMouseClient() : m_hDevice(INVALID_HANDLE_VALUE), m_Connected(false), m_LockWindow(NULL), m_SafetyEnabled(false) {
        // Default config with anti-detection
        m_Config.EnableJitter = TRUE;
        m_Config.EnableHardwareSimulation = TRUE;
        m_Config.EnableSmoothMovement = TRUE;
        m_Config.StealthMode = TRUE;
        m_Config.MovementSmoothness = 10;
        m_Config.MinDelayMs = 1;
        m_Config.MaxDelayMs = 5;
    }

    ~SimpleMouseClient() {
        Disconnect();
    }

    // --- NEW: SAFETY CONFIGURATION ---

    // Set the window handle to lock clicks to. 
    // If set, clicks will be blocked if this window is not focused or cursor is outside.
    void SetLockWindow(HWND hWindow) {
        m_LockWindow = hWindow;
        m_SafetyEnabled = (hWindow != NULL);
    }

    // Connect to driver
    bool Connect() {
        if (m_Connected) return true;

        m_hDevice = CreateFileW(
            MOUSE_DEVICE_PATH,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );

        if (m_hDevice == INVALID_HANDLE_VALUE) {
            return false;
        }

        m_Connected = true;
        return Configure(m_Config);
    }

    // Disconnect
    void Disconnect() {
        if (m_hDevice != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hDevice);
            m_hDevice = INVALID_HANDLE_VALUE;
        }
        m_Connected = false;
    }

    // Configure anti-detection features
    bool Configure(const MOUSE_DRIVER_CONFIG& config) {
        if (!m_Connected) return false;

        m_Config = config;

        DWORD bytesReturned;
        return DeviceIoControl(
            m_hDevice,
            IOCTL_MOUSE_CONFIGURE,
            (PVOID)&config,
            sizeof(config),
            NULL,
            0,
            &bytesReturned,
            NULL
        );
    }

    void ClampToLockWindow(LONG& x, LONG& y) {
        if (!m_LockWindow) return;

        RECT rect;
        // Get the "Client" area (excluding title bar and borders)
        GetClientRect(m_LockWindow, &rect);

        // Convert top-left and bottom-right to Screen Coordinates
        POINT tl = { rect.left, rect.top };
        POINT br = { rect.right, rect.bottom };
        ClientToScreen(m_LockWindow, &tl);
        ClientToScreen(m_LockWindow, &br);

        // Define a "Safety Margin" (e.g., 5 pixels) so we don't click the border
        long margin = 5;

        // Apply Clamp
        if (x < tl.x + margin) x = tl.x + margin;
        if (x > br.x - margin) x = br.x - margin;
        if (y < tl.y + margin) y = tl.y + margin;
        if (y > br.y - margin) y = br.y - margin;
    }

    void MoveToCenter() {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        if (!logFile.is_open()) {
            logFile.open("SMM_Debug.log", std::ios::app);  // fallback to current dir
        }
        if (!m_LockWindow) return;

        RECT rect;
        // Get the "Client" area (excluding title bar and borders)
        GetClientRect(m_LockWindow, &rect);
        POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
        ClientToScreen(m_LockWindow, &center);
		logFile << "[MOUSE] Moving to center (" << rect.right << ", " << rect.left << ")" << std::endl;
        MoveAbsolute(center.x, center.y);
    }

    // Move mouse (relative)
    // Note: Movement is usually allowed even if unsafe, to avoid "fighting" the user's hand.
    // However, if you want to strictly prevent movement outside, add IsSafeToClick() here too.
    bool Move(LONG dx, LONG dy, bool simulateHardware = true) {
        if (!m_Connected) return false;

        ENHANCED_MOUSE_MOVE move = { 0 };
        move.Signature = MOUSE_SIGNATURE_MAGIC;
        move.DeltaX = dx;
        move.DeltaY = dy;
        move.Absolute = FALSE;
        move.SimulateHardware = simulateHardware;

        DWORD bytesReturned;
        bool result = DeviceIoControl(
            m_hDevice,
            IOCTL_MOUSE_MOVE,
            &move,
            sizeof(move),
            NULL,
            0,
            &bytesReturned,
            NULL
        );

        if (result) {
            InjectMouseMove(dx, dy, false);
        }

        return result;
    }

    // Move mouse smoothly to position (relative)
    bool MoveTo(LONG targetX, LONG targetY, ULONG durationMs = 100) {
        if (!m_Connected || !m_Config.EnableSmoothMovement) {
            return Move(targetX, targetY);
        }

        ULONG steps = m_Config.MovementSmoothness;
        if (steps == 0) steps = 1;

        ULONG delayPerStep = durationMs / steps;

        for (ULONG i = 0; i < steps; i++) {
            LONG stepX = (targetX * (i + 1)) / steps - (targetX * i) / steps;
            LONG stepY = (targetY * (i + 1)) / steps - (targetY * i) / steps;

            if (!Move(stepX, stepY)) {
                return false;
            }

            if (delayPerStep > 0 && i < steps - 1) {
                Sleep(delayPerStep);
            }
        }

        return true;
    }

    // Move to absolute position on screen
    bool MoveAbsolute(LONG x, LONG y) {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        if (!logFile.is_open()) {
            logFile.open("SMM_Debug.log", std::ios::app);  // fallback to current dir
        }
        if (!m_Connected) return false;

		logFile << "[MOUSE] Requested MoveAbsolute to (" << x << ", " << y << ")" << std::endl;
        ClampToLockWindow(x, y);
		logFile << "[MOUSE] Clamped MoveAbsolute to (" << x << ", " << y << ")" << std::endl;

        // Convert to absolute coordinates (0-65535 range)
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        LONG absX = (x * 65535) / screenWidth;
        LONG absY = (y * 65535) / screenHeight;

        ENHANCED_MOUSE_MOVE move = { 0 };
        move.Signature = MOUSE_SIGNATURE_MAGIC;
        move.DeltaX = absX;
        move.DeltaY = absY;
        move.Absolute = TRUE;
        move.SimulateHardware = true;

        DWORD bytesReturned;
        bool result = DeviceIoControl(
            m_hDevice,
            IOCTL_MOUSE_MOVE,
            &move,
            sizeof(move),
            NULL,
            0,
            &bytesReturned,
            NULL
        );

        if (result) {
            InjectMouseMove(absX, absY, true);
        }

        return result;
    }

    // Press mouse button
    bool PressButton(MouseButton button, bool simulateHardware = true) {
        if (!m_Connected) return false;

        // --- SAFETY CHECK ---
        if (!IsSafeToClick()) return false;
        // --------------------

        ENHANCED_MOUSE_BUTTON btn = { 0 };
        btn.Signature = MOUSE_SIGNATURE_MAGIC;
        btn.Button = button;
        btn.Down = TRUE;
        btn.SimulateHardware = simulateHardware;

        DWORD bytesReturned;
        bool result = DeviceIoControl(
            m_hDevice,
            IOCTL_MOUSE_BUTTON,
            &btn,
            sizeof(btn),
            NULL,
            0,
            &bytesReturned,
            NULL
        );

        if (result) {
            InjectMouseButton(button, true);
        }

        return result;
    }

    // Release mouse button
    bool ReleaseButton(MouseButton button, bool simulateHardware = true) {
        if (!m_Connected) return false;

        // --- SAFETY CHECK ---
        if (!IsSafeToClick()) return false;
        // --------------------

        ENHANCED_MOUSE_BUTTON btn = { 0 };
        btn.Signature = MOUSE_SIGNATURE_MAGIC;
        btn.Button = button;
        btn.Down = FALSE;
        btn.SimulateHardware = simulateHardware;

        DWORD bytesReturned;
        bool result = DeviceIoControl(
            m_hDevice,
            IOCTL_MOUSE_BUTTON,
            &btn,
            sizeof(btn),
            NULL,
            0,
            &bytesReturned,
            NULL
        );

        if (result) {
            InjectMouseButton(button, false);
        }

        return result;
    }

    // Click mouse button
    bool Click(MouseButton button = MOUSE_LEFT, ULONG holdMs = 50) {
        if (!PressButton(button)) return false;
        Sleep(holdMs);
        if (!ReleaseButton(button)) return false;
        return true;
    }

    // Double click
    bool DoubleClick(MouseButton button = MOUSE_LEFT) {
        if (!Click(button, 50)) return false;
        Sleep(100);
        if (!Click(button, 50)) return false;
        return true;
    }

    // Scroll mouse wheel
    bool Scroll(SHORT delta, bool horizontal = false) {
        if (!m_Connected) return false;

        // --- SAFETY CHECK ---
        if (!IsSafeToClick()) return false;
        // --------------------

        ENHANCED_MOUSE_WHEEL wheel = { 0 };
        wheel.Signature = MOUSE_SIGNATURE_MAGIC;
        wheel.Delta = delta;
        wheel.Horizontal = horizontal;
        wheel.SimulateHardware = true;

        DWORD bytesReturned;
        bool result = DeviceIoControl(
            m_hDevice,
            IOCTL_MOUSE_WHEEL,
            &wheel,
            sizeof(wheel),
            NULL,
            0,
            &bytesReturned,
            NULL
        );

        if (result) {
            InjectMouseWheel(delta, horizontal);
        }

        return result;
    }

    // Scroll up (positive = up, negative = down)
    bool ScrollVertical(int notches) {
        return Scroll((SHORT)(notches * WHEEL_DELTA), false);
    }

    // Scroll horizontal (positive = right, negative = left)
    bool ScrollHorizontal(int notches) {
        return Scroll((SHORT)(notches * WHEEL_DELTA), true);
    }

    // Hold button for duration
    bool HoldButton(MouseButton button, ULONG durationMs) {
        if (!PressButton(button)) return false;
        Sleep(durationMs);
        if (!ReleaseButton(button)) return false;
        return true;
    }

    // Drag from current position
    bool Drag(MouseButton button, LONG dx, LONG dy, ULONG durationMs = 100) {
        if (!PressButton(button)) return false;
        Sleep(50);
        bool result = MoveTo(dx, dy, durationMs);
        Sleep(50);
        if (!ReleaseButton(button)) return false;
        return result;
    }

    // Fast mode
    bool SetFastMode(bool enable) {
        MOUSE_DRIVER_CONFIG config = m_Config;
        config.EnableJitter = !enable;
        config.EnableHardwareSimulation = !enable;
        config.EnableSmoothMovement = !enable;
        config.StealthMode = !enable;
        return Configure(config);
    }

    // Configure smoothness
    bool SetSmoothness(ULONG steps) {
        MOUSE_DRIVER_CONFIG config = m_Config;
        config.MovementSmoothness = steps;
        return Configure(config);
    }

    // Get status
    bool GetStatus(MOUSE_DRIVER_STATUS& status) {
        if (!m_Connected) return false;

        DWORD bytesReturned;
        return DeviceIoControl(
            m_hDevice,
            IOCTL_MOUSE_GET_STATUS,
            NULL,
            0,
            &status,
            sizeof(status),
            &bytesReturned,
            NULL
        );
    }

    bool GetPos(LONG& x, LONG& y) {
        POINT pt;
        if (GetCursorPos(&pt)) {
            x = pt.x;
            y = pt.y;
            return true;
        }
        return false;
    }

    bool WithinBoundsCheck(LONG x, LONG y) {
        std::ofstream logFile("C:\\Driver\\SMM_Debug.log", std::ios::app);
        if (!logFile.is_open()) {
            logFile.open("SMM_Debug.log", std::ios::app);  // fallback to current dir
        }
        if (!m_LockWindow) return false;

        RECT rect;
        // Get the "Client" area (excluding title bar and borders)
        GetClientRect(m_LockWindow, &rect);
        if ((x < rect.right) && (x > rect.left) && (y < rect.bottom) && (y < rect.top)) { 
            return true; 
        }
        return false;
    }

    bool IsConnected() const { return m_Connected; }
    MOUSE_DRIVER_CONFIG GetConfig() const { return m_Config; }
};