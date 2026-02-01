#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <Windows.h>
#include <iostream>
#include <vector>
#include <filesystem>
#include "ProfileInterface.h"
#include "Profile.h"

// Defines global log file for the Main EXE
extern std::ofstream g_LogFile;

extern ProfileSettings g_ProfileSettings;

class ProfileLoader {
private:
    HMODULE hProfileDll = nullptr;
    BotProfile* currentProfile = nullptr;

    // [CONFIG] Directories
    std::string profileDir = "C:\\SMM\\Profiles";

    // [CONFIG] Source Paths
    std::string userSource = "Z:\\SMM\\Profiles";
    std::string devSource = "Z:\\VSProjects\\source\\repos\\MemoryTest\\SMM";
    std::string secondarySource = "C:\\SMM";

public:
    BotProfile* GetActiveProfile() { return currentProfile; }

    // Helper: Tries to find a file in source folders, then copies/patches it to Staging
    bool FindAndStage(const std::string& filename, bool patchCircular = false) {
        std::ifstream in;
        std::string foundPath = "";
        std::vector<std::string> pathsToCheck;

        pathsToCheck.push_back(userSource + "\\" + filename);
        pathsToCheck.push_back(devSource + "\\" + filename);
        pathsToCheck.push_back(devSource + "\\SMM\\" + filename);
        pathsToCheck.push_back(secondarySource + "\\" + filename);

        char path[MAX_PATH];
        if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
            std::string exeDir = std::string(path);
            exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
            pathsToCheck.push_back(exeDir + "\\" + filename);
        }

        for (const auto& p : pathsToCheck) {
            in.open(p);
            if (in.is_open()) {
                foundPath = p;
                break;
            }
            in.close(); in.clear();
        }

        if (foundPath.empty()) {
            // Only log errors for core files, optional headers can fail silently if not needed
            if (filename.find("Repair.h") == std::string::npos) {
                std::string msg = "[Loader] ERROR: Could not find file: " + filename;
                if (g_LogFile.is_open()) g_LogFile << msg << std::endl;
            }
            return false;
        }

        std::ofstream out(profileDir + "\\" + filename);
        if (!out.is_open()) return false;

        std::string line;
        while (std::getline(in, line)) {
            if (patchCircular && line.find("#include \"Pathfinding2.h\"") != std::string::npos) {
                out << "// [LOADER PATCH] Circular include removed\n";
                out << "struct PathNode;\n";
            }
            else if (line.find("#pragma once") != std::string::npos) {
                out << line << "\n";
                out << "extern std::ofstream g_LogFile;\n";
            }
            else {
                out << line << "\n";
            }
        }
        return true;
    }

    bool LoadLastProfile(std::string& outError) {
        std::string namePath = profileDir + "\\LastProfileName.txt";
        std::ifstream nameFile(namePath);
        if (nameFile.is_open()) {
            std::string filename;
            std::getline(nameFile, filename);
            nameFile.close();
            filename.erase(filename.find_last_not_of(" \n\r\t") + 1);

            if (!filename.empty()) {
                if (g_LogFile.is_open()) g_LogFile << "[Loader] Auto-Loading: " << filename << std::endl;
                if (FindAndStage(filename)) {
                    std::ifstream stagedFile(profileDir + "\\" + filename);
                    std::stringstream buffer;
                    buffer << stagedFile.rdbuf();
                    return CompileAndLoad(buffer.str(), outError, filename);
                }
            }
        }

        std::string sourcePath = profileDir + "\\LastProfile.txt";
        std::ifstream in(sourcePath);
        if (!in.is_open()) {
            outError = "No previous profile history found.";
            return false;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        return CompileAndLoad(buffer.str(), outError);
    }

    bool CompileAndLoad(const std::string& profileCode, std::string& outError, const std::string& filename = "") {
        if (currentProfile) { currentProfile = nullptr; }
        if (hProfileDll) { FreeLibrary(hProfileDll); hProfileDll = nullptr; }

        CreateDirectoryA(profileDir.c_str(), NULL);
        { std::ofstream save(profileDir + "\\LastProfile.txt"); save << profileCode; }
        if (!filename.empty()) { std::ofstream saveName(profileDir + "\\LastProfileName.txt"); saveName << filename; }

        {
            std::ofstream fh(profileDir + "\\ForceHeader.h");
            fh << "#pragma once\n#include <fstream>\nextern std::ofstream g_LogFile;\n";
        }

        // [CONFIG] Headers to Stage
        std::vector<std::string> headers = {
            "Vector.h", "Entity.h", "dllmain.h", "ProfileInterface.h",
            "SimpleMouseClient.h", "SimpleKeyboardClient.h",
            "Pathfinding2.h", "MovementController.h",
            "Behaviors.h", "Profile.h", "json.hpp",
            "Database.h", "Combat.h", "Gathering.h", "Mailing.h", "LuaAnchor.h", "Camera.h"
        };

        for (const auto& h : headers) {
            FindAndStage(h);
        }

        if (!FindAndStage("WorldState.h", true)) { outError = "Missing WorldState.h"; return false; }

        std::string srcPath = profileDir + "\\ActiveProfile.cpp";
        std::ofstream out(srcPath);

        // --- GENERATED SOURCE HEADER ---
        out << "#include \"ForceHeader.h\"\n";
        out << "std::ofstream g_LogFile(\"C:\\\\SMM\\\\Profiles\\\\ProfileLog.txt\");\n";
        out << "class WorldState; extern WorldState* g_GameState;\n";
        out << "WorldState* g_GameState = nullptr;\n";

        out << "#include <Windows.h>\n#include <vector>\n#include <string>\n#include <cmath>\n#include <atomic>\n";
        out << "#include \"Vector.h\"\n";
        out << "#include \"Entity.h\"\n";
        out << "#include \"Profile.h\"\n";
        out << "class ProfileLoader;\n";
        out << "#include \"dllmain.h\"\n";
        out << "#include \"WorldState.h\"\n";
        out << "#include \"Pathfinding2.h\"\n";
        out << "#include \"MovementController.h\"\n";
        out << "#include \"Behaviors.h\"\n";
        out << "#include \"ProfileInterface.h\"\n";

        // Define proxies for globals
        out << "using BotSettings = ProfileSettings;\n";
        out << "ProfileSettings g_ProfileSettings;\n";
        out << "std::atomic<bool> g_IsRunning(true);\n";

        out << profileCode << "\n";

        out << "class ProfileWrapper : public MyProfile {\n";
        out << "public:\n";
        out << "    void Setup(WorldState* state, ProfileSettings* settings) override {\n";
        out << "        g_GameState = state;\n";
        out << "        MySettings userSettings;\n";
        out << "        *settings = userSettings;\n";
        out << "        g_ProfileSettings = userSettings;\n";
        out << "        MyProfile::Setup(state, settings);\n";
        out << "    }\n";
        out << "};\n";

        out << "extern \"C\" __declspec(dllexport) BotProfile* CreateProfile() { return new ProfileWrapper(); }";
        out.close();

        // 4. COMPILER CONFIG
        std::string baseDir = "Z:\\MiniBuilder\\";
        std::string msvcVer = "14.44.35207";
        std::string sdkVer = "10.0.26100.0";
        std::string binDir = baseDir + "VC\\Tools\\MSVC\\" + msvcVer + "\\bin\\Hostx64\\x64";
        std::string incMSVC = baseDir + "VC\\Tools\\MSVC\\" + msvcVer + "\\include";
        std::string incUCRT = baseDir + "Windows Kits\\10\\Include\\" + sdkVer + "\\ucrt";
        std::string incShrd = baseDir + "Windows Kits\\10\\Include\\" + sdkVer + "\\shared";
        std::string incUm = baseDir + "Windows Kits\\10\\Include\\" + sdkVer + "\\um";
        std::string libMSVC = baseDir + "VC\\Tools\\MSVC\\" + msvcVer + "\\lib\\x64";
        std::string libUCRT = baseDir + "Windows Kits\\10\\Lib\\" + sdkVer + "\\ucrt\\x64";
        std::string libUm = baseDir + "Windows Kits\\10\\Lib\\" + sdkVer + "\\um\\x64";
        std::string detourLibPath = "Z:\\VSProjects\\source\\repos\\MemoryTest\\x64\\Release";

        std::string logPath = profileDir + "\\compile_log.txt";
        std::string dllOutput = profileDir + "\\ActiveProfile.dll";

        // 5. BUILD COMMAND
        std::stringstream cmd;
        cmd << "set PATH=" << binDir << ";%PATH% && ";
        cmd << "cl.exe /D COMPILING_PROFILE /nologo /LD /MD /EHsc /O2 /std:c++17 /wd4566 /wd4005 /wd4244 /FI \"ForceHeader.h\" ";
        cmd << "/I \"" << incMSVC << "\" ";
        cmd << "/I \"" << incUCRT << "\" ";
        cmd << "/I \"" << incShrd << "\" ";
        cmd << "/I \"" << incUm << "\" ";
        cmd << "/I \"" << profileDir << "\" ";
        cmd << "/I \"" << "Z:\\VSProjects\\source\\repos\\MemoryTest\\Detour\\Include" << "\" ";
        cmd << "\"" << srcPath << "\" ";
        cmd << "/Fe:\"" << dllOutput << "\" ";

        cmd << "/link /LIBPATH:\"" << libMSVC << "\" "
            << "/LIBPATH:\"" << libUCRT << "\" "
            << "/LIBPATH:\"" << libUm << "\" "
            << "/LIBPATH:\"" << detourLibPath << "\" "
            << "Detour.lib SMM.lib "
            << "User32.lib Shell32.lib Gdi32.lib Advapi32.lib ";

        cmd << "> \"" << logPath << "\" 2>&1";

        // [FIX] Use CreateProcess to hide the window
        std::string cmdStr = "cmd.exe /C " + cmd.str();
        std::vector<char> mutableCmd(cmdStr.begin(), cmdStr.end());
        mutableCmd.push_back(0);

        STARTUPINFOA si = { sizeof(STARTUPINFOA) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE; // Hides the window
        PROCESS_INFORMATION pi = { 0 };

        if (!CreateProcessA(NULL, mutableCmd.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            outError = "Failed to launch compiler process.";
            return false;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode != 0) {
            std::ifstream logReader(logPath);
            std::stringstream logBuffer;
            if (logReader) logBuffer << logReader.rdbuf();
            outError = "Compilation Failed:\n" + logBuffer.str();
            return false;
        }

        hProfileDll = LoadLibraryA(dllOutput.c_str());
        if (!hProfileDll) { outError = "LoadLibrary Failed"; return false; }

        auto factory = (CreateProfileFn)GetProcAddress(hProfileDll, "CreateProfile");
        if (!factory) { outError = "CreateProfile export missing"; return false; }

        currentProfile = factory();
        if (currentProfile) {
            extern WorldState* g_GameState;
            extern ProfileSettings g_ProfileSettings;
            currentProfile->Setup(g_GameState, &g_ProfileSettings);
        }

        return true;
    }
};