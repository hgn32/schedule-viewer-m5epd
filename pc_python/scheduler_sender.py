import win32com.client
import json
import time
import re
import unicodedata
from datetime import datetime, timedelta, timezone
import os
import threading
import pythoncom

try:
    import serial
except ImportError:
    serial = None

CONFIG_FILE = 'config.json'
FILTER_FILE = 'filter_words.txt'

shared_serial = None
serial_lock = threading.Lock()

class FileWriter:
    def __init__(self, filename='output.txt'):
        self.filename = filename

    def write(self, data):
        if isinstance(data, bytes):
            data = data.decode('utf-8')
        with open(self.filename, 'a', encoding='utf-8') as f:
            f.write(data)

def load_config():
    with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
        return json.load(f)

def load_filter_patterns():
    if not os.path.exists(FILTER_FILE):
        return []
    patterns = []
    with open(FILTER_FILE, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if '=' in line:
                try:
                    pattern, repl = line.split('=', 1)
                    patterns.append((re.compile(pattern), repl))
                except re.error as e:
                    print(f"[WARN] Invalid regex: {line} ({e})")
    return patterns

def to_halfwidth(text):
    return unicodedata.normalize('NFKC', text)

def clean_text(text, pattern_list):
    text = to_halfwidth(text)
    for pattern, repl in pattern_list:
        text = pattern.sub(repl, text)
    return text.strip()

def get_outlook_appointments(patterns):
    pythoncom.CoInitialize()
    outlook = win32com.client.Dispatch("Outlook.Application")
    namespace = outlook.GetNamespace("MAPI")
    calendar = namespace.GetDefaultFolder(9).Items
    calendar.IncludeRecurrences = True

    jst = timezone(timedelta(hours=9))
    now = datetime.now(tz=jst)
    start = now.replace(hour=0, minute=0, second=0, microsecond=0)
    end = start + timedelta(days=1)

    calendar.Sort("[Start]")
    restriction = f"[Start] >= '{start.strftime('%m/%d/%Y %H:%M %p')}' AND [End] <= '{end.strftime('%m/%d/%Y %H:%M %p')}'"
    items = calendar.Restrict(restriction)

    events = []
    for item in items:
        try:
            # 0	空き
            # 1	仮の予定
            # 2	忙しい
            # 3	外出中
            # 4	他の予定あり
            if item.BusyStatus in (0, 1):
                continue
            start_dt = item.Start
            end_dt = item.End
            # item.Start/.End from Outlook COM carry JST wall-clock values
            # tagged as UTC, so .timestamp() yields epoch values 9h ahead
            # of true UTC. Subtract the JST offset to correct.
            start_ts = int(start_dt.timestamp()) - 9 * 3600
            end_ts = int(end_dt.timestamp()) - 9 * 3600

            subject = clean_text(item.Subject, patterns)
            location = clean_text(item.Location, patterns)
            if not location:
                location = ""
            summary = f"{subject}"
            events.append((start_ts, end_ts, summary, location))
        except Exception:
            continue
    return events

def build_event_message():
    now_ts = int((datetime.utcnow() + timedelta(hours=9)).timestamp())
    patterns = load_filter_patterns()

    message = [f"NOW:{now_ts}", "EVENT:CLEAR"]
    for start_ts, end_ts, summary, location in get_outlook_appointments(patterns):
        message.append(f"EVENT:ADD\t{start_ts}\t{end_ts}\t{summary}\t{location}")
    message.append("EVENT:FINISH")
    return "\n".join(message) + "\n"

def send_schedule(com_port):
    msg = build_event_message()
    print(f"[SEND] 送信データ:\n{msg}")

    if com_port.upper() == "FILE":
        fw = FileWriter()
        fw.write(msg)
    else:
        global shared_serial
        if shared_serial:
            try:
                with serial_lock:
                    shared_serial.write(msg.encode('utf-8'))
            except Exception as e:
                print(f"[ERROR] COM送信失敗: {e}")

def wait_until_next_10min():
    now = datetime.now()
    next_minute = (now.minute // 10 + 1) * 10
    next_hour = now.hour
    if next_minute == 60:
        next_minute = 0
        next_hour = (now.hour + 1) % 24
    next_time = now.replace(hour=next_hour, minute=next_minute, second=0, microsecond=0)
    wait_sec = (next_time - now).total_seconds()
    if wait_sec < 0:
        wait_sec += 600
    print(f"[INFO] 次の送信まで {int(wait_sec)} 秒待機 ({next_time.strftime('%H:%M')})")
    time.sleep(wait_sec)

def serial_listener(com_port):
    global shared_serial
    if com_port.upper() == "FILE" or serial is None:
        print("[DEBUG] シリアル監視スキップ（FILEまたはpyserial未インポート）")
        return

    def reopen_serial():
        global shared_serial
        idle_count = 0
        while True:
            try:
              shared_serial = serial.Serial(com_port, 115200, timeout=0.1)
              print("[DEBUG] シリアルポート再接続成功")
              break
            except Exception as e:
                print(f"[ERROR] シリアルポート再接続失敗")
            idle_count += 1
            sleep_duration = 1 if idle_count < 10 else 60
            time.sleep(sleep_duration)

    try:
        shared_serial = serial.Serial(com_port, 115200, timeout=0.1)
        print("[DEBUG] シリアルポート接続成功")
    except Exception as e:
        print(f"[ERROR] シリアルポート初回接続失敗")
        shared_serial = None

    def listen():
        idle_count = 0
        while True:
            try:
                if shared_serial.in_waiting:
                    idle_count = 0
                    line = shared_serial.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(f"[RECV] 受信データ: {line}")
                        if line == "REQ:ALL":
                            print("[INFO] REQ:ALL 受信 → 即時送信")
                            send_schedule(com_port)
            except Exception as e:
                print(f"[ERROR] シリアル監視中エラー")
                reopen_serial()
            idle_count += 1
            sleep_duration = 0.5 if idle_count < 10 else 10
            time.sleep(sleep_duration)

    t = threading.Thread(target=listen, daemon=True)
    t.start()
    print("[DEBUG] シリアル監視スレッド開始")

def main():
    config = load_config()
    com_port = config.get("com_port")

    serial_listener(com_port)

    while True:
        wait_until_next_10min()
        send_schedule(com_port)

if __name__ == '__main__':
    main()
  