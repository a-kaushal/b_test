#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>

#include "Memory.h"

// --- KEYBOARD SPECIFIC DEFINITIONS ---
#define KBD_SIGNATURE_MAGIC 0x4B42584D
#define KBD_IOCTL_BASE 0x8B3

#define IOCTL_SEND_KEY      CTL_CODE(FILE_DEVICE_UNKNOWN, KBD_IOCTL_BASE + 0x01, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_KBD_CONFIGURE CTL_CODE(FILE_DEVICE_UNKNOWN, KBD_IOCTL_BASE + 0x02, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_KBD_GET_STATUS CTL_CODE(FILE_DEVICE_UNKNOWN, KBD_IOCTL_BASE + 0x03, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Obfuscated device path
#define KBD_DEVICE_PATH L"\\\\.\\{8F6F0AC2-8C9E-4D8A-9F3B-2E1D5C4A7B9E}"

#pragma pack(push, 1)
typedef struct _ENHANCED_KEY_INPUT {
    ULONG Signature;
    USHORT VirtualKey;
    USHORT ScanCode;
    BOOLEAN Down;
    BOOLEAN SimulateHardware;
    ULONG CustomDelayMs;
    UCHAR Reserved[8];
} ENHANCED_KEY_INPUT;

typedef struct _KBD_DRIVER_CONFIG {
    BOOLEAN EnableJitter;
    BOOLEAN EnableHardwareSimulation;
    BOOLEAN StealthMode;
    ULONG MinDelayMs;
    ULONG MaxDelayMs;
    UCHAR Reserved[16];
} KBD_DRIVER_CONFIG;

typedef struct _KBD_DRIVER_STATUS {
    ULONG Version;
    ULONG KeysSent;
    ULONG LastKeyTime;
    BOOLEAN Active;
    KBD_DRIVER_CONFIG CurrentConfig;
    UCHAR Reserved[12];
} KBD_DRIVER_STATUS;
#pragma pack(pop)

class SimpleKeyboardClient {
private:
    HANDLE m_hDevice;
    bool m_Connected;
    KBD_DRIVER_CONFIG m_Config;

    // Async hold tracking
    std::map<WORD, std::thread> m_HoldThreads;
    std::map<WORD, std::atomic<bool>> m_HoldFlags;
    std::mutex m_ThreadMutex;

    void InjectKeyViaInput(WORD vk, WORD scancode, bool down) {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.wScan = scancode;
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;

        SendInput(1, &input, sizeof(INPUT));
    }

    // Background thread for holding key
    void HoldKeyThread(WORD vk, WORD scancode, ULONG durationMs) {
        SendKey(vk, scancode, true, true);

        // Check flag every 10ms for early release
        ULONG elapsed = 0;
        while ((durationMs == INFINITE || elapsed < durationMs) && m_HoldFlags[vk].load()) {
            Sleep(10);
            if (durationMs != INFINITE) {
                elapsed += 10;
            }
        }

        SendKey(vk, scancode, false, true);
        m_HoldFlags[vk].store(false);
    }

public:
    SimpleKeyboardClient() : m_hDevice(INVALID_HANDLE_VALUE), m_Connected(false) {
        // Default config with anti-detection enabled
        m_Config.EnableJitter = TRUE;
        m_Config.EnableHardwareSimulation = TRUE;
        m_Config.StealthMode = TRUE;
        m_Config.MinDelayMs = 10;
        m_Config.MaxDelayMs = 50;
    }

    ~SimpleKeyboardClient() {
        StopAllHolds();
        Disconnect();
    }

    bool Connect() {
        if (m_Connected) return true;

        m_hDevice = CreateFileW(
            KBD_DEVICE_PATH,
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

    void Disconnect() {
        if (m_hDevice != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hDevice);
            m_hDevice = INVALID_HANDLE_VALUE;
        }
        m_Connected = false;
    }

    bool Configure(const KBD_DRIVER_CONFIG& config) {
        if (!m_Connected) return false;

        m_Config = config;

        DWORD bytesReturned;
        return DeviceIoControl(
            m_hDevice,
            IOCTL_KBD_CONFIGURE,
            (PVOID)&config,
            sizeof(config),
            NULL,
            0,
            &bytesReturned,
            NULL
        );
    }

    bool SendKey(WORD vk, WORD scancode, bool down, bool simulateHardware = true) {
        if (!m_Connected) return false;

        ENHANCED_KEY_INPUT input = { 0 };
        input.Signature = KBD_SIGNATURE_MAGIC;
        input.VirtualKey = vk;
        input.ScanCode = scancode;
        input.Down = down;
        input.SimulateHardware = simulateHardware;
        input.CustomDelayMs = 0;

        DWORD bytesReturned;
        bool result = DeviceIoControl(
            m_hDevice,
            IOCTL_SEND_KEY,
            &input,
            sizeof(input),
            NULL,
            0,
            &bytesReturned,
            NULL
        );

        if (result) {
            InjectKeyViaInput(vk, scancode, down);
        }

        return result;
    }

    bool PressKey(WORD vk, WORD scancode = 0) {
        return SendKey(vk, scancode, true, true);
    }

    bool ReleaseKey(WORD vk, WORD scancode = 0) {
        return SendKey(vk, scancode, false, true);
    }

    bool TypeKey(WORD vk, WORD scancode = 0) {
        if (!SendKey(vk, scancode, true, true)) return false;
        if (!SendKey(vk, scancode, false, true)) return false;
        return true;
    }

    bool TypeString(const std::wstring& text) {
        if (!m_Connected) return false;

        for (wchar_t ch : text) {
            SHORT vk = VkKeyScanW(ch);
            if (vk == -1) continue;

            BYTE keyCode = LOBYTE(vk);
            BYTE shiftState = HIBYTE(vk);

            if (shiftState & 1) {
                SendKey(VK_SHIFT, 0x2A, true, true);
            }

            TypeKey(keyCode, 0);

            if (shiftState & 1) {
                SendKey(VK_SHIFT, 0x2A, false, true);
            }
        }

        return true;
    }

    bool SetFastMode(bool enable) {
        KBD_DRIVER_CONFIG config = m_Config;
        config.EnableJitter = !enable;
        config.EnableHardwareSimulation = !enable;
        config.StealthMode = !enable;

        return Configure(config);
    }

    bool SetJitter(bool enable) {
        KBD_DRIVER_CONFIG config = m_Config;
        config.EnableJitter = enable;
        return Configure(config);
    }

    bool SetHardwareSimulation(bool enable) {
        KBD_DRIVER_CONFIG config = m_Config;
        config.EnableHardwareSimulation = enable;
        return Configure(config);
    }

    bool SetStealthMode(bool enable) {
        KBD_DRIVER_CONFIG config = m_Config;
        config.StealthMode = enable;
        return Configure(config);
    }

    bool SetTimingRange(ULONG minMs, ULONG maxMs) {
        KBD_DRIVER_CONFIG config = m_Config;
        config.MinDelayMs = minMs;
        config.MaxDelayMs = maxMs;
        return Configure(config);
    }

    bool GetStatus(KBD_DRIVER_STATUS& status) {
        if (!m_Connected) return false;

        DWORD bytesReturned;
        return DeviceIoControl(
            m_hDevice,
            IOCTL_KBD_GET_STATUS,
            NULL,
            0,
            &status,
            sizeof(status),
            &bytesReturned,
            NULL
        );
    }

    bool IsConnected() const {
        return m_Connected;
    }

    KBD_DRIVER_CONFIG GetConfig() const {
        return m_Config;
    }

    bool HoldKey(WORD vk, ULONG durationMs, WORD scancode = 0) {
        if (!m_Connected) return false;
        if (!SendKey(vk, scancode, true, true)) return false;
        Sleep(durationMs);
        if (!SendKey(vk, scancode, false, true)) return false;
        return true;
    }

    bool HoldKeyAsync(WORD vk, ULONG durationMs, WORD scancode = 0) {
        if (!m_Connected) return false;
        StopHold(vk);
        std::lock_guard<std::mutex> lock(m_ThreadMutex);
        m_HoldFlags[vk].store(true);
        m_HoldThreads[vk] = std::thread(&SimpleKeyboardClient::HoldKeyThread, this, vk, scancode, durationMs);
        m_HoldThreads[vk].detach();
        return true;
    }

    bool StartHold(WORD vk, WORD scancode = 0) {
        if (!m_Connected) return false;
        StopHold(vk);
        std::lock_guard<std::mutex> lock(m_ThreadMutex);
        m_HoldFlags[vk].store(true);
        m_HoldThreads[vk] = std::thread(&SimpleKeyboardClient::HoldKeyThread, this, vk, scancode, INFINITE);
        m_HoldThreads[vk].detach();
        return true;
    }

    bool StopHold(WORD vk) {
        std::lock_guard<std::mutex> lock(m_ThreadMutex);
        if (m_HoldFlags.find(vk) != m_HoldFlags.end()) {
            m_HoldFlags[vk].store(false);
            Sleep(50);
            m_HoldThreads.erase(vk);
            m_HoldFlags.erase(vk);
            return true;
        }
        return false;
    }

    void StopAllHolds() {
        std::lock_guard<std::mutex> lock(m_ThreadMutex);
        for (auto& pair : m_HoldFlags) {
            pair.second.store(false);
        }
        Sleep(100);
        m_HoldThreads.clear();
        m_HoldFlags.clear();
    }

    bool IsHolding(WORD vk) {
        std::lock_guard<std::mutex> lock(m_ThreadMutex);
        return m_HoldFlags.find(vk) != m_HoldFlags.end() && m_HoldFlags[vk].load();
    }

    bool HoldKeys(const std::vector<WORD>& keys, ULONG durationMs) {
        if (!m_Connected || keys.empty()) return false;
        for (WORD vk : keys) {
            if (!SendKey(vk, 0, true, true)) {
                for (WORD releaseVk : keys) {
                    if (releaseVk == vk) break;
                    SendKey(releaseVk, 0, false, true);
                }
                return false;
            }
        }
        Sleep(durationMs);
        for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
            SendKey(*it, 0, false, true);
        }
        return true;
    }

    bool HoldKeysAsync(const std::vector<WORD>& keys, ULONG durationMs) {
        if (!m_Connected || keys.empty()) return false;
        for (WORD vk : keys) {
            HoldKeyAsync(vk, durationMs);
        }
        return true;
    }
};