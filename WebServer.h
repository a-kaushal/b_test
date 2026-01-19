#pragma once
#include <thread>
#include <atomic>
#include <mutex>
#include <string>

class WebServer {
public:
    // Starts the web server in a background thread
    static void Start(int port = 8080);

    // Stops the server
    static void Stop();

    // --- Interface for Main Bot Logic ---

    // Check if the user clicked "Start Bot" in the UI
    static bool IsBotActive();

    // Set the state (e.g., if the bot stops itself)
    static void SetBotActive(bool active);

    // Check if a profile load was requested via the UI
    static bool IsProfileLoadRequested();

    // Get the name of the profile to load
    static std::string GetRequestedProfile();

    // Notify the UI that the profile has been loaded
    static void ConfirmProfileLoaded(const std::string& profileName);

private:
    static void ServerThread(int port);
    static void HandleClient(unsigned __int64 clientSocket); // SOCKET is uint64 on x64

    // Response Builders
    static std::string BuildJSON();       // Legacy: Map/Entity Data
    static std::string BuildStatusJSON(); // New: Dashboard Status
    static std::string GetHTML();         // New: Controller UI

    static std::atomic<bool> running;
    static std::thread serverThread;

    // State Variables
    static std::atomic<bool> botActive;
    static std::atomic<bool> profileLoadReq;
    static std::string currentProfile;
    static std::string pendingProfile;
    static unsigned long long startTime;
};