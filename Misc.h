#pragma once

#include "MovementController.h"
#include "dllmain.h"

void EndScript(MovementController& pilot, int failCode) {
    switch (failCode) {
    case 1: g_LogFile << "Ground Path Generation Failed. Quitting Script." << std::endl; break;
    case 2: g_LogFile << "Flying Path Generation Failed. Quitting Script." << std::endl; break;
    case 3: g_LogFile << "Respawn Failed: Unable to reach corpse on any Z-layer. Quitting Script." << std::endl; break;
    }

    pilot.Stop();

    if (g_LogFile.is_open()) {
        g_LogFile.close();
    }
    //FreeLibraryAndExitThread(hModule, 0);

    g_IsRunning = false;
    RaiseException(0xDEADBEEF, 0, 0, nullptr); // Forcibly exit all threads (including GUI)
}