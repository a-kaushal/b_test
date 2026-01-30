import shutil
import os
import time
import psutil
from datetime import datetime
import winreg
import getpass
import win32net
import win32netcon
import ctypes
import subprocess
import re
import logging
import sys
import traceback


#NVIDIA DRIVER INSTALL
# #############
# --- SETTINGS ---
# TARGET_VERSION = "538.67"  # Desired NVIDIA driver version
# INSTALLER_PATH = r"Z:\Installers\538.67_grid_win10_win11_server2019_server2022_dch_64bit_international.exe"  # Update this path
# INSTALL_ARGS = ["-s", "/noreboot", "/clean"]
# CHECK_CMD = ["wmic", "path", "win32_videocontroller", "get", "driverVersion"]

# # --- STEP 1: Check for existing driver ---
# def get_current_driver_version():
#     try:
#         result = subprocess.run(CHECK_CMD, capture_output=True, text=True)
#         output = result.stdout.strip()
#         match = re.search(r"(\d+\.\d+)", output)
#         if match:
#             return match.group(1)
#     except Exception as e:
#         print(f"⚠️ Could not check current driver: {e}")
#     return None

# current_version = get_current_driver_version()
# print(f"🧩 Current NVIDIA driver version: {current_version or 'None detected'}")

# # --- STEP 2: Compare versions ---
# if current_version == TARGET_VERSION:
#     print(f"✅ Driver {TARGET_VERSION} is already installed. No action needed.")
#     exit(0)

# # --- STEP 3: Verify installer exists ---
# if not os.path.exists(INSTALLER_PATH):
#     print(f"❌ Installer not found at: {INSTALLER_PATH}")
#     exit(1)

# # --- STEP 4: Run silent installation ---
# print(f"🚀 Installing NVIDIA driver {TARGET_VERSION} silently...")

# args = [INSTALLER_PATH] + INSTALL_ARGS
# process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
# start_time = time.time()

# # Wait for process to finish
# while process.poll() is None:
#     elapsed = int(time.time() - start_time)
#     print(f"⏳ Installing... {elapsed}s elapsed", end="\r")
#     time.sleep(5)

# stdout, stderr = process.communicate()
# exit_code = process.returncode

# print("\n🧾 Installation finished.")
# print(f"Exit code: {exit_code}")

# # --- STEP 5: Evaluate result ---
# if exit_code == 0:
#     print("✅ NVIDIA driver installed successfully.")
# else:
#     print("⚠️ Installation may have failed.")
#     print(r"Check logs at: C:\Program Files\NVIDIA Corporation\Installer2\InstallerCore.log")
#     if stderr:
#         print(stderr.decode(errors="ignore"))
# time.sleep(1000)



# Constants
STD_INPUT_HANDLE = -10
ENABLE_QUICK_EDIT = 0x0040

class TeeOutput:
    def __init__(self, filename, exclude_strings=['WowClassic.exe is Running or Unknown', 'Inf', 'inf', 'nf'], indent=''):
        self.file = open(filename, 'a', buffering=1)
        self.stdout = sys.stdout
        self.last_message = ''
        self.indent = indent
        self.exclude_strings = exclude_strings or []
        self.buffer = ''

    def _process_buffer(self):
        message = self.buffer.rstrip('\n')
        self.buffer = ''
        
        # Check if the message contains any excluded strings
        if not any(exclude in message for exclude in self.exclude_strings):
            if message.strip():  # Only process non-empty messages
                timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                
                # Split message into lines and indent all but the first
                lines = message.splitlines()
                formatted_lines = [f"{timestamp} - {lines[0]}"]
                formatted_lines.extend(f"{' ' * len(timestamp)}   {self.indent}{line}" 
                                       for line in lines[1:])
                
                timestamped_message = '\n'.join(formatted_lines) + '\n'
                
                self.file.write(timestamped_message)
                self.file.flush()  # Flush after each write

    def write(self, message):
        if message != self.last_message:  # Avoid duplicate processing
            self.buffer += message
            if self.buffer.endswith('\n'):
                self._process_buffer()
            
            self.stdout.write(message)
            self.stdout.flush()  # Flush stdout as well
            self.last_message = message

    def flush(self):
        self.file.flush()
        self.stdout.flush()

def get_unpartitioned_space():
    # Get disk information
    disk_info = subprocess.check_output("wmic diskdrive get Index,Size /format:list", shell=True).decode()
    
    # Get partition information
    partition_info = subprocess.check_output("wmic partition get DiskIndex,Size /format:list", shell=True).decode()

    # Parse disk information
    disks = {}
    current_index = None
    for line in disk_info.splitlines():
        if line.strip():
            key, value = line.split('=')
            if key.strip() == 'Index':
                current_index = int(value)
                disks[current_index] = {'total': 0, 'used': 0}
            elif key.strip() == 'Size':
                disks[current_index]['total'] = int(value)

    # Parse partition information
    current_index = None
    for line in partition_info.splitlines():
        if line.strip():
            key, value = line.split('=')
            if key.strip() == 'DiskIndex':
                current_index = int(value)
            elif key.strip() == 'Size':
                if current_index in disks:
                    disks[current_index]['used'] += int(value)

    # Calculate and print unpartitioned space
    for index, info in disks.items():
        unpartitioned = info['total'] - info['used']
        return (unpartitioned / (1024**2))
        # print(f"Disk {index}:")
        # print(f"  Total size: {info['total'] / (1024**3):.2f} GB")
        # print(f"  Partitioned: {info['used'] / (1024**3):.2f} GB")
        # print(f"  Unpartitioned: {unpartitioned / (1024**3):.2f} GB")
        # print()

def extend_partition(drive_letter):
    extend_size_mb = get_unpartitioned_space()
    if extend_size_mb < 1000:
        return
    else:
        extend_size_mb = extend_size_mb - 20

    # Get the volume information
    volume_info = subprocess.check_output(f"wmic logicaldisk where DeviceID='{drive_letter}:' get Size,FreeSpace /value", shell=True).decode()
    
    # Extract current size and free space
    current_size = int(re.search(r"Size=(\d+)", volume_info).group(1))
    free_space = int(re.search(r"FreeSpace=(\d+)", volume_info).group(1))
    
    # Calculate new size
    new_size = current_size + (extend_size_mb * 1024 * 1024)
    
    # Extend the volume
    extend_command = f'powershell "Resize-Partition -DriveLetter {drive_letter} -Size {new_size}"'
    subprocess.run(extend_command, shell=True, check=True)
    
    print(f"Partition {drive_letter}: extended by {extend_size_mb} MB")

def get_max_password_age():
    server = None  # Use None for the local machine
    info = win32net.NetUserModalsGet(server, 0)
    return info['max_passwd_age']

def set_max_password_age(days):
    server = None  # Use None for the local machine
    info = win32net.NetUserModalsGet(server, 0)
    info['max_passwd_age'] = days * 86400  # Convert days to seconds
    win32net.NetUserModalsSet(server, 0, info)

def add_to_startup(file_path=""):
    USER_NAME = getpass.getuser()
    if file_path == "":
        file_path = os.path.dirname(os.path.realpath(__file__))
    bat_path = r'C:\Users\%s\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup' % USER_NAME
    shutil.copy(file_path, bat_path)

def is_in_startup(file_name):
    """Check if run_watchdog.bat or its shortcut is in the Windows Startup folder."""
    USER_NAME = getpass.getuser()
    batch_path = r'C:\Users\%s\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup\%s' % (USER_NAME, file_name)
    
    try:
        if os.path.exists(batch_path):
            return True
        return False
    except Exception as e:
        return False

def run_batch_script(file_path=""):
    """Run the run_watchdog.bat script."""
    try:
        if not os.path.exists(file_path):
            return False
        subprocess.run([file_path], shell=True)
        return True
    except Exception as e:
        return False

def set_run_as_admin(file_path):
    # Ensure the file path is absolute
    file_path = os.path.abspath(file_path)

    # Define the registry key path
    key_path = r"Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"

    try:
        # Open the registry key
        key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path, 0, winreg.KEY_ALL_ACCESS)
    except WindowsError:
        # If the key doesn't exist, create it
        key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, key_path)

    try:
        # Get the current value
        current_value, _ = winreg.QueryValueEx(key, file_path)
    except WindowsError:
        current_value = ""

    # Add RUNASADMIN if it's not already there
    if "RUNASADMIN" not in current_value:
        new_value = "RUNASADMIN " + current_value if current_value else "RUNASADMIN"
        winreg.SetValueEx(key, file_path, 0, winreg.REG_SZ, new_value.strip())
        print(f"'Run as administrator' option set for {file_path}")
    else:
        print(f"'Run as administrator' option was already set for {file_path}")

    # Close the key
    winreg.CloseKey(key)

def getTasks(names):
	r = os.popen('tasklist /v').read().strip().split('\n')
	#print ('# of tasks is %s' % (len(r)))
	res = []
	temp = []
	for name in names:
		for i in range(len(r)):
			s = r[i]
			if name in r[i][0:r[i].find(' ')]:
				if name == 'explorer.exe':
					temp.append(s)
				else:
					res.append(s)
					#print ('%s in r[i]' %(name))
					#return r[i]
					break
		if (name == 'explorer.exe') and (len(temp) > 1):
			res.append(temp[1])
		elif i == (len(r)-1):
			res.append('')
	return res

#######
# Disable QuickEdit mode
#######
# Load kernel32.dll
kernel32 = ctypes.windll.kernel32

# Set argument and return types for Windows API functions
kernel32.GetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
kernel32.GetConsoleMode.restype = ctypes.c_int
kernel32.SetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
kernel32.SetConsoleMode.restype = ctypes.c_int

# Get the handle to the standard input device (console input)
handle = kernel32.GetStdHandle(STD_INPUT_HANDLE)
if handle == -1:
    raise ctypes.WinError(ctypes.get_last_error())

# Retrieve the current console mode
current_mode = ctypes.c_ulong()
if kernel32.GetConsoleMode(handle, ctypes.byref(current_mode)) == 0:
    raise ctypes.WinError(ctypes.get_last_error())

# Disable the QuickEdit flag by bitwise AND with the inverse of ENABLE_QUICK_EDIT
new_mode = current_mode.value & ~ENABLE_QUICK_EDIT

# Apply the new console mode
if kernel32.SetConsoleMode(handle, new_mode) == 0:
    raise ctypes.WinError(ctypes.get_last_error())

print("QuickEdit mode has been disabled.")

#######
# Disable Hibernation
#######
os.system("powercfg -h off")
print("Hibernation has been disabled.")

# Update Windows Timezone and Time
os.system("tzutil /s \"AUS Eastern Standard Time\"")

# Redirect stdout to our custom TeeOutput
file1 = open(r'C:\Users\A\Desktop\bn.txt', 'r')
lines = file1.readlines()
for line in lines:
	if 'vGPU' in line:
		pc_name = line
file1.close()
sys.stdout = TeeOutput('Z:\\PythonScriptOutput\\output' + pc_name + '.log')

print(pc_name + ' ' + ' START')

# Example usage
extend_partition("C")  # Extend C: drive
add_to_startup(r'Z:\\open.bat')
# if not is_in_startup(r'start_watchdog.bat'):
#     add_to_startup(r'Z:\\Common Files\\start_watchdog.bat')
#     run_batch_script(r'Z:\\Common Files\\start_watchdog.bat')

# Removes run_watchdog.log and start_watchdog.bat from windows startup. Remove once files are fixed and working
startup_folder = os.path.join(os.environ['APPDATA'], r'Microsoft\Windows\Start Menu\Programs\Startup')
for file in os.listdir(startup_folder):
    file_path = os.path.join(startup_folder, 'run_watchdog.log')
    if os.path.isfile(file_path) and 'run_watchdog.log' in file.lower():
        print(f"Removing shortcut from Startup folder: {file_path}")
        os.remove(file_path)
    file_path = os.path.join(startup_folder, 'start_watchdog.bat')
    if os.path.isfile(file_path) and 'start_watchdog.bat' in file.lower():
        print(f"Removing shortcut from Startup folder: {file_path}")
        os.remove(file_path)

last_reboot = psutil.boot_time()
now = datetime.now()

#file_path = r"C:\Users\A\Desktop\PM\Launcher.exe"
#set_run_as_admin(file_path)

current_age = get_max_password_age()
print(f"Current maximum password age: {current_age // 86400} days")

if (current_age // 86400) > 10000:
    print("Password expiration is already disabled.")
else:
    try:
        set_max_password_age(4294967295 // 86400)
        print("Password expiration has been disabled successfully.")
    except Exception as e:
        print(f"An error occurred: {e}")

with open("main.py") as file:
    exec(file.read())