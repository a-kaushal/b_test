#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

#pragma comment(lib, "Ws2_32.lib")

class WebServer {
public:
    static void Start(int port);
    static void Stop();

    // Getters/Setters
    static bool IsBotActive();
    static void SetBotActive(bool active);
    static bool IsProfileLoadRequested();
    static std::string GetRequestedProfile();
    static void ConfirmProfileLoaded(const std::string& profileName);

    // Dashboard HTML Generator
    static std::string GetHTML();

private:
    static void ServerThread(int port);
    static void HandleClient(unsigned __int64 clientSocketRaw);
    
    // [FIX] These are now private static members so they can access startTime
    static std::string GenerateJSONState();
    static std::string ReadLogFileTail(int charsToRead);

    static std::atomic<bool> running;
    static std::thread serverThread;
    static std::atomic<bool> botActive;
    static std::atomic<bool> profileLoadReq;
    static std::string currentProfile;
    static std::string pendingProfile;
    static unsigned long long startTime;
};