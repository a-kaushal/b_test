import logged_pyautogui as pyautogui
import time
import sys
from misc import *
import os
import wexpect

def bootup():
    password_index = {
    "vGPU-02-D": "kesq89627@",
    "vGPU-02-N": "battlephil94",
    "vGPU-03-D": "tenleyhank",
    "vGPU-03-N": "alexisbest02",
    "vGPU-04-D": "mikeb99$",
    "vGPU-04-N": "wow_battle",
    "vGPU-05-D": "GsdR3$daw!HP!",
    "vGPU-05-N": "p@rt561!",
    "vGPU-06-D": "ty$56!adf",
    "vGPU-06-N": "jacklin$$123",
    "vGPU-07-D": "cdawgjens01",
    "vGPU-07-N": "56@ifg23",
    "vGPU-08-D": "lew!bn56",
    "vGPU-08-N": "jim_man_pass"
    }
    file1 = open(r'C:\Users\A\Desktop\bn.txt', 'r')
    lines = file1.readlines()
    for line in lines:
        if 'vGPU' in line:
            pc_name = line
    file1.close()

    res, loc = find_on_screen(r'Z:\PythonScreenshots\bn_icon_task.png', loc = 1, locateall = 1, inverse = 1)
    if res == False:
        print("Starting battle.net")
        time.sleep(3)
        while find_on_screen(r'Z:\PythonScreenshots\network_connected.png', inverse = 1) != True:
            time.sleep(5)
        time.sleep(5)
        #pyautogui.click(loc.left + (loc.width / 2), loc.top + (loc.height / 2))

        #os.startfile(r"C:\Program Files (x86)\Battle.net\Battle.net Launcher.exe")
        launch_battlenet_as_user("password")

        if find_on_screen(r'Z:\PythonScreenshots\bn_open.png', r'Z:\PythonScreenshots\bn_restart_update.png', either = 1) == True:
            time.sleep(5)
            while find_on_screen(r'Z:\PythonScreenshots\bn_open.png', r'Z:\PythonScreenshots\bn_restart_update.png', either = 1) != True:
                time.sleep(2)
            res, loc = find_on_screen(r'Z:\PythonScreenshots\bn_restart_update.png', inverse = 1, loc = 1)
            if res == True:
                #pyautogui.press('enter')
                pyautogui.click(loc.left + (loc.width / 2), loc.top + (loc.height / 2))
                time.sleep(5)
                find_on_screen(r'Z:\PythonScreenshots\bn_open.png')
            elif (find_on_screen(r'Z:\PythonScreenshots\bn_open.png', inverse = 1) != True) and (find_on_screen(r'Z:\PythonScreenshots\bn_login_error.png') == True):
                res, loc = find_on_screen(r'Z:\PythonScreenshots\bn_retry.png', loc = 1)
                pyautogui.click(loc.left + (loc.width / 2), loc.top + (loc.height / 2))
                find_on_screen(r'Z:\PythonScreenshots\bn_open.png')
                time.sleep(2)
                login(password_index[pc_name]) 
        time.sleep(2)
        login(password_index[pc_name])
        while find_on_screen(r'Z:\PythonScreenshots\bn_logged_in_dark.png', r'Z:\PythonScreenshots\bn_logged_in_ready.png', either = 1) != True:
            pyautogui.click(1000, 317)
            time.sleep(2)
            if find_on_screen(r'Z:\PythonScreenshots\bn_login_error.png', r'Z:\PythonScreenshots\bn_login.png', either = 1) == True:
                if find_on_screen(r'Z:\PythonScreenshots\bn_login.png', inverse = 1):
                    time.sleep(2)
                    login(password_index[pc_name])                
                if find_on_screen(r'Z:\PythonScreenshots\bn_login_error.png', inverse = 1):
                    res, loc = find_on_screen(r'Z:\PythonScreenshots\bn_retry.png', loc = 1)
                    pyautogui.click(loc.left + (loc.width / 2), loc.top + (loc.height / 2))
                    find_on_screen(r'Z:\PythonScreenshots\bn_open.png')
                    time.sleep(2)
                    login(password_index[pc_name])
            if find_on_screen(r'Z:\PythonScreenshots\bn_clientupdate.png') == True:
                res, l = find_on_screen(r'Z:\PythonScreenshots\bn_clientupdate_restart.png', loc = 1)
                if res == 1:
                    pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
                    while find_on_screen(r'Z:\PythonScreenshots\bn_login.png', inverse = 1) != True:
                        time.sleep(2)
                    time.sleep(2)
                    login(password_index[pc_name])
            # if find_on_screen(r'Z:\PythonScreenshots\bn_login_error.png') == True:
            #     res, loc = find_on_screen(r'Z:\PythonScreenshots\bn_retry.png', loc = 1)
            #     pyautogui.click(loc.left + (loc.width / 2), loc.top + (loc.height / 2))
            #     find_on_screen(r'Z:\PythonScreenshots\bn_open.png')
            #     time.sleep(2)
            #     login(password_index[pc_name])
            # elif find_on_screen(r'Z:\PythonScreenshots\bn_login.png', inverse = 1) == True:
            #     login(password_index[pc_name])
        if find_on_screen(r'Z:\PythonScreenshots\bn_logged_in_dark.png', inverse = 1) == True:
            pyautogui.click(1000, 317)
        if find_on_screen(r'Z:\PythonScreenshots\bn_logged_in_ready.png') != True:
            with open(LOG_PATH, 'r') as file1:
                Lines = file1.readlines()
            if len(Lines) > 70:
                for i in range(0, len(Lines)):
                    if i == len(Lines) - 1:
                        Lines[i] = "ERROR STARTING UP BATTLE.NET" + "\n"
                        break
                    else:
                        Lines[i] = Lines[i + 1]
            else:
                Lines.append("\n")
            with open(LOG_PATH, 'w') as file1:
                file1.seek(0)
                file1.writelines(Lines)
        else:
            print("Success")


def login(password):
    if find_on_screen(r'Z:\PythonScreenshots\bn_login_email.png', inverse = 1) == True:
        if find_on_screen(r'Z:\PythonScreenshots\bn_login_email_prompt.png', inverse = 1) == True:
            try:
                with open(r'C:\Users\A\Desktop\bn.txt', 'r') as file:
                    first_line = file.readline().strip()
            except FileNotFoundError:
                print("Error: File not found.")
                sys.exit()
            except Exception as e:
                print(f"Error: {str(e)}")
                sys.exit()
            pyautogui.click(489,260)
            time.sleep(1)
            pyautogui.write(first_line)
            time.sleep(1)
            pyautogui.press('enter')
        else:
            pyautogui.click(489,260)
            time.sleep(1)
            pyautogui.press('enter')
        find_on_screen(r'Z:\PythonScreenshots\bn_login.png')
        time.sleep(2)
        pyautogui.click(511,267)
        time.sleep(1)
    pyautogui.write(password)
    time.sleep(2)
    try:
        while find_on_screen(r'Z:\PythonScreenshots\bn_password_missing.png', inverse = 1) == True:
            if find_on_screen(r'Z:\PythonScreenshots\bn_new_version.png', inverse = 1) == True:
                pyautogui.press('enter')
                find_on_screen(r'Z:\PythonScreenshots\bn_open.png')
                time.sleep(2)
                pyautogui.write(password)
                time.sleep(2)
            if find_on_screen(r'Z:\PythonScreenshots\bn_open.png', inverse = 1) == True:
                pyautogui.write(password)
                time.sleep(2)
            else:
                time.sleep(5)
        res, l = find_on_screen(r'Z:\PythonScreenshots\bn_login.png', timewait = 60, loc = 1)
        while find_on_screen(r'Z:\PythonScreenshots\bn_login.png', inverse = 1) == True:
            time.sleep(2)
            res, l = find_on_screen(r'Z:\PythonScreenshots\bn_login.png', timewait = 60, loc = 1)
            #pyautogui.press('enter')
            pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
            time.sleep(3)
        #res, loc = find_on_screen(r'Z:\PythonScreenshots\bn_icon_task.png', timewait = 60, loc = 1, locateall = 1)
        res1 = False
        while res1 == False:
            res1, loc = find_on_screen(r'Z:\PythonScreenshots\bn_icon_task.png', inverse = 1, loc = 1, locateall = 1)
            res2 = find_on_screen(r'Z:\PythonScreenshots\bn_security_check.png', inverse = 1)
            if res2 == True:
                print("LOGIN EMAIL CHECK NEEDED")
                sys.exit()
        for l in loc:
            pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
            pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
    except:
        return

def find_battlenet_path():
    """
    Find Battle.net executable path
    """
    common_paths = [
        r"C:\Program Files (x86)\Battle.net\Battle.net Launcher.exe",
        r"C:\Program Files\Battle.net\Battle.net Launcher.exe"
    ]
    
    for path in common_paths:
        if os.path.exists(path):
            return path
    
    return None

def launch_battlenet_as_user(password):
    """
    Launch Battle.net as Windows user with automated password entry
    """
    battlenet_path = find_battlenet_path()
    
    if not battlenet_path:
        print("Error: Battle.net executable not found.")
        return False

    try:
        # Prepare the command to launch Battle.net as Windows user
        command = f'runas /user:PC "{battlenet_path}"'
        
        # Use wexpect to handle command-line interaction
        child = wexpect.spawn(command)
        
        # Expect password prompt and send password
        result = child.expect([
            'Enter the password for PC:',  # Windows 10/11 prompt
            'Password for Windows:',       # Alternative prompt
            wexpect.EOF,                   # Process ended
            wexpect.TIMEOUT                # No response
        ])
        
        if result == 0 or result == 1:
            # Send password and press enter
            child.sendline(password)
            
            # Optional: wait for process to complete
            child.expect(wexpect.EOF)
            
            print("Battle.net launch command sent.")
            return True
        elif result == 2:
            print("Process ended unexpectedly.")
            return False
        else:
            print("Timeout waiting for password prompt.")
            return False
    
    except Exception as e:
        print(f"Error launching Battle.net: {e}")
        return False