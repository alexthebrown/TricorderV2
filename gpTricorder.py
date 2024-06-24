import tkinter as tk
import requests as rq
import pathlib as pl
import os
import RPi.GPIO as GPIO
import multiprocessing
import time
from tkinter import ttk, Canvas
import cv2
from PIL import Image, ImageTk

# Weather Variables
cWeather = None
class weather:
    def __init__(self,temp, precip, humidity, sfc):
        self.temp = temp
        self.humidity = humidity
        self.precip = precip
        self.sfc = None
video_paths = []
videoDir = '/home/tricorder/networkdrive/Videos'
video_buttons = []
cl_buttons = []
clPos = 0

GPIO.setmode(GPIO.BCM)
GPIO.setup(17, GPIO.IN)  # Up button
GPIO.setup(18, GPIO.IN)  # Down button
GPIO.setup(27, GPIO.IN)  # Left button
GPIO.setup(22, GPIO.IN)  # Right button
GPIO.setup(23, GPIO.IN)  # Enter button

# Define key mappings
key_mappings = {
    17: 'up',
    18: 'down',
    27: 'left',
    22: 'right',
    23: 'enter'
}


def highlight_next_button(event):
    global currentPage
    global clPos
    current_button = window.focus_get()
    if currentPage == "mm":
        if current_button == planet_butt:
            highlight_button(CL_butt)
        elif current_button == CL_butt:
            highlight_button(stat_butt)
        elif current_button == stat_butt:
            highlight_button(sensor_butt)
        elif current_button == sensor_butt:
            highlight_button(select_butt)
        elif current_button == select_butt:
            highlight_button(input_butt)
        elif current_button == input_butt:
            highlight_button(planet_butt)

    elif(currentPage == "pl"):
        pass
    elif(currentPage == "cl"):
        if clPos == len(cl_buttons) - 1:
            clPos = 0
        elif clPos == 0:
            clPos = clPos + 1
        else:
            clPos = clPos + 1
        highlight_button(cl_buttons[clPos])

    elif(currentPage == "stat"):
        pass

def highlight_previous_button(event):
    global currentPage
    global clPos
    current_button = window.focus_get()
    if currentPage == "mm":
        if current_button == planet_butt:
            highlight_button(input_butt)
        elif current_button == CL_butt:
            highlight_button(planet_butt)
        elif current_button == stat_butt:
            highlight_button(CL_butt)
        elif current_button == sensor_butt:
            highlight_button(stat_butt)
        elif current_button == select_butt:
            highlight_button(sensor_butt)
        elif current_button == input_butt:
            highlight_button(select_butt)
    elif(currentPage == "pl"):
        pass
    elif(currentPage == "cl"):
        if clPos == 0: # If this is the back button
            clPos = len(cl_buttons) - 1
        else:
            clPos = clPos - 1
        highlight_button(cl_buttons[clPos])
    elif(currentPage == "stat"):
        pass

def handle_enter(event):
    active_button = window.focus_get()
    if active_button:
        active_button.config(relief=tk.SUNKEN)  # Simulate button click
        active_button.invoke()  # Execute the button's associated command

def hat():
    # Read button states
        up_state = GPIO.input(17)
        down_state = GPIO.input(18)
        left_state = GPIO.input(27)
        right_state = GPIO.input(22)
        enter_state = GPIO.input(23)
        
        # Simulate key presses
        if up_state == GPIO.LOW:
            highlight_previous_button(None)
            
        elif down_state == GPIO.LOW:
            highlight_next_button(None)
            
        elif left_state == GPIO.LOW:
            highlight_previous_button(None)
        elif right_state == GPIO.LOW:
            highlight_next_button(None)
        elif enter_state == GPIO.LOW:
            handle_enter(None)
        window.after(250,hat)    

def highlight_button(button):
    planet_butt.config(relief=tk.RAISED)
    CL_butt.config(relief=tk.RAISED)
    stat_butt.config(relief=tk.RAISED)
    sensor_butt.config(relief=tk.RAISED)
    select_butt.config(relief=tk.RAISED)
    input_butt.config(relief=tk.RAISED)
    
    if button:
        button.config(relief=tk.SUNKEN)
        button.focus_set()  # Set focus on the highlighted button


def show_planet_page():
    global currentPage
    header.pack_forget()
    center.pack_forget()
    topButtons.pack_forget()
    bottomButtons.pack_forget()
    cWeather = get_weather()
    tempVar.set(cWeather.temp)
    humVar.set(cWeather.humidity)
    sfcVar.set(cWeather.sfc)
    precVar.set(cWeather.precip)
    currentPage = "pl"
    highlight_button(planet_back_button)
    planet_page.pack(side='left')

def enumerate_videos():
    path = pl.Path(videoDir)
    for item in path.iterdir():
        if item.is_file():
            if item.name.endswith('.mp4'):
                video_paths.append(item)
                print(item)


def show_captains_log_page():
    global currentPage
    header.pack_forget()
    center.pack_forget()
    topButtons.pack_forget()
    bottomButtons.pack_forget()
    clPos = 0
    currentPage = "cl"
    highlight_button(captains_log_back_button)
    captains_log_page.pack()

def show_status_page():
    global currentPage
    header.pack_forget()
    center.pack_forget()
    topButtons.pack_forget()
    bottomButtons.pack_forget()
    currentPage = "stat"
    highlight_button(status_back_button)
    status_page.pack()

def show_main_menu():
    global currentPage
    planet_page.pack_forget()
    captains_log_page.pack_forget()
    status_page.pack_forget()
    header.pack()
    center.pack()
    topButtons.pack()
    bottomButtons.pack()
    currentPage = "mm"

    highlight_button(planet_butt)

def show_video_page(path):
    global currentPage
    captains_log_page.pack_forget()
    player_page.pack()
    currentPage = "player"
    start_video(path)

def get_weather():
    URL = "https://api.weather.gov/gridpoints/DVN/33,63/forecast/hourly"
    r = rq.get(url=URL)
    data = r.json()
    shortForecast = data['properties']['periods'][0]['shortForecast']
    temp = data['properties']['periods'][0]['temperature']
    precip = data['properties']['periods'][0]['probabilityOfPrecipitation']['value']
    humidity = data['properties']['periods'][0]['relativeHumidity']['value']
    return weather(temp,precip,humidity,shortForecast)

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
stat_butt = tk.Button(topButtons, font=(trekFont,39), text="STATUS", bg='#86DF64', fg='black', padx=5, pady=5, command=show_status_page)

userLabel = tk.Label(bottomButtons, text=username, font=(trekFont,39), bg='black', fg='#DAD778', pady=9)
sensor_butt = tk.Button(bottomButtons, font=(trekFont,39), text="SENSORS", bg='#86DF64', fg='black', padx=5, pady=5)
select_butt = tk.Button(bottomButtons, font=(trekFont,39), text="SELECT", bg='#86DF64', fg='black', padx=5, pady=5)
input_butt = tk.Button(bottomButtons, font=(trekFont,39), text="INPUT", bg='#86DF64', fg='black', padx=5, pady=5)

# Create separate frames for each page
planet_page = tk.Frame(window, bg='black')
captains_log_page = tk.Frame(window, bg='black')
status_page = tk.Frame(window, bg='black')
player_page = tk.Frame(window, bg='black')

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
                       
# Captain's Log Internal Frames

cl_header = tk.Frame(captains_log_page, bg='black', padx=14, pady=3)
logHolder = tk.Frame(captains_log_page, bg='black', padx=14, pady=3)
logsL = tk.Frame(logHolder, bg="black")
logsR = tk.Frame(logHolder, bg="black")

# Add widgets to the captain's log page
captains_log_label = tk.Label(cl_header, text="Captain's Log Page", font=(trekFont,75), bg='black', fg='#DAD778', padx=5)
captains_log_back_button = tk.Button(cl_header, font=(trekFont,30), text="Back", bg='#86DF64', fg='black', padx=5, pady=5)

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

def update_frame(self):
    if not is_paused and not is_stopped:
        ret, frame = cap.read()
        if ret:
            frame = cv2.cvtColor(frame, cv2.COLOR_BAYER_BG2BGR)
            frame = Image.fromarray(frame)
            frame = ImageTk.PhotoImage(frame)

            canvas.create_image(0,0,anchor=tk.NW,image=frame)
            canvas.image = frame
        
    if not is_stopped:
        self.after(30, update_frame)

def play_video(self):
    global is_paused
    global is_stopped
    is_paused = False
    is_stopped = False
    update_frame()

def pause_video(self):
    global is_paused
    is_paused = True

def stop_video(self):
    global is_stopped
    is_stopped = True
    cap.release()
    canvas.delete("all")
    show_captains_log_page()

def on_close(self):
    global is_stopped
    is_stopped = True
    cap.release()

# Add bits inside video player
canvas = Canvas(player_page, width=720, height=526)
canvas.pack()
play_button = tk.Button(player_page, text="Play", command=play_video)
play_button.pack()
pause_button = tk.Button(player_page, text="Pause",command=pause_video)
pause_button.pack()
stop_button = tk.Button(player_page, text="Stop", command=stop_video)
stop_button.pack()



# Add widgets to the status page
status_label = tk.Label(status_page, text="Status Page", font=(trekFont,30), bg='black', fg='#DAD778')
status_back_button = tk.Button(status_page, font=(trekFont,30), text="Back", bg='#86DF64', fg='black', padx=5, pady=5)

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
status_back_button.config(command=show_main_menu)

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






status_back_button.pack()
status_label.pack()

# Initially show the main menu
show_main_menu()


highlight_button(planet_butt)

window.bind("<Right>", highlight_next_button)
window.bind("<Left>", highlight_previous_button)
window.bind("<Up>", highlight_previous_button)
window.bind("<Down>", highlight_next_button)
window.bind("<Return>", handle_enter)

window.after(2000,hat)
window.mainloop()
GPIO.cleanup()

    



