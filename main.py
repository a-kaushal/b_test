import win32gui
import re
import os
import signal
import re
import time
import subprocess
import psutil
import logged_pyautogui as pyautogui
import pyperclip
from pywinauto import application
import win32con
from datetime import datetime
from datetime import timedelta
import wmi
import random
import math
from pm_logs import *
from pm_window import *
from misc import *
import getpass
import shutil
import sys
import importlib
import json
import cv2
from bn_start import *
import ctypes
import traceback
spirit_healer_revive()

dst_path = r'C:\Program Files (x86)\World of Warcraft\_classic_\Interface\AddOns\AutoGear'
src_path = r'Z:\AddOns\AutoGear'
if os.path.exists(dst_path):
    shutil.rmtree(dst_path)
#os.mkdir(dst_path)
shutil.copytree(src_path, dst_path)

if not ctypes.windll.shell32.IsUserAnAdmin():
    ctypes.windll.shell32.ShellExecuteW(None, "runas", sys.executable, f'"{__file__}" "{sys.argv[1]}" if len(sys.argv) > 1 else __file__', None, 1)
else:
    #subprocess.run(sys.argv[1] if len(sys.argv) > 1 else r'Z:\Windows Setup\auto_timezone.bat', shell=True)
    subprocess.run(r"Z:\Windows Setup\enable_bsod_auto_restart.bat", shell=True)

# Copy pixelmaster options and options.py file and Profiles folder
#shutil.copy(r'Z:\Settings\QuestingSettings.json', r'C:\Users\A\AppData\Local\PixelMaster\Settings\QuestingSettings.json')
shutil.copy(r'Z:\Settings\QuestingSettings.json', r'C:\Users\A\Desktop\PM\Configs\Settings\QuestingSettings.json')
shutil.copy(r'Z:\Settings\AutoTrainSettings.json', r'C:\Users\A\Desktop\PM\Configs\Settings\AutoTrainSettings.json')
shutil.copy(r'Z:\Settings\ItemDestroyerSettings.json', r'C:\Users\A\Desktop\PM\Configs\Settings\ItemDestroyerSettings.json')
shutil.copy(r'Z:\Settings\AdvancedCombatClasses_MoPSettings.json', r'C:\Users\A\Desktop\PM\Configs\Settings\AdvancedCombatClasses_MoPSettings.json')

#Disable autoFM if needed
with open(r'C:\Users\A\Desktop\PM\Configs\Settings\AppSettings.json', 'r+') as f:
	data = json.load(f)
	if (PLUGIN_STATUS[0] == "off") and (data['DisabledPlugins'] == None):
		data['DisabledPlugins'] = ['AutoFMs']
	if (PLUGIN_STATUS[0] == "on") and (data['DisabledPlugins'] != None):
		data['DisabledPlugins'] = None
	f.seek(0)        # <--- should reset file position to the beginning.
	json.dump(data, f, indent=4)
	f.truncate()     # remove remaining part

#shutil.copy(r'Z:\Settings\AppSettings.json', r'C:\Users\A\AppData\Local\PixelMaster\Settings\AppSettings.json')
shutil.copytree(r'Z:\\Common Files\\Profiles', r'C:\Users\A\Desktop\PM\Profiles', dirs_exist_ok=True)
#for i in range(0, len(SYS_NAME)):
file1 = open(r'C:\Users\A\Desktop\bn.txt', 'r')
lines = file1.readlines()
for line in lines:
	if 'vGPU' in line:
		pc_name = line
file1.close()
shutil.copy(r'Z:\\Common Files\\Bot Options\\options' + pc_name + '.py', r'C:\Users\A\Desktop\PM')
shutil.move(r'C:\Users\A\Desktop\PM\options' + pc_name + '.py', r'C:\Users\A\Desktop\PM\options.py')
sys.path.insert(0, r"C:\Users\A\Desktop\PM")

from options import *

cmd_hwnd = 0

def winEnumHandler(hwnd, ctx):
	global cmd_hwnd
	if win32gui.IsWindowVisible(hwnd):
		if "cmd" in win32gui.GetWindowText(hwnd):
			cmd_hwnd = hwnd 

#def main_loop():
last_reboot = psutil.boot_time()
now = datetime.now()
if (datetime.timestamp(now) - last_reboot < 1800):
	bootup()

imgNames = ['WowClassic.exe', 'PixelMaster.exe', 'Discord.exe', 'steam.exe', 'notepad.exe', 'explorer.exe', 'BlizzardError.exe']
battle_exe = r"C:\Program Files (x86)\Battle.net\Battle.net.exe"
pixel_exe = r"C:\Users\A\Desktop\PM\PixelMaster.exe"

r = getTasks(imgNames)

file1 = open(r'C:\Users\A\Desktop\bn.txt', 'r')
lines = file1.readlines()
for line in lines:
	if 'vGPU' in line:
		pc_name = line
file1.close()

# Update Max FPS in WOW
#update_max_fps(30)

if (r[0] == ''):
	try:
		shutil.copytree('Z:\AddOns\Dejunk', 'C:\Program Files (x86)\World of Warcraft\_classic_\Interface\AddOns\Dejunk')
	except:
		print("Dejunk already exists")

	start_dir = 'C:\Program Files (x86)\World of Warcraft\_classic_\WTF\Account'  # Replace with the actual starting directory
	target_dir_name = SERVER_NAME[0]  # Replace with the name of the directory you want to find
	target_file_name = "TradeSkillMaster.lua"  # Replace with the name of the text file you want to open

	found_dir = find_directory(start_dir, target_dir_name)
	if found_dir:
		# Opens, copies and edits TSM config files
		open_file(found_dir + '\SavedVariables', target_file_name, 0)

		shutil.copy(r'Z:\\AddOns\\Dejunk.lua', found_dir + '\SavedVariables')

		# Edit bindings
		with open(found_dir + '\\bindings-cache.wtf', 'r', encoding='utf-8') as file:
			content = file.read()

		# # Check if the text already exists in the file
		# if "bind SHIFT-D DEJUNK_DESTROY_NEXT_ITEM" in content:
		# 	print("Text already exists in the file. No changes made.")

		# # Append the text to the end of the file
		# else:
		# 	with open(found_dir + '\\bindings-cache.wtf', 'a', encoding='utf-8') as file:
		# 		file.write("bind SHIFT-D DEJUNK_DESTROY_NEXT_ITEM\n")
		# 		print(f"Text successfully appended to bindings-cache.wtf")

		# Check if the text already exists in the file
		if "bind ` TOGGLEFPS" in content:
			print("Text already exists in the file. No changes made.")

		# Append the text to the end of the file
		else:
			with open(found_dir + '\\bindings-cache.wtf', 'a', encoding='utf-8') as file:
				file.write("bind ` TOGGLEFPS\n")
				print(f"Text successfully appended to bindings-cache.wtf")
	else:
		print(f"Directory '{target_dir_name}' not found in {start_dir}")
	shutil.copy(r'Z:\\Settings\\Config.wtf', r'C:\Program Files (x86)\World of Warcraft\_classic_\WTF')

# if os.path.exists(r'C:\Users\A\Desktop\PM\Configs\AppData\KnownFPs.json'):
# 	print("File KnownFPs.json exists!")
# elif faction[0].lower() == "horde":
# 	with open(r'C:\Users\A\Desktop\PM\Configs\AppData\KnownFPs.json', 'w') as file:
# 		file.write('{"' + SERVER_NAME[0] + '-' + CHAR_NAME[0] + '":[{"TaxiNodeId":402,"FlightMasterId":40809},{"TaxiNodeId":439,"FlightMasterId":41140},{"TaxiNodeId":404,"FlightMasterId":41142}]}')
# 	print("Created new file: KnownFPs.json")

win32gui.EnumWindows(winEnumHandler, None)
if cmd_hwnd != 0:
	win32gui.ShowWindow(cmd_hwnd, win32con.SW_MAXIMIZE)

#x = threading.Thread(target=screen_record)
#x.start()

#time.sleep(3600)
driver = None
history = []
crashes = 0
stuck_restarts = 0
elapsed_time = 60000
started = 0
running = 1
profile1 = 0
profile2 = 0
profile3 = 0
restarted = 0
char_selected = 0
char_not_found_cnt = 0
# active_profile = PROFILE_NAME[0][0]
# active_profile_personal = PERSONAL_PROFILE_NAME[0][0]
# active_profile_path = PERSONAL_PROFILE_PATH[0][0]
active_profile = []
active_profile_personal = []
active_profile_path = []
active_profile_old = ""
log_status = 0

last_log = datetime.now()
new_profile = -1
movement = ['w', 's', 'w', 's']
#movement = ['s', 's', 's', 's']
movement_pos = 0
pm_not_responding = 0
screenshot_number = 0
screenshot_count = 0
screenshot_1 = []
screenshot_2 = []
resurrect_count = 0
bot_switch = 0
settingswap = 1

current_day = datetime.now().day
r1 = random.uniform(-1, 1)
r2 = random.uniform(-1, 1)

interact_check = datetime.now() - timedelta(seconds=6000)
fatigue_timer = datetime.now() - timedelta(seconds=6000)
resurrect_start_time = datetime.now() - timedelta(seconds=6000)
travel_timer = None
failed_start = datetime.now()
failed_count = 0
in_air_check_start = datetime.now()
in_air_check_count = 0
restock_timer = 20
restock_start = 0
stuck_start = datetime.now()
restart_time = datetime.now()
trainer_time = datetime.now() - timedelta(seconds=6000)
restart_hearthstone = 0
in_combat = 0
last_restock = 0
error_finish = 0
stuck_count = 0
stopped_count = 0
start_move_check = None
move_check_count = 0
current_restock = last_restock
action = np.zeros(100)
vendor_cooldown = 300
repaired = 600
error_count = 0
error_time = datetime.now()
error_log_time = datetime.now()
log_time = None
bot_number = -1
old_bot_number = -1
restart_count = 0
restart_reset = 0
restart = 0
transport_timer = 0
run_time = 0
screengrab = 0
last_error = None
override = 0
health_stuck_cnt = 0
health_check = 0
health_check_start = -1
health_check_screenshot1 = None
health_check_screenshot2 = None
in_game = 0
ignore = 0
attempt = 0
mail_task = 0
mail_task_done = 0
first_run = 0
bot_switch_ready = 0
in_game_check = 0
mail_task_time = 180
attempt_start = datetime.now()
mail_override = 0
thr = None
past_log = 0
navmesh_check = datetime.now()
char_select_failed = 0
read_type = "OCR"
flying_cooldown = 180
error_restart = 0

notResponding = 'Not Responding'
screenshot_1 = None
if (len(sys.argv) == 3) and (sys.argv[1] == '1'):
	close_pm_and_wotlk(pixel_exe, battle_exe, cmd_hwnd, bot_number = int(sys.argv[2]), start = 0, combat_type = COMBAT[int(sys.argv[2])-1], combat_class = COMBAT_CLASS[int(sys.argv[2])-1])
	time.sleep(10)

for i in range(0, NUM_BOTS):
	active_profile.append(PROFILE_NAME[i][0])
	active_profile_personal.append(PERSONAL_PROFILE_NAME[i][0])
	active_profile_path.append(PERSONAL_PROFILE_PATH[i][0])

# Set profile type (Done due to opening settings in PM causing app crash)
with open(r'C:\Users\A\Desktop\PM\Configs\Settings\QuestingSettings.json', 'r') as file:
    data = json.load(file)
if 'CurrentProfile' in data:
    data['CurrentProfile'] = PM_SETTINGS[bot_number - 1]
with open(r'C:\Users\A\Desktop\PM\Configs\Settings\QuestingSettings.json', 'w') as file:
    json.dump(data, file, indent = 4)

while True:
	prev_time = datetime.now()
	try:
		#bot_number = 1
		#new_profile = pixelmaster_custom_script(1, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
		
		# RESTARTS SCRIPT IF RESTART BUTTON PRESSED ON WEB SERVER. HANDLED BY WATCHDOG SCRIPT NOW
		# files=[f for f in os.listdir(r"Z:\httpd") if os.path.isfile(os.path.join(r"Z:\httpd", f))]
		# for f in files:
		# 	if "py_restart" + str(int(re.search(r'\d+', SCREENSHOT_PATH).group())) in f:
		# 		os.remove(r"Z:\\httpd\\" + f)
		# 		print("Resetting Script")
		# 		os.execv(sys.executable, ['python', sys.argv[0]])
		# 		sys.exit()


		# if restart == 1:
		# 	restart_count += 1
		# 	restart = 0
		# 	if restart_count >= 3:
		# 		close_pm_and_wotlk(pixel_exe, battle_exe, cmd_hwnd, active_profile[bot_number-1], active_profile_personal[bot_number-1], active_profile_path[bot_number-1], pids, bot_number)
		# 		action = np.zeros(100)
		# 		restart_count = 0
		if restart_reset == 5:
			if restart_count > 0:
				restart_count -= 1
			restart_reset = 0

		if (run_time != 0) and (len(run_time) > 4) and ((int(run_time[3]) >= 4) or (int(run_time[2]) >= 1)):
			if profile_web_action(browser, 'stop', bot_number, read_type) == True:
				time.sleep(5)
				try:
					pyautogui.keyDown('alt')
					time.sleep(0.2)
					pyautogui.press('f3')
					time.sleep(0.2)
				finally:
					pyautogui.keyUp('alt')
				time.sleep(5)

		if bot_number == -1:
			for i in range(0, NUM_BOTS):
				temp, Lines, comp_check = rw_file("r", ["Active Profile" + str(i), "Active Personal" + str(i)])
				if (temp[0] != None) and (temp[0] in PROFILE_NAME[i]):
					if comp_check[0] == True:
						for j in range(0, len(PROFILE_NAME[i])):
							if temp[0] in PROFILE_NAME[i][j]:
								if len(PROFILE_NAME[i]) > (j + 1):
									active_profile[i] = PROFILE_NAME[i][j+1]
								else:
									active_profile[i] = ""
					else:
						active_profile[i] = temp[0]
				else:
					written = 0
					j = 0
					for line in Lines:
						if "Active Profile" in line:
							written = 1
							Lines[j] = "Active Profile" + str(i) + ": " + active_profile[i]
						j += 1
					if written == 0:
						Lines.append("Active Profile" + str(i) + ": " + active_profile[i])
					rw_file("w", Lines)

				if (temp[1] != None) and (temp[1] in PERSONAL_PROFILE_NAME[i]):				
					if comp_check[1] == True:
						for j in range(0, len(PERSONAL_PROFILE_NAME[i])):
							if temp[1] in PERSONAL_PROFILE_NAME[i][j]:
								if len(PERSONAL_PROFILE_NAME[i]) > (j + 1):
									active_profile_personal[i] = PERSONAL_PROFILE_NAME[i][j+1]
									active_profile_path[i] = PERSONAL_PROFILE_PATH[i][j+1]
								else:
									active_profile_personal[i] = ""
									active_profile_path[i] = ""
					else:
						active_profile_personal[i] = temp[1]
						active_profile_path[i] = PERSONAL_PROFILE_PATH[i][PERSONAL_PROFILE_NAME[i].index(temp[1])]
				else:
					written = 0
					j = 0
					for line in Lines:
						if "Active Personal" in line:
							written = 1
							Lines[j] = "Active Personal" + str(i) + ": " + active_profile_personal[i]
						j += 1
					if written == 0:
						Lines.append("Active Personal" + str(i) + ": " + active_profile_personal[i])
					rw_file("w", Lines)
		time_now = datetime.now()
		if time_now.day > current_day:			
			r1 = random.uniform(-1, 1)
			r2 = random.uniform(-1, 1)
			current_day = time_now.day
		if LOWER_HOUR > UPPER_HOUR:
			if LOWER_HOUR - 1 > time_now.hour:
				lowerbound = 0
				upperbound = ((UPPER_HOUR + r1) * 60.0)
			else:
				lowerbound = ((LOWER_HOUR - 1) * 60.0)
				upperbound = (24*60)
		else:
			upperbound = ((UPPER_HOUR + r1) * 60.0)
			lowerbound = ((LOWER_HOUR - 1) * 60.0)
		if ((time_now.hour * 60) + time_now.minute) >= lowerbound:
			if (((time_now.hour * 60) + time_now.minute) <= upperbound) and (((time_now.hour * 60) + time_now.minute) >= lowerbound):
				if (bot_number == 2) and (NUM_BOTS > 1):
					bot_switch = 1
				old_bot_number = bot_number
				bot_number = 1
			elif (((time_now.hour * 60) + time_now.minute) > upperbound + 10) and (((time_now.hour * 60) + time_now.minute) < lowerbound + 10):
				old_bot_number = 1
				bot_switch_ready = 1
				bot_number = -1
				bot_switch = 1
			elif (NUM_BOTS <= 1) and (bot_switch_ready == 0):
				#bot_number = -1
				r = getTasks(imgNames)
				if (r[0] == '') or ((r[1] == '') and (r[2] == '') and (r[3] == '') and (r[4] == '') and (r[5] != '')):
					old_bot_number = 1
					bot_switch_ready = 1
					bot_number = -1
				else:
					bot_switch_ready = 1
					bot_number = 1
				bot_switch = 1
				print("Waiting for next bot cycle")
			elif bot_switch_ready == 0:
				if bot_number == 1:
					bot_switch = 1
				old_bot_number = bot_number
				bot_number = 2
		else:
			if (bot_number == 2) and (NUM_BOTS > 1):
				bot_switch = 1
			old_bot_number = bot_number
			bot_number = 1
		if (bot_switch == 0) and (bot_number != None):			
			running = 1
			if elapsed_time >= 30:
				restart_reset += 1

				# Shift existing screenshots: 0->1, 1->2, etc.
				for i in range(2, 0, -1):
					old_file = f"{SCREENSHOT_PATH}{i-1}.png"
					new_file = f"{SCREENSHOT_PATH}{i}.png"
					if os.path.exists(old_file):
						# If a previous screenshot exists at destination, remove it
						if os.path.exists(new_file):
							os.remove(new_file)
						# Move the file to its new position
						shutil.move(old_file, new_file)
				# TAKE SCREENSHOTS OF SCREEN EVERY MINUTE
				myScreenshot = pyautogui.screenshot()
				myScreenshot.save(SCREENSHOT_PATH + '0.png')
				screenshot_number += 1
				if screenshot_number > 2:
					screenshot_number = 0

				# TAKE SCREENSHOT AND COMPARE WITH LAST MINUTE TO CHECK IF STUCK
				if ignore == 0:
					if ((elapsed_time != 60000) and (move_check_count == 0)) or (override == 1):
						override = 0
						if find_on_screen(r'Z:\PythonScreenshots\in_game.png', r'Z:\PythonScreenshots\in_game_cata.png', either = 1, inverse = 1) != True:
							in_game_check += 1
							if in_game_check == 10:
								print("Game not detected for 5 minutes. Restarting everything")
								close_pm_and_wotlk(pixel_exe, battle_exe, cmd_hwnd, active_profile[bot_number-1], active_profile_personal[bot_number-1], active_profile_path[bot_number-1], pids, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
								time.sleep(2)
								transport_timer = 0
						else:
							in_game_check = 0

						screenshot_2 = check_movement()
						diff = datetime.now() - attempt_start
						if (attempt >= 1) and (diff.seconds > 300):
							attempt = 0
							screenshot_count = 0

						if (screenshot_1 == screenshot_2):
							screenshot_count += 1

						if ((screenshot_1 == screenshot_2) and (transport_timer == 0) and attempt == 1) or (in_game == 2):
							if profile_web_action(browser, 'stop', bot_number, read_type) == True:
								try:
									pyautogui.keyDown(movement[movement_pos])
									time.sleep(1)
									pyautogui.press('space')
									time.sleep(0.1)
								finally:
									pyautogui.keyUp(movement[movement_pos])
								movement_pos += 1
								if movement_pos == 4:
									movement_pos = 0
								print("No movement for 2 minutes. Restarting everything")
								close_pm_and_wotlk(pixel_exe, battle_exe, cmd_hwnd, active_profile[bot_number-1], active_profile_personal[bot_number-1], active_profile_path[bot_number-1], pids, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
								time.sleep(2)
								transport_timer = 0
								attempt = 2
								screenshot_count = 0
								screenshot_1 = None

						elif (screenshot_1 == screenshot_2) and (transport_timer == 0) and (screenshot_count > 3):
							print("No movement for 2 minutes. Retrying")
							stop_running_profile()
							time.sleep(1)
							try:
								pyautogui.keyDown('s')
								time.sleep(1)
								pyautogui.press('space')
								time.sleep(0.1)
							finally:
								pyautogui.keyUp('s')
							time.sleep(2)
							res, loc = find_on_screen(r'Z:\PythonScreenshots\wow_corpse_stuck.png', inverse = 1, loc = 1)
							if res == True:
								pyautogui.click(loc.left + (loc.width / 2), loc.top + (loc.height / 2))
								time.sleep(5)
							try:
								pyautogui.keyDown('alt')
								time.sleep(0.2)
								pyautogui.press('f3')
								time.sleep(0.2)
							finally:
								pyautogui.keyUp('alt')
							time.sleep(5)
							attempt = 1
							attempt_start = datetime.now()
							screenshot_count = 0
							screenshot_1 = None
						
						elif (screenshot_1 == screenshot_2) and (transport_timer == 0) and (attempt == 2):
							sys.exit()
						screenshot_1 = screenshot_2
				else:
					ignore = 0

				#func_timeout(0, 5, pixel_exe, battle_exe, cmd_hwnd, bot_number)
				r = getTasks(imgNames)
				pids = []
				if (r[1] != '') or (r[2] != '') or (r[3] != '') or (r[4] != '') or (r[5] != ''):
					pm_loc, pm_res = find_pm_loc(exit = 1)
					if (pm_res == False):
						close_pm(pixel_exe, cmd_hwnd, bot_number, force = 1)
						time.sleep(5)
						r = getTasks(imgNames)

				for process in r:
					#print(process + '\n')
					pids.append(find_first_number(process))

				# Check for Server disconnects
				if check_text_in_region(469, 423, 554, 439, "reconnect", [], [], "", wait = 1) == True:
					print("Disconnected from server")
					close_pm_and_wotlk(pixel_exe, battle_exe, cmd_hwnd, active_profile[bot_number-1], active_profile_personal[bot_number-1], active_profile_path[bot_number-1], pids, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], closed_app = "WOW")
					time.sleep(2)
					transport_timer = 0

				if find_on_screen(r'Z:\PythonScreenshots\wotlk_multiple_window.png', r'Z:\PythonScreenshots\wotlk_multiple_window_highlighted.png', either = 1, inverse = 1, region = (406, 722, 388, 46)) == True:
					# Kill WOTLK
					try:
						subprocess.check_output("Taskkill /PID %d /F" % pids[0])
					except Exception as e:
						print(e)
					#subprocess.check_output("Taskkill /PID %d /F" % pids[2])
					time.sleep(10)
					close_pm(pixel_exe, cmd_hwnd, bot_number)
					# Launch PM
					login_pm(pixel_exe, cmd_hwnd, bot_number, settingswap = 1, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
					# Launch WOTLK
					wow_hwnd = start_wotlk(battle_exe, bot_number, cmd_hwnd)
					new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
					new_profile = pixelmaster_custom_script(1, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
				
				# if check_disconnect() == 1:
				# 	close_pm_and_wotlk(pixel_exe, battle_exe, cmd_hwnd, active_profile[bot_number-1], active_profile_personal[bot_number-1], active_profile_path[bot_number-1], pids, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				
				if ('Not Responding' in r[0]) or ('Not Responding' in r[1]) or ('Not Responding' in r[2]) or ('Not Responding' in r[3]) or ('Not Responding' in r[4]) or ('Not Responding' in r[5]):
					if 'Not Responding' in r[0]:
						print('%s is Not responding' % (imgNames[0]))
						# Kill WOTLK
						wow_nr = 0
						while 'Not Responding' in r[0]:
							if wow_nr == 5:
								# Kill PM
								close_pm(pixel_exe, cmd_hwnd, bot_number)
							if wow_nr >= 5:
								try:
									subprocess.check_output("Taskkill /PID %d /F" % pids[0])
								except Exception as e:
									print(e)
							r = getTasks(imgNames)
							pids = []
							for process in r:
								print(process + '\n')
								pids.append(find_first_number(process))
							wow_nr += 1
							time.sleep(3)
						if wow_nr >= 5:
							time.sleep(10)
							# Launch PM
							login_pm(pixel_exe, cmd_hwnd, bot_number, settingswap = 1, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
							# Launch WOTLK
							wow_hwnd = start_wotlk(battle_exe, bot_number, cmd_hwnd)
							new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1],)
							new_profile = pixelmaster_custom_script(1, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
					# elif ('Not Responding' in r[1]) or ('Not Responding' in r[2]) or ('Not Responding' in r[3]) or ('Not Responding' in r[4]) or ('Not Responding' in r[5]):
					# 	print('%s is Not responding' % (imgNames[1]))
					# 	if pm_not_responding == 1:							
					# 		stop_running_profile()
					# 		time.sleep(1)
					# 		pyautogui.press('s')
					# 		close_pm(pixel_exe, cmd_hwnd, bot_number)
					# 		# Launch PM
					# 		login_pm(pixel_exe, cmd_hwnd, bot_number, settingswap = 1, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
					# 		# Launch WOTLK
					# 		new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
					# 		new_profile = pixelmaster_custom_script(1, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
					# 		pm_not_responding = 0
					# 	pm_not_responding += 1
					crashes += 1

				elif (r[0] == '') or ((r[1] == '') and (r[2] == '') and (r[3] == '') and (r[4] == '') and (r[5] == '')):
					if r[0] != '':
						print('%s - No such process. Starting now' % (imgNames[1]))
						# Move character forward a bit in case stuck in spot
						try:
							pyautogui.keyDown(movement[movement_pos])
							time.sleep(1)
							pyautogui.press('space')
							time.sleep(1)
						finally:
							pyautogui.keyUp(movement[movement_pos])
						movement_pos += 1
						if movement_pos == 4:
							movement_pos = 0
						# Launch PM
						login_pm(pixel_exe, cmd_hwnd, bot_number, settingswap = 1, swapvalue = PM_SETTINGS[bot_number - 1], combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], extra_setting = ADDITIONAL_SETTINGS[0])
						new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
						new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)

						settingswap = 0

					if r[0] == '':
						print('%s - No such process. Starting now' % (imgNames[0]))
						# Launch PM
						login_pm(pixel_exe, cmd_hwnd, bot_number, settingswap = 1, swapvalue = PM_SETTINGS[bot_number - 1], combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], extra_setting = ADDITIONAL_SETTINGS[0])
						settingswap = 0
						# Launch WOTLK
						wow_hwnd = start_wotlk(battle_exe, bot_number, cmd_hwnd)
						# SET MAIL TASK IF IN FIRST 30 MIN
						if "gathering" in PERSONAL_PROFILE_NAME[0][0] and "hinterlands" in PERSONAL_PROFILE_NAME[0][0] and ((((time_now.hour * 60) + time_now.minute) >= lowerbound) and (((time_now.hour * 60) + time_now.minute) <= (lowerbound + mail_task_time)) and mail_task_done == 0):
							cancel_pm_profile(1, cmd_hwnd, 1, combat_type = COMBAT[1-1], combat_class = COMBAT_CLASS[1-1], rot_type = ROTATION_TYPE[1-1])
							state = pixelmaster_custom_script(1, MAIL_TASK_NAME[bot_number-1], "GoTo", cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
							mail_task = 1
							mail_task_done = 1
						else:
							#if profile_check(cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1]) == False:
							state = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, screengrab = screengrab, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
							state = pixelmaster_custom_script(1, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, screengrab, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
							if state == False:
								close_pm_and_wotlk(pixel_exe, battle_exe, cmd_hwnd, active_profile[bot_number-1], active_profile_personal[bot_number-1], active_profile_path[bot_number-1], pids, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
								time.sleep(2)
								transport_timer = 0
								continues
							#elif choose_char(bot_number, cmd_hwnd) == True:
								#pm_play()
							#	pyautogui.keyDown('alt')
								# time.sleep(0.2)
								# pyautogui.press('f3')
								# time.sleep(0.2)
								# pyautogui.keyUp('alt')

				else:					
					print('%s is Running or Unknown\nCrashes: %i\nTimes Stuck: %i' % (imgNames[0], crashes, stuck_restarts))
					stuck = 0
				
				elapsed_time = 0
		elif (running == 1) and (bot_switch_ready == 1):
			if bot_number != None:
				if old_bot_number == -1:
					old_bot_number = bot_number
				if started == 0:
					browser = start_web(old_bot_number)
					started = 1
				#restart_pm_wow(battle_exe, pixel_exe, browser, cmd_hwnd)
				r = getTasks(imgNames)
				pids = []
				for process in r:
					#print(process + '\n')
					pids.append(find_first_number(process))
				if (r[1] != '') or (r[2] != '') or (r[3] != '') or (r[4] != '') or (r[5] != ''):
					profile_web_action(browser, 'stop', old_bot_number, read_type)
					#stop_running_profile()
					time.sleep(2)
					# Take screenshot of bag items
					if find_on_screen(r'Z:\PythonScreenshots\wow_speech_box.png', inverse = 1) == True:
						pyautogui.press('enter')
						time.sleep(1)
					try:
						pyautogui.keyDown('s')
						time.sleep(1)
					finally:
						pyautogui.keyUp('s')
						time.sleep(1)
					try:
						pyautogui.keyDown('shift')
						pyautogui.press('b')
					finally:
						pyautogui.keyUp('shift')
						pyautogui.moveTo(1, 1)
					time.sleep(1)
					myScreenshot = pyautogui.screenshot()
					myScreenshot.save(SCREENSHOT_PATH + "gear_" + CHAR_NAME[old_bot_number-1] + "_" + str(time_now.day) + "_" + str(time_now.month) + "_end" + '.png')

					#cancel_pm_profile(1, cmd_hwnd, old_bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
					close_pm(pixel_exe, cmd_hwnd, old_bot_number)
				if r[0] != '':
					try:
						subprocess.check_output("Taskkill /PID %d /F" % pids[0])
					except Exception as e:
						print(e)
					time.sleep(10)
					elapsed_time = 60
				if bot_number == -1 or (bot_number == 1):
					os.system("shutdown /s /t 0")
					#time.sleep(10)
				# Restart explorer.exe
				batch_file = r"Z:\Common Files\res_exp.bat"
				subprocess.call(["start", "explorer.exe"],shell=True)
				time.sleep(5)
				hwnd = win32gui.GetForegroundWindow()
				win32gui.MoveWindow(hwnd, 0, 0, 1024, 768, True)
				pyautogui.click(384, 69)
				pyautogui.write(batch_file)
				pyautogui.press('enter')
				print("Waiting 30 sec before starting next bot")
				time.sleep(30)

				if bot_number == -1:
					bot_number = None
					#os.system('shutdown -s')
				else:
					res, l = find_on_screen(r'Z:\PythonScreenshots\explorer_error.png', r'Z:\PythonScreenshots\explorer_error_highlight.png', loc = 1, either = 1, inverse = 1)
					if res == True:
						pyautogui.moveTo(l.left + (l.width / 2), l.top + (l.height / 2))
						time.sleep(2)
						if find_on_screen(r'Z:\PythonScreenshots\explorer_exe.png', inverse = 1):
							pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
							time.sleep(1)
							pyautogui.press('enter')

					if bot_switch == 1:
						mail_task_done = 0
						bot_switch = 0
						settingswap = 1
						bot_switch_ready = 0
						screengrab = 1
						continue
					else:
						running = 0
			else:
				time.sleep(30)
				continue

		# if find_on_screen(r'Z:\PythonScreenshots\win_start_menu.png', inverse = 1) == True:
		# 	pyautogui.press('win')

		# SET MAIL TASK IF IN FIRST 30 MIN
		# if (((time_now.hour * 60) + time_now.minute) >= ((LOWER_HOUR + r2) * 60.0)) and (((time_now.hour * 60) + time_now.minute) <= (((LOWER_HOUR + r2) * 60.0) + mail_task_time)):
		# 	if mail_task_done == 0:
		# 		mail_task = 1
		# 		mail_task_done = 1
		# elif mail_override != 1:
		# 	mail_task = 0

		# if find_on_screen(r'Z:\PythonScreenshots\start_menu_open_1.png', r'Z:\PythonScreenshots\start_menu_open_2.png', inverse = 1, either = 1):
		# 	print("Start menu open")
		# 	pyautogui.click(512, 40)

		if started == 0:
			browser = start_web(bot_number)
			started = 1
		if running == 1:
			browser, history, action, current_restock, last_log, char_selected, stopped_count, bot_running, profile_state, profile_name, char_level, char_name, run_time, log_status, log_time, read_type, error_log_time_temp = update_log(browser, history, current_restock, action, last_log, stopped_count, active_profile[bot_number-1], active_profile_personal[bot_number-1], bot_number, char_select_failed, read_type, log_time, log_status = log_status, cmd_hwnd = cmd_hwnd, active_profile_path = active_profile_path[bot_number-1], mail_task = mail_task)
			if error_log_time_temp != None:
				error_log_time = error_log_time_temp
			if profile_state == "Running":
				bot_running = 1
			elif profile_state == "Stopped":
				bot_running = 0
			if move_check_count < 0:
				move_check_count += 1
			# if cooldown >= 5:
			# 	if elapsed_time % 10 == 0:
			# 		result, start_move_check = check_pos_change(start_move_check)
			# 		if result == True:
			# 			if move_check_count > 0:
			# 				move_check_count = 0
			# 		elif result == False:
			# 			move_check_count += 1
			# 			if move_check_count >= 3:
			# 				print("Not moving restarting profile")
			# 				stop_running_profile()
			# 				time.sleep(5)
			# 				pyautogui.keyDown('alt')
							# time.sleep(0.2)
							# pyautogui.press('f3')
							# time.sleep(0.2)
							# pyautogui.keyUp('alt')

			# 				start_move_check = None
			# 				move_check_count = 0
			# 				cooldown = 0

		# Checks if mount stuck moving
		# if ("gathering" in PERSONAL_PROFILE_NAME[0][0]):
		# 	stuck_moving_time = datetime.now()
		# 	if (datetime.now() - last_log).seconds > 600:
		# 		print("Stuck flying for 10 min")				
		# 		pyautogui.press('f4')
		# 		time.sleep(1)
		# 		try:
		# 			pyautogui.keyDown('w')
		# 			time.sleep(1)
		# 		finally:
		# 			pyautogui.keyUp('w')
		# 			time.sleep(1)
		# 		pyautogui.press('f3')
		# 		time.sleep(5)

		# IF BAGS FULL WITHIN 30 MIN PERFORM MAIL TASK
		if (current_restock != 0) and (current_restock != last_restock):
			if last_restock == 0:
				last_restock = current_restock
			else:
				if MAIL_TASK_NAME[bot_number-1] != '':
					# if PERSONAL_PROFILE_NAME[bot_number-1][0] != 'skinning_450 (sholazar)':
					stop_running_profile()
					time.sleep(5)
					cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
					state = pixelmaster_custom_script(1, MAIL_TASK_NAME[bot_number-1], "GoTo", cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
					mail_task = 1
					mail_task_done = 1
					mail_override = 1

					current_restock = 0
					last_restock = 0

		# CHECK IF BOT IS OUT OF MAP
		# image = pyautogui.screenshot(region=(485, 111, 51, 14))
		# image.save('screenshot' + pc_name + '.png')
		# image = cv2.imread('screenshot' + pc_name + '.png')
		# data = pytesseract.image_to_string(image, lang='eng', config='--psm 6')
		# if ('fatigue' in data.lower()) and (datetime.now() - fatigue_timer).seconds > 60:
		# 	print("BOT OUT OF MAP. RESETTING SCRIPT")
		# 	pyautogui.keyDown('s')
		# 	time.sleep(0.5)
		# 	pyautogui.keyUp('s')
		# 	time.sleep(0.5)
		# 	stop_running_profile()
		# 	time.sleep(0.5)
		# 	pyautogui.press('f3')
		# 	fatigue_timer = datetime.now()

		if action[46] == 1:
			try:
				pyautogui.keyDown('x')
				time.sleep(5)
			finally:
				pyautogui.keyUp('x')
			action[46] == 0

		# TRAINING SKILLS
		# if action[42] >= 1:
		# 	if action[42] == 1:
		# 		pyautogui.press('f4')
		# 		time.sleep(1)
		# 		pyautogui.click(210, 503)
		# 		time.sleep(1)
		# 		pyautogui.press('esc')
		# 		time.sleep(1)
		# 		pyautogui.press('f3')
		# 		action[42] = 0
			# print("Training exit")
			# sys.exit()
		# FAILED TO SELL 
		if action[23] == 1:
			if MAIL_TASK_NAME[bot_number-1] != '':
				# if PERSONAL_PROFILE_NAME[bot_number-1][0] != 'skinning_450 (sholazar)':
				cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				state = pixelmaster_custom_script(1, MAIL_TASK_NAME[bot_number-1], "GoTo", cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
				mail_task = 1
				mail_task_done = 1
				mail_override = 1
				action = np.zeros(100)
		# AH
		if action[0] == 1:
			profile_web_action(browser, 'stop', bot_number, read_type)
			#stop_running_profile()
			auction_house()
			cancel_pm_profile(2, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			sys.exit()
			new_profile = pixelmaster_custom_script(0, 'use_hearthstone.cs', "GoTo", cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
			action = np.zeros(100)
		# ERROR BUT MAIL SCREEN UP
		if action[47] == 1:
			print("Checking wow TSM window open")
			if find_on_screen(r'Z:\PythonScreenshots\wow_tsm_send.png', inverse = 1) == True:
				print("Profile failed but mail window open")
				pyautogui.press('f4')
				pyautogui.press('f4')
				action[1] = 1
				action[6] = 0
				action[45] = 0
				mail_task = 1
			action[47] = 0
		# COMPLETED
		if (action[1] == 1) and (new_profile >= -1):
			#cancel_pm_profile(2, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			if "leveling" in active_profile_personal[bot_number-1]:
				filename = r"C:\Users\A\Desktop\PM\Profiles\ProfileStatus\grinding_location.log"
				if active_profile_personal[bot_number-1] == "alliance leveling":
					update_file(filename, alliance_order)
				if active_profile_personal[bot_number-1] == "horde leveling":
					update_file(filename, horde_order)
				cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
			elif restart_hearthstone == 1:
				cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
				restart_hearthstone = 0
			elif (mail_task == 1) or (find_on_screen(r'Z:\PythonScreenshots\wow_tsm_send.png', inverse = 1) == True):
				print("Performing mail task")

				# pyautogui.press('b')
				# time.sleep(1)
				# d = datetime.now()
				# myScreenshot = pyautogui.screenshot()
				# file_name = os.path.join("Z:\\httpd\\screenshot\\mail\\", f'qan-{d:%Y-%m-%d %H:%M}({i}).png')
				# myScreenshot.save(file_name)
				# pyautogui.press('b')

				pyautogui.moveTo(230, 752)
				time.sleep(2)				
				res, l = find_on_screen(r'Z:\PythonScreenshots\tsm_okay.png', inverse = 1, loc = 1)
				if res == True:
					pyautogui.click(512, 222)
					time.sleep(1)
				res, l = find_on_screen(r'Z:\PythonScreenshots\wow_tsm_groups_not_selected.png', inverse = 1, loc = 1) 
				while res == True:
					if res == True:
						print(l)
						pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
					pyautogui.moveTo(230, 752)
					time.sleep(2)
					res, l = find_on_screen(r'Z:\PythonScreenshots\wow_tsm_groups_not_selected.png', inverse = 1, loc = 1) 
				res, l = find_on_screen(r'Z:\PythonScreenshots\wow_tsm_mailing_not_selected.png', inverse = 1, loc = 1)
				while res == True:
					if res == True:
						pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
					pyautogui.moveTo(230, 752)
					time.sleep(2)
					res, l = find_on_screen(r'Z:\PythonScreenshots\wow_tsm_mailing_not_selected.png', inverse = 1, loc = 1)
				res, l = find_on_screen(r'Z:\PythonScreenshots\wow_tsm_mail.png', inverse = 1, loc = 1)
				if res == True:
					pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
					pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
					start_time = datetime.now()
					time_passed = 0
					time_to_wait = 20
					while time_passed <= time_to_wait:
						if find_on_screen(r'Z:\PythonScreenshots\wow_mail_confirm.png', inverse = 1):
							pyautogui.moveTo(100, 100)
							res, l = find_on_screen(r'Z:\PythonScreenshots\wow_mail_confirm_accept.png', loc = 1)
							pyautogui.moveTo(l.left + (l.width / 2), l.top + (l.height / 2))
							time.sleep(0.5)
							pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
							time_to_wait += 5
							time.sleep(1)
						time_passed = (datetime.now() - start_time).seconds
					pyautogui.press('b')
					mail_task = 0
				else:
					print("Could not mail, restarting wow and PM")
					
				cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task, change_profile = 1)
				mail_override = 0
			else:
				sys.exit()
				cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				if (len(PROFILE_NAME[bot_number-1]) > 1) or (len(PERSONAL_PROFILE_NAME[bot_number-1]) > 1):
					if PROFILE_NAME[bot_number-1][0] != "":
						profile_index = PROFILE_NAME[bot_number-1].index(active_profile[bot_number-1])
					else:
						profile_index = 1000
					if PERSONAL_PROFILE_NAME[bot_number-1][0] != "":
						profile_index_personal = PERSONAL_PROFILE_NAME[bot_number-1].index(active_profile_personal[bot_number-1])
					else:
						profile_index_personal = 1000

					if active_profile[bot_number-1] in profile_name:
						if profile_index < (len(PROFILE_NAME[bot_number-1]) - 1):
							active_profile[bot_number-1] = PROFILE_NAME[bot_number-1][profile_index + 1]
						else:
							active_profile[bot_number-1] = PROFILE_NAME[bot_number-1][profile_index] + " COMPLETED"
						index = 0
						for line in Lines:
							if ("Active Profile" + str(bot_number)) in line:
								Lines[index] = "Active Profile" + str(bot_number) + ": " + active_profile[bot_number-1]
							index += 1
						rw_file("w", Lines)
						print("Starting next profile: " + active_profile[bot_number-1])
						new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])

					if active_profile_personal[bot_number-1] in profile_name:
						if profile_index_personal < (len(PERSONAL_PROFILE_NAME[bot_number-1]) - 1):
							active_profile_personal[bot_number-1] = PERSONAL_PROFILE_NAME[bot_number-1][profile_index_personal + 1]
							active_profile_path[bot_number-1] = PERSONAL_PROFILE_PATH[bot_number-1][profile_index_personal + 1]
						else:
							active_profile_personal[bot_number-1] = PERSONAL_PROFILE_NAME[bot_number-1][profile_index_personal] + " COMPLETED"
							active_profile_path[bot_number-1] = PERSONAL_PROFILE_PATH[bot_number-1][profile_index_personal] + " COMPLETED"
						index = 0
						for line in Lines:
							if ("Active Personal" + str(bot_number))  in line:
								Lines[index] = "Active Personal" + str(bot_number) + ": " + active_profile_personal[bot_number-1]				
							index += 1
						rw_file("w", Lines)
						print("Starting next profile: " + active_profile_personal[bot_number-1])
						new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
					else:
						sys.exit()
				else:
					new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
					new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
			action[1] = 0
			action = np.zeros(100)
		# RESURRECTION STARTED
		if action[12] == 1:
			if (mail_task == 1) and (first_run == 0):
				# first_run = 1
				# stop_running_profile()
				time.sleep(10)
				# pyautogui.keyDown('alt')
				# time.sleep(0.2)
				# pyautogui.press('f3')
				# time.sleep(0.2)
				# pyautogui.keyUp('alt')
			#resurrect_count += 1
			resurrect_start_time = datetime.now()
			if resurrect_count == 1:
				resurrect_start_time = datetime.now()
				resurrect_start_time2 = datetime.now()
			action[12] = 0
		# PREPERATION STARTED or LOOTING ITEMS FINISHED
		check_time = datetime.now()
		if (action[15] == 1) or (action[22] == 1):
			if bot_switch == 1:
				if NUM_BOTS <= 1:
					bot_number = -1
				bot_switch_ready = 1
			action[15] = 0
			action[22] = 0
		# if action[15] == 1:
		# 	if action[12] == 1:
		# 		action[12] = 0
		# 	action[15] = 0
		# 	prep_started = 1
		# elif (action[12] == 1) and ((check_time - resurrect_start_time2).seconds >= 120):		
		# 	profile_web_action(browser, 'stop', bot_number, read_type)		
		# 	restart = 1
		# 	time.sleep(5)
		# 	pyautogui.keyDown(movement[movement_pos])
		# 	time.sleep(1)
		# 	pyautogui.press('space')
		# 	time.sleep(1)
		# 	pyautogui.keyUp(movement[movement_pos])
		# 	movement_pos += 1
		# 	if movement_pos == 4:
		# 		movement_pos = 0
		# 	resurrect_start_time2 = check_time
		# 	pyautogui.keyDown('alt')
		# time.sleep(0.2)
		# pyautogui.press('f3')
		# time.sleep(0.2)
		# pyautogui.keyUp('alt')
		if (action[13] == 1) and ('Jormungar' in profile_name) and (vendor_cooldown >= 300):
			profile_web_action(browser, 'stop', bot_number, read_type)
			pyautogui.press('s')
			time.sleep(5)		
			cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			cancel_pm_profile(2, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			pixelmaster_custom_script(0, 'goto_exit_cave', 'GoTo', cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
			vendor_cooldown = 0
			action = np.zeros(100)
		# FLYING
		#if action[4] == 1:
		# COMBAT
		if action[5] == 1:
			move_check_count = -20
			in_combat = 1
			action[5] = 0
		# ERROR
		if action[6] == 1:
			error_count += 1
			error_finish = 1
			action[6] = 0
		# ERROR RUNNING TO CORPSE
		# if action[19] == 1:
		# 	stop_running_profile()
		# 	error_count += 1
		# 	error_finish = 1

		# 	pyautogui.keyDown('w')
		# 	time.sleep(5)
		# 	pyautogui.keyUp('w')

		# 	action[19] = 0
		# STUCK or NOT MOVING
		if action[7] == 1:
			if active_profile[bot_number-1] == 'gathering_twilight':
				pyautogui.press('s')
			stuck_count += 1
			if stuck_count == 1:
				stuck_start = datetime.now()
			elif ((datetime.now() - stuck_start).seconds < 30):
				if (stuck_count >= 4):
					print("Player stuck over 4 times in last 30 seconds, retrying")
					restart_attempt = 1					
					error_count += 1
					profile_web_action(browser, 'stop', bot_number, read_type)
					#stop_running_profile()
					time.sleep(5)
					try:
						pyautogui.keyDown('x')
						pyautogui.keyDown(movement[movement_pos])
						time.sleep(1)
						pyautogui.press('space')
						time.sleep(1)
					finally:
						pyautogui.keyUp('x')
						pyautogui.keyUp(movement[movement_pos])
					movement_pos += 1
					if movement_pos == 4:
						movement_pos = 0
					try:
						pyautogui.keyDown('alt')
						time.sleep(0.2)
						pyautogui.press('f3')
						time.sleep(0.2)
					finally:
						pyautogui.keyUp('alt')
					stuck_start = datetime.now()
					stuck_count = 0
					stuck_restarts += 1
					restart_time = datetime.now()
					time.sleep(5)
			elif ((datetime.now() - stuck_start).seconds > 30):
				stuck_start = datetime.now()
				stuck_count = 1
				restart_attempt = 0
			action[7] = 0
		# WRONG PROFILE
		if action[8] == 1:
			new_profile = -1
			action[8] = 0
		if (action[8] != 1) and (new_profile == 20):
			profile_web_action(browser, 'stop', bot_number, read_type)
			#stop_running_profile()
			time.sleep(5)
			cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			cancel_pm_profile(2, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			time.sleep(5)
			new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
		# DURABILITY LOW
		# ICECROWN RESTOCKING
		if action[9] == 1 or action[10] == 1:
			if (active_profile[bot_number-1] == 'icecrown') and (repaired >= 1800):
				profile_web_action(browser, 'stop', bot_number, read_type)
				#stop_running_profile()
				time.sleep(5)
				cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				cancel_pm_profile(2, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				time.sleep(5)
				new_profile = pixelmaster_custom_script(0, 'goto_icecrown_repair', 'GoTo', cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
				repaired = 0
				action[9] = 0
				action[10] = 0
		# FLYING TO DESTINATION
		if (running == 1) and ('_ah' in profile_name) and (action[11] == 1):
			if (faction[bot_number-1] == 'horde'):
				try:
					pyautogui.keyDown('shift')
					time.sleep(0.5)
					pyautogui.press('1')
					time.sleep(0.5)
				finally:
					pyautogui.keyUp('shift')
					time.sleep(0.5)
				pyautogui.press('=')
				time.sleep(5)
				pyautogui.click(297,391)
			elapsed_time = -300
			action = np.zeros(100)
		# TOO MANY DEATHS BY PLAYERS
		if action[16] == 1:
			action[16] = 0
			# PAUSE FOR 5 MIN
			print("Too many player deaths. Waiting 5 min")
			time.sleep(300)

			# CLOSE EVERYTHING METHOD WAY
			# try:
				# subprocess.check_output("Taskkill /PID %d /F" % pids[0])
			# except Exception as e:
				# print(e)
			# time.sleep(10)
			# close_pm(pixel_exe, cmd_hwnd, bot_number)
			# # Launch PM
			# login_pm(pixel_exe, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			# # Launch WOTLK
			# wow_hwnd = start_wotlk(battle_exe, bot_number, cmd_hwnd)
			# new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd)
			# new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])

			cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			cancel_pm_profile(2, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)

			stuck = 0
			action = np.zeros(100)
			error_finish = 0
		if action[17] == 1:
			transport_timer = datetime.now()
			if thr != None:
				thr.stop()
			action[17] = 0
		if transport_timer != 0:
			if (transport_timer - datetime.now()).seconds > 300:
				if thr != None:
					if thr.state != 'Running':
						thr.start()
				transport_timer = 0
		#Stuck running forward
		if (action[15] == 1) and (action[18] == 1):
			profile_web_action(browser, 'stop', bot_number, read_type)
			print("stuck moving forward")
			try:
				pyautogui.keyDown('s')
				time.sleep(1)			
			finally:
				pyautogui.keyUp('s')
				time.sleep(1)
			pyautogui.press('-')
			time.sleep(3)
			try:
				pyautogui.keyDown('alt')
				time.sleep(0.2)
				pyautogui.press('f3')
				time.sleep(0.2)
			finally:
				pyautogui.keyUp('alt')
				time.sleep(5)
			action[15] = 0
			action[18] = 0
		if action[20] == 1:
			pyautogui.press('s')
			close_pm(pixel_exe, cmd_hwnd, bot_number)
			login_pm(pixel_exe, cmd_hwnd, bot_number, settingswap = 1, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
			action[20] = 0
		if action[24] == 1:
			pyautogui.press('f4')
			time.sleep(2)
			r = getTasks(['WowClassic.exe'])
			# choose_char(bot_number, cmd_hwnd)
			while r[0] != '':
				try:
					subprocess.check_output("Taskkill /PID %d /F" % pids[0])
				except Exception as e:
					print(e)
				time.sleep(5)
				r = getTasks(imgNames)
			cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			#time.sleep(600)
			action[24] = 0
		if action[25] == 1:
			close_pm_and_wotlk(pixel_exe, battle_exe, cmd_hwnd, active_profile[bot_number-1], active_profile_personal[bot_number-1], active_profile_path[bot_number-1], pids, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			time.sleep(10)
			action[25] = 0
		if action[26] >= 1:
			# if ((datetime.now() - trainer_time).seconds > 1800):
			# 	cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			# 	new_profile = pixelmaster_custom_script(0, 'use_hearthstone', "GoTo", cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
			# 	restart_hearthstone = 1
			# 	trainer_time = datetime.now()
			action[26] = 0
		if action[27] == 1:
			if ((datetime.now() - trainer_time).seconds > 1800):
				print("Hearthstone used")
				stop_running_profile()
				time.sleep(2)
				try:
					pyautogui.keyDown('alt')
					time.sleep(0.2)
					pyautogui.press('f3')
					time.sleep(0.2)
				finally:
					pyautogui.keyUp('alt')
				trainer_time = datetime.now()
				time.sleep(5)
			action[27] = 0
		# if action[28] >= 1 or action[42] == 1:
		# 	time.sleep(1)
		# 	pyautogui.press('f4')
		# 	time.sleep(2)
		# 	for i in range(0, 15):
		# 		pyautogui.click(210, 503)
		# 		time.sleep(0.5)
		# 	print(action[28])
		# 	filename = r'C:\Users\A\Desktop\PM\Profiles\ProfileStatus\level_training.log'
		# 	lines = ""
		# 	# Check if the file exists
		# 	if os.path.exists(filename):
		# 		# Read the entire file
		# 		with open(filename, 'r') as file:
		# 			lines = file.read()

		# 	# Write the modified content back to the file
		# 	with open(filename, 'w') as file:
		# 		file.write("lvl" + str(int(action[28])) + " trained\n")
		# 		file.write(lines)
		# 	# stop_running_profile()
		# 	# time.sleep(1)
		# 	# pyautogui.keyDown('s')
		# 	# time.sleep(1)
		# 	# pyautogui.keyUp('s')
		# 	# time.sleep(1)
		# 	action = np.zeros(100)
		# 	pyautogui.press('esc')

		# 	cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
		# 	new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
		# 	action[28] = 0
		# NOT FLYING VENDOR FAIL
		if action[29] == 1 and action[30] == 1:
			print("Failed to reach closest vendor")
			stop_running_profile()
			time.sleep(2)
			try:		
				pyautogui.keyDown('shift')
				time.sleep(0.5)
				pyautogui.press('1')
				time.sleep(0.5)
			finally:
				pyautogui.keyUp('shift')
				pyautogui.press('s')
				time.sleep(0.5)
			pyautogui.press('=')
			time.sleep(5)
			try:
				pyautogui.keyDown('space')
				time.sleep(5)			
			finally:
				pyautogui.keyUp('space')
				time.sleep(1)
			pyautogui.press('f3')
			action[29] = 0
			action[30] = 0
			time.sleep(5)
		# STUCK FLYING WHEN SHOULD BE ON GROUND
		if action[31] == 1:
			if in_air_check_count == 0:
				in_air_check_start = datetime.now()
			in_air_check_count += 1
			if ((datetime.now() - in_air_check_start).seconds > 60):
				in_air_check_count = 0
			if in_air_check_count >= 3:
				print("STUCK IN AIR DETECTED NOT ERROR FINISH")
				stop_running_profile()
				time.sleep(2)
				try:
					pyautogui.keyDown('shift')
					time.sleep(0.5)
					pyautogui.press('1')
					time.sleep(0.5)
				finally:
					pyautogui.keyUp('shift')
					time.sleep(0.5)
				pyautogui.press('=')
				time.sleep(1)
				pyautogui.press('f3')
				time.sleep(5)
				error_finish = 0
			action[31] = 0
		# CATACLYSM LVL 77-82 CANT FLY TO VENDOR
		# if action[33] == 1 and action[34] == 1 and flying_cooldown >= 180:
		# 	print("77-82 PROFILE CANT FLY TO VENDOR")
		# 	stop_running_profile()
		# 	time.sleep(2)
		# 	pyautogui.press('f3')
		# 	time.sleep(5)	
		# 	action[33] = 0
		# 	action[34] = 0
		# 	flying_cooldown = 0
		# LOG REQUEST TO RESTART
		if action[35] == 1:
			print("LOG RESTART AUTO")
			stop_running_profile()
			time.sleep(2)
			pyautogui.press('f3')
			time.sleep(5)	
			action[35] = 0
			action[36] = 0
		# LOG REQUEST TO RESTART
		if action[36] == 1:
			if ((datetime.now() - interact_check).seconds > 600):
				interact_check = datetime.now()
				action[36] = 0
			if ((datetime.now() - interact_check).seconds > 20) and ((datetime.now() - interact_check).seconds < 60):
				print("LOG INTERACT WITH NPC")
				stop_running_profile()
				time.sleep(2)
				pyautogui.press('f3')
				time.sleep(5)	
				interact_check = datetime.now()
				action[36] = 0
		# if action[37] == 1:
		# 	stop_running_profile()
		# 	time.sleep(2)
		# 	pyautogui.press('f3')
		# 	time.sleep(5)
		#	action[37] = 0
		# TRAVELLING TO DESTINATION. DISABLE GATHERING
		if action[38] == 1:
			print("TRAVELLING: DISABLING GATHERING")
			pyautogui.press('f3')
			time.sleep(1)
			login_pm(pixel_exe, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], skip = True)
			open_questingbot_settings('active_profile', 'skinning', '')
			pm_open_log(bot_number)
			pyautogui.press('f3')
			time.sleep(5)
			travel_timer = datetime.now()
			action[38] = 0
		# REACHED DESTINATION. ENABLE GATHERING
		if (action[39] == 1) or (travel_timer != None and (datetime.now() - travel_timer).seconds > 600):
			print("ENABLING GATHERING")
			pyautogui.press('f3')
			time.sleep(1)
			login_pm(pixel_exe, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], skip = True)
			open_questingbot_settings('active_profile', 'gathering', '')
			pm_open_log(bot_number)
			pyautogui.press('f3')
			travel_timer = None
			time.sleep(5)
			action[39] = 0
		# WAITING FOR USER INPUT
		if action[40] == 1:
			stop_running_profile()
			sys.exit()
		# GOSSIP NPC FAILED
		if action[41] == 1:
			pyautogui.press('f3')
			time.sleep(0.5)
			try:
				pyautogui.keyDown('s')
				time.sleep(5)
			finally:
				pyautogui.keyUp('s')
				time.sleep(0.5)
			pyautogui.press('f3')
			#sys.exit()
			action[41] = 0
		if action[43] >= 1:
			im = pyautogui.screenshot(region=(445, 203, 129, 15))
			im.save('screenshot_script' + pc_name + '.png')
			image_path = 'screenshot_script' + pc_name + '.png'
			# Read the image
			img = cv2.imread(image_path)
				
			# OCR Configuration - fixed quotation marks
			custom_config = '--oem 3 --psm 6 -c tessedit_char_whitelist=ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,!? '

			# Perform OCR
			text = pytesseract.image_to_string(img, config=custom_config)

			if("custom" in text.lower()) or ("script" in text.lower()):
				print("Clicking on Enable Custom Scripts Popup")
				pyautogui.press('f4')
				time.sleep(2)
				pyautogui.click(456, 234)
				time.sleep(2)
				pyautogui.press('f3')
				action[43] = 0

			action[43] += 1
			if action[43] == 10:
				action[43] = 0

		if (char_selected == 0 or profile_state == "Stopped_Test") and running == 1:
			###############
			# REMOVE WHEN WEB WORKING
			char_not_found_cnt = -100
			###############
			char_not_found_cnt += 1
			if char_not_found_cnt >= 5:
				#profile_web_action(browser, 'stop', bot_number, read_type)
				stop_running_profile()
				print(char_selected)
				print(profile_state)
				restart = 1
				# No character selected in web. Checking PM
				login_pm(pixel_exe, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
				if profile_check(cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1]) == False:
					new_profile = start_pixelmasterprofile(active_profile[bot_number-1], bot_number, cmd_hwnd = cmd_hwnd, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
					new_profile = pixelmaster_custom_script(0, active_profile_personal[bot_number-1], active_profile_path[bot_number-1], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
					time.sleep(2)
				else:
					print("CHAR SELECTED IN WEB. STARTING AGAIN")
					try:
						pyautogui.keyDown('alt')
						time.sleep(0.2)
						pyautogui.press('f3')
						time.sleep(0.2)
					finally:
						pyautogui.keyUp('alt')
						time.sleep(5)
				browser.quit()
				browser = start_web(bot_number)
				char_not_found_cnt = 0

		# error_count = 5
		# action[44] = 1
		# resurrect_start_time = datetime.now()
		if last_error != None:
			diff_error = (log_time - last_error).seconds
			diff_log = (datetime.now() - log_time).seconds
			if (diff_log > 50) and (diff_error < 20) and error_restart == 1:
				print("Retrying Profile Start after error")
				try:
					pyautogui.keyDown('alt')
					time.sleep(0.2)
					pyautogui.press('f3')
					time.sleep(0.2)
				finally:
					pyautogui.keyUp('alt')
				last_error = datetime.now()
			else:
				error_restart = 0

		#
		# if (action[44] == 1) and (find_on_screen(r'Z:\PythonScreenshots\wow_dead.png', inverse = 1)):
		# 	action[44] = 0
		# 	stop_running_profile()
		# 	time.sleep(1)
		# 	if spirit_healer_revive() == False:
		# 		print("Failed spirit healer revive")
		# 		sys.exit()

		# 	file1 = open(r'C:\Users\A\Desktop\bn.txt', 'r')
		# 	lines = file1.readlines()
		# 	for line in lines:
		# 		if 'vGPU' in line:
		# 			pc_name = line

		# 	filename = r'Z:\\Common Files\\ProfileErrors\\spirit_healer_revive.txt'
		# 	lines = ''
		# 	if os.path.exists(filename):
		# 		# Read the entire file
		# 		with open(filename, 'r') as file:
		# 			lines = file.read()
					
		# 	# Write the modified content back to the file
		# 	with open(filename, 'w') as file:
		# 		file.write(pc_name+" Spirit healer revived at "+datetime.now().strftime("%m/%d/%Y, %H:%M:%S")+"\n")
		# 		file.write(lines)

		# 	# time.sleep(420)
		# 	pyautogui.press('f3')
		# 	error_count = 0
		# else:
		# 	action[44] = 0

		if action[49] == 1:			
			action[44] = 0
			action[45] = 0
			action[49] = 0
			if find_on_screen(r'Z:\PythonScreenshots\wow_dead.png', inverse = 1):
				stop_running_profile()
				time.sleep(1)
				if spirit_healer_revive() == False:
					print("Failed spirit healer revive")
					sys.exit()

				file1 = open(r'C:\Users\A\Desktop\bn.txt', 'r')
				lines = file1.readlines()
				for line in lines:
					if 'vGPU' in line:
						pc_name = line

				filename = r'Z:\\Common Files\\ProfileErrors\\spirit_healer_revive.txt'
				lines = ''
				if os.path.exists(filename):
					# Read the entire file
					with open(filename, 'r') as file:
						lines = file.read()
						
				# Write the modified content back to the file
				with open(filename, 'w') as file:
					file.write(pc_name+" Spirit healer revived at "+datetime.now().strftime("%m/%d/%Y, %H:%M:%S")+"\n")
					file.write(lines)

				time.sleep(5)
				pyautogui.press('f3')
				time.sleep(2)
				error_count = 0

		# Check if there have been 5 or more errors in the last 5 minutes
		if (action[44] == 1) and (datetime.now() - resurrect_start_time).seconds >= 300:
			action[44] = 0
			action[45] = 0
		if error_count == 1:
			error_time = datetime.now()
		if error_count >= 2:
			error_check = datetime.now()
			if (error_check - error_time).seconds <= 300:
				if ((action[44] == 1) or (action[45] == 1)) and (find_on_screen(r'Z:\PythonScreenshots\wow_dead.png', inverse = 1)):
					action[44] = 0
					action[45] = 0
					stop_running_profile()
					time.sleep(1)
					if spirit_healer_revive() == False:
						print("Failed spirit healer revive")
						sys.exit()

					file1 = open(r'C:\Users\A\Desktop\bn.txt', 'r')
					lines = file1.readlines()
					for line in lines:
						if 'vGPU' in line:
							pc_name = line

					filename = r'Z:\\Common Files\\ProfileErrors\\spirit_healer_revive.txt'
					lines = ''
					if os.path.exists(filename):
						# Read the entire file
						with open(filename, 'r') as file:
							lines = file.read()
							
					# Write the modified content back to the file
					with open(filename, 'w') as file:
						file.write(pc_name+" Spirit healer revived at "+datetime.now().strftime("%m/%d/%Y, %H:%M:%S")+"\n")
						file.write(lines)

					time.sleep(420)
					pyautogui.press('f3')
					error_count = 0
				else:
					action[45] = 0
					action[44] = 0
			else:
				error_count = 0
				
			
		if error_count >= 5:
			res, l = find_on_screen(r'Z:\PythonScreenshots\in_game.png', r'Z:\PythonScreenshots\in_game_cata.png', either = 1, loc = 1, inverse = 1)
			if res == True:
				try:
					pyautogui.keyDown('s')
					time.sleep(1)
				finally:
					pyautogui.keyUp('s')
			#sys.exit()
			os.system("shutdown -r -t 0")
			cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
			new_profile = pixelmaster_custom_script(0, 'use_hearthstone.cs', "GoTo", cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
			restart_hearthstone = 1
			error_finish = 0
			error_count = 0

		if resurrect_count > 0:
			current_time = datetime.now()
			if (current_time - resurrect_start_time).seconds >= 600:
				resurrect_count = 0

		if error_finish == 1:
			if (profile_name == '') or (profile_state == 'Finished'):
				restart = 1
				time.sleep(5)
				#stop_running_profile()
				if profile_web_action(browser, 'stop', bot_number, read_type) == True:
					if resurrect_count >= 3:
						resurrect_count = 0
						if (r[1] != '') or (r[2] != '') or (r[3] != '') or (r[4] != '') or (r[5] != ''):
							profile_web_action(browser, 'stop', bot_number, read_type)
							#stop_running_profile()
							time.sleep(2)
							cancel_pm_profile(1, cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
							#close_pm(pixel_exe, cmd_hwnd, bot_number)
						if r[0] != '':
							try:
								subprocess.check_output("Taskkill /PID %d /F" % pids[0])
							except Exception as e:
								print(e)
							time.sleep(10)
							elapsed_time = 60
							continue
						# pyautogui.press('enter')
						# time.sleep(1)
						# pyautogui.write('/reload')
						# time.sleep(1)
						# pyautogui.press('enter')
						# time.sleep(5)
						# resurrect_count = 0
					if last_error == None:
						last_error = datetime.now()
						diff = datetime.now() - last_error
					else:
						diff = datetime.now() - last_error

					if ((diff.seconds < 120) and (diff.seconds > 15)) or (find_on_screen(r'Z:\PythonScreenshots\health_stuck.png', inverse = 1) == True):
						#override = 1
						#elapsed_time = 60
						print("Health is stuck, ERROR FINISH")
						pyautogui.press('esc')
						time.sleep(1)
						pyautogui.click(517, 411)
						time.sleep(30)
						pyautogui.press('enter')
						time.sleep(10)
						try:
							pyautogui.keyDown('alt')
							time.sleep(0.2)
							pyautogui.press('f3')
							time.sleep(0.2)
						finally:
							pyautogui.keyUp('alt')
					else:
						print("ERROR FINISH")
						try:
							pyautogui.keyDown(movement[movement_pos])
							time.sleep(1)
							pyautogui.press('space')
							time.sleep(1)
						finally:
							pyautogui.keyUp(movement[movement_pos])
						movement_pos += 1
						if movement_pos == 4:
							movement_pos = 0
						time.sleep(2)
						pyautogui.keyDown('alt')
						time.sleep(0.2)
						pyautogui.press('f3')
						time.sleep(0.2)
						pyautogui.keyUp('alt')
					time.sleep(5)
					last_error = error_log_time
			else:
				print("IN ERROR DECISION BLOCK")
				print(last_error)
				print(error_log_time)
				if last_error == None:
					last_error = datetime.now() - timedelta(seconds=6000)

				if in_air_check_count > 0:
					print("STUCK IN AIR DETECTED")
					try:
						pyautogui.keyDown('space')
						time.sleep(0.5)
						pyautogui.keyDown(movement[movement_pos])
						time.sleep(5)
					finally:
						pyautogui.keyUp(movement[movement_pos])
						time.sleep(0.5)
						pyautogui.keyUp('space')
	
					time.sleep(0.5)
					try:
						pyautogui.keyDown('x')
						time.sleep(0.5)
						pyautogui.keyDown(movement[movement_pos])
						time.sleep(5)
					finally:
						pyautogui.keyUp(movement[movement_pos])
						time.sleep(0.5)
						pyautogui.keyUp('x')

					pyautogui.press('f3')
					time.sleep(5)
					movement_pos += 1
					if movement_pos == 4:
						movement_pos = 0
					in_air_check_count = 0
					last_error = error_log_time
					
				elif last_error != error_log_time:
					print("Error occured, retrying")
					stop_running_profile()
					time.sleep(0.2)
					try:
						pyautogui.keyDown(movement[movement_pos])
						pyautogui.keyDown('z')
						time.sleep(1)
						pyautogui.press('space')
						time.sleep(1)
					finally:
						pyautogui.keyUp(movement[movement_pos])
						pyautogui.keyUp('z')
						movement_pos += 1
					if movement_pos == 4:
						movement_pos = 0
					time.sleep(2)
					try:
						pyautogui.keyDown('alt')
						time.sleep(0.2)
						pyautogui.press('f3')
						time.sleep(0.2)
					finally:
						pyautogui.keyUp('alt')
					error_restart = 1
					last_error = error_log_time
			error_finish = 0		

		# # CHECK IF OUT OF RANGE FROM GATHERING NODE
		# for i in range(0, 4):
		# 	# print("Check at " + now.strftime("%Y-%m-%d %H:%M:%S"))
		# 	if check_text_in_region(457, 129, 566, 152, "out of range", [], [], "", wait = 1, text_color = "red") == True:
		# 		print("Out of range to gathering node. Moving forward")
		# 		try:
		# 			pyautogui.keyDown('w')
		# 			time.sleep(0.2)
		# 		finally:
		# 			pyautogui.keyUp('w')
		# 	time.sleep(0.5)
		time.sleep(2)
		current_time = datetime.now()
		elapsed_time += (current_time - prev_time).seconds
		vendor_cooldown += (current_time - prev_time).seconds
		flying_cooldown += (current_time - prev_time).seconds
		repaired += (current_time - prev_time).seconds
		#time.sleep(120)
	except Exception as e:
		print(e)
		traceback.print_exception(type(e), e, e.__traceback__)
		ExtendableThread.stop_all()
		close_pm_and_wotlk(pixel_exe, battle_exe, cmd_hwnd, active_profile[bot_number-1], active_profile_personal[bot_number-1], active_profile_path[bot_number-1], pids, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], start = 0)
		print("Retrying")