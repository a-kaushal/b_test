#pragma once
#include <mutex>
#include <fstream>
#include <atomic>

#include "Vector.h"

// --- GLOBAL VARIABLES (Shared between threads) ---

// 1. The Mutex: Prevents the GUI from reading while the Main Thread is writing (Fixes Crash)
extern std::mutex g_EntityMutex;

// 2. The Logger: Prevents opening/closing the file 100 times/sec (Fixes Lag)
extern std::ofstream g_LogFile;

// 3. The Run Flag: Stops all threads safely
extern std::atomic<bool> g_IsRunning;

uint64_t get_bits_u128(uint64_t low, uint64_t high, int start, int len);

void GUIDBreakdown(uint32_t& low_counter, uint32_t& type_field, uint32_t& instance, uint32_t& id, uint32_t& map_id, uint32_t& server_id, ULONG_PTR guidLow, ULONG_PTR guidHigh);

// Forward declaration so we don't need to include the header here
class ProfileLoader;

// Expose the global instance to other files (Behaviors.h)
extern ProfileLoader g_ProfileLoader;