#pragma once
#include <mutex>
#include <fstream>
#include <atomic>

// --- GLOBAL VARIABLES (Shared between threads) ---

// 1. The Mutex: Prevents the GUI from reading while the Main Thread is writing (Fixes Crash)
extern std::mutex g_EntityMutex;

// 2. The Logger: Prevents opening/closing the file 100 times/sec (Fixes Lag)
extern std::ofstream g_LogFile;

// 3. The Run Flag: Stops all threads safely
extern std::atomic<bool> g_IsRunning;