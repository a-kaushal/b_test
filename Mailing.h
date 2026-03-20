#pragma once
#include <windows.h>
#include <iostream>

#include "SimpleKeyboardClient.h"
#include "SimpleMouseClient.h"
#include "dllmain.h"

#include <filesystem> 

namespace fs = std::filesystem;

// --- PYTHON SCRIPT ---
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
import sys

# --- CONFIGURATION ---
# - We keep your path since you confirmed it works
pytesseract.pytesseract.tesseract_cmd = r'C:\Program Files\Tesseract-OCR\tesseract.exe'

def send_packet(pipe, cmd_type, x, y):
    try:
        data = struct.pack('iii', int(cmd_type), int(x), int(y))
        pipe.write(data)
        pipe.flush()
        pipe.read(1) # Wait for ACK
        print(f" -> Command {cmd_type} acknowledged.")
    except Exception as e:
        print(f"Pipe Error: {e}")

def find_text_center(text_to_find, instance = 1):
    print(f"Searching for '{text_to_find}' (Instance {instance})...")
    try:
        # 1. Take Screenshot
        screen = pyautogui.screenshot()
        if screen is None:
            print("ERROR: Screenshot returned None.")
            return None

        # 2. Convert to Numpy
        screen_np = np.array(screen)
        
        # 3. Convert to BGR (OpenCV format)
        img = cv2.cvtColor(screen_np, cv2.COLOR_RGB2BGR)
        
        # 4. Run Tesseract (Wrapped in Try/Catch)
        # We add 'print' debugging to pinpoint if this specific line crashes
        # print("Running image_to_data...") 
        d = pytesseract.image_to_data(img, output_type=Output.DICT, config='--psm 11')
        
        currentinstance = 1
        n_boxes = len(d['text'])
        
        for i in range(n_boxes):
            # Check for non-empty text
            txt = d['text'][i].strip().lower()
            if not txt: 
                continue
                
            if text_to_find.lower() in txt:
                # print(f"Found match: {txt}") # Debug print
                if currentinstance == instance:
                    x, y, w, h = d['left'][i], d['top'][i], d['width'][i], d['height'][i]
                    return (x + w // 2, y + h // 2)
                currentinstance += 1
        return None

    except pytesseract.TesseractError as e:
        print(f"!!! TESSERACT ERROR: {e}")
        return None
    except Exception as e:
        print(f"!!! CRASH in find_text_center: {e}")
        import traceback
        traceback.print_exc()
        return None

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

def main():
    print("Python: Connecting to C++ Pipe...")
    
    try:
        # Open pipe for Reading AND Writing ('r+b')
        with open(r'\\.\pipe\WowBotPipe', 'r+b', buffering=0) as pipe:
            
            # --- STEP 1: GROUPS ---
            coords = find_text_center("Groups")
            if coords:
                print(f"Found Groups at {coords}")
                send_packet(pipe, 0, coords[0], coords[1])
            else:
                print("Python Error: 'Groups' not found (1st call).")
                # Don't exit, just continue to keep window open for debug
            
            # --- STEP 2: DELAY ---
            print("Waiting 2s...")
            time.sleep(2)

            # --- STEP 3: SECOND SEARCH ---
            print("Starting Second Search...") 
            # coords_2 = find_text_center("Selected", instance = 2)
            coords_2 = find_text_center("Groups") 

            if coords_2:
                coords_2 = list(coords_2)

                x, y, width, height = get_wow_window_size()
                if width != None and height != None:
                    coords_2[0] += 0 / (1920 / width)
                    coords_2[1] += 614 / (1080 / height)

                print(f"Found Second Target at {coords_2}")
                send_packet(pipe, 1, coords_2[0], coords_2[1])
            else:
                print("Python Error: Second text not found.")

            # --- STEP 4: CHECK CONFIRMATION ---
            time.sleep(1)
            coords_3 = find_text_center("someone")
            if coords_3:
                coords_3 = list(coords_3)

                x, y, width, height = get_wow_window_size()
                if width != None and height != None:
                    coords_3[0] += -112 / (1920 / width)
                    coords_3[1] += 88 / (1080 / height)      

                # Loop 10 times, 5 seconds apart
                for i in range(10):
                    print(f"Clicking... ({i+1}/15)")
                    send_packet(pipe, 3, int(coords_3[0]), int(coords_3[1]))
                    time.sleep(5)
            else:
                # Not found -> Pause 15 seconds for mail to finish
                print("'someone' text NOT found. Pausing 15 seconds...")
                time.sleep(15)

            # Close connection
            send_packet(pipe, -1, 0, 0)
            
    except FileNotFoundError:
        print("Pipe not found. Is the C++ Bot running?")
    except Exception as e:
        print(f"CRITICAL MAIN ERROR: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(f"Fatal Script Error: {e}")
    
    # [IMPORTANT] This keeps the window open so you can read the error!
    #print("\nScript finished. Press Enter to close...")
    #input() 
)ScriptDelimiter";

// Protocol: [Command Type, X, Y]
struct CommandPacket {
    int type;
    int x;
    int y;
};

// Global variables to store the saved locations
int loc2_x = -1, loc2_y = -1;
int loc3_x = -1, loc3_y = -1;

std::string DropScriptToTemp() {
    fs::path tempDir = fs::temp_directory_path();
    fs::path scriptPath = tempDir / "wow_bot_internal.py";
    std::ofstream out(scriptPath);
    out << PYTHON_SCRIPT_CONTENT;
    out.close();
    return scriptPath.string();
}

void DeleteScript(std::string path) {
    try {
        if (fs::exists(path)) fs::remove(path);
    }
    catch (...) {}
}

bool PerformMailing(SimpleMouseClient& mouse) {
    if (g_LogFile.is_open()) g_LogFile << "Server: Creating Two-Way Pipe..." << std::endl;

    std::string scriptPath = DropScriptToTemp();

    // [FIX 1] Use SW_SHOWNOACTIVATE (Value 4)
    // This makes the window visible but DOES NOT steal focus from the game.
    // If you prefer it hidden, use SW_MINIMIZE (Value 6)
    HINSTANCE hRes = ShellExecuteA(
        NULL, "open", "python",
        scriptPath.c_str(),
        NULL, SW_MINIMIZE
    );

    // [FIX 2] Force Game Focus (Safety Net)
    // Immediately find the game window and force it to foreground, just in case.
    HWND hGameWindow = FindWindowA(NULL, "World of Warcraft");
    if (hGameWindow) {
        SetForegroundWindow(hGameWindow);
    }

    HANDLE hPipe = CreateNamedPipe(
        TEXT("\\\\.\\pipe\\WowBotPipe"),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 1024, 1024, 0, NULL
    );

    if (g_LogFile.is_open()) g_LogFile << "Server: Waiting for connection..." << std::endl;

    BOOL connected = ConnectNamedPipe(hPipe, NULL);

    if (connected) {
        if (g_LogFile.is_open()) g_LogFile << "Server: Python Connected." << std::endl;

        CommandPacket pkg;
        DWORD bytesRead, bytesWritten;
        char ack[] = "K";

        while (ReadFile(hPipe, &pkg, sizeof(pkg), &bytesRead, NULL)) {
            if (bytesRead == sizeof(pkg)) {
                if (pkg.type == -1) break;

                // [FIX 3] Ensure Game is Focused Before Clicking
                // Every time we get a command, make sure we aren't clicking the console
                if (hGameWindow && GetForegroundWindow() != hGameWindow) {
                    SetForegroundWindow(hGameWindow);
                    Sleep(50); // Small delay to let windows switch
                }

                switch (pkg.type) {
                case 0: // CLICK IMMEDIATE
                    mouse.MoveAbsolute(pkg.x, pkg.y);
                    mouse.Click(MOUSE_LEFT);
                    break;
                case 1: // SAVE LOCATION 2
                    loc2_x = pkg.x;
                    loc2_y = pkg.y;
                    mouse.MoveAbsolute(pkg.x, pkg.y);
                    Sleep(50);
                    mouse.Click(MOUSE_LEFT);
                    mouse.Click(MOUSE_LEFT);
                    break;
                case 2: // SAVE LOCATION 3
                    loc3_x = pkg.x;
                    loc3_y = pkg.y;
                    mouse.MoveAbsolute(pkg.x, pkg.y);
                    mouse.Click(MOUSE_LEFT);
                    break;
                case 3: // CONFIRMATION CLICK
                    loc3_x = pkg.x;
                    loc3_y = pkg.y;
                    mouse.MoveAbsolute(pkg.x, pkg.y);
                    mouse.Click(MOUSE_LEFT);
                    break;
                }
                WriteFile(hPipe, ack, 1, &bytesWritten, NULL);
            }
        }
    }

    CloseHandle(hPipe);
    Sleep(1000); // Give Python a second to finish printing
    DeleteScript(scriptPath);

    if (loc2_x != -1) return true;
    return false;
}