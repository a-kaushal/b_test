from selenium import webdriver
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.common.keys import Keys
from selenium.webdriver.common.action_chains import ActionChains
from selenium.webdriver.common.by import By
from selenium.webdriver.support import expected_conditions as EC
from selenium.common.exceptions import TimeoutException
from selenium.webdriver.support.ui import WebDriverWait
import pickle
import time
import numpy as np
import os
import signal
import re
import subprocess
import psutil
import logged_pyautogui as pyautogui
from pywinauto import application
import win32gui, win32con
from datetime import datetime
import wmi
import random
import math
import sys
import cv2
from pm_window import *
sys.path.append(r"C:\Users\A\Desktop\PM")
from options import *
from difflib import SequenceMatcher

def get_xpath(elm):
    e = elm
    xpath = elm.tag_name
    while e.tag_name != "html":
        e = e.find_element(By.XPATH, "..")
        neighbours = e.find_elements(By.XPATH, "../" + e.tag_name)
        level = e.tag_name
        if len(neighbours) > 1:
            level += "[" + str(neighbours.index(e) + 1) + "]"
        xpath = level + "/" + xpath
    return "/" + xpath

def start_web(bot_number):
    ###################
    return None
    ###################
    options = webdriver.ChromeOptions()
    options.add_experimental_option("detach", False)
    options.add_argument("--user-data-dir=C:/Users/A/AppData/Local/Google/Chrome/User Data/Profile1")
    #options.add_argument("--headless")

    browser = webdriver.Chrome(options=options)

    url = 'https://pixelmaster.me/'
    done = 0
    while done == 0:
        try:
            browser.get(url)
            done = 1
        except Exception as e:
            time.sleep(20)
    # cookies = pickle.load(open("cookies.pkl", "rb"))
    # for cookie in cookies:
    #     driver.add_cookie(cookie)
    delay = 3600

    try:
        WebDriverWait(browser, delay).until(EC.presence_of_element_located((By.XPATH, '/html/body/app/section/aside/div[2]/div/nav/ul/li[1]/a')))
        logged_out = browser.find_elements(By.XPATH, '/html/body/app/section/section/header/div[3]/nav/a[2]')

        if (len(logged_out) > 0):
            browser.find_element(By.XPATH, '/html/body/app/section/section/header/div[3]/nav/a[2]').click()

            WebDriverWait(browser, delay).until(EC.any_of(EC.element_to_be_clickable((By.CLASS_NAME, 'form-control')), EC.element_to_be_clickable((By.CLASS_NAME, 'w-100 btn btn-lg btn-primary'))))
            email = browser.find_element(By.NAME, 'Input.Email')
            password = browser.find_element(By.NAME, 'Input.Password')
            email.send_keys("stinkypineapple")
            password.send_keys("z%7eDsh$wXy3^S")
            remember_me = browser.find_element(By.CLASS_NAME , 'form-check-input').click()
            submit = browser.find_element(By.ID , 'login-submit').click()

            WebDriverWait(browser, delay).until(EC.element_to_be_clickable((By.XPATH, '/html/body/app/section/aside/div[2]/div/nav/ul/li[4]/a')))
            #pickle.dump(browser.get_cookies(), open("cookies.pkl", "wb"))

        WebDriverWait(browser, delay).until(EC.element_to_be_clickable((By.XPATH, '/html/body/app/section/aside/div[2]/div/nav/ul/li[4]/a')))
        time.sleep(2)
        browser.find_element(By.XPATH, '/html/body/app/section/aside/div[2]/div/nav/ul/li[4]/a').click()
        WebDriverWait(browser, delay).until(EC.presence_of_element_located((By.XPATH, '/html/body/app/section/section/main/div/div[2]')))
        time.sleep(5)
        if browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div/div[2]') != []:
            select_char(browser, CHAR_NAME[bot_number-1], 5)
            # if CHAR_NAME[bot_number-1] not in browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div/div[2]')[0].get_attribute("innerHTML"):
            #     element = browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[1]/div/div[2]')
            #     selected_bot = element.find_elements(By.XPATH, './child::*')
            #     i = 1
            #     while browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[1]/div/div[2]/div[' + str(i) + ']') != []:
            #         e = browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[1]/div/div[2]/div[' + str(i) + ']')
            #         dropdown = browser.find_element(By.XPATH , '/html/body/app/section/section/main/div[1]/div/div[1]').click()
            #         e.click()
            #         time.sleep(5)
            #         if CHAR_NAME[bot_number-1] in browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[4]/td')[0].get_attribute("innerHTML"):
            #             break
            #         i = i + 1
    except Exception as e:
        print(e)
    print("Web Page Ready")
    return browser

def update_log(browser, history, last_restock, action, latest_log, stopped_count, active_profile, active_profile_personal, bot_number, char_select_failed, read_type, log_time, log_status = 0, cmd_hwnd = 0, mail_task = "", active_profile_path = [""]):
    read_type = "OCR"
    error_time = None
    exception_total = 0
    ###################
    # OCR READ
    ###################
    if (char_select_failed > 10) or (read_type == "OCR"):
        try:
            read_type = "OCR"
            hwnd = win32gui.FindWindow(None, 'Log Console')

            if hwnd == 0:
                pm_loc, pm_res = find_pm_loc("moveTo")
                if pm_loc == None:
                    login_pm(r"C:\Users\A\Desktop\PM\PixelMaster.exe", cmd_hwnd, 1, settingswap = 1, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1])
                    new_profile = pixelmaster_custom_script(0, active_profile_personal, active_profile_path, cmd_hwnd, 1, combat_type = COMBAT[bot_number-1], combat_class = COMBAT_CLASS[bot_number-1], rot_type = ROTATION_TYPE[bot_number-1], mail_task = mail_task)
                else:
                    pm_open_log(bot_number, run = 1)
                log_status = 1
            
            file1 = open(r'C:\Users\A\Desktop\bn.txt', 'r')
            lines = file1.readlines()
            for line in lines:
                if 'vGPU' in line:
                    pc_name = line

            #image = pyautogui.screenshot(region=(266, 32, 574, 288))
            #image = pyautogui.screenshot(region=(748, 419, 254, 305))
            pm_log_screenshot(r"Z:\httpd\screenshot\\" + pc_name + ".png")
            #image.save(r"Z:\httpd\screenshot\\" + pc_name + ".png")

            image = cv2.imread(r"Z:\httpd\screenshot\\" + pc_name + ".png", cv2.IMREAD_COLOR)
            if os.path.exists(r"Z:\httpd\screenshot\\" + pc_name + "2.png"):
                image_old = cv2.imread(r"Z:\httpd\screenshot\\" + pc_name + "2.png", cv2.IMREAD_COLOR)
            else:
                image_old = None

            image = image[30:342, 0:258]
            hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
            frame_threshold = cv2.inRange(hsv, (0, 0, 0), (160, 190, 170))

            data = pytesseract.image_to_string(frame_threshold, lang='eng', config='--psm 6')
        except Exception:
            return browser, history, action, last_restock, latest_log, 1, stopped_count, 1, "", "Not Working", 0, "", "", log_status, log_time, read_type, error_time

        #print(data)
        index = 0
        logs = []
        for i in range(0, len(data)):
            if data[i] == '\n':
                if (i - index) > 2:
                    logs.append(data[index:i])
                index = i + 1
        number_index = -1
        index = 0
        while index < len(logs):
            element = logs[index]
            if (element.find(':') == -1) or (element[element.find(':')-2:element.find(':')] != ("%02d" % datetime.now().hour)):
                if number_index != -1:
                    logs[number_index] += " " + logs[index]
                    logs.pop(index)
                else:
                    logs.pop(index)
                continue
            else:
                number_index = index
            index += 1
        # for element in logs:
        #     print(element)

        log_start_time = datetime.now()
        log_found = 0
        if not np.array_equal(image, image_old):
            shutil.copy(r"Z:\httpd\screenshot\\" + pc_name + ".png", r"Z:\httpd\screenshot\\" + pc_name + "2.png")
            if logs != []:
                if history == []:
                    for element in logs:
                        now = datetime.now()
                        log = element
                        log_time = datetime(now.year, now.month, now.day, int(log[log.find(':')-2:log.find(':')]), int(log[log.find(':')+1:log.find(':')+3]), int(log[log.find(':')+4:log.find(':')+6]))
                        #if (datetime.timestamp(now) - datetime.timestamp(log_time)) > 3600:
                            #log_time = datetime.fromtimestamp(datetime.timestamp(log_time) + 6*60*60 + 30*60)
                        action, last_restock, error_time, exception_total = find_log_items(action, log, log_time, last_restock, active_profile, active_profile_personal, error_time, 1, exception_total)
                        if (datetime.timestamp(now) - datetime.timestamp(log_time)) < 30:
                            log = log.replace(log[log.find(':')-2:log.find(':')+6], str(log_time.hour).zfill(2) + ':' + str(log_time.minute).zfill(2) + ':' + str(log_time.second).zfill(2))
                            print(log)
                            history.append(log)
                            latest_log = datetime.now()
                            action, last_restock, error_time, exception_total = find_log_items(action, history[-1], log_time, last_restock, active_profile, active_profile_personal, error_time)

                            with open(LOG_PATH, 'r') as file1:
                                Lines = file1.readlines()
                            if len(Lines) > 70:
                                for i in range(0, len(Lines)):
                                    if i == len(Lines) - 1:
                                        Lines[i] = log + "\n"
                                        break
                                    else:
                                        Lines[i] = Lines[i + 1]
                            else:
                                Lines.append(log + "\n")
                            with open(LOG_PATH, 'w') as file1:
                                file1.seek(0)
                                file1.writelines(Lines)
                            with open(LOG_PATH_FULL, 'a') as file2:
                                file2.write(log + '\n')
                else:
                    offset = 0
                    similar_found = 0
                    index = len(logs) - 1
                    #print(logs)
                    now = datetime.now()
                    count = 1
                    for element in logs[::-1]:
                        log = element
                        if (log.find(':')) != -1:
                            try:
                                if int(log[log.find(':')+4:log.find(':')+6]) > 59:
                                    log = log[0:log.find(':')+4] + "59" + log[log.find(':')+4:log.find(':')+6]
                                if int(log[log.find(':')+1:log.find(':')+3]) > 59:
                                    log = log[0:log.find(':')+1] + "59" + log[log.find(':')+1:log.find(':')+3]
                                if int(log[log.find(':')-2:log.find(':')]) > 23:
                                    log = log[0:log.find(':')-2] + "23" + log[log.find(':')-2:log.find(':')]
                                log_time = datetime(now.year, now.month, now.day, int(log[log.find(':')-2:log.find(':')]), int(log[log.find(':')+1:log.find(':')+3]), int(log[log.find(':')+4:log.find(':')+6]))
                                if (datetime.timestamp(now) - datetime.timestamp(log_time)) > 3600:
                                    log_time = datetime.fromtimestamp(datetime.timestamp(log_time) + 6*60*60 + 30*60)
                                if count == 1:
                                    log_start_time = log_time
                                if (datetime.timestamp(now) - datetime.timestamp(log_time)) < 30:
                                    log_found = 1
                                    log = log.replace(log[log.find(':')-2:log.find(':')+6], str(log_time.hour).zfill(2) + ':' + str(log_time.minute).zfill(2) + ':' + str(log_time.second).zfill(2))
                                    #print(element)
                                    if index > 0:
                                        if logs[index].lower() == logs[index - 1].lower():
                                            logs.pop(index)
                                            index -= 1
                                            continue
                                    similarity_ratio = SequenceMatcher(None, history[-1].lower(), log.lower()).ratio()
                                    #if history[-1].lower() != log.lower():
                                    if (similarity_ratio < 0.95) and (similar_found == 0):
                                        offset += 1
                                    else:
                                        break
                            except Exception:
                                continue
                        count += 1
                    index -= 1

                    if offset >= 0:
                        now = datetime.now()
                        for pos in range(offset + 1):
                            if pos == offset:                                
                                duplicate_check = 1
                            else:
                                duplicate_check = 0
                            latest_log = datetime.now()                            
                            removed_item = ''
                            #if (offset - pos) <= len(logs):
                            log = logs[-1-offset+pos+1]
                            try:
                                if int(log[log.find(':')+4:log.find(':')+6]) > 59:
                                    log = log[0:log.find(':')+4] + "59" + log[log.find(':')+4:log.find(':')+6]
                                if int(log[log.find(':')+1:log.find(':')+3]) > 59:
                                    log = log[0:log.find(':')+1] + "59" + log[log.find(':')+1:log.find(':')+3]
                                if int(log[log.find(':')-2:log.find(':')]) > 23:
                                    log = log[0:log.find(':')-2] + "23" + log[log.find(':')-2:log.find(':')]
                                log_time = datetime(now.year, now.month, now.day, int(log[log.find(':')-2:log.find(':')]), int(log[log.find(':')+1:log.find(':')+3]), int(log[log.find(':')+4:log.find(':')+6]))
                                
                                action, last_restock, error_time, exception_total = find_log_items(action, log, log_time, last_restock, active_profile, active_profile_personal, error_time, check_exception = 1, exception_count = exception_total)
                                if (datetime.timestamp(now) - datetime.timestamp(log_time)) > 3600:
                                    log_time = datetime.fromtimestamp(datetime.timestamp(log_time) + 6*60*60 + 30*60)
                                if (datetime.timestamp(now) - datetime.timestamp(log_time)) < 30:
                                    log = log.replace(log[log.find(':')-2:log.find(':')+6], str(log_time.hour).zfill(2) + ':' + str(log_time.minute).zfill(2) + ':' + str(log_time.second).zfill(2))
                                    if len(history) > 200:
                                        removed_item = history.pop(len(history)-1)
                                    if removed_item.lower() != log.lower():
                                        if duplicate_check == 0:
                                            print(log)
                                        history.append(log)
                                    action, last_restock, error_time, exception_total = find_log_items(action, log, log_time, last_restock, active_profile, active_profile_personal, error_time, duplicate_check = duplicate_check)

                                    if duplicate_check == 1:
                                        history.pop(len(history)-1)

                                    with open(LOG_PATH, 'r') as file1:
                                        Lines = file1.readlines()
                                    if len(Lines) > 70:
                                        temp_val = Lines[1]
                                        Lines[1] = Lines[0]
                                        Lines[0] = log + "\n"
                                        for i in range(1, len(Lines)):
                                            # if i == len(Lines) - 1:
                                            #     Lines[i] = log + "\n"
                                            #     break
                                            if i != (len(Lines) - 1):
                                                temp_val2 = Lines[i + 1]
                                                Lines[i + 1] = temp_val
                                                temp_val = temp_val2
                                    else:
                                        Lines.append(log + "\n")
                                    with open(LOG_PATH, 'w') as file1:
                                        file1.seek(0)
                                        file1.writelines(Lines)
                                    with open(LOG_PATH_FULL, 'a') as file2:
                                        file2.write(log + '\n')
                            except Exception as e:
                                print(e)
                                continue
        #if log_found == 0:
            #log_time = log_start_time
        char_select_failed = 0
        return browser, history, action, last_restock, latest_log, 1, stopped_count, 1, "", "Not Working", 0, "", "", log_status, log_time, read_type, error_time
    ###################
    # WEB SCRAPE
    ###################
    profile_name = ''
    profile_state = ''
    char_level = ''
    char_name = ''
    run_time = 0
    bot_running = 0
    char_selected = 0
    log_found = 0
    log_start_time = datetime.now()
    try:
        if select_char(browser, CHAR_NAME[bot_number-1], char_select_failed):
            char_selected = 1
            char_select_failed = 0
        else:
            char_select_failed += 1

        # RESTART PAGE IF CRASHED
        if browser.find_elements(By.XPATH, '/html/body/div[6]/h5') != []:
            browser.quit()
            browser = start_web(bot_number)

        profile_state = ""

        # CHECK IF BOT IS RUNNING
        if char_selected == 1:
            if (browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[3]/td') != []) and (browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[3]/td').text == "Running"):
                bot_running = 1
                stopped_count = 0
            else:
                bot_running = 0
                stopped_count += 1          

            if browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[4]/td') != []:
                profile_state = browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[4]/td').text
            if browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[2]/td') != []:
                profile_name = browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[2]/td').text
            if browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[6]/td') != []:
                char_level = browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[6]/td').text
            if browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[5]/td') != []:
                char_name = browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[5]/td').text
            if browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[3]/td') != []:
                run_time = browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[3]/td').text

            logs = browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[1]/div[1]/div/div[2]/div/div')
            temp = logs.copy()

            if temp != []:
                log_found = 1
                if history == []:
                    for element in temp:
                        if hasattr(element, 'text') == True:
                            now = datetime.now()
                            log = element.text

                            if now.hour < (int(log[log.find(':')-2:log.find(':')])):
                                log_time = datetime(now.year, now.month, now.day - 1, int(log[log.find(':')-2:log.find(':')]), int(log[log.find(':')+1:log.find(':')+3]), int(log[log.find(':')+4:log.find(':')+6]))
                            else:                          
                                log_time = datetime(now.year, now.month, now.day, int(log[log.find(':')-2:log.find(':')]), int(log[log.find(':')+1:log.find(':')+3]), int(log[log.find(':')+4:log.find(':')+6]))

                            if (datetime.timestamp(now) - datetime.timestamp(log_time)) > 3600:
                                log_time = datetime.fromtimestamp(datetime.timestamp(log_time) + 6*60*60 + 30*60)
                            if (datetime.timestamp(now) - datetime.timestamp(log_time)) < 30:
                                log =  log.replace(log[log.find(':')-2:log.find(':')+6], str(log_time.hour).zfill(2) + ':' + str(log_time.minute).zfill(2) + ':' + str(log_time.second).zfill(2))
                                print(log)
                                history.append(log)
                                latest_log = datetime.now()
                                action, last_restock, error_time, exception_total = find_log_items(action, history[-1], log_time, last_restock, active_profile, active_profile_personal, error_time)

                                with open(LOG_PATH, 'r') as file1:
                                    Lines = file1.readlines()
                                if len(Lines) > 70:
                                    for i in range(0, len(Lines)):
                                        if i == len(Lines) - 1:
                                            Lines[i] = log + "\n"
                                            break
                                        else:
                                            Lines[i] = Lines[i + 1]
                                else:
                                    Lines.append(log + "\n")
                                with open(LOG_PATH, 'w') as file1:
                                    file1.seek(0)
                                    file1.writelines(Lines)
                                with open(LOG_PATH_FULL, 'a') as file2:
                                    file2.write(log + '\n')
                else:
                    offset = 0
                    index = len(temp) - 1
                    #print(temp)
                    now = datetime.now()
                    for element in temp[::-1]:
                        if hasattr(element, 'text') == True:
                            log = element.text

                            if now.hour < (int(log[log.find(':')-2:log.find(':')])):
                                log_time = datetime(now.year, now.month, now.day - 1, int(log[log.find(':')-2:log.find(':')]), int(log[log.find(':')+1:log.find(':')+3]), int(log[log.find(':')+4:log.find(':')+6]))
                            else:                          
                                log_time = datetime(now.year, now.month, now.day, int(log[log.find(':')-2:log.find(':')]), int(log[log.find(':')+1:log.find(':')+3]), int(log[log.find(':')+4:log.find(':')+6]))

                            if (datetime.timestamp(now) - datetime.timestamp(log_time)) > 3600:
                                log_time = datetime.fromtimestamp(datetime.timestamp(log_time) + 6*60*60 + 30*60)
                            if (datetime.timestamp(now) - datetime.timestamp(log_time)) < 30:
                                log = log.replace(log[log.find(':')-2:log.find(':')+6], str(log_time.hour).zfill(2) + ':' + str(log_time.minute).zfill(2) + ':' + str(log_time.second).zfill(2))
                                #print(element)
                                if index > 0:
                                    if temp[index] == temp[index - 1]:
                                        temp.pop(index)
                                        continue
                                if history[-1] != log:
                                    offset += 1
                                else:
                                    break
                        else:
                            temp.pop(index)
                        index -= 1
                    if offset != 0:
                        now = datetime.now()
                        for pos in range(offset):
                            latest_log = datetime.now()                            
                            removed_item = ''
                            log = temp[-1-offset+pos+1].text

                            if now.hour < (int(log[log.find(':')-2:log.find(':')])):
                                log_time = datetime(now.year, now.month, now.day - 1, int(log[log.find(':')-2:log.find(':')]), int(log[log.find(':')+1:log.find(':')+3]), int(log[log.find(':')+4:log.find(':')+6]))
                            else:
                                log_time = datetime(now.year, now.month, now.day, int(log[log.find(':')-2:log.find(':')]), int(log[log.find(':')+1:log.find(':')+3]), int(log[log.find(':')+4:log.find(':')+6]))

                            if (datetime.timestamp(now) - datetime.timestamp(log_time)) > 3600:
                                log_time = datetime.fromtimestamp(datetime.timestamp(log_time) + 6*60*60 + 30*60)
                            if (datetime.timestamp(now) - datetime.timestamp(log_time)) < 30:
                                log = log.replace(log[log.find(':')-2:log.find(':')+6], str(log_time.hour).zfill(2) + ':' + str(log_time.minute).zfill(2) + ':' + str(log_time.second).zfill(2))
                                if len(history) > 200:
                                    removed_item = history.pop(len(history)-1)
                                if removed_item != log:
                                    print(log)
                                    history.append(log)
                                action, last_restock, error_time, exception_total = find_log_items(action, log, log_time, last_restock, active_profile, active_profile_personal, error_time)

                                with open(LOG_PATH, 'r') as file1:
                                    Lines = file1.readlines()
                                if len(Lines) > 70:
                                    temp_val = Lines[1]
                                    Lines[1] = Lines[0]
                                    Lines[0] = log + "\n"
                                    for i in range(1, len(Lines)):
                                        # if i == len(Lines) - 1:
                                        #     Lines[i] = log + "\n"
                                        #     break
                                        if i != (len(Lines) - 1):
                                            temp_val2 = Lines[i + 1]
                                            Lines[i + 1] = temp_val
                                            temp_val = temp_val2
                                else:
                                    Lines.append(log + "\n")
                                with open(LOG_PATH, 'w') as file1:
                                    file1.seek(0)
                                    file1.writelines(Lines)
                                with open(LOG_PATH_FULL, 'a') as file2:
                                    file2.write(log + '\n')
    except Exception as e:
        print(e)
        browser.quit()
        browser = start_web(bot_number)
    if log_found == 0:
        log_time = log_start_time
    run_time = "0:00:00:01"
    return browser, history, action, last_restock, latest_log, char_selected, stopped_count, bot_running, profile_state, profile_name, char_level, char_name, run_time, log_status, log_time, read_type, error_time

def find_log_items(action, latest_log, log_time, last_restock, active_profile, active_profile_personal, error_time, check_exception = 0, exception_count = 0, duplicate_check = 0):    
    if check_exception == 1:
        exception_count += 1
        if (('unhandled' in latest_log) and ('exception' in latest_log)):
            if exception_count >= 2:
                action[6] = 1
                action[45] = 1
                exception_count = 0
        else:
            exception_count = 0
        return action, last_restock, error_time, exception_count
    active_profile = active_profile.lower()
    active_profile_personal = active_profile_personal.lower()
    latest_log = latest_log.lower()
    if check_exception == 0:
        if duplicate_check == 0:
            if ('auction house reached' in latest_log):
                action[0] = 1                
            if ('profile \'hearthstone\' finished successfully' in latest_log):
                action[1] = 1
            if (('profile' in latest_log) and ('finished successfully' in latest_log)):
                action[1] = 1
            if ('whisper' in latest_log):
                action[2] = 1
            if ('bag free' in latest_log):
                last_restock = datetime.now()
                action[3] = 1
            if ('flying path' in latest_log):
                action[4] = 1
            if ('combat started' in latest_log):
                action[5] = 1
            if (('unhandled' in latest_log) and ('exception' in latest_log)) or ('finished with error' in latest_log):
                error_time = log_time
                action[6] = 1
                action[45] = 1
            #or ('failed to find a complete path' in latest_log)
            if ('player is stucked' in latest_log) or ('player stucked' in latest_log) or ('player stucked' in latest_log):
                action[7] = 1
            if (active_profile in latest_log) or (active_profile_personal in latest_log) or ('\'' + active_profile + '\'' in latest_log) or ('\'' + active_profile_personal + '\'' in latest_log):
                action[8] = 1
            if ('gear durability percent is' in latest_log):
                action[9] = 1
            if ('group #3' in latest_log):
                action[10] = 1
            if ('interacting with' in latest_log) and ('flight master' in latest_log):
                action[11] = 1
            if ('resurrection started' in latest_log):
                action[12] = 1
            if ('going to vendor' in latest_log):
                action[13] = 1
            if ('task \'sell, mail and repair items\' in group' in latest_log) and ('completed' in latest_log):
                action[14] = 1
            if ('preparation started' in latest_log):
                action[15] = 1
            if ('deaths by players' in latest_log):
                action[16] = 1
            if ('transport' in latest_log):
                action[17] = 1
            if ('preparation finished' in latest_log):
                action[18] = 1
            if (('corpse position' in latest_log) or ('carpse position' in latest_log)) and ('failed' in latest_log):
                action[19] = 1
            if ('login again' in latest_log) or ('logged in' in latest_log):
                action[20] = 1
            if (('not' in latest_log) and ('read addon data' in latest_log)) or (('not' in latest_log) and ('pixel cell' in latest_log)):
                action[21] = 1
            if ('looting items finished' in latest_log):
                action[22] = 1
            if ('failed to sell and mail items' in latest_log):
                action[23] = 1
            # if ('game world' in latest_log):
            #     action[24] = 1
            if ('please login again' in latest_log):
                action[25] = 1
            if ('training' in latest_log) and ('skills' in latest_log):
                action[26] = int(''.join(filter(str.isdigit, latest_log[latest_log.find(':')+6:len(latest_log)])))
            if ('use hearthstone' in latest_log) and ('completed' in latest_log):
                action[27] = 1
            if ('trained' in latest_log) and ('skills' in latest_log):
                action[28] = int(''.join(filter(str.isdigit, latest_log[latest_log.find(':')+6:len(latest_log)])))
                print(action[28])
            if ('closest vendor' in latest_log) and ('fail' in latest_log):
                action[29] = 1
            if ('not find' in latest_log) and ('vendor' in latest_log):
                action[30] = 1
            if ('flying' in latest_log) and ('and' in latest_log) and ('destination' in latest_log) and ('is' in latest_log) and ('in' in latest_log) and ('the' in latest_log) and ('air' in latest_log):
                action[31] = 1
            if ('77-82' in latest_log):
                action[33] = 1
            if ('free bag slots is' in latest_log) or ('going to repair items' in latest_log):
                action[34] = 1
            if ('restart auto' in latest_log):
                action[35] = 1
            if ('interact with npc' in latest_log):
                action[36] = 1
            if ('mining only' in latest_log):
                action[37] = 1
            if ('travelling py' in latest_log):
                action[38] = 1
            if ('gathering py' in latest_log):
                action[39] = 1
            if ('pause for input' in latest_log):
                action[40] = 1
            if ('destiny but no object' in latest_log):
                action[41] = 1
            if ('train ' in latest_log) and (('miner' in latest_log) or ('herbalist' in latest_log)):
                action[42] = 1
            if ('profile' in latest_log):
                action[43] = 1
            if ('corpse' in latest_log) and ('failed' in latest_log):
                action[44] = 1
            if ('mail' in latest_log) and ('error' in latest_log):
                action[46] = 1
            if (('unhandled' in latest_log) and ('exception' in latest_log)) or (('mailing' in latest_log) and ('no object found' in latest_log)):
                action[47] = 1
            if ('go to outlands' in latest_log):
                action[48] = 1
            if ('stop now' in latest_log):        
                pyautogui.press('f4')
                time.sleep(1)
                sys.exit()
        else:
            if ('corpse' in latest_log) and ('failed' in latest_log):                
                action[49] = 1
        if ('blacklisting gathering node' in latest_log):
            print(active_profile_personal)
            if ('gathering' in active_profile_personal):
                print("Y")
                with open(r'Z:\\Common Files\\ProfileErrors\\' + active_profile_personal + '_errors.txt', 'a') as file:
                    file.write(latest_log)

    return action, last_restock, error_time, exception_count

def tester(browser):
    time.sleep(10)
    if browser.find_elements(By.XPATH, '/html/body/div[6]/h5') == []:
        print("YES")

def profile_web_action(browser, action, bot_number, type = "OCR"):
    if (type == "OCR") and (action == "stop"):        
        stop_running_profile()
        return True
    else:
        return True
    ###############################
    char_selected = 0
    try:
        if browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[4]/td') != []:
            if CHAR_NAME[bot_number-1] not in browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[4]/td')[0].get_attribute("innerHTML"):
                element = browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[1]/div/div[2]')
                selected_bot = element.find_elements(By.XPATH, './child::*')
                i = 1
                while browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[1]/div/div[2]/div[' + str(i) + ']') != []:
                    e = browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[1]/div/div[2]/div[' + str(i) + ']')
                    dropdown = browser.find_element(By.XPATH , '/html/body/app/section/section/main/div[1]/div/div[1]').click()
                    e.click()
                    time.sleep(5)
                    if browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[4]/td') != []:
                        if CHAR_NAME[bot_number-1] in browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[4]/td')[0].get_attribute("innerHTML"):
                            char_selected = 1
                            break
                    i = i + 1
            else:
                char_selected = 1
        state = ''
        if (browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[3]/td') != []):
            state = browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[3]/td').text
        if char_selected == 1:
            if action == "stop" and state == 'Running':
                print("Stopping profile from browser")
                browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[1]/div[2]/button[2]').click()
    except Exception as e:
        print(e)
        return False
    return True

def select_char(browser, char_name, char_select_failed):
    if char_select_failed >= 5:
        if browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[5]/td') != []:
            if char_name not in browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[5]/td')[0].get_attribute("innerHTML"):
                browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[1]/a').click()
                WebDriverWait(browser, 20).until(EC.presence_of_element_located((By.XPATH, '/html/body/app/section/section/main/div/div[2]/div[1]/div[1]')))
                time.sleep(5)
        elif browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div/div[2]/div[1]/div[1]') == []:
            return False 

        i = 1
        while browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div/div[2]/div[' + str(i) + ']/div[1]') != []:
            e = browser.find_element(By.XPATH, '/html/body/app/section/section/main/div/div[2]/div[' + str(i) + ']/div[1]')
            location = e.location
            size = e.size
            x = location['x'] + size['width'] // 2
            y = location['y'] + size['height'] // 2

            actions = ActionChains(browser)

            # Move to the element and click
            actions.move_to_element(e).key_down(Keys.CONTROL).click().key_up(Keys.CONTROL).perform()
            
            # Wait for character name to show up
            WebDriverWait(browser, 20).until(EC.presence_of_element_located((By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[5]/td')))
            time.sleep(5)
            if char_name not in browser.find_elements(By.XPATH, '/html/body/app/section/section/main/div[2]/div[2]/div[2]/div/table/thead/tr[5]/td')[0].get_attribute("innerHTML"):
                browser.find_element(By.XPATH, '/html/body/app/section/section/main/div[1]/a').click()
            else:
                return True
            WebDriverWait(browser, 20).until(EC.presence_of_element_located((By.XPATH, '/html/body/app/section/section/main/div/div[2]/div[' + str(i+1) + ']/div[1]')))
            time.sleep(5)
            i += 1
        return False