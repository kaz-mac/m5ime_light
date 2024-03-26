"""
ime2serialwrite.py
IMEの状態の変化をシリアルポートに出力する

Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
Released under the MIT license.
see https://opensource.org/licenses/MIT

ビルド方法
pyinstaller --onefile --windowed --icon=app_icon.ico --add-data "app_icon.png;." ime2serialwrite.py

使い方：
.exeファイルと同じ場所に以下の様な config.ini ファイルを置く（portは適宜修正のこと）
[Serial]
port=COM3
baudrate=115200
"""
from ctypes import *
from time import sleep
import win32gui
import win32con
import win32api
import serial
import configparser
from pystray import MenuItem as item
import pystray
from PIL import Image
import threading
import sys
import os
import webbrowser
imm32 = WinDLL("imm32")

## パス関連
def resource_path(relative_path):
    if getattr(sys, 'frozen', False):
        base_path = sys._MEIPASS
    else:
        base_path = os.path.dirname(__file__)
    return os.path.join(base_path, relative_path)
icon_path = resource_path('app_icon.png')
config_path = os.path.join(os.path.dirname(sys.executable), 'config.ini')

## 設定ファイルの読み込み
config = configparser.ConfigParser()
config.read(config_path)
port = config['Serial']['port']
baudrate = int(config['Serial']['baudrate'])

## グローバル変数
ser = None
icon = None
reconnect = True

## シリアルポートに接続
def connect_serialport():
    global ser
    if ser is not None: return
    for i in range(10):
        print("connecting...")
        try:
            ser = serial.Serial(port, baudrate, timeout=1)
            print("connected")
            break
        except serial.SerialException:
            print("cannot connect to "+port+", retrying")
            sleep(0.5)

## シリアルポートを切断
def disconnect_serialport():
    global ser
    print("disconnecting...")
    if ser is not None:
        ser.close()
        ser = None
        print("disconnected")

## シリアルポートに出力
def send_serialport(text):
    global ser
    if ser is None: return
    if not reconnect: return
    for i in range(10):
        try:
            if ser is not None:
                ser.write(f"{text}\r\n".encode())
                print(f"data {text} sent")
                break
        except serial.SerialException:
            print("cannot send data, retrying")
            disconnect_serialport()
            sleep(0.5)
            connect_serialport()  # 再接続する

## 状態監視
def check_ime_status():
    IMEStatusLast = -1
    while True:
        hWnd1 = win32gui.GetForegroundWindow()
        hWnd2 = imm32.ImmGetDefaultIMEWnd(hWnd1)
        IMEStatus = win32api.SendMessage(hWnd2, win32con.WM_IME_CONTROL, 0x005, 0)
        if IMEStatus != IMEStatusLast:
            send_serialport("EN" if IMEStatus==0 else "JA")
        IMEStatusLast = IMEStatus
        sleep(0.1)

## メニュー操作
def menu_connect_serialport():
    global reconnect
    reconnect = True
    connect_serialport()
def menu_disconnect_serialport():
    global reconnect
    reconnect = True
    disconnect_serialport()

## スタートアップフォルダを開く
def open_startup_folder():
    startup_path = os.path.expandvars('%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup')
    webbrowser.open(startup_path)

## 終了
def exit_action():
    global icon
    send_serialport("SL")   # 終了前にスリープモードを指示する
    icon.stop()
    disconnect_serialport()

## タスクトレイアイコンの設定
def setup_tray_icon():
    global icon
    icon_image = Image.open(icon_path)
    menu_items = (
        item('接続', menu_connect_serialport),
        item('切断', menu_disconnect_serialport),
        item('スタートアップ', open_startup_folder),
        item('終了', exit_action), 
    )
    icon = pystray.Icon("IME2SerialWrite", icon_image, "IME2SerialWrite", menu_items)
    icon.run()

## Windowsの終了時に特定の処理を行う
def on_system_event(dwCtrlType):
    if dwCtrlType in (win32con.CTRL_SHUTDOWN_EVENT, win32con.CTRL_LOGOFF_EVENT):
        print("Shutting down, cleaning up...")
        exit_action()
        return True
    return False

## メイン
if __name__ == "__main__":
    connect_serialport()
    ime_thread = threading.Thread(target=check_ime_status)
    ime_thread.daemon = True
    ime_thread.start()
    setup_tray_icon()
