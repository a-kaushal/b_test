#pragma once
#include <windows.h>
#include <iostream>

#include "SimpleKeyboardClient.h"
#include "SimpleMouseClient.h"
#include "dllmain.h"

#include <filesystem> // To find the Temp folder

namespace fs = std::filesystem;


// --- 1. EMBED THE PYTHON SCRIPT AS A STRING ---
// formatting note: R"Delimiter( ...content... )Delimiter"
const std::string PYTHON_SCRIPT_CONTENT = R"ScriptDelimiter(
import cv2
import pytesseract
from pytesseract import Output
import pyautogui
import numpy as np
import time
import pygetwindow as gw
import struct
import os

def get_wow_window_size():
    # Attempt to find the window with the title "World of Warcraft"
    # Note: Window titles are case-sensitive in some libraries, but gw is usually flexible.
    try:
        # Returns a list of windows matching the title
        windows = gw.getWindowsWithTitle('World of Warcraft')
        
        if windows:
            wow_window = windows[0] # Grab the first match
            
            # Use these attributes
            x = wow_window.left
            y = wow_window.top
            width = wow_window.width
            height = wow_window.height
            
            print(f"--- Window Found ---")
            print(f"Title: {wow_window.title}")
            print(f"Top-Left Corner: ({x}, {y})")
            print(f"Size: {width}x{height}")
            print(f"Region Tuple: ({x}, {y}, {width}, {height})")
            
            # Check if the window is actually active/visible
            if wow_window.isMinimized:
                print("Warning: The window is currently minimized!")
                
            return x, y, width, height
        else:
            print("Could not find a window titled 'World of Warcraft'.")
            print("Make sure the game is running and the title matches exactly.")
            return None, None, None, None

    except Exception as e:
        print(f"An error occurred: {e}")
        return None, None, None, None

def send_packet(pipe, cmd_type, x, y):
    # Pack 3 Integers: [Type, X, Y]
    data = struct.pack('iii', cmd_type, x, y)
    pipe.write(data)
    pipe.flush()
    
    # Wait for C++ to say "OK"
    pipe.read(1)
    print(f" -> Command {cmd_type} acknowledged by C++.")
    
# --- CONFIGURATION ---
# Windows users must set this path. Mac/Linux can usually comment it out.
pytesseract.pytesseract.tesseract_cmd = r'C:\Program Files\Tesseract-OCR\tesseract.exe'

def find_text_center(text_to_find, instance = 1):
    print(f"Searching for '{text_to_find}'...")
    screen = pyautogui.screenshot()
    screen_np = np.array(screen)
    img = cv2.cvtColor(screen_np, cv2.COLOR_RGB2BGR)
    
    # '--psm 11' is crucial for finding text in game interfaces
    d = pytesseract.image_to_data(img, output_type=Output.DICT, config='--psm 11')
    
    currentinstance = 1
    n_boxes = len(d['text'])
    for i in range(n_boxes):
        if text_to_find.lower() in d['text'][i].strip().lower():
            if currentinstance == instance:
                x, y, w, h = d['left'][i], d['top'][i], d['width'][i], d['height'][i]
                return (x + w // 2, y + h // 2)
            currentinstance += 1
    return None

def main():
    print("Python: Connecting to C++ Pipe...")
    try:
        # Open pipe for Reading AND Writing ('r+b')
        with open(r'\\.\pipe\WowBotPipe', 'r+b', buffering=0) as pipe:
            
            # --- STEP 1: GROUPS ---
            coords = find_text_center("Groups")
            if coords:
                send_packet(pipe, 0, coords[0], coords[1])
            else:
                print("Python Error: 'Groups' not found.")
                return

            # --- STEP 2: DELAY ---
            print("Python: Waiting 2 seconds for menu animation...")
            time.sleep(2)

            # --- STEP 3: MAIL SELECTED GROUPS ---
            # Search for "Selected"
            coords_2 = find_text_center("Selected", instance = 2)

            if coords_2:
                send_packet(pipe, 1, coords_2[0], coords_2[1])
            else:
                print("Python Error: Second text not found.")

            # --- STEP 4: CHECK IF CONFIRMATION NEEDED ---
            time.sleep(1)
            confirm_popup = False
            coords_3 = find_text_center("someone")
            if coords_3:                
                x, y, width, height = get_wow_window_size()
                if width != None and height != None:
                    coords_3[0] += -112 / (1920 / width)
                    coords_3[1] += 88 / (1080 / height)

                send_packet(pipe, 3, coords_3[0], coords_3[1])
            else:
                print("Python Error: Confirmation text not found.")

            # Close connection
            send_packet(pipe, -1, 0, 0)
            
    except FileNotFoundError:
        print("Error: Run the C++ script FIRST!")

if __name__ == "__main__":
    main()
)ScriptDelimiter";


// Protocol: [Command Type, X, Y]
// Type 0 = Click Immediately
// Type 1 = Save Location 2
// Type 2 = Save Location 3 (if exists)
struct CommandPacket {
    int type;
    int x;
    int y;
};

// Global variables to store the saved locations
int loc2_x = -1, loc2_y = -1;
int loc3_x = -1, loc3_y = -1;

// --- 2. HELPER: DROP SCRIPT TO TEMP ---
std::string DropScriptToTemp() {
    // Get the system temp path (e.g. C:\Users\You\AppData\Local\Temp\)
    fs::path tempDir = fs::temp_directory_path();
    fs::path scriptPath = tempDir / "wow_bot_internal.py";

    std::cout << "[DLL] Dropping script to: " << scriptPath << std::endl;

    // Write the string to the file
    std::ofstream out(scriptPath);
    out << PYTHON_SCRIPT_CONTENT;
    out.close();

    return scriptPath.string();
}

// --- 3. HELPER: CLEANUP ---
void DeleteScript(std::string path) {
    try {
        if (fs::exists(path)) {
            fs::remove(path);
            std::cout << "[DLL] Temporary script deleted." << std::endl;
        }
    }
    catch (...) {}
}

bool PerformMailing(SimpleMouseClient& mouse) {
    g_LogFile << "Server: Creating Two-Way Pipe..." << std::endl;

    // STEP A: Drop the file
    std::string scriptPath = DropScriptToTemp();

    // STEP B: Launch Python pointing to the TEMP file
    // Note: Working directory is NULL (defaults to game), but script path is absolute.
    HINSTANCE hRes = ShellExecuteA(
        NULL, "open", "python",
        scriptPath.c_str(),
        NULL, SW_MINIMIZE
    );

    // STEP C: Create Pipe & Wait (Standard Logic)
    HANDLE hPipe = CreateNamedPipe(
        TEXT("\\\\.\\pipe\\WowBotPipe"),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 1024, 1024, 0, NULL
    );

    // 3. Wait for Python to connect
    g_LogFile << "Server: Waiting for connection..." << std::endl;
    BOOL connected = ConnectNamedPipe(hPipe, NULL);

    if (connected) {
        g_LogFile << "Server: Python Connected." << std::endl;

        CommandPacket pkg;
        DWORD bytesRead, bytesWritten;
        char ack[] = "K";

        while (ReadFile(hPipe, &pkg, sizeof(pkg), &bytesRead, NULL)) {
            if (bytesRead == sizeof(pkg)) {

                // Exit signal
                if (pkg.type == -1) break;

                switch (pkg.type) {
                case 0: // CLICK IMMEDIATE
                    g_LogFile << "[CMD] Click Immediate received." << std::endl;
                    mouse.MoveAbsolute(pkg.x, pkg.y);
                    mouse.Click(MOUSE_LEFT);
                    break;

                case 1: // SAVE LOCATION 2
                    loc2_x = pkg.x;
                    loc2_y = pkg.y;
                    g_LogFile << "[CMD] Location 2 SAVED: " << loc2_x << ", " << loc2_y << std::endl;
                    mouse.MoveAbsolute(pkg.x, pkg.y);
                    Sleep(50);
                    mouse.Click(MOUSE_LEFT);
                    mouse.Click(MOUSE_LEFT);
                    break;

                case 2: // SAVE LOCATION 3
                    loc3_x = pkg.x;
                    loc3_y = pkg.y;
                    g_LogFile << "[CMD] Location 3 SAVED: " << loc3_x << ", " << loc3_y << std::endl;
                    mouse.MoveAbsolute(pkg.x, pkg.y);
                    mouse.Click(MOUSE_LEFT);
                    break;
                }

                // Send Acknowledgement back to Python
                WriteFile(hPipe, ack, 1, &bytesWritten, NULL);
            }
        }
    }

    // Summary of what we captured
    g_LogFile << "\n--- FINAL MEMORY DUMP ---" << std::endl;
    g_LogFile << "Location 2: " << (loc2_x != -1 ? "Stored" : "Empty") << std::endl;
    g_LogFile << "Location 3: " << (loc3_x != -1 ? "Stored" : "Empty") << std::endl;

    CloseHandle(hPipe);
    // Wait a moment for Python to fully close before deleting the file
    Sleep(1000);
    DeleteScript(scriptPath);
    return 0;
}