#pragma once
#include <windows.h>
#include <vector>
#include <mutex>
#include "Entity.h"

// Start the GUI window in its own thread
void StartGuiThread(HMODULE hDllInst);

// Update the data shown in the GUI
void UpdateGuiData(const std::vector<GameEntity>& newData);