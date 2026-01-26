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
    std::string sourceDir = "Z:\\VSProjects\\source\\repos\\MemoryTest\\SMM";
    std::string secondarySource = "C:\\SMM";

public:
    BotProfile* GetActiveProfile() { return currentProfile; }

    // Helper: Tries to find a file in Z:, then C:\SMM, then copies/patches it to Profiles\/
    bool FindAndStage(const std::string& filename, bool patchCircular = false) {
        std::string srcPath;
        std::ifstream in;

        // 1. Try Primary (Z:)
        srcPath = primarySource + "\\" + filename;
        in.open(srcPath);

        // 2. Try Secondary (C:\SMM) if Primary failed
        if (!in.is_open()) {
            srcPath = secondarySource + "\\" + filename;
            in.open(srcPath);
        }

        // 3. Try Current Directory
        if (!in.is_open()) {
            char path[MAX_PATH];
            if (GetModuleFileNameA(NULL, path, MAX_PATH)) {
                std::string exeDir = std::string(path);
                exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
                srcPath = exeDir + "\\" + filename;
                in.open(srcPath);
            }
        }

        if (!in.is_open()) {
            if (g_LogFile.is_open()) g_LogFile << "[Loader] ERROR: Could not find header: " << filename << std::endl;
            return false;
        }

        // 4. Process & Save
        std::ofstream out(profileDir + "\\" + filename);
        if (!out.is_open()) return false;

        std::string line;
        while (std::getline(in, line)) {
            // [PATCH] Fix Circular Dependency
            if (patchCircular && line.find("#include \"Pathfinding2.h\"") != std::string::npos) {
                out << "// [LOADER PATCH] Circular include removed\n";
                out << "struct PathNode;\n";
            }
            // [PATCH] Inject Global Logger Declaration
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

    bool CompileAndLoad(const std::string& profileCode, std::string& outError) {
        if (currentProfile) { currentProfile = nullptr; }
        if (hProfileDll) { FreeLibrary(hProfileDll); hProfileDll = nullptr; }

        CreateDirectoryA(profileDir.c_str(), NULL); 

        {
            std::ofstream save(profileDir + "\\LastProfile.txt");
            save << profileCode;
        }

        // 1. GENERATE LOG HELPER
        {
            std::ofstream fh(profileDir + "\\ForceHeader.h");
            fh << "#pragma once\n#include <fstream>\nextern std::ofstream g_LogFile;\n";
        }

        // 2. STAGE HEADERS
        std::vector<std::string> headers = {
            "Vector.h", "Entity.h", "dllmain.h", "ProfileInterface.h",
            "SimpleMouseClient.h", "SimpleKeyboardClient.h",
            "Pathfinding2.h", "MovementController.h",
            "Behaviors.h",
            "Profile.h",
            "json.hpp"
        };

        for (const auto& h : headers) {
            if (!FindAndStage(h)) {
                outError = "Missing Header: " + h;
                return false;
            }
        }

        if (!FindAndStage("WorldState.h", true)) { outError = "Missing WorldState.h"; return false; }

        // 3. GENERATE C++ SOURCE
        std::string srcPath = profileDir + "\\ActiveProfile.cpp";
        std::ofstream out(srcPath);

        // Globals & Includes
        out << "#include \"ForceHeader.h\"\n";
        out << "std::ofstream g_LogFile(\"C:\\\\SMM\\\\Profiles\\\\ProfileLog.txt\");\n";
        out << "class WorldState; extern WorldState* g_GameState;\n";
        out << "WorldState* g_GameState = nullptr;\n";

        out << "#include <Windows.h>\n#include <vector>\n#include <string>\n#include <cmath>\n";
        out << "#include \"Vector.h\"\n";
        out << "#include \"Entity.h\"\n";
        out << "class ProfileLoader;\n";
        out << "#include \"dllmain.h\"\n";
        out << "#include \"Profile.h\"\n";
        out << "#include \"WorldState.h\"\n";
        out << "#include \"Pathfinding2.h\"\n";
        out << "#include \"MovementController.h\"\n";
        out << "#include \"ProfileInterface.h\"\n";
        out << "#include \"Behaviors.h\"\n";

        out << "using BotSettings = ProfileSettings;\n";

        // Insert the User's C++ Code (Must define MyProfile)
        out << profileCode << "\n";

        // Generate Wrapper
        out << "class ProfileWrapper : public MyProfile {\n";
        out << "public:\n";
        out << "    void Setup(WorldState* state, ProfileSettings* settings) override {\n";
        out << "        g_GameState = state;\n";
        // This creates an instance of the user's settings class and copies it to the engine's pointer
        out << "        MySettings userSettings;\n";
        out << "        *settings = userSettings;\n";
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
        cmd << "cl.exe /D COMPILING_PROFILE /nologo /LD /MD /EHsc /O2 /std:c++17 /wd4566 /wd4005 /FI \"ForceHeader.h\" ";

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
            << "Detour.lib ";

        cmd << "> \"" << logPath << "\" 2>&1";

        int result = system(cmd.str().c_str());

        if (result != 0) {
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

// Config variables for FindAndStage...
std::string primarySource = "Z:\\VSProjects\\source\\repos\\MemoryTest\\SMM";
};