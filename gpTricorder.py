import tkinter as tk
import requests as rq
import pathlib as pl
import os
import platform
import socket
import subprocess
import time
import re
from tkinter import ttk, Canvas
try:
    import cv2
except ImportError:
    cv2 = None
from PIL import Image, ImageTk
# import picamera
import io
from html.parser import HTMLParser
from html import unescape
from datetime import datetime

# Detect GPIO support at runtime so the app can still run on non-RPi systems
use_gpio = False
try:
    import RPi.GPIO as GPIO
    use_gpio = True
except (ImportError, RuntimeError):
    GPIO = None

# Weather Variables
cWeather = None
class weather:
    def __init__(self, temp, precip, humidity, sfc, wind_speed="", wind_dir="", detailed="", alerts=None, hazard="UNKNOWN"):
        self.temp = temp
        self.humidity = humidity
        self.precip = precip
        self.sfc = sfc
        self.wind_speed = wind_speed
        self.wind_dir = wind_dir
        self.detailed = detailed
        self.alerts = alerts or []
        self.hazard = hazard

class RosterEvent:
    def __init__(self, date="", start="", title="", location="", details=""):
        self.date = date
        self.start = start
        self.title = title
        self.location = location
        self.details = details

WEATHER_URL = 'https://api.weather.gov/gridpoints/DVN/33,63/forecast/hourly'
WEATHER_ALERT_URL = 'https://api.weather.gov/alerts/active?point=41.66,-91.53'
NORMAL_BUTTON_BG = '#86DF64'
HIGHLIGHT_BUTTON_BG = '#DAD778'
ROSTER_PAGE_SIZE = 3
PAGE_PAD_X = 58
PAGE_WRAP = 600
video_paths = []
video_buttons = []
cl_buttons = []
clPos = 0
roster_page_index = 0
LOCAL_MODE = os.environ.get("TRICORDER_LOCAL", "").lower() in ("1", "true", "yes")
IS_RASPBERRY_PI = (
    not LOCAL_MODE
    and platform.system() == "Linux"
    and pl.Path("/proc/device-tree/model").exists()
)
PROJECT_DIR = pl.Path(__file__).resolve().parent
PI_VIDEO_DIR = pl.Path('/home/tricorder/networkdrive/Videos')
LOCAL_VIDEO_DIR = PROJECT_DIR / 'videos'
VIDEO_DIR = pl.Path(os.environ.get(
    'TRICORDER_VIDEO_DIR',
    PI_VIDEO_DIR if IS_RASPBERRY_PI else LOCAL_VIDEO_DIR
))

gpio_states = {}

# Define key mappings
key_mappings = {
    17: 'up',
    18: 'down',
    27: 'left',
    22: 'right',
    23: 'enter'
}


def initialize_gpio():
    global use_gpio
    if LOCAL_MODE:
        use_gpio = False
        return
    if not use_gpio:
        return
    try:
        GPIO.setmode(GPIO.BCM)
        for pin in key_mappings:
            GPIO.setup(pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)
            gpio_states[pin] = GPIO.input(pin)
    except RuntimeError as e:
        print(f"GPIO disabled: {e}")
        use_gpio = False


def wake_tricorder_display():
    """Wake the Tk window and, on Pi/Linux, ask X11 to unblank the display."""
    try:
        window.deiconify()
        window.lift()
        window.focus_force()
    except tk.TclError:
        pass

    if platform.system() != "Linux" or not os.environ.get("DISPLAY"):
        return

    for command in (("xset", "s", "reset"), ("xset", "dpms", "force", "on")):
        try:
            subprocess.run(command, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            return


def get_button_list_for_page():
    def active_buttons(buttons):
        return [button for button in buttons if str(button.cget('state')) != tk.DISABLED]

    if currentPage == "mm":
        return [
            planet_butt,
            CL_butt,
            roster_butt,
            input_butt,
            stat_butt
        ]
    if currentPage == "cl":
        return cl_buttons
    if currentPage == "player":
        return [play_button, pause_button, stop_button]
    if currentPage == "pl":
        return [planet_back_button, refresh_weather_button]
    if currentPage == "status":
        return [status_back_button, status_refresh_button]
    if currentPage == "input":
        return [input_back_button, input_submit_button]
    if currentPage == "roster":
        return active_buttons([roster_back_button, roster_prev_button, roster_next_button, roster_refresh_button])
    return []


def highlight_next_button(event=None):
    wake_tricorder_display()
    buttons = get_button_list_for_page()
    if not buttons:
        return
    current_button = current_highlighted
    try:
        index = buttons.index(current_button)
        next_button = buttons[(index + 1) % len(buttons)]
    except ValueError:
        next_button = buttons[0]
    highlight_button(next_button)


def highlight_previous_button(event=None):
    wake_tricorder_display()
    buttons = get_button_list_for_page()
    if not buttons:
        return
    current_button = current_highlighted
    try:
        index = buttons.index(current_button)
        previous_button = buttons[(index - 1) % len(buttons)]
    except ValueError:
        previous_button = buttons[-1]
    highlight_button(previous_button)


def handle_enter(event=None):
    global current_highlighted
    wake_tricorder_display()

    if current_highlighted:
        current_highlighted.invoke()

def hat():
    if not use_gpio:
        window.after(250, hat)
        return

    for pin, key in key_mappings.items():
        current_state = GPIO.input(pin)
        previous_state = gpio_states.get(pin, GPIO.HIGH)
        gpio_states[pin] = current_state
        if previous_state == GPIO.HIGH and current_state == GPIO.LOW:
            wake_tricorder_display()
            if key in ('up', 'left'):
                highlight_previous_button()
            elif key in ('down', 'right'):
                highlight_next_button()
            elif key == 'enter':
                handle_enter()
            break

    window.after(250, hat)


def all_highlightable_buttons():
    return [
        planet_butt,
        CL_butt,
        stat_butt,
        input_butt,
        roster_butt,
        planet_back_button,
        refresh_weather_button,
        captains_log_back_button,
        status_back_button,
        status_refresh_button,
        input_back_button,
        input_submit_button,
        roster_back_button,
        roster_prev_button,
        roster_next_button,
        roster_refresh_button,
        play_button,
        pause_button,
        stop_button,
        *video_buttons,
    ]


def highlight_button(button):
    global current_highlighted
    for candidate in all_highlightable_buttons():
        candidate.config(bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, relief=tk.RAISED)

    current_highlighted = button

    if button:
        button.config(bg=HIGHLIGHT_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, relief=tk.RAISED)
        button.focus_set()  # Set focus on the highlighted button


def show_planet_page():
    global currentPage
    header.pack_forget()
    center.pack_forget()
    topButtons.pack_forget()
    bottomButtons.pack_forget()
    global cWeather
    cWeather = get_weather()
    tempVar.set(cWeather.temp)
    humVar.set(cWeather.humidity)
    sfcVar.set(cWeather.sfc)
    precVar.set(cWeather.precip)
    sfc_label.config(text=build_hazard_text(cWeather))
    currentPage = "pl"
    highlight_button(planet_back_button)
    planet_page.pack(side='left')


def show_status_page():
    global currentPage
    header.pack_forget()
    center.pack_forget()
    topButtons.pack_forget()
    bottomButtons.pack_forget()
    currentPage = "status"
    refresh_status_text()
    highlight_button(status_refresh_button)
    status_page.pack(side='left', fill='both', expand=True)


def show_input_page():
    global currentPage
    header.pack_forget()
    center.pack_forget()
    topButtons.pack_forget()
    bottomButtons.pack_forget()
    currentPage = "input"
    highlight_button(input_submit_button)
    input_page.pack(side='left', fill='both', expand=True)


def show_roster_page():
    global currentPage
    header.pack_forget()
    center.pack_forget()
    topButtons.pack_forget()
    bottomButtons.pack_forget()
    player_page.pack_forget()
    status_page.pack_forget()
    input_page.pack_forget()
    currentPage = "roster"
    refresh_roster()
    highlight_button(roster_back_button)
    roster_page.pack(side='left', fill='both', expand=True)


def get_local_ip():
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(1)
        sock.connect(("8.8.8.8", 80))
        address = sock.getsockname()[0]
        sock.close()
        return address
    except OSError:
        return "Offline"


def measure_link_latency():
    start = time.monotonic()
    try:
        rq.get('https://api.weather.gov', timeout=5)
        return int((time.monotonic() - start) * 1000), True
    except Exception:
        return 0, False


def refresh_status_text():
    latency_ms, internet_ok = measure_link_latency()
    try:
        socket.gethostbyname('api.weather.gov')
        dns_status = "OK"
    except OSError:
        dns_status = "FAIL"

    local_ip = get_local_ip()
    weather_status = "READY" if cWeather and cWeather.sfc != "Error" else "STANDBY"
    status_result = (
        "SUBSPACE LINK\n"
        f"NET: {'ONLINE' if internet_ok else 'OFFLINE'}\n"
        f"LATENCY: {latency_ms} ms\n"
        f"DNS: {dns_status}\n"
        f"LOCAL IP: {local_ip}\n"
        f"WEATHER: {weather_status}\n"
        f"GPIO: {'READY' if use_gpio else 'SIM'}\n"
        f"LOGS: {len(video_paths)} files"
    )
    status_text.config(text=status_result)


def submit_input():
    command = input_entry.get().strip()
    if not command:
        input_result.config(text="Please type a command or note above.")
        return
    if command.lower() in ('status', 'refresh', 'weather'):
        refresh_status_text()
        input_result.config(text=f"Command received: {command}. Status refreshed.")
    else:
        input_result.config(text=f"Stored note: {command}")
    input_entry.delete(0, tk.END)


def enumerate_videos():
    if not VIDEO_DIR.exists():
        try:
            VIDEO_DIR.mkdir(parents=True, exist_ok=True)
        except OSError as e:
            print(f"Video directory not available: {VIDEO_DIR} ({e})")
            return
    for item in VIDEO_DIR.iterdir():
        if item.is_file() and item.name.endswith('.mp4'):
            video_paths.append(item)
            print(item)


def show_captains_log_page():
    global currentPage, clPos
    header.pack_forget()
    center.pack_forget()
    topButtons.pack_forget()
    bottomButtons.pack_forget()
    player_page.pack_forget()
    status_page.pack_forget()
    input_page.pack_forget()
    clPos = 0
    currentPage = "cl"
    highlight_button(captains_log_back_button)
    captains_log_page.pack()


def show_main_menu():
    global currentPage
    planet_page.pack_forget()
    captains_log_page.pack_forget()
    player_page.pack_forget()
    status_page.pack_forget()
    input_page.pack_forget()
    roster_page.pack_forget()
    header.pack()
    center.pack()
    topButtons.pack()
    bottomButtons.pack()
    currentPage = "mm"
    highlight_button(planet_butt)

def show_video_page(path):
    global currentPage
    captains_log_page.pack_forget()
    status_page.pack_forget()
    input_page.pack_forget()
    player_page.pack()
    currentPage = "player"
    highlight_button(play_button)
    start_video(str(path))

def parse_wind_mph(wind_text):
    numbers = [int(value) for value in re.findall(r'\d+', wind_text or "")]
    return max(numbers) if numbers else 0


def get_weather_alerts():
    try:
        r = rq.get(WEATHER_ALERT_URL, timeout=8)
        r.raise_for_status()
        data = r.json()
        alerts = []
        for feature in data.get('features', [])[:3]:
            props = feature.get('properties', {})
            event = props.get('event') or props.get('headline') or "Weather Alert"
            severity = props.get('severity', '')
            alerts.append(f"{event} {severity}".strip())
        return alerts
    except Exception as e:
        print(f"Weather alert fetch error: {e}")
        return []


def classify_hazard(temp, precip, wind_speed, alerts):
    try:
        temp = int(temp)
    except (TypeError, ValueError):
        temp = 0
    try:
        precip = int(precip)
    except (TypeError, ValueError):
        precip = 0

    if alerts:
        return "RED"
    if temp <= 20 or temp >= 95 or wind_speed >= 35 or precip >= 70:
        return "AMBER"
    if temp <= 32 or temp >= 88 or wind_speed >= 25 or precip >= 40:
        return "CAUTION"
    return "GREEN"


def build_hazard_text(report):
    lines = [
        f"HAZARD: {report.hazard}",
        f"SCAN: {report.sfc}",
        f"WIND: {report.wind_dir} {report.wind_speed}".strip(),
    ]
    if report.alerts:
        lines.append("ALERTS:")
        lines.extend(report.alerts[:2])
    else:
        lines.append("ALERTS: NONE ACTIVE")
    return "\n".join(lines)


def get_weather():
    try:
        r = rq.get(url=WEATHER_URL, timeout=10)
        data = r.json()
        period = data['properties']['periods'][0]
        shortForecast = period.get('shortForecast', 'Unknown')
        temp = period.get('temperature', 0)
        precip_obj = period.get('probabilityOfPrecipitation', {})
        precip = precip_obj.get('value', 0) if isinstance(precip_obj, dict) else 0
        humidity_obj = period.get('relativeHumidity', {})
        humidity = humidity_obj.get('value', 0) if isinstance(humidity_obj, dict) else 0
        wind_speed = period.get('windSpeed', '')
        wind_dir = period.get('windDirection', '')
        detailed = period.get('detailedForecast', shortForecast)
        alerts = get_weather_alerts()
        hazard = classify_hazard(temp, precip, parse_wind_mph(wind_speed), alerts)
        return weather(temp, precip, humidity, shortForecast, wind_speed, wind_dir, detailed, alerts, hazard)
    except Exception as e:
        print(f"Weather fetch error: {e}")
        return weather(0, 0, 0, "Error", "", "", "Unable to fetch weather", [], "UNKNOWN")

def get_roster():
    """Fetch and parse TrekFest event schedule"""
    events = []
    try:
        r = rq.get('https://trekfest.org/event-schedule', timeout=15)
        r.raise_for_status()
        html = r.text

        # Extract text blocks from HTML
        text = re.sub(r'<[^>]+>', '\n', html)
        text = unescape(text.replace('&nbsp;', ' '))
        lines = [line.strip() for line in text.split('\n') if line.strip()]
        
        # Parse events: look for time markers and dates
        i = 0
        while i < len(lines):
            line = lines[i]
            
            # Look for time markers (e.g., "12:30 PM")
            if re.search(r'\d{1,2}:\d{2}\s*(AM|PM|am|pm)', line):
                time_match = re.search(r'\d{1,2}:\d{2}\s*(AM|PM|am|pm)', line)
                start_time = time_match.group(0).upper() if time_match else line.strip()
                event_title = ""
                event_location = ""
                
                # Next few lines are title and location
                if i + 1 < len(lines):
                    event_title = lines[i + 1].strip(" -:•")
                if i + 2 < len(lines) and not re.search(r'\d{1,2}:\d{2}', lines[i + 2]):
                    event_location = lines[i + 2].strip(" -:•")
                if "@" in event_title and not event_location:
                    event_title, event_location = [part.strip() for part in event_title.split("@", 1)]
                if " - " in event_title and not event_location:
                    event_title, event_location = [part.strip() for part in event_title.split(" - ", 1)]
                
                if event_title:
                    event = RosterEvent(
                        date="TrekFest 2026",
                        start=start_time,
                        title=event_title,
                        location=event_location,
                        details=""
                    )
                    events.append(event)
                    i += 3
                else:
                    i += 1
            else:
                i += 1
        
        return events
    except Exception as e:
        print(f"Roster fetch error: {e}")
        return []

roster_events = []
def refresh_roster():
    global roster_events, roster_page_index
    roster_events = get_roster()
    roster_page_index = 0
    update_roster_display()


def get_roster_page_count():
    if not roster_events:
        return 1
    if len(roster_events) <= 2:
        return 1
    remaining = len(roster_events) - 2
    return 1 + ((remaining + ROSTER_PAGE_SIZE - 1) // ROSTER_PAGE_SIZE)


def show_previous_roster_page():
    global roster_page_index
    if roster_page_index > 0:
        roster_page_index -= 1
        update_roster_display()
    highlight_button(roster_prev_button if roster_page_index > 0 else roster_back_button)


def show_next_roster_page():
    global roster_page_index
    page_count = get_roster_page_count()
    if roster_page_index < page_count - 1:
        roster_page_index += 1
        update_roster_display()
    highlight_button(roster_next_button if roster_page_index < page_count - 1 else roster_back_button)

initialize_gpio()
window = tk.Tk(className='Tricorder')
trekFont = "Trek"
username = "Scott Thunder"
window_size = os.environ.get("TRICORDER_WINDOW_SIZE", "720x576")
window.geometry(window_size)
window.configure(bg='black',width=720,height=576)

window.attributes('-fullscreen', IS_RASPBERRY_PI)
if IS_RASPBERRY_PI:
    window.configure(cursor='none')

tempVar = tk.StringVar()
humVar = tk.StringVar()
sfcVar = tk.StringVar()
precVar=tk.StringVar()

# Main Menu Frames
header = tk.Frame(window)
center = tk.Frame(window)
topButtons = tk.Frame(window)
bottomButtons = tk.Frame(window,bg='black')


current_highlighted = None




top = tk.Label(header, text="USS ENTERPRISE NCC-1701 STANDARD ISSUE",
               font=(trekFont, 42), bg='black', fg='#86DF64', padx=5, pady=10)
tricorder = tk.Label(center, text="TRICORDER",
                     font=(trekFont,135), fg='#DAD778', bg='black')

planet_butt = tk.Button(topButtons, font=(trekFont,39), text="PLANET", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=5, pady=5, command=show_planet_page)
CL_butt = tk.Button(topButtons, font=(trekFont,39), text="CAPTAIN'S LOG", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=5, pady=5, command=show_captains_log_page)

userLabel = tk.Label(bottomButtons, text=username, font=(trekFont,39), bg='black', fg='#DAD778', pady=9)
stat_butt = tk.Button(bottomButtons, font=(trekFont,39), text="STATUS", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=5, pady=5, command=show_status_page)
input_butt = tk.Button(bottomButtons, font=(trekFont,39), text="INPUT", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=5, pady=5, command=show_input_page)
roster_butt = tk.Button(topButtons, font=(trekFont,39), text="DUTY ROSTER", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=5, pady=5, command=show_roster_page)

# Create separate frames for each page
planet_page = tk.Frame(window, bg='black', padx=PAGE_PAD_X, pady=18)
captains_log_page = tk.Frame(window, bg='black', padx=PAGE_PAD_X, pady=18)
player_page = tk.Frame(window, bg='black')
status_page = tk.Frame(window, bg='black', padx=PAGE_PAD_X, pady=18)
input_page = tk.Frame(window, bg='black', padx=PAGE_PAD_X, pady=18)
roster_page = tk.Frame(window, bg='black', padx=PAGE_PAD_X, pady=18)

# Planet Internal Frames
pl_header = tk.Frame(planet_page,bg='black',padx=0,pady=4)
pl_middle = tk.Frame(planet_page,bg='black')
temp_frame = tk.Frame(pl_middle,bg='#DAD778',padx=10,pady=8)
right_pl_frame = tk.Frame(pl_middle,bg='black',padx=5,pady=5)
humid_frame = tk.Frame(right_pl_frame,bg='#DAD778',padx=10,pady=8)
precip_frame = tk.Frame(right_pl_frame, bg='#DAD778',padx=10,pady=8)
sfc_frame = tk.Frame(planet_page,bg='black', pady=12)


# Add widgets to the planet page (WEATHER)
planet_label = tk.Label(pl_header, text="Hazard Scan", font=(trekFont,42), bg='black', fg='#DAD778',padx=10)
planet_back_button = tk.Button(pl_header, font=(trekFont,26), text="Back", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=8, pady=4)

temp_title = tk.Label(temp_frame, font=(trekFont,26),text='TEMP', bg='#DAD778',fg='black', padx=5, pady=4)
temp_label =tk.Label(temp_frame, font=(trekFont,28),textvariable=tempVar, padx=0, pady=4,bg='#DAD778', fg='black')
temp_symbol = tk.Label(temp_frame, font=(trekFont,26),text="°", padx=0, bg='#DAD778', fg='black')
farenheight = tk.Label(temp_frame, text="F",font=(trekFont, 30),padx=8, bg='#DAD778',fg='black')

humid_label = tk.Label(humid_frame, font=(trekFont,24),text="HUM ",fg='black',bg='#DAD778')
humid_var_label =tk.Label(humid_frame,font=(trekFont,24),textvariable=humVar, fg='black', bg='#DAD778')
humidPC_label = tk.Label(humid_frame, font=(trekFont,24),text='%', fg='black', bg='#DAD778')

precip_label = tk.Label(precip_frame, font=(trekFont,24), text='RAIN ', fg='black', bg='#DAD778')
precip_var_label = tk.Label(precip_frame, font=(trekFont,24),textvariable=precVar, fg='black', bg='#DAD778')
precipPC_label = tk.Label(precip_frame, font=(trekFont,24), text='%', fg='black', bg='#DAD778')

sfc_label = tk.Label(sfc_frame, font=(trekFont,25), textvariable=sfcVar, fg='#DAD778', bg='black', justify='left', wraplength=PAGE_WRAP)
refresh_weather_button = tk.Button(planet_page, font=(trekFont,26), text="Refresh", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=8, pady=4, command=show_planet_page)
                       
# Captain's Log Internal Frames

cl_header = tk.Frame(captains_log_page, bg='black', padx=0, pady=3)
logHolder = tk.Frame(captains_log_page, bg='black', padx=14, pady=3)
logsL = tk.Frame(logHolder, bg="black")
logsR = tk.Frame(logHolder, bg="black")

# Add widgets to the captain's log page
captains_log_label = tk.Label(cl_header, text="Captain's Log", font=(trekFont,42), bg='black', fg='#DAD778', padx=5)
captains_log_back_button = tk.Button(cl_header, font=(trekFont,26), text="Back", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=8, pady=4)

# Add status page widgets
status_nav = tk.Frame(status_page, bg='black')
status_label = tk.Label(status_page, text="Subspace Link", font=(trekFont,42), bg='black', fg='#DAD778')
status_back_button = tk.Button(status_nav, font=(trekFont,26), text="Back", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=8, pady=4)
status_refresh_button = tk.Button(status_nav, font=(trekFont,26), text="Refresh", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=8, pady=4, command=refresh_status_text)
status_text = tk.Label(status_page, font=(trekFont,28), text="", bg='black', fg='#DAD778', justify='left', wraplength=PAGE_WRAP)

# Add input page widgets
input_label = tk.Label(input_page, text="Command", font=(trekFont,42), bg='black', fg='#DAD778')
input_back_button = tk.Button(input_page, font=(trekFont,26), text="Back", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=8, pady=4)
input_entry = tk.Entry(input_page, font=(trekFont,26), width=18, bg='#DAD778', fg='black')
input_submit_button = tk.Button(input_page, font=(trekFont,26), text="Submit", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=8, pady=4)
input_result = tk.Label(input_page, font=(trekFont,25), text="Enter a command or note.", bg='black', fg='#DAD778', wraplength=PAGE_WRAP, justify='left')

# Add roster page widgets
roster_header = tk.Frame(roster_page, bg='black', padx=0, pady=3)
roster_controls = tk.Frame(roster_page, bg='black', padx=0, pady=3)
roster_content_frame = tk.Frame(roster_page, bg='black', padx=0, pady=3)
roster_scroll = tk.Frame(roster_content_frame, bg='black')

roster_label = tk.Label(roster_header, text="Duty Roster", font=(trekFont,42), bg='black', fg='#DAD778', padx=5)
roster_back_button = tk.Button(roster_controls, font=(trekFont,24), text="Back", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=6, pady=3)
roster_prev_button = tk.Button(roster_controls, font=(trekFont,24), text="Prev", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=6, pady=3)
roster_next_button = tk.Button(roster_controls, font=(trekFont,24), text="Next", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=6, pady=3)
roster_refresh_button = tk.Button(roster_controls, font=(trekFont,24), text="Refresh", bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=6, pady=3)
roster_page_label = tk.Label(roster_header, text="", font=(trekFont,20), bg='black', fg='#DAD778', padx=5)
roster_text = tk.Label(roster_scroll, font=(trekFont,30), text="Loading events...", bg='black', fg='#DAD778', justify='left', wraplength=PAGE_WRAP)

def update_roster_display():
    """Update the roster display with fetched events"""
    if not roster_events:
        roster_text.config(text="NO EVENTS FOUND\nPress REFRESH to scan again.")
        roster_page_label.config(text="Page 1 of 1")
        roster_prev_button.config(state=tk.DISABLED)
        roster_next_button.config(state=tk.DISABLED)
    else:
        page_count = get_roster_page_count()
        if roster_page_index == 0:
            start = 0
        else:
            start = 2 + ((roster_page_index - 1) * ROSTER_PAGE_SIZE)
        end = start + ROSTER_PAGE_SIZE
        lines = []
        if roster_page_index == 0:
            now_event = roster_events[0]
            lines.append(f"NOW: {now_event.start}")
            lines.append(now_event.title)
            if now_event.location:
                lines.append(f"AT: {now_event.location}")
            if len(roster_events) > 1:
                next_event = roster_events[1]
                lines.append("")
                lines.append(f"NEXT: {next_event.start}")
                lines.append(next_event.title)
                if next_event.location:
                    lines.append(f"AT: {next_event.location}")
            if len(roster_events) > 2:
                lines.append("")
                lines.append("MORE: press NEXT")
        else:
            for event in roster_events[start:end]:
                lines.append(f"{event.start}  {event.title}")
                if event.location:
                    lines.append(f"AT: {event.location}")
                lines.append("")
        text_output = "\n".join(lines).strip()
        roster_text.config(text=text_output if text_output else "No events available.")
        roster_page_label.config(text=f"Page {roster_page_index + 1} of {page_count}")
        roster_prev_button.config(state=tk.NORMAL if roster_page_index > 0 else tk.DISABLED)
        roster_next_button.config(state=tk.NORMAL if roster_page_index < page_count - 1 else tk.DISABLED)

enumerate_videos()
alternator = 0
for video in video_paths:
    if alternator == 0:
        newButton = tk.Button(logsL,text=video.name.removesuffix('.mp4'),font=(trekFont,30), bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', command= lambda tV=video:show_video_page(tV))
        alternator = 1
    else:
        newButton = tk.Button(logsR,text=video.name.removesuffix('.mp4'),font=(trekFont,30), bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', command= lambda tV=video:show_video_page(tV))
        alternator = 0
    video_buttons.append(newButton)

def start_video(path):
    global cap
    global is_paused
    global is_stopped
    if cv2 is None:
        is_paused = True
        is_stopped = True
        canvas.delete("all")
        canvas.create_text(360, 260, text="OpenCV is not installed.\nVideo playback is disabled.", fill=HIGHLIGHT_BUTTON_BG, font=(trekFont, 30), justify='center')
        print("OpenCV is not installed; video playback is disabled.")
        return
    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        is_paused = True
        is_stopped = True
        canvas.delete("all")
        canvas.create_text(360, 260, text="Unable to open video.", fill=HIGHLIGHT_BUTTON_BG, font=(trekFont, 30), justify='center')
        return
    is_paused = False
    is_stopped = False
    update_frame()

def update_frame():
    if not is_paused and not is_stopped:
        ret, frame = cap.read()
        if ret:
            frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            frame = Image.fromarray(frame)
            frame = ImageTk.PhotoImage(frame)

            canvas.create_image(-110,0,anchor=tk.NW,image=frame)
            canvas.image = frame
            window.after(30, update_frame)
        
    # if not is_stopped:
    #     window.after(10, update_frame)

def play_video():
    global is_paused
    global is_stopped
    if is_paused:
        is_paused = False
        update_frame()

def pause_video():
    global is_paused
    is_paused = True

def stop_video():
    global is_stopped
    is_stopped = True
    if 'cap' in globals() and cap is not None:
        cap.release()
    canvas.delete("all")
    show_captains_log_page()

def on_close():
    global is_stopped
    is_stopped = True
    if 'cap' in globals() and cap is not None:
        cap.release()
    window.destroy()

# camera = picamera.PiCamera()
# camera.resolution = (320,240)

# Add bits inside video player
canvas = Canvas(player_page, width=720, height=526)
canvas.pack()
play_button = tk.Button(player_page, font=(trekFont,30), text="Play", command=play_video, bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=5, pady=5)
pause_button = tk.Button(player_page, font=(trekFont,30), text="Pause",command=pause_video, bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=5, pady=5)
stop_button = tk.Button(player_page, font=(trekFont,30), text="Stop", command=stop_video, bg=NORMAL_BUTTON_BG, activebackground=HIGHLIGHT_BUTTON_BG, fg='black', padx=5, pady=5)
play_button.pack(side='left')
pause_button.pack(side='left')
stop_button.pack(side='left')



header.pack()
top.pack()
center.pack()
tricorder.pack()
topButtons.pack()
planet_butt.pack(side='left')
CL_butt.pack(side='left')
stat_butt.pack(side='left')
roster_butt.pack(side='left')

bottomButtons.pack()
userLabel.pack(side='left')
input_butt.pack(side='left')

# Add functionality to back buttons
planet_back_button.config(command=show_main_menu)
captains_log_back_button.config(command=show_main_menu)
status_back_button.config(command=show_main_menu)
input_back_button.config(command=show_main_menu)
roster_back_button.config(command=show_main_menu)
roster_prev_button.config(command=show_previous_roster_page)
roster_next_button.config(command=show_next_roster_page)
roster_refresh_button.config(command=refresh_roster)
input_submit_button.config(command=submit_input)

# Add back buttons to respective pages
pl_header.pack()
planet_back_button.pack(side='left')
planet_label.pack(side='left')
pl_middle.pack()
temp_frame.pack(side='left')
temp_title.pack()
temp_label.pack(side='left')
temp_symbol.pack(side='left')
farenheight.pack(side='bottom')

right_pl_frame.pack(side='right')

precip_frame.pack()
precip_label.pack(side='left')
precip_var_label.pack(side='left')
precipPC_label.pack(side='left')

humid_frame.pack(side='bottom')
humid_label.pack(side='left')
humid_var_label.pack(side='left')
humidPC_label.pack(side='left')

sfc_frame.pack(side='bottom')
sfc_label.pack()
refresh_weather_button.pack(pady=15)


cl_header.pack()
captains_log_back_button.pack(side='left')
captains_log_label.pack(side='left')
cl_buttons.append(captains_log_back_button)
logHolder.pack(side='left')
logsL.pack(side="left")
logsR.pack(side="left")
for new_button in video_buttons:
    new_button.pack(side="top")
    cl_buttons.append(new_button)






status_label.pack(pady=10)
status_nav.pack(pady=8)
status_back_button.pack(side='left', padx=8)
status_refresh_button.pack(side='left', padx=8)
status_text.pack(pady=18, padx=0)

input_label.pack(pady=20)
input_back_button.pack(side='left', pady=10)
input_entry.pack(pady=20)
input_submit_button.pack(pady=10)
input_result.pack(pady=15, padx=20)


# Pack roster page widgets
roster_header.pack()
roster_label.pack(side='left')
roster_page_label.pack(side='left')
roster_controls.pack()
roster_back_button.pack(side='left', padx=5)
roster_prev_button.pack(side='left')
roster_next_button.pack(side='left')
roster_refresh_button.pack(side='left', padx=5)
roster_content_frame.pack(fill='both', expand=True)
roster_scroll.pack(fill='both', expand=True)
roster_text.pack(pady=20, padx=20)

# Initially show the main menu
show_main_menu()


highlight_button(planet_butt)

window.bind("<Right>", highlight_next_button)
window.bind("<Left>", highlight_previous_button)
window.bind("<Up>", highlight_previous_button)
window.bind("<Down>", highlight_next_button)
window.bind("<Return>", handle_enter)
window.bind("<Escape>", lambda event: show_main_menu())

window.protocol("WM_DELETE_WINDOW", on_close)
window.after(200, hat)
window.mainloop()
if use_gpio:
    GPIO.cleanup()

    



