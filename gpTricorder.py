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
import picamera
import io

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
            highlight_button(sensor_butt)
        elif current_button == sensor_butt:
            highlight_button(stat_butt)
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

    elif(currentPage == "sensor"):
        pass

    elif(currentPage == "player"):
        if current_button == play_button:
            highlight_button(pause_button)
        elif current_button == pause_button:
            highlight_button(stop_button)
        elif current_button == stop_button:
            highlight_button(play_button)

def highlight_previous_button(event):
    global currentPage
    global clPos
    current_button = window.focus_get()
    if currentPage == "mm":
        if current_button == planet_butt:
            highlight_button(input_butt)
        elif current_button == CL_butt:
            highlight_button(planet_butt)
        elif current_button == sensor_butt:
            highlight_button(CL_butt)
        elif current_button == stat_butt:
            highlight_button(sensor_butt)
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
    elif(currentPage == "sensor"):
        pass

    elif(currentPage == "player"):
        if current_button == play_button:
            highlight_button(stop_button)
        elif current_button == pause_button:
            highlight_button(play_button)
        elif current_button == stop_button:
            highlight_button(pause_button)

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
    play_button.config(relief=tk.RAISED)
    pause_button.config(relief=tk.RAISED)
    stop_button.config(relief=tk.RAISED)
    
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
    player_page.pack_forget()
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
    currentPage = "sensor"
    highlight_button(sensor_back_button)
    sensor_page.pack()
    start_camera()  # Start the camera when showing the sensor page

def show_main_menu():
    global currentPage
    planet_page.pack_forget()
    captains_log_page.pack_forget()
    sensor_page.pack_forget()
    header.pack()
    center.pack()
    topButtons.pack()
    bottomButtons.pack()
    currentPage = "mm"

    highlight_button(planet_butt)
    on_closing()  # Close the camera when returning to the main menu

def show_video_page(path):
    global currentPage
    captains_log_page.pack_forget()
    player_page.pack()
    currentPage = "player"
    highlight_button(play_button)
    start_video(str(path))

def start_camera():
    global stream
    stream = io.BytesIO()
    camera.start_preview()
    update_image()

def update_image():
    camera.capture(stream, format='jpeg', use_video_port=True)
    stream.seek(0)
    image = Image.open(stream)
    image = ImageTk.PhotoImage(image)
    image_label.configure(image=image)
    image_label.image = image
    stream.seek(0)
    stream.truncate()
    if currentPage == "sensor":
        window.after(100, update_image)  # Continue updating the image if still on the sensor page

def on_closing():
    camera.stop_preview()
    camera.close()

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
