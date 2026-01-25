#pragma once
#include <windows.h>
#include <iostream>
#include <string>

// Function to Launch Notepad, Inject DLL, and Resume
bool LaunchAndInject(const std::string& dllPath) {
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    // 1. Start Notepad (Suspended)
    // We use CREATE_SUSPENDED so it doesn't run any code before we inject
    if (!CreateProcessA("C:\\Windows\\System32\\notepad.exe", NULL, NULL, NULL, FALSE,
        CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        std::cerr << "[Error] Failed to launch Notepad." << std::endl;
        return false;
    }

    std::cout << "[Info] Notepad launched with PID: " << pi.dwProcessId << std::endl;

    // 2. Allocate Memory for DLL Path in Target Process
    void* pRemotePath = VirtualAllocEx(pi.hProcess, NULL, dllPath.length() + 1, MEM_COMMIT, PAGE_READWRITE);
    if (!pRemotePath) {
        std::cerr << "[Error] Failed to allocate memory in target." << std::endl;
        TerminateProcess(pi.hProcess, 0); // Cleanup
        return false;
    }

    // 3. Write DLL Path to Target Memory
    WriteProcessMemory(pi.hProcess, pRemotePath, dllPath.c_str(), dllPath.length() + 1, NULL);

    // 4. Create Remote Thread to LoadLibrary
    HMODULE hKernel32 = GetModuleHandleA("Kernel32.dll");
    void* pLoadLibrary = (void*)GetProcAddress(hKernel32, "LoadLibraryA");

    HANDLE hThread = CreateRemoteThread(pi.hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)pLoadLibrary,
        pRemotePath, 0, NULL);

    if (!hThread) {
        std::cerr << "[Error] Failed to create remote thread." << std::endl;
        TerminateProcess(pi.hProcess, 0);
        return false;
    }

    // 5. Wait for Injection to Finish
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);

    // 6. Resume the Main Thread (So Notepad actually runs and keeps our DLL alive)
    ResumeThread(pi.hThread);

    // 7. Optional: Hide the Notepad Window
    // We need to wait a moment for the window to actually create after resuming
    Sleep(500);
    // Enumerate windows to find the one belonging to our PID
    HWND hNotepad = NULL;
    // (Simple polling for demonstration)
    for (int i = 0; i < 10; i++) {
        // You would typically use EnumWindows here, but FindWindow is a lazy fallback
        // Note: This might find a *different* notepad if you have multiple open.
        // For a robust solution, use EnumWindows and check GetWindowThreadProcessId.
    }

    // Cleanup Handles (Does not kill the process)
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::cout << "[Success] Injected into Notepad!" << std::endl;
    return true;
}