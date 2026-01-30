from PIL import Image
from PIL import ImageGrab
import pytesseract
import cv2
import numpy as np
import os
import signal
import re
import time
import subprocess
import psutil
import logged_pyautogui as pyautogui
from pywinauto import application
import win32gui, win32con
from datetime import datetime
import wmi
import random
import math
from pm_logs import *
from pm_window import *
import sys
sys.path.append(r"C:\Users\A\Desktop\PM")
from options import *
import threading
import keyboard
import ctypes

ALT_CHAR_NAMES = {
	"Pagle": "Alinradivh",
	"Benediction": "Airusten",
	"Faerlina": "Romfrawn",
	"Mankrik": "Blandston",
	"Arugal": "Andalai",
	"Atiesh": "Evynel",
	"Nazgrim": "Selen",
	"Galakras": "Goopfloop",
}

class location_p:
	def __init__ (self):
		"""Constructor"""
		self.left = 0
		self.top = 0
		self.width = 0
		self.height = 0

class WindowMgr:
	"""Encapsulates some calls to the winapi for window management"""

	def __init__ (self):
		"""Constructor"""
		self._handle = None

	def find_window(self, class_name, window_name=None):
		"""find a window by its class_name"""
		self._handle = win32gui.FindWindow(class_name, window_name)

	def _window_enum_callback(self, hwnd, wildcard):
		"""Pass to win32gui.EnumWindows() to check all the opened windows"""
		if re.match(wildcard, str(win32gui.GetWindowText(hwnd))) is not None:
			self._handle = hwnd

	def find_window_wildcard(self, wildcard):
		"""find a window whose title matches the wildcard regex"""
		self._handle = None
		win32gui.EnumWindows(self._window_enum_callback, wildcard)

	def set_foreground(self):
		"""put the window in the foreground"""
		win32gui.SetForegroundWindow(self._handle)

	def minimize(self):
		"""put the window in the foreground"""
		win32gui.ShowWindow(self._handle, win32con.SW_MINIMIZE)

	def maximize(self):
		"""put the window in the foreground"""
		print("Maximize Window")
		win32gui.ShowWindow(self._handle, win32con.SW_MAXIMIZE)

	def move_window(self, x, y, w, h):
		win32gui.MoveWindow(self._handle, x, y, w, h, True)

class ExtendableThread(threading.Timer):
	_registry = set()  # track all active threads

	def __init__(self,  *args, **kwargs):
		super(ExtendableThread, self).__init__(*args, **kwargs)
		self.thr = threading.Timer(*args, **kwargs)
		self.thr.daemon = True
		self.start_time = None
		self.stop_time = None
		self.state = None
		self.kwargs = kwargs
		if 'interval' in kwargs:
			self.duration = kwargs['interval']
		else:
			self.duration = args[0]

	def start(self):
		if self.state == 'Running':
			self.stop()
		self.thr.start()
		self.state = 'Running'
		self.start_time = datetime.now()

		# ✅ Register this instance when it starts
		ExtendableThread._registry.add(self)

	def stop(self):
		if self.state == 'Running':
			self.thr.cancel()
			self.stop_time = datetime.now()
			self.state = 'Stopped'

			# ✅ Remove from registry when stopped
			ExtendableThread._registry.discard(self)

	def extend(self,  *args, **kwargs):
		self.stop()
		temp = {}
		for key, value in kwargs.items():
			if key == 'interval':
				temp[key] = (self.duration - (datetime.now() - self.start_time).seconds) + value
			else:
				temp[key] = value
		self.kwargs = temp
		self.thr = threading.Timer(**temp)
		self.thr.daemon = True
		self.thr.start()
		self.state = 'Running'

	def pause(self):
		self.duration = self.duration - (datetime.now() - self.start_time).seconds
		self.thr.cancel()

	def cont(self):
		temp = {}
		for key, value in self.kwargs.items():
			if key == 'interval':
				temp[key] = self.duration
			else:
				temp[key] = value
		self.thr = threading.Timer(**temp)
		self.thr.daemon = True
		self.thr.start()
		self.state = 'Running'

	def get_time_left(self):
		if self.state == 'Running':
			print(self.duration - (datetime.now() - self.start_time).seconds)

	def restart(self,  *args, **kwargs):
		if self.state == 'Running':
			self.stop()
		self.thr = threading.Timer(*args, **kwargs)
		self.thr.daemon = True
		self.thr.start()
		self.state = 'Running'
		self.start_time = datetime.now()

	@classmethod
	def stop_all(cls):
		"""Stop all currently running ExtendableThreads"""
		if not cls._registry:
			print("No active threads to stop.")
			return
		print(f"Stopping {len(cls._registry)} thread(s)...")
		for t in list(cls._registry):
			t.stop()
		print("All ExtendableThreads stopped.")

	def __repr__(self):
		return f"<ExtendableThread state={self.state} interval={getattr(self, 'duration', '?')}>"

def press_key(key, delay, window):
	window.type_keys(key)
	time.sleep(delay)
	return

def move_from_tb():
	pyautogui.moveTo(15, 704)

def is_admin():
	try:
		return ctypes.windll.shell32.IsUserAnAdmin()
	except:
		return False

def run_as_admin(cmd):
	if is_admin():
		# Already running as admin, just run the command
		subprocess.run(cmd, shell=True)
	else:
		# Re-run the program with admin rights
		ctypes.windll.shell32.ShellExecuteW(None, "runas", sys.executable, " ".join(sys.argv), None, 1)

def start_wotlk(path, bot_number, cmd_hwnd = 0, thr = None):
	new_thread = False
	if thr == None:
		new_thread = True
		escape = [0]
		extend = [0]
		thr = ExtendableThread(interval = 480, function = func_timeout, args = (cmd_hwnd, bot_number, escape, extend))
		thr.start()
	else:
		escape = [0]
		extend = [0]
		thr.extend(interval = 120, function = func_timeout, args = (cmd_hwnd, bot_number, escape, extend))
	# Select battle.net in taskbar and check if battle.net is on right screen 	
	# while find_on_screen(r'Z:\PythonScreenshots\bn_main_screen.png', r'Z:\PythonScreenshots\bn_main_screen_classic.png', either = 1, inverse = 1) != True:
	result = False
	while result != True:
		if find_on_screen(r'Z:\PythonScreenshots\bn_highlighted_icon.png', inverse = 1) != True:
			res, loc = find_on_screen(r'Z:\PythonScreenshots\bn_icon_task.png', loc = 1, locateall = 1)
			count = 1
			for l in loc:
				pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
				pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
				# pyautogui.click(489, 745)
				res = find_on_screen(r'Z:\PythonScreenshots\bn_main_screen.png', r'Z:\PythonScreenshots\bn_main_screen2.png', either = 1)
				print(res)
				if res != True:					
					if find_on_screen(r'Z:\PythonScreenshots\bn_clientupdate.png') == True:
						res, l = find_on_screen(r'Z:\PythonScreenshots\bn_clientupdate_restart.png', loc = 1)
						if res == 1:
							pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
							while find_on_screen(r'Z:\PythonScreenshots\bn_login.png', inverse = 1) != True:
								time.sleep(2)
							time.sleep(2)
							print("NOT LOGGED IN")
							sys.exit()
							pyautogui.write('kesq89627@')
							time.sleep(2)
							res, l = find_on_screen(r'Z:\PythonScreenshots\bn_login.png', timewait = 60, loc = 1)
							while find_on_screen(r'Z:\PythonScreenshots\bn_login.png', inverse = 1) == True:
								#pyautogui.press('enter')
								pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
								time.sleep(3)
							res, loc = find_on_screen(r'Z:\PythonScreenshots\bn_icon_task.png', timewait = 60, loc = 1, locateall = 1)
							for l in loc:
								pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
								pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
							res = find_on_screen(r'Z:\PythonScreenshots\bn_main_screen.png', r'Z:\PythonScreenshots\bn_main_screen2.png', either = 1)
					else:
						continue
				Maximize = win32gui.GetForegroundWindow()
				win32gui.ShowWindow(Maximize, win32con.SW_MAXIMIZE)
				time.sleep(2)
				pyautogui.click(1000, 317)
				time.sleep(2)

				# Check battlenet name
				# image = pyautogui.screenshot(region=(785, 36, 120, 20))
				# image.save("screenshot.png")
				# image = cv2.imread('screenshot.png')
				# data = pytesseract.image_to_string(image, lang='eng', config='--psm 6')
				# print(BOT_NAME[bot_number - 1])
				# if (BOT_NAME[bot_number - 1] in data):
				# 	break
				# pyautogui.click(1005, 8)
				#time.sleep(1)
		result = find_on_screen(r'Z:\PythonScreenshots\bn_main_screen.png', r'Z:\PythonScreenshots\bn_main_screen2.png', either = 1, inverse = 1)
		# else:
		# 	pyautogui.click(1000, 317)
		# 	time.sleep(2)
	# if find_on_screen(r'Z:\PythonScreenshots\bn_highlighted_icon.png', inverse = 1) != True:
	# 	pyautogui.click(489, 745)
	#time.sleep(1)
	# Resize Battle.net
	hwnd = win32gui.GetForegroundWindow()	
	win32gui.MoveWindow(hwnd, 0, 0, 1024, 768, True)
	time.sleep(1)

	Minimize = win32gui.GetForegroundWindow()
	# Launch WOTLK
	loc = None
	while loc == None:
		pyautogui.click(229, 55)
		time.sleep(2)
		res, loc = find_on_screen(r'Z:\PythonScreenshots\bn_wow_classic.png', r'Z:\PythonScreenshots\bn_wow_classic2.png', either = 1, loc = 1)
	pyautogui.click(loc.left + (loc.width / 2), 350)
	#pyautogui.click(500, 350)
	#pyautogui.click(296, 115)
	find_on_screen(r'Z:\PythonScreenshots\bn_wotlk_selected_classic.png', r'Z:\PythonScreenshots\bn_update.png', either = 1)
	if find_on_screen(r'Z:\PythonScreenshots\bn_update.png', inverse = 1) == True:
		pyautogui.click(162, 617)
		while find_on_screen(r'Z:\PythonScreenshots\bn_wotlk_selected_classic.png', inverse = 1) != True:
			pyautogui.moveTo(162, 760)
			time.sleep(2)
	#find_on_screen(r'Z:\PythonScreenshots\bn_wotlk_selected.png', r'Z:\PythonScreenshots\bn_wotlk_selected_classic.png', either = 1)
	pyautogui.click(162, 617)
	#pyautogui.click(163, 551)
	time.sleep(1)
	wow_hwnd = WindowMgr()
	while wow_hwnd._handle == None:
		wow_hwnd = WindowMgr()
		wow_hwnd.find_window_wildcard("World of Warcraft")

	width = 0
	height = 0
	while (width != 1040) and (height != 744):
		wow_hwnd.maximize()
		time.sleep(1)
		rect = win32gui.GetWindowRect(hwnd)  # Get the window's rectangle (left, top, right, bottom)
		width = rect[2] - rect[0]  # Calculate width
		height = rect[3] - rect[1]  # Calculate height
		print(width)
		print(height)
	time.sleep(2)
	# Resize WOW
	#hwnd = win32gui.GetForegroundWindow()
	#win32gui.MoveWindow(hwnd, 25, 25, 974, 678, True)

	# Minimize Battle.net
	pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
	pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
	time.sleep(1)

	pyautogui.click(1005, 8)
	time.sleep(1)
	pyautogui.click(512, 500)
	found = False
	for i in range(0, 5):
		res = check_text_in_region(486, 380, 539, 394, "cancel", ["enter"], [], "key", 0, 1)
		if res == False:
			found = True
		time.sleep(1)
	if found == False:
		print("Pressing enter")
		res = check_text_in_region(486, 380, 539, 394, "cancel", ["enter"], [], "key", 0)

	if new_thread == True:
		thr.stop()
	return 0

def find_first_number(string):
	match = re.search(r"\s(\d+)\s", string)
	if match:
		return int(match.group(1))
	else:
		return None

def find_on_screen(image, image2 = None, x_click = 0, y_click = 0, inverse = 0, either = 0, loc = 0, region = (0, 0, 1024, 768), locateall = 0, gray = False, timewait = 30, num_clicks = -1, no_move = 0, conf = 0.9, print_exception = 1):
	location = None
	location_res = None
	checks = 0
	start = datetime.now()
	end = datetime.now()
	diff = end - start
	click_count = 0
	if x_click == [0] and y_click == [0]:
		if loc == 1:
			return False, location
		else:
			return False
	#move_from_tb()
	while (location == None) and (diff.seconds <= timewait):
		#if no_move == 0:
			#pyautogui.moveTo(500, 500)
		checks += 1
		location = find_on_screen_result(image, region = region, locateall = locateall, gray = gray, conf = conf, print_exception = print_exception)
		if (either == 1) and (location == None):
			location = find_on_screen_result(image2, region = region, locateall = locateall, gray = gray, conf = conf, print_exception = print_exception)
			
		if (inverse == 1) and (location != None):
			if loc == 1:
				return True, location
			else:
				return True
		elif (inverse == 1):
			if loc == 1:
				return False, location
			else:
				return False
		if (image2 != None) and (checks % 10 == 0) and (either == 0):
			if (find_on_screen(image2, x_click = x_click, y_click = y_click, inverse = 1) == 0) and (num_clicks != click_count):
				for i in range(len(x_click)):
					if y_click[i] == '':
						pyautogui.press(x_click[i])
					elif y_click[i] == 'w':
						time.sleep(x_click[i])
					elif y_click[i] == 'c':
						image2 = None
					else:				
						pyautogui.click(x_click[i], y_click[i])
						if no_move == 0:
							pyautogui.moveTo(1, 1)
				click_count += 1
		#print(location)
		end = datetime.now()
		diff = end - start
		#print(diff.seconds)
	if (location == None) and (diff.seconds >= timewait):
		if loc == 1:
			return False, location
		else:
			return False
	elif loc == 1:
		return True, location
	else:
		return True

def find_on_screen_result(image, region = (0, 0, 1024, 768), locateall = 0, gray = False, conf = 0.9, print_exception = 1):
	if locateall == 1:
		try:
			location = None
			location_res = pyautogui.locateAllOnScreen(image, confidence = conf, grayscale = gray)
			count = 0
			if location_res != None:
				try:
					for l in location_res:
						if location == None:
							location = [l]
						else:
							location.append(l)
						count += 1
				except Exception as e:
					if print_exception == 1:
						print("File: %s, Exception: %s" % (image, e))
				if count == 0:
					location = None
			return location
		except Exception as e:
			pass
	else:
		try:
			location = pyautogui.locateOnScreen(image, grayscale = gray)
			return location	
		except Exception as e:
			pass		
	return None		

def check_pos_change(start_screen):
	# Grab the top screen
	if find_on_screen(r'Z:\PythonScreenshots\in_game.png', inverse = 1) == True:
		if start_screen == None:
			screenBuffer = ImageGrab.grab(bbox=(890, 103, 946, 155))
			return True, screenBuffer
		# Set 
		screenBuffer = ImageGrab.grab(bbox=(890, 103, 946, 155))
		current_screen = screenBuffer.getdata()
		start_screen = start_screen.getdata()
		img_a_array = np.array(start_screen)
		img_b_array = np.array(current_screen)
		difference = (img_a_array != img_b_array).sum()
		if difference == 0:
			return False, screenBuffer
		if difference >= 1:
			return True, screenBuffer
	else:
		return None, None

def getTasks(names, escape = 0):
	r = os.popen('tasklist /v').read().strip().split('\n')
	#print ('# of tasks is %s' % (len(r)))
	res = []
	temp = []
	for name in names:
		for i in range(len(r)):
			try:
				s = r[i]
				if name in r[i][0:r[i].find(' ')]:
					if name == 'BlizzardError.exe' and escape == 0:						
						tasklist = getTasks(['BlizzardError.exe'], escape = 1)
						pids = []
						for process in tasklist:
							pids.append(find_first_number(process))		
						while tasklist[0] != '':
							print("Killing WOW crash screen")
							try:
								subprocess.check_output("Taskkill /PID %d /F" % pids[0])
							except Exception as e:
								print(e)
							time.sleep(1)
							tasklist = getTasks(['BlizzardError.exe'], escape = 1)
					elif name == 'explorer.exe':
						temp.append(s)
					else:
						res.append(s)
						#print ('%s in r[i]' %(name))
						#return r[i]
						break
			except Exception as e:
				print(e)
				print(r)
				print(i)
				print(r[i])
		if (name == 'explorer.exe') and (len(temp) > 1):
			res.append(temp[1])
		elif i == (len(r)-1):
			res.append('')
	return res

def auction_house():
	time.sleep(1)
	res1, loc1 = find_on_screen(r'Z:\PythonScreenshots\ah_wow_ui.png', loc = 1, inverse = 1)
	if res1 == True:
		pyautogui.click(loc1.left + (loc1.width / 2), loc1.top + (loc1.height / 2))
		time.sleep(1)
	res2, loc2 = find_on_screen(r'Z:\PythonScreenshots\ah_tsm_auctioning.png', loc = 1, inverse = 1)
	if res2 == True:
		pyautogui.click(loc2.left + (loc2.width / 2), loc2.top + (loc2.height / 2))
		time.sleep(1)
	# if find_on_screen(r'Z:\PythonScreenshots\ah_wow_ui.png', inverse = 1) == True:
	# 	pyautogui.click(292, 516)
	# 	time.sleep(1)
	# if find_on_screen(r'Z:\PythonScreenshots\ah_tsm_auctioning.png', inverse = 1) != True:
	# 	pyautogui.click(268, 64)
	# 	time.sleep(1)
	pyautogui.click(153, 493)
	#time.sleep(30)
	pyautogui.moveTo(776, 95)
	find_on_screen(r'Z:\PythonScreenshots\ah_all_scanned.png')
	pyautogui.moveTo(776, 95)
	pyautogui.keyDown('ctrl')
	while find_on_screen(r'Z:\PythonScreenshots\ah_done_posting.png', inverse = 1) != True:
		pyautogui.moveTo(776, 95)
		for i in range(20):
			pyautogui.scroll(2)
			time.sleep(10/1000)
			#mouse.scroll(wheel_dist=1)
	pyautogui.keyUp('ctrl')
	return

def check_mount():
	if find_on_screen_result(r'Z:\PythonScreenshots\flying_check.png') != None:
		return 'flying'
	elif find_on_screen_result(r'Z:\PythonScreenshots\mount_check.png') != None:
		return 'ground'
	return 'none'

def rw_file(action, data, filepath = r"C:\Users\A\Desktop\PM\saved_status.txt"):
	if action == "r":
		index = 0
		result = []
		comp_check = []
		if os.path.isfile(filepath) == True:
			status = open(filepath, 'r')
			Lines = status.readlines()
			for d in data:
				for line in Lines:
					if d in line:
						index1 = line.find(":")
						if "COMPLETED" in line:
							result.append(line[index1 + 2:-10])
							comp_check.append(True)
						elif len(line) > index1 + 3:
							result.append(line[index1 + 2:-1])
							comp_check.append(False)
				status.close()
				if len(result) <= index:
					result.append(None)
					comp_check.append(False)
				index += 1
			return result, Lines, comp_check
		for d in data:
			result.append(None)
			Lines = [""]
			return result, Lines, comp_check

	if action == "w":
		status = open(filepath, 'w')

		for line in data:
			if line != '\n':
				status.write(line + '\n')
		status.close()
		return

def check_movement():
	myScreenshot = pyautogui.screenshot(region=(910, 71, 60, 67))
	return myScreenshot

def check_disconnect():
	myScreenshot = pyautogui.screenshot(region=(335, 337, 347, 20))
	myScreenshot.save(r'C:\Users\A\Desktop\image.png')

	image_path = r"C:\Users\A\Desktop\image.png"
	img = Image.open(image_path) 

	#pytesseract.tesseract_cmd = path_to_tesseract
	try:
		text = pytesseract.image_to_string(myScreenshot, timeout=5)	
	except Exception as e:
		return 0
	if "have been disconnected from the" in text[:-1]:
		return 1
	else:
		return 0

def take_screenshot(file_name, path):
	if os.path.exists(path) == False:
		os.makedirs(path)
	myScreenshot = pyautogui.screenshot()
	myScreenshot.save(path + '"\"' + file_name)

def func_timeout(cmd_hwnd, bot_number, escape, extend, error_ignore = [0]):
	try:
		# start = datetime.now()
		# time_now = datetime.now()
		# while (escape[0] == 0) and ((time_now-start).seconds <= timer):
		# 	res1, l1 = find_on_screen(r'Z:\PythonScreenshots\explorer_error.png', inverse = 1, loc = 1, no_move = 1)
		# 	res2, l2 = find_on_screen(r'Z:\PythonScreenshots\explorer_error_highlight.png', inverse = 1, loc = 1, no_move = 1)
		# 	if find_on_screen(r'Z:\PythonScreenshots\pm_user_control.png', inverse = 1) != True:
		# 		if find_pm_loc("quick_search") == True:
		# 			if res1 == True:
		# 				pyautogui.click(l1.left + (l1.width / 2), l1.top + (l1.height / 2), button = 'right')
		# 				res3, l3 = find_on_screen(r'Z:\PythonScreenshots\close_window.png', loc = 1, no_move = 1)
		# 				pyautogui.click(l3.left + (l3.width / 2), l3.top + (l3.height / 2))
		# 			if res2 == True:
		# 				pyautogui.click(l2.left + (l2.width / 2), l2.top + (l2.height / 2), button = 'right')
		# 				res3, l3 = find_on_screen(r'Z:\PythonScreenshots\close_window.png', loc = 1, no_move = 1)
		# 				pyautogui.click(l3.left + (l3.width / 2), l3.top + (l3.height / 2))
		# 	print(timer)
		# 	if extend[0] != 0:
		# 		timer += extend[0]
		# 		extend[0] = 0
		# 	time_now = datetime.now()
		# 	if keyboard.is_pressed('ctrl+c'):
		# 		return
		# 	time.sleep(1)
		# if ((time_now-start).seconds > timer):
		print("Timed out, restarting script")

		res, l = find_on_screen(r'Z:\PythonScreenshots\pm_load_profile.png', loc = 1, inverse = 1)
		if res == True:
			pyautogui.press(954, 692)
			time.sleep(2)
			pyautogui.press(685, 718)
			time.sleep(2)
			pyautogui.press(541, 16)
			time.sleep(2)
			
		# batch_file = r"Z:\Common Files\res_exp.bat"
		# subprocess.call(["start", "explorer.exe"],shell=True)
		# time.sleep(5)
		# hwnd = win32gui.GetForegroundWindow()
		# win32gui.MoveWindow(hwnd, 0, 0, 1024, 768, True)
		# pyautogui.click(384, 69)
		# pyautogui.write(batch_file)
		# pyautogui.press('enter')

		# time.sleep(10)
		res, l = find_on_screen(r'Z:\PythonScreenshots\explorer_error.png', r'Z:\PythonScreenshots\explorer_error_highlight.png', loc = 1, either = 1, inverse = 1)
		if res == True:
			pyautogui.moveTo(l.left + (l.width / 2), l.top + (l.height / 2))
			time.sleep(2)
			if find_on_screen(r'Z:\PythonScreenshots\explorer_exe.png', inverse = 1):
				pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2))
				time.sleep(1)
				pyautogui.press('enter')

		os.execv(sys.executable, ['python', sys.argv[0], "1", str(bot_number)])
		sys.exit()
	except KeyboardInterrupt:
		return

def screen_record():
	print("Starting Recording")
	subprocess.run(["python", "record.py"])
	print("Stopping Recording")

def wow_full_screen_check(cmd_hwnd):
	return
	# Checks if taskbar is visible
	# if find_on_screen(r'Z:\PythonScreenshots\bn_icon_task.png', inverse = 1) != True:
		# pyautogui.press("alt")
		# cmd_hwnd.set_foreground()

	# wow_hwnd = WindowMgr()
	# wow_hwnd.find_window_wildcard(".*World of Warcraft.*")
	# if wow_hwnd != None:
	# 	# wow_hwnd.minimize()
	# 	pyautogui.press("alt");
	# 	cmd_hwnd.set_foreground()


def find_directory(search_dir, target_dir):
	"""
	Recursively searches for a directory with the given name within the specified directory.
	
	Args:
		search_dir (str): The directory to start the search from.
		target_dir (str): The name of the directory to search for.
		
	Returns:
		str: The full path of the target directory if found, otherwise None.
	"""
	for entry in os.listdir(search_dir):
		path = os.path.join(search_dir, entry)
		if os.path.isdir(path):
			if entry == target_dir:
				return 1000
			else:
				found_dir = find_directory(path, target_dir)
				if found_dir == 1000:
					return path
				if found_dir != None:
					return found_dir
	return None

def open_file(dir_path, filename, bot_number):
	"""
	Opens a text file within the specified directory.
	
	Args:
		dir_path (str): The full path of the directory containing the file.
		filename (str): The name of the text file to open.
	"""
	file_path = os.path.join(dir_path, filename)
	file_path2 = r'Z:\Settings\TradeSkillMaster.lua'
	line_count = 0
	edited = False
	if os.path.isfile(file_path) == True:
		try:
			with open(file_path, 'r') as file:
				lines = file.readlines()
				for line in lines:
					# Checks if mail char names are in file
					if ALT_CHAR_NAMES[SERVER_NAME[bot_number]] in line:
						edited = True
			edited = False
			if edited == False:
				print("Copying TSM files")
				shutil.copy(file_path2, file_path)
				shutil.copy('Z:\Settings\TradeSkillMaster_AppHelper.lua', os.path.join(dir_path, "TradeSkillMaster_AppHelper.lua"))
				shutil.copy('Z:\Settings\TSMv.txt', os.path.join(dir_path, "TSMv.txt"))
				print("Replacing Characters")
				new_lines = []
				with open(file_path, 'r') as file:
					lines = file.readlines()
					for line in lines:
						# Replace the old string with the new string in the current line
						new_line = line.replace("CHAR_NAME", CHAR_NAME[bot_number])
						new_line = new_line.replace("MAIL_NAME", ALT_CHAR_NAMES[SERVER_NAME[bot_number]])
						new_line = new_line.replace("SERVER_NAME", SERVER_NAME[bot_number])
						# Append the modified line to the new list
						new_lines.append(new_line)
				with open(file_path, 'w') as file:
				# Write the modified lines to the file
					file.writelines(new_lines)
				# with open(file_path_ver, 'w') as file:
				# # Write the modified lines to the file
				# 	file.writelines(c1)
		except FileNotFoundError:
			print(f"File '{filename}' not found in {dir_path}")

def find_pm_loc(action = None, wait = 5, cmd_hwnd = 0, launch_stage = 0, exit = 0):
	res = False
	loc = None
	start_time = datetime.now()
	while True:
		if find_on_screen(r'Z:\PythonScreenshots\pm_stuck_open.png', r'Z:\PythonScreenshots\pm_stuck_open_no_highlight.png', inverse = 1, either = 1) == True:
			pyautogui.click(954, 697)
			time.sleep(2)
		res, loc = find_on_screen(r'Z:\PythonScreenshots\pm_highlight.png', r'Z:\PythonScreenshots\pm_highlight_selected.png', either = 1, inverse = 1, loc = 1, locateall = 1, print_exception = 0)
		if res == True:
			break
		res, loc = find_on_screen(r'Z:\PythonScreenshots\pm_not_selected.png', inverse = 1, loc = 1, locateall = 1, print_exception = 0)
		if res == True:
			break
		res, loc = find_on_screen(r'Z:\PythonScreenshots\pm_highlight_actual_icon.png', r'Z:\PythonScreenshots\pm_highlight_selected_actual_icon.png', either = 1, inverse = 1, loc = 1, locateall = 1, print_exception = 0)
		if res == True:
			break
		res, loc = find_on_screen(r'Z:\PythonScreenshots\pm_not_selected_actual_icon.png', inverse = 1, loc = 1, locateall = 1, print_exception = 0)
		if res == True:
			break
		if action == "quick_search":
			break
		end_time = datetime.now()
		time.sleep(1)
		if ((end_time - start_time).seconds > wait) and (wait != -1):
			break
	count = 0
	left_temp = 0

	pixel_exe = r"C:\Users\A\Desktop\PM\PixelMaster.exe"
	bot_number = 1
	# if exit == 0:
	# 	# If PM is not open login to it
	# 	if (launch_stage == 0) and (res == False):
	# 		login_pm(pixel_exe, cmd_hwnd, bot_number, settingswap = 1, swapvalue = PM_SETTINGS[bot_number - 1], combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], extra_setting = ADDITIONAL_SETTINGS[0])
	# 	if (launch_stage >= 1) and (res == False):
	# 		login_pm(pixel_exe, cmd_hwnd, bot_number, settingswap = 1, swapvalue = PM_SETTINGS[bot_number - 1], combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], extra_setting = ADDITIONAL_SETTINGS[0])
	# 	# If PM is not open add the relevant profile
	# 	if (launch_stage >= 2) and (res == False):
	# 		pixelmaster_custom_script(0, PERSONAL_PROFILE_NAME[0][0], PERSONAL_PROFILE_PATH[0][0], cmd_hwnd, bot_number, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = 0, run_profile = 1)

	if loc != None:
		for l in loc:
			if (count >= 1) and (abs(l.left - left_temp) >= 5):
				pyautogui.click(x = l.left + (l.width / 2), y = l.top + (l.height / 2), button='right')
				time.sleep(5)
				pyautogui.click(l.left + (l.width / 2), l.top + (l.height / 2) - 43)
			if count == 0:
				temp = l
			left_temp = l.left
			count += 1
		loc = temp
	if action == "moveTo":
		if loc != None:
			pyautogui.moveTo(loc.left + (loc.width / 2), loc.top + (loc.height / 2))
	if action == "quick_search":
		if res != True:
			res = False
		loc = location_p()
		return loc, res
	return loc, res

def get_next_value(current_value, array):
	try:
		if current_value == None:
			index = 0
			return array[index]
		else:
			index = array.index(current_value)
			if index < len(array) - 1:
				return array[index + 1]
	except ValueError:
		pass
	return array[0] if array else None

def update_file(filename, array):
	current_value = None
	
	# Read the current value from the file if it exists
	if os.path.exists(filename):
		with open(filename, 'r') as file:
			current_value = file.read().strip()
	
	# Get the next value
	next_value = get_next_value(current_value, array)
	
	# Write the next value to the file
	if next_value is not None:
		with open(filename, 'w') as file:
			file.write(next_value)
		print(f"File '{filename}' has been updated with the value: {next_value}")
		return next_value
	else:
		print("The array is empty. No update performed.")
		return None

def spirit_healer_revive():
	# Blizzard's exact "Spirit Healer" green in BGR (from Classic/Retail when dead)
	LOWER_GREEN = np.array([0, 250, 0])    # BGR
	UPPER_GREEN = np.array([50, 255, 100])

	result = None
	res, loc = find_on_screen(r'Z:\PythonScreenshots\wow_dead.png', inverse = 1, loc = 1)
	# pyautogui.click(loc.left + (loc.width / 2), loc.top + (loc.height / 2))
	# time.sleep(10)
	# pyautogui.press('enter')
	# Move Forward after each failed attempt
	for attempt in range(0, 2):
		# Capture the screen
		screenshot = pyautogui.screenshot()
		screenshot_np = np.array(screenshot)
		# screenshot_bgr = cv2.cvtColor(screenshot_np, cv2.COLOR_RGB2BGR)
		
		frame = cv2.cvtColor(screenshot_np, cv2.COLOR_BGRA2BGR)
		# Create mask for the very specific lime-green color
		mask = cv2.inRange(frame, LOWER_GREEN, UPPER_GREEN)
		
		# Clean up noise
		kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3,3))
		mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, iterations=2)
		mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel, iterations=1)
		
		# Find contours
		contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
		
		for cnt in contours:
			area = cv2.contourArea(cnt)
			if area > MIN_AREA:
				x, y, w, h = cv2.boundingRect(cnt)
				# Extra sanity check: aspect ratio of "Spirit Healer" text is roughly 4-8:1
				if 3 < w/h < 10:
					result = True

		print(f"\nSPIRIT HEALER DETECTED at {x},{y}  (size {w}x{h}) – YOU ARE DEAD!")

		# # Convert to HSV for color filtering
		# hsv = cv2.cvtColor(screenshot_bgr, cv2.COLOR_BGR2HSV)

		# # Define green color range (adjust these values as needed)
		# lower_green = np.array([40, 40, 40])   # Lower HSV threshold (Hue, Saturation, Value)
		# upper_green = np.array([80, 255, 255]) # Upper HSV threshold

		# # Create a mask to isolate green regions
		# mask = cv2.inRange(hsv, lower_green, upper_green)

		# # Apply mask to original image
		# green_text = cv2.bitwise_and(screenshot_bgr, screenshot_bgr, mask=mask)

		# # Invert to create white text on black background (improves OCR accuracy)
		# inverted = cv2.bitwise_not(green_text)

		# Use Tesseract to extract text and bounding box data
		# data = pytesseract.image_to_data(inverted, output_type=pytesseract.Output.DICT, config='--psm 6')

		# # Words to search for (case-insensitive)
		# target_words = ["spirit", "healer"]

		# # Loop through detected text and their bounding boxes		
		# for i in range(0, len(data["text"])):
		# 	text = data["text"][i].strip().lower()  # Get text and make it lowercase			
		# 	for target_string in target_words:
		# 		# Determine the length of the smaller string for comparison
		# 		min_length = min(len(text), len(target_string))

		# 		# Count the number of matching characters
		# 		matches = sum(1 for i in range(min_length) if text[i] == target_string[i])

		# 		# Calculate the percentage of matches
		# 		match_percentage = (matches / len(target_string)) * 100
		# 		print(match_percentage)
		# 		print(text)
		# 		if match_percentage > 50:
		# 			# Get the bounding box position
		# 			x = data["left"][i]
		# 			y = data["top"][i]
		# 			w = data["width"][i]
		# 			h = data["height"][i]

		# 			# Calculate the position 30 pixels below the center of the word
		# 			click_x = x + w // 2
		# 			click_y = y + h + 30

		# 			# Move the mouse and perform a right-click
		# 			pyautogui.moveTo(click_x, click_y)
		# 			pyautogui.rightClick()
		# 			time.sleep(5)

		# 			print(f"Found '{text}' at ({x}, {y}). Right-clicked at ({click_x}, {click_y}).")
		# 			#break  # Stop after the first match (remove if you want to handle multiple matches)

		# 			pyautogui.click(76, 263)
		# 			pyautogui.click(76, 263)
		# 			pyautogui.click(76, 263)
		# 			#pyautogui.leftClick()
		# 			time.sleep(2)
		# 			pyautogui.click(451, 234)
		# 			pyautogui.click(451, 234)
		# 			pyautogui.click(451, 234)
		# 			time.sleep(2)

		# 			print("Revived from spirit healer")			
		# 			return True
		pyautogui.keyDown('w')
		time.sleep(0.5)
		pyautogui.keyUp('w')
		time.sleep(1)
	return False


def check_text_in_region(x1, y1, x2, y2, text_to_read, x_click, y_click, type, exists=1, wait=0, text_color='default'):
	# Define the region to capture (x, y, width, height)
	width, height = x2 - x1, y2 - y1

	# Take a screenshot of the specified region
	screenshot = pyautogui.screenshot(region=(x1, y1, width, height))

	# Convert the screenshot to a NumPy array (RGB)
	rgb_screenshot = np.array(screenshot)

	# Convert RGB to BGR for OpenCV
	image = cv2.cvtColor(rgb_screenshot, cv2.COLOR_RGB2BGR)

	if text_color.lower() == 'red':
		# Convert image to HSV color space
		hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)

		# Define red color range in HSV
		lower_red1 = np.array([0, 100, 100])
		upper_red1 = np.array([10, 255, 255])
		lower_red2 = np.array([160, 100, 100])
		upper_red2 = np.array([179, 255, 255])

		# Create masks for red
		mask1 = cv2.inRange(hsv, lower_red1, upper_red1)
		mask2 = cv2.inRange(hsv, lower_red2, upper_red2)
		red_mask = cv2.bitwise_or(mask1, mask2)

		# Optional: Morphological operations to clean up the mask
		kernel = np.ones((3, 3), np.uint8)
		red_mask = cv2.dilate(red_mask, kernel, iterations=1)

		# Apply the red mask to the original image
		filtered_image = cv2.bitwise_and(image, image, mask=red_mask)

		# Convert to grayscale
		gray = cv2.cvtColor(filtered_image, cv2.COLOR_BGR2GRAY)

	else:
		# Standard processing for non-red text
		gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)

	# Preprocessing for better OCR
	gray = cv2.convertScaleAbs(gray, alpha=1.5, beta=0)
	gray = cv2.GaussianBlur(gray, (3, 3), 0)

	# OCR
	text = pytesseract.image_to_string(gray, config='--psm 6')

	if (text_to_read in text.lower()) and (exists == 1):
		if wait == 1:
			return True
		if type == "button":
			for i in range(0, len(x_click)):
				pyautogui.click(x_click[i], y_click[i])
				time.sleep(2)
		if type == "key":
			for i in range(0, len(x_click)):
				pyautogui.press(x_click[i])
				time.sleep(2)
	elif not(text_to_read in text.lower()) and exists == 0:
		if wait == 1:
			return True
		if type == "button":
			for i in range(0, len(x_click)):
				pyautogui.click(x_click[i], y_click[i])
				time.sleep(2)
		if type == "key":
			for i in range(0, len(x_click)):
				pyautogui.press(x_click[i])
				time.sleep(2)
	return False

def update_max_fps(fps):
	file_path = r"C:\Program Files (x86)\World of Warcraft\_classic_\WTF\Config.wtf"
	
	try:
		# Check if file exists
		if not os.path.exists(file_path):
			print("Error: Config.wtf file not found.")
			return
		
		# Read file content
		with open(file_path, 'r') as file:
			lines = file.readlines()
		
		# Flag to track if maxFPS line exists
		max_fps_found = False
		target_fps_found = False
		updated_lines = []
		
		# Regex to match SET maxFPS line
		max_fps_pattern = re.compile(r'^\s*SET\s+maxFPS\s+"(\d+)"\s*$')
		# Regex to match SET targetFPS line
		target_fps_pattern = re.compile(r'^\s*SET\s+targetFPS\s+"(\d+)"\s*$')
		
		for line in lines:
			match = max_fps_pattern.match(line.strip())
			match2 = target_fps_pattern.match(line.strip())
			if match:
				current_fps = int(match.group(1))
				if current_fps != 30:
					updated_lines.append('SET maxFPS "' + str(fps) + '"\n')
					print(f"Changed maxFPS from {current_fps} to " + str(fps))
				else:
					updated_lines.append(line)
					print("maxFPS is already " + str(fps))
				max_fps_found = True
			elif match2:
				current_fps = int(match2.group(1))
				if current_fps != 30:
					updated_lines.append('SET targetFPS "' + str(fps) + '"\n')
					print(f"Changed targetFPS from {current_fps} to " + str(fps))
				else:
					updated_lines.append(line)
					print("targetFPS is already " + str(fps))
				target_fps_found = True
			else:
				updated_lines.append(line)
		
		# If maxFPS line not found, append it
		if not max_fps_found:
			updated_lines.append('SET maxFPS "' + str(fps) + '"\n')
			print("Added SET maxFPS '" + str(fps) + "'")
		# If targetFPS line not found, append it
		if not target_fps_found:
			updated_lines.append('SET targetFPS "' + str(fps) + '"\n')
			print("Added SET targetFPS '" + str(fps) + "'")		
		
		# Write updated content back
		os.remove(r"C:\Program Files (x86)\World of Warcraft\_classic_\WTF\Config.wtf")
		with open(file_path, 'w') as file:
			file.writelines(updated_lines)
		print("Config.wtf updated successfully")
	
	except PermissionError:
		print("Error: Permission denied. Run script as administrator.")
	except Exception as e:
		print(f"Error: {str(e)}")