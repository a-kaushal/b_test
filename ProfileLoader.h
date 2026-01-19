#pragma once
#include <string>
#include <fstream>
#include <Windows.h>
#include <iostream>
#include "ProfileInterface.h"

class ProfileLoader {
private:
    HMODULE hProfileDll = nullptr;
    BotProfile* currentProfile = nullptr;
    std::string profileDir = "C:\\Driver\\Profiles\\"; // Ensure this exists

public:
    BotProfile* GetActiveProfile() { return currentProfile; }

    bool CompileAndLoad(const std::string& profileCode, std::string& outError) {
        // 1. Unload existing
        if (currentProfile) {
            // If you exported a delete function, use it. Otherwise just delete if allocated in same heap (risky across DLLs)
            // Ideally: Call FreeProfile(currentProfile) from DLL.
            // For simplicity here, we assume simple destruction.
            delete currentProfile;
            currentProfile = nullptr;
        }
        if (hProfileDll) {
            FreeLibrary(hProfileDll);
            hProfileDll = nullptr;
        }

        // 2. Save Code to File
        std::string srcPath = profileDir + "ActiveProfile.cpp";
        std::ofstream out(srcPath);
        if (!out.is_open()) {
            outError = "Could not write to " + srcPath;
            return false;
        }

        // Inject necessary headers
        out << "#include \"../SMM/ProfileInterface.h\"\n";
        out << "#include \"../SMM/Vector.h\"\n"; // Add other needed headers with correct relative paths
        out << profileCode << "\n";
        out << "extern \"C\" __declspec(dllexport) BotProfile* CreateProfile() { return new MyProfile(); }";
        out.close();

        // 3. Compile
        // Command: cl.exe /LD ActiveProfile.cpp /Fe:ActiveProfile.dll
        // You might need to set up vcvarsall.bat environment variables before running the bot
        std::string cmd = "cl.exe /LD /EHsc \"" + srcPath + "\" /Fe:\"" + profileDir + "ActiveProfile.dll\"";

        // Execute compilation
        int result = system(cmd.c_str());
        if (result != 0) {
            outError = "Compilation failed. Check console/logs.";
            return false;
        }

        // 4. Load DLL
        std::string dllPath = profileDir + "ActiveProfile.dll";
        hProfileDll = LoadLibraryA(dllPath.c_str());
        if (!hProfileDll) {
            outError = "Failed to load DLL. Error: " + std::to_string(GetLastError());
            return false;
        }

        // 5. Instantiate
        auto factory = (CreateProfileFn)GetProcAddress(hProfileDll, "CreateProfile");
        if (!factory) {
            outError = "Could not find CreateProfile entry point.";
            return false;
        }

        currentProfile = factory();
        if (currentProfile) {
            currentProfile->Setup();
        }

        return true;
    }
};