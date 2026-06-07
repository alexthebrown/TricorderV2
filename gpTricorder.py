import tkinter as tk
import requests as rq
import pathlib as pl
import os
import time
import re
from tkinter import ttk, Canvas
import cv2
from PIL import Image, ImageTk
# import picamera
import io
from html.parser import HTMLParser
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
    def __init__(self, temp, precip, humidity, sfc, wind_speed="", wind_dir="", detailed=""):
        self.temp = temp
        self.humidity = humidity
        self.precip = precip
        self.sfc = sfc
        self.wind_speed = wind_speed
        self.wind_dir = wind_dir
        self.detailed = detailed

class RosterEvent:
    def __init__(self, date="", start="", title="", location="", details=""):
        self.date = date
        self.start = start
        self.title = title
        self.location = location
        self.details = details

VIDEO_DIR = '/home/tricorder/networkdrive/Videos'
WEATHER_URL = 'https://api.weather.gov/gridpoints/DVN/33,63/forecast/hourly'
video_paths = []
video_buttons = []
cl_buttons = []
clPos = 0
roster_page_num = 0  # Track current roster page for pagination

gpio_states = {}
if use_gpio:
    GPIO.setmode(GPIO.BCM)
    for pin in (17, 18, 27, 22, 23):
        GPIO.setup(pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)
        gpio_states[pin] = GPIO.input(pin)

# Define key mappings
key_mappings = {
    17: 'up',
    18: 'down',
    27: 'left',
    22: 'right',
    23: 'enter'
}


def get_button_list_for_page():
    if currentPage == "mm":
        return [planet_butt, CL_butt, sensor_butt, stat_butt, select_butt, input_butt]
    if currentPage == "cl":
        return cl_buttons
    if currentPage == "player":
        return [play_button, pause_button, stop_button]
    if currentPage == "pl":
        return [planet_back_button]
    if currentPage == "sensor":
        return [roster_prev_button, roster_next_button, roster_back_button, roster_refresh_button]
    if currentPage == "status":
        return [status_back_button]
    if currentPage == "input":
        return [input_back_button, input_submit_button]
    return []


def highlight_next_button(event=None):
    buttons = get_button_list_for_page()

    if not buttons:
        return

    current_button = window.focus_get()

    if current_button not in buttons:
        highlight_button(buttons[0])
        return

    index = buttons.index(current_button)
    next_button = buttons[(index + 1) % len(buttons)]
    highlight_button(next_button)


def highlight_previous_button(event=None):
    buttons = get_button_list_for_page()

    if not buttons:
        return

    current_button = window.focus_get()

    if current_button not in buttons:
        highlight_button(buttons[-1])
        return

    index = buttons.index(current_button)
    previous_button = buttons[(index - 1) % len(buttons)]
    highlight_button(previous_button)

def handle_enter(event=None):
    active_button = window.focus_get()
    if active_button:
        active_button.config(relief=tk.SUNKEN)
        active_button.invoke()

def sleep_system():
    """Put system into sleep mode (GPIO button press will wake it)"""
    if use_gpio:
        try:
            os.system('sudo systemctl suspend 2>/dev/null &')
        except:
            pass

def wake_system():
    """Ensure system is awake and responsive"""
    # Light up the screen or bring window to focus
    window.lift()
    window.focus_force()

def hat():
    if not use_gpio:
        window.after(100, hat)
        return

    inputs = {
        17: GPIO.input(17),
        18: GPIO.input(18),
        27: GPIO.input(27),
        22: GPIO.input(22),
        23: GPIO.input(23)
    }

    pressed = False
    for pin, value in inputs.items():
        previous = gpio_states.get(pin, GPIO.HIGH)
        if previous == GPIO.HIGH and value == GPIO.LOW:
            pressed = True
            wake_system()  # Wake system when any button is pressed
            if pin in (17, 27):
                highlight_previous_button()
            elif pin in (18, 22):
                highlight_next_button()
            elif pin == 23:
                handle_enter()
        gpio_states[pin] = value

    if pressed:
        window.after(200, hat)
    else:
        window.after(100, hat)

def highlight_button(button):
    # Reset all buttons to green
    planet_butt.config(bg='#86DF64')
    CL_butt.config(bg='#86DF64')
    stat_butt.config(bg='#86DF64')
    sensor_butt.config(bg='#86DF64')
    select_butt.config(bg='#86DF64')
    input_butt.config(bg='#86DF64')
    play_button.config(bg='#86DF64')
    pause_button.config(bg='#86DF64')
    stop_button.config(bg='#86DF64')
    planet_back_button.config(bg='#86DF64')
    captains_log_back_button.config(bg='#86DF64')
    roster_back_button.config(bg='#86DF64')
    roster_refresh_button.config(bg='#86DF64')
    roster_prev_button.config(bg='#86DF64')
    roster_next_button.config(bg='#86DF64')
    status_back_button.config(bg='#86DF64')
    input_back_button.config(bg='#86DF64')
    input_submit_button.config(bg='#86DF64')
    
    if button:
        button.config(bg='#DAD778')  # Highlight selected button in yellow
        button.focus_force()


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
    # Display detailed forecast with wind info
    forecast_detail = cWeather.detailed if cWeather.detailed else cWeather.sfc
    if cWeather.wind_speed or cWeather.wind_dir:
        forecast_detail += f"\nWind: {cWeather.wind_dir} {cWeather.wind_speed}"
    sfc_label.config(text=forecast_detail)
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
    highlight_button(status_back_button)
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





def refresh_status_text():
    now = time.strftime('%Y-%m-%d %H:%M:%S')
    video_count = len(video_paths)
    status_result = (
        f"Current Time: {now}\n"
        f"Video Count: {video_count}\n"
        f"Weather API: {'Connected' if cWeather else 'Unknown'}\n"
        f"GPIO Support: {'Enabled' if use_gpio else 'Disabled'}\n"
        f"App Version: TricorderV2"
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
    path = pl.Path(VIDEO_DIR)
    if not path.exists():
        return
    for item in path.iterdir():
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


def show_sensor_page():
    global currentPage
    header.pack_forget()
    center.pack_forget()
    topButtons.pack_forget()
    bottomButtons.pack_forget()
    status_page.pack_forget()
    input_page.pack_forget()
    currentPage = "sensor"
    refresh_roster()
    highlight_button(roster_back_button)
    roster_page.pack(side='left', fill='both', expand=True)


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

# def start_camera():
#     global stream
#     stream = io.BytesIO()
#     camera.start_preview()
#     update_image()

# def update_image():
#     camera.capture(stream,format='jpeg', use_video_port=True)
#     stream.seek(0)
#     image = Image.open(stream)
#     image = ImageTk.PhotoImage(image)
#     image_label.configure(image=image)
#     image_label.image = image
#     stream.seek(0)
#     stream.truncate()
#     window.after(100,update_image)

# def on_closing():
#     camera.stop_preview()
#     camera.close()
#     show_main_menu()


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
        return weather(temp, precip, humidity, shortForecast, wind_speed, wind_dir, detailed)
    except Exception as e:
        print(f"Weather fetch error: {e}")
        return weather(0, 0, 0, "Error", "", "", "Unable to fetch weather")

def get_roster():
    """Fetch and parse TrekFest event schedule"""
    events = []
    try:
        r = rq.get('https://trekfest.org/event-schedule', timeout=15)
        r.raise_for_status()
        html = r.text
        
        # Extract text blocks from HTML
        text = re.sub(r'<[^>]+>', '\n', html)
        text = re.sub(r'&nbsp;', ' ', text)
        text = re.sub(r'&amp;', '&', text)
        lines = [line.strip() for line in text.split('\n') if line.strip()]
        
        # Parse events: look for time markers and dates
        i = 0
        while i < len(lines):
            line = lines[i]
            
            # Look for time markers (e.g., "12:30 PM")
            if re.search(r'\d{1,2}:\d{2}\s*(AM|PM|am|pm)', line):
                start_time = line.strip()
                event_title = ""
                event_location = ""
                
                # Next few lines are title and location
                if i + 1 < len(lines):
                    event_title = lines[i + 1].strip()
                if i + 2 < len(lines) and not re.search(r'\d{1,2}:\d{2}', lines[i + 2]):
                    event_location = lines[i + 2].strip()
                
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
    global roster_events, roster_page_num
    roster_page_num = 0
    roster_events = get_roster()
    update_roster_display()

window = tk.Tk(className='Tricorder')
window.attributes('-fullscreen', False)
trekFont = "Trek"
username = "Scott Thunder"
window.configure(bg='black',width=720,height=576)

window.attributes('-fullscreen',True)

tempVar = tk.StringVar()
humVar = tk.StringVar()
sfcVar = tk.StringVar()
precVar=tk.StringVar()

# Main Menu Frames
header = tk.Frame(window)
center = tk.Frame(window)
topButtons = tk.Frame(window)
bottomButtons = tk.Frame(window,bg='black')





top = tk.Label(header, text="USS ENTERPRISE NCC-1701 STANDARD ISSUE",
               font=(trekFont, 42), bg='black', fg='#86DF64', padx=5, pady=10)
tricorder = tk.Label(center, text="TRICORDER",
                     font=(trekFont,135), fg='#DAD778', bg='black')

planet_butt = tk.Button(topButtons, font=(trekFont,39), text="PLANET", bg='#86DF64', fg='black', padx=5, pady=5, command=show_planet_page)
CL_butt = tk.Button(topButtons, font=(trekFont,39), text="CAPTAIN'S LOG", bg='#86DF64', fg='black', padx=5, pady=5, command=show_captains_log_page)
sensor_butt = tk.Button(topButtons, font=(trekFont,39), text="SENSORS", bg='#86DF64', fg='black', padx=5, pady=5, command=show_sensor_page)

userLabel = tk.Label(bottomButtons, text=username, font=(trekFont,39), bg='black', fg='#DAD778', pady=9)
stat_butt = tk.Button(bottomButtons, font=(trekFont,39), text="STATUS", bg='#86DF64', fg='black', padx=5, pady=5, command=show_status_page)
select_butt = tk.Button(bottomButtons, font=(trekFont,39), text="SELECT", bg='#86DF64', fg='black', padx=5, pady=5, command=show_sensor_page)
input_butt = tk.Button(bottomButtons, font=(trekFont,39), text="INPUT", bg='#86DF64', fg='black', padx=5, pady=5, command=show_input_page)

# Create separate frames for each page
planet_page = tk.Frame(window, bg='black')
captains_log_page = tk.Frame(window, bg='black')
sensor_page = tk.Frame(window, bg='black')
player_page = tk.Frame(window, bg='black')
status_page = tk.Frame(window, bg='black')
input_page = tk.Frame(window, bg='black')
roster_page = tk.Frame(window, bg='black')

# Planet Internal Frames
pl_header = tk.Frame(planet_page,bg='black',padx=5,pady=5)
pl_middle = tk.Frame(planet_page,bg='black')
temp_frame = tk.Frame(pl_middle,bg='#DAD778',padx=5,pady=5)
right_pl_frame = tk.Frame(pl_middle,bg='black',padx=5,pady=5)
humid_frame = tk.Frame(right_pl_frame,bg='#DAD778',pady=5)
precip_frame = tk.Frame(right_pl_frame, bg='#DAD778')
sfc_frame = tk.Frame(planet_page,bg='black', pady=10)


# Add widgets to the planet page (WEATHER)
planet_label = tk.Label(pl_header, text="Planet Conditions", font=(trekFont,81), bg='black', fg='#DAD778',padx=10)
planet_back_button = tk.Button(pl_header, font=(trekFont,45), text="Back", bg='#86DF64', fg='black', padx=5, pady=5)

temp_title = tk.Label(temp_frame, font=(trekFont,30),text='Temperature:', bg='#DAD778',fg='black', padx=5, pady=5)
temp_label =tk.Label(temp_frame, font=(trekFont,30),textvariable=tempVar, padx=0, pady=5,bg='#DAD778', fg='black')
temp_symbol = tk.Label(temp_frame, font=(trekFont,30),text="°", padx=0, bg='#DAD778', fg='black')
farenheight = tk.Label(temp_frame, text="F",font=(trekFont, 45),padx=15, bg='#DAD778',fg='black')

humid_label = tk.Label(humid_frame, font=(trekFont,30),text="Humidity: ",fg='black',bg='#DAD778')
humid_var_label =tk.Label(humid_frame,font=(trekFont,30),textvariable=humVar, fg='black', bg='#DAD778')
humidPC_label = tk.Label(humid_frame, font=(trekFont,30),text='%', fg='black', bg='#DAD778')

precip_label = tk.Label(precip_frame, font=(trekFont,30), text='Chance Precip: ', fg='black', bg='#DAD778')
precip_var_label = tk.Label(precip_frame, font=(trekFont,30),textvariable=precVar, fg='black', bg='#DAD778')
precipPC_label = tk.Label(precip_frame, font=(trekFont,30), text='%', fg='black', bg='#DAD778')

sfc_label = tk.Label(sfc_frame, font=(trekFont,30), textvariable=sfcVar, fg='#DAD778', bg='black')
refresh_weather_button = tk.Button(planet_page, font=(trekFont,30), text="Refresh", bg='#86DF64', fg='black', padx=5, pady=5, command=show_planet_page)
                       
# Captain's Log Internal Frames

cl_header = tk.Frame(captains_log_page, bg='black', padx=14, pady=3)
logHolder = tk.Frame(captains_log_page, bg='black', padx=14, pady=3)
logsL = tk.Frame(logHolder, bg="black")
logsR = tk.Frame(logHolder, bg="black")

# Add widgets to the captain's log page
captains_log_label = tk.Label(cl_header, text="Captain's Log Page", font=(trekFont,75), bg='black', fg='#DAD778', padx=5)
captains_log_back_button = tk.Button(cl_header, font=(trekFont,30), text="Back", bg='#86DF64', fg='black', padx=5, pady=5)

# Add widgets to the Sensors page
image_label = tk.Label(sensor_page)

# Add status page widgets
status_label = tk.Label(status_page, text="Status Overview", font=(trekFont,60), bg='black', fg='#DAD778')
status_back_button = tk.Button(status_page, font=(trekFont,30), text="Back", bg='#86DF64', fg='black', padx=5, pady=5)
status_text = tk.Label(status_page, font=(trekFont,26), text="", bg='black', fg='#DAD778', justify='left')

# Add input page widgets
input_label = tk.Label(input_page, text="Command Console", font=(trekFont,60), bg='black', fg='#DAD778')
input_back_button = tk.Button(input_page, font=(trekFont,30), text="Back", bg='#86DF64', fg='black', padx=5, pady=5)
input_entry = tk.Entry(input_page, font=(trekFont,28), width=24, bg='#DAD778', fg='black')
input_submit_button = tk.Button(input_page, font=(trekFont,30), text="Submit", bg='#86DF64', fg='black', padx=5, pady=5)
input_result = tk.Label(input_page, font=(trekFont,26), text="Enter a command or note.", bg='black', fg='#DAD778', wraplength=680, justify='left')

# Add roster page widgets
def roster_prev_page():
    global roster_page_num
    roster_page_num = max(0, roster_page_num - 1)
    update_roster_display()

def roster_next_page():
    global roster_page_num
    events_per_page = 5
    total_pages = (len(roster_events) + events_per_page - 1) // events_per_page
    roster_page_num = min(roster_page_num + 1, total_pages - 1)
    update_roster_display()

roster_header = tk.Frame(roster_page, bg='black', padx=14, pady=3)
roster_content_frame = tk.Frame(roster_page, bg='black', padx=14, pady=3)
roster_scroll = tk.Frame(roster_content_frame, bg='black')
roster_footer = tk.Frame(roster_page, bg='black', padx=14, pady=3)

roster_label = tk.Label(roster_header, text="Duty Roster", font=(trekFont,75), bg='black', fg='#DAD778', padx=5)
roster_back_button = tk.Button(roster_header, font=(trekFont,30), text="Back", bg='#86DF64', fg='black', padx=5, pady=5)
roster_refresh_button = tk.Button(roster_header, font=(trekFont,30), text="Refresh", bg='#86DF64', fg='black', padx=5, pady=5)
roster_text = tk.Label(roster_scroll, font=(trekFont,20), text="Loading events...", bg='black', fg='#DAD778', justify='left', wraplength=650)
roster_prev_button = tk.Button(roster_footer, font=(trekFont,30), text="PREV", bg='#86DF64', fg='black', padx=5, pady=5, command=roster_prev_page)
roster_next_button = tk.Button(roster_footer, font=(trekFont,30), text="NEXT", bg='#86DF64', fg='black', padx=5, pady=5, command=roster_next_page)
roster_page_label = tk.Label(roster_footer, text="", font=(trekFont,20), bg='black', fg='#DAD778')

def update_roster_display():
    """Update the roster display with fetched events (5 per page)"""
    global roster_page_num
    if not roster_events:
        roster_text.config(text="No events found for TrekFest.")
        roster_page_label.config(text="")
        return
    
    # Calculate pagination
    events_per_page = 5
    total_pages = (len(roster_events) + events_per_page - 1) // events_per_page
    roster_page_num = max(0, min(roster_page_num, total_pages - 1))
    
    start_idx = roster_page_num * events_per_page
    end_idx = start_idx + events_per_page
    page_events = roster_events[start_idx:end_idx]
    
    text_output = ""
    for event in page_events:
        text_output += f"{event.start} - {event.title}\n"
        if event.location:
            text_output += f"  Location: {event.location}\n"
        text_output += "\n"
    
    roster_text.config(text=text_output if text_output else "No events available.")
    roster_page_label.config(text=f"Page {roster_page_num + 1} of {total_pages}")

enumerate_videos()
alternator = 0
for video in video_paths:
    if alternator == 0:
        newButton = tk.Button(logsL,text=video.name.removesuffix('.mp4'),font=(trekFont,30), bg= '#86DF64',fg='black', command= lambda tV=video:show_video_page(tV))
        alternator = 1
    else:
        newButton = tk.Button(logsR,text=video.name.removesuffix('.mp4'),font=(trekFont,30), bg= '#86DF64',fg='black', command= lambda tV=video:show_video_page(tV))
        alternator = 0
    video_buttons.append(newButton)

def start_video(path):
    global cap
    cap = cv2.VideoCapture(path)
    global is_paused 
    is_paused = False
    global is_stopped
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
play_button = tk.Button(player_page, font=(trekFont,30), text="Play", command=play_video, bg='#86DF64', fg='black', padx=5, pady=5)
pause_button = tk.Button(player_page, font=(trekFont,30), text="Pause",command=pause_video, bg='#86DF64', fg='black', padx=5, pady=5)
stop_button = tk.Button(player_page, font=(trekFont,30), text="Stop", command=stop_video, bg='#86DF64', fg='black', padx=5, pady=5)
play_button.pack(side='left')
pause_button.pack(side='left')
stop_button.pack(side='left')



# Add widgets to the status page
sensor_label = tk.Label(sensor_page, text="Sensor Page", font=(trekFont,30), bg='black', fg='#DAD778')
sensor_back_button = tk.Button(sensor_page, font=(trekFont,30), text="Back", bg='#86DF64', fg='black', padx=5, pady=5)

header.pack()
top.pack()
center.pack()
tricorder.pack()
topButtons.pack()
planet_butt.pack(side='left')
CL_butt.pack(side='left')
stat_butt.pack(side='left')

bottomButtons.pack()
userLabel.pack(side='left')
sensor_butt.pack(side='left')
select_butt.pack(side='left')
input_butt.pack(side='left')

# Add functionality to back buttons
planet_back_button.config(command=show_main_menu)
captains_log_back_button.config(command=show_main_menu)
sensor_back_button.config(command=show_main_menu)
status_back_button.config(command=show_main_menu)
input_back_button.config(command=show_main_menu)
roster_back_button.config(command=show_main_menu)
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






status_label.pack(pady=20)
status_back_button.pack(side='left', pady=10)
status_text.pack(pady=20, padx=20)

input_label.pack(pady=20)
input_back_button.pack(side='left', pady=10)
input_entry.pack(pady=20)
input_submit_button.pack(pady=10)
input_result.pack(pady=15, padx=20)


sensor_back_button.pack()
sensor_label.pack()

# Pack roster page widgets
roster_header.pack()
roster_back_button.pack(side='left')
roster_label.pack(side='left')
roster_refresh_button.pack(side='left')
roster_content_frame.pack(fill='both', expand=True)
roster_scroll.pack(fill='both', expand=True)
roster_text.pack(pady=20, padx=20)
roster_footer.pack()
roster_prev_button.pack(side='left', padx=5, pady=10)
roster_page_label.pack(side='left', padx=20, pady=10)
roster_next_button.pack(side='left', padx=5, pady=10)

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

    



