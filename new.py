import tkinter as tk
import requests as rq
import pathlib as pl
import os

class Weather:
    def __init__(self, temp, precip, humidity, sfc):
        self.temp = temp
        self.humidity = humidity
        self.precip = precip
        self.sfc = sfc

current_weather = Weather(None, None, None, None)
current_page = "mm"

video_paths = []
video_dir = '' # INSERT VIDEO FOLDER PATH HERE
video_buttons = []
cl_buttons = []
cl_pos = 0

def highlight_next_button(event):
    global current_page, cl_pos
    current_button = window.focus_get()
    if current_page == "mm":
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
    elif current_page == "cl":
        cl_pos = (cl_pos + 1) % len(cl_buttons)
        highlight_button(cl_buttons[cl_pos])

def highlight_previous_button(event):
    global current_page, cl_pos
    current_button = window.focus_get()
    if current_page == "mm":
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
    elif current_page == "cl":
        cl_pos = (cl_pos - 1) % len(cl_buttons)
        highlight_button(cl_buttons[cl_pos])

def handle_enter(event):
    active_button = window.focus_get()
    if active_button:
        active_button.config(relief=tk.SUNKEN)  # Simulate button click
        active_button.invoke()  # Execute the button's associated command

def highlight_button(button):
    for b in [planet_butt, CL_butt, stat_butt, sensor_butt, select_butt, input_butt]:
        b.config(relief=tk.RAISED)
    if button:
        button.config(relief=tk.SUNKEN)
        button.focus_set()  # Set focus on the highlighted button

def show_planet_page():
    global current_page
    switch_page(planet_page)
    update_weather()
    highlight_button(planet_back_button)
    current_page = "pl"

def show_captains_log_page():
    global current_page
    switch_page(captains_log_page)
    highlight_button(captains_log_back_button)
    current_page = "cl"

def show_status_page():
    global current_page
    switch_page(status_page)
    highlight_button(status_back_button)
    current_page = "stat"

def show_sensors_page():
    global current_page
    switch_page(sensors_page)
    highlight_button(sensors_back_button)
    current_page = "sens"

def show_select_page():
    global current_page
    switch_page(select_page)
    highlight_button(select_back_button)
    current_page = "sel"

def show_input_page():
    global current_page
    switch_page(input_page)
    highlight_button(input_back_button)
    current_page = "inp"

def show_main_menu():
    global current_page
    for page in [planet_page, captains_log_page, status_page, sensors_page, select_page, input_page]:
        page.pack_forget()
    header.pack()
    center.pack()
    top_buttons.pack()
    bottom_buttons.pack()
    highlight_button(planet_butt)
    current_page = "mm"

def switch_page(page):
    for widget in window.winfo_children():
        widget.pack_forget()
    page.pack(side='left', fill='both', expand=True)

def play_video(path):
    command = f"omxplayer {path}"
    os.system(command)

def get_weather():
    URL = "https://api.weather.gov/gridpoints/DVN/33,63/forecast/hourly"
    r = rq.get(url=URL)
    data = r.json()
    short_forecast = data['properties']['periods'][0]['shortForecast']
    temp = data['properties']['periods'][0]['temperature']
    precip = data['properties']['periods'][0]['probabilityOfPrecipitation']['value']
    humidity = data['properties']['periods'][0]['relativeHumidity']['value']
    return Weather(temp, precip, humidity, short_forecast)

def update_weather():
    global current_weather
    current_weather = get_weather()
    temp_var.set(current_weather.temp)
    hum_var.set(current_weather.humidity)
    sfc_var.set(current_weather.sfc)
    prec_var.set(current_weather.precip)

def enumerate_videos():
    path = pl.Path(video_dir)
    for item in path.iterdir():
        if item.is_file() and item.name.endswith('.mkv'):
            video_paths.append(item)

window = tk.Tk(className='Tricorder')
window.geometry('320x240')
window.maxsize(320, 240)
window.resizable(False, False)
window.configure(bg='black')

trek_font = "Trek"
username = "Scott Thunder"

temp_var = tk.StringVar()
hum_var = tk.StringVar()
sfc_var = tk.StringVar()
prec_var = tk.StringVar()

# Main Menu Frames
header = tk.Frame(window)
center = tk.Frame(window)
top_buttons = tk.Frame(window)
bottom_buttons = tk.Frame(window, bg='black')

top = tk.Label(header, text="USS ENTERPRISE NCC-1701 STANDARD ISSUE",
               font=(trek_font, 15), bg='black', fg='#86DF64', padx=5, pady=5)
tricorder = tk.Label(center, text="TRICORDER",
                     font=(trek_font, 45), fg='#DAD778', bg='black')

planet_butt = tk.Button(top_buttons, font=(trek_font, 13), text="PLANET", bg='#86DF64', fg='black', padx=5, pady=5, command=show_planet_page)
CL_butt = tk.Button(top_buttons, font=(trek_font, 13), text="CAPTAIN'S LOG", bg='#86DF64', fg='black', padx=5, pady=5, command=show_captains_log_page)
stat_butt = tk.Button(top_buttons, font=(trek_font, 13), text="STATUS", bg='#86DF64', fg='black', padx=5, pady=5, command=show_status_page)

user_label = tk.Label(bottom_buttons, text=username, font=(trek_font, 13), bg='black', fg='#DAD778', pady=9)
sensor_butt = tk.Button(bottom_buttons, font=(trek_font, 13), text="SENSORS", bg='#86DF64', fg='black', padx=5, pady=5, command=show_sensors_page)
select_butt = tk.Button(bottom_buttons, font=(trek_font, 13), text="SELECT", bg='#86DF64', fg='black', padx=5, pady=5, command=show_select_page)
input_butt = tk.Button(bottom_buttons, font=(trek_font, 13), text="INPUT", bg='#86DF64', fg='black', padx=5, pady=5, command=show_input_page)

# Create separate frames for each page
planet_page = tk.Frame(window, bg='black')
captains_log_page = tk.Frame(window, bg='black')
status_page = tk.Frame(window, bg='black')
sensors_page = tk.Frame(window, bg='black')
select_page = tk.Frame(window, bg='black')
input_page = tk.Frame(window, bg='black')

# Planet Page Layout
pl_header = tk.Frame(planet_page, bg='black', padx=5, pady=5)
pl_middle = tk.Frame(planet_page, bg='black')
temp_frame = tk.Frame(pl_middle, bg='#DAD778', padx=5, pady=5)
right_pl_frame = tk.Frame(pl_middle, bg='black', padx=5, pady=5)
humid_frame = tk.Frame(right_pl_frame, bg='#DAD778', pady=5)
precip_frame = tk.Frame(right_pl_frame, bg='#DAD778')
sfc_frame = tk.Frame(planet_page, bg='black', pady=10)

planet_label = tk.Label(pl_header, text="Planet Conditions", font=(trek_font, 27), bg='black', fg='#DAD778', padx=10)
planet_back_button = tk.Button(pl_header, font=(trek_font, 15), text="Back", bg='#86DF64', fg='black', padx=5, pady=5, command=show_main_menu)

temp_title = tk.Label(temp_frame, font=trek_font, text='Temperature:', bg='#DAD778', fg='black', pady=5)
temp_value = tk.Label(temp_frame, font=trek_font, textvariable=temp_var, bg='#DAD778', fg='black')

humid_title = tk.Label(humid_frame, font=trek_font, text='Humidity:', bg='#DAD778', fg='black', pady=5)
humid_value = tk.Label(humid_frame, font=trek_font, textvariable=hum_var, bg='#DAD778', fg='black')

prec_title = tk.Label(precip_frame, font=trek_font, text='Precipitation:', bg='#DAD778', fg='black', pady=5)
prec_value = tk.Label(precip_frame, font=trek_font, textvariable=prec_var, bg='#DAD778', fg='black')

sfc_title = tk.Label(sfc_frame, font=trek_font, text='Forecast:', bg='black', fg='#DAD778', pady=5)
sfc_value = tk.Label(sfc_frame, font=trek_font, textvariable=sfc_var, bg='black', fg='#DAD778')

pl_header.pack()
pl_middle.pack()
temp_frame.pack(side='left')
right_pl_frame.pack(side='right')
humid_frame.pack()
precip_frame.pack()
sfc_frame.pack()
planet_label.pack(side='left')
planet_back_button.pack(side='right')
temp_title.pack()
temp_value.pack()
humid_title.pack()
humid_value.pack()
prec_title.pack()
prec_value.pack()
sfc_title.pack()
sfc_value.pack()

# Captain's Log Page Layout
cl_header = tk.Frame(captains_log_page, bg='black', padx=5, pady=5)
cl_middle = tk.Frame(captains_log_page, bg='black')
cl_bottom = tk.Frame(captains_log_page, bg='black', pady=10)

cl_label = tk.Label(cl_header, text="Captain's Log", font=(trek_font, 27), bg='black', fg='#DAD778', padx=10)
captains_log_back_button = tk.Button(cl_header, font=(trek_font, 15), text="Back", bg='#86DF64', fg='black', padx=5, pady=5, command=show_main_menu)

cl_header.pack()
cl_middle.pack()
cl_bottom.pack()
cl_label.pack(side='left')
captains_log_back_button.pack(side='right')

# Status Page Layout
stat_header = tk.Frame(status_page, bg='black', padx=5, pady=5)
stat_middle = tk.Frame(status_page, bg='black')
stat_bottom = tk.Frame(status_page, bg='black', pady=10)

stat_label = tk.Label(stat_header, text="Status", font=(trek_font, 27), bg='black', fg='#DAD778', padx=10)
status_back_button = tk.Button(stat_header, font=(trek_font, 15), text="Back", bg='#86DF64', fg='black', padx=5, pady=5, command=show_main_menu)

stat_header.pack()
stat_middle.pack()
stat_bottom.pack()
stat_label.pack(side='left')
status_back_button.pack(side='right')

# Sensors Page Layout
sensors_header = tk.Frame(sensors_page, bg='black', padx=5, pady=5)
sensors_middle = tk.Frame(sensors_page, bg='black')
sensors_bottom = tk.Frame(sensors_page, bg='black', pady=10)

sensors_label = tk.Label(sensors_header, text="Sensors", font=(trek_font, 27), bg='black', fg='#DAD778', padx=10)
sensors_back_button = tk.Button(sensors_header, font=(trek_font, 15), text="Back", bg='#86DF64', fg='black', padx=5, pady=5, command=show_main_menu)

sensors_header.pack()
sensors_middle.pack()
sensors_bottom.pack()
sensors_label.pack(side='left')
sensors_back_button.pack(side='right')

# Select Page Layout
select_header = tk.Frame(select_page, bg='black', padx=5, pady=5)
select_middle = tk.Frame(select_page, bg='black')
select_bottom = tk.Frame(select_page, bg='black', pady=10)

select_label = tk.Label(select_header, text="Select", font=(trek_font, 27), bg='black', fg='#DAD778', padx=10)
select_back_button = tk.Button(select_header, font=(trek_font, 15), text="Back", bg='#86DF64', fg='black', padx=5, pady=5, command=show_main_menu)

select_header.pack()
select_middle.pack()
select_bottom.pack()
select_label.pack(side='left')
select_back_button.pack(side='right')

# Input Page Layout
input_header = tk.Frame(input_page, bg='black', padx=5, pady=5)
input_middle = tk.Frame(input_page, bg='black')
input_bottom = tk.Frame(input_page, bg='black', pady=10)

input_label = tk.Label(input_header, text="Input", font=(trek_font, 27), bg='black', fg='#DAD778', padx=10)
input_back_button = tk.Button(input_header, font=(trek_font, 15), text="Back", bg='#86DF64', fg='black', padx=5, pady=5, command=show_main_menu)

input_header.pack()
input_middle.pack()
input_bottom.pack()
input_label.pack(side='left')
input_back_button.pack(side='right')

# Main Menu Layout
header.pack()
center.pack()
top_buttons.pack()
bottom_buttons.pack()
top.pack()
tricorder.pack()
planet_butt.pack(side='left')
CL_butt.pack(side='left')
stat_butt.pack(side='left')
sensor_butt.pack(side='left')
select_butt.pack(side='left')
input_butt.pack(side='left')
user_label.pack(side='left')

highlight_button(planet_butt)

# Bind keys to navigate menu
window.bind('<Up>', highlight_previous_button)
window.bind('<Down>', highlight_next_button)
window.bind('<Return>', handle_enter)

window.mainloop()
