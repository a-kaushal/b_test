#pragma once
#include <thread>
#include <atomic>
#include <mutex>
#include <string>

class WebServer {
public:
    // Starts the web server in a background thread on the specified port (default 8080)
    static void Start(int port = 8080);

    // Stops the server (optional, mostly for cleanup)
    static void Stop();

private:
    static void ServerThread(int port);
    static void HandleClient(unsigned __int64 clientSocket); // SOCKET is uint64 on x64
    static std::string BuildJSON();
    static std::string GetHTML();

    static std::atomic<bool> running;
    static std::thread serverThread;
};