import tkinter as tk
import requests as rq
import pathlib as pl
import os

# This is a working example for the tricorder program. It works well on windows but had to be updated to run on the Raspberry Pi set up. 

# Weather Variables
cWeather = None
class weather:
    def __init__(self,temp, precip, humidity, sfc):
        self.temp = temp
        self.humidity = humidity
        self.precip = precip
        self.sfc = sfc

cWeather = weather(None,None,None,None)
currentPage = "mm"

video_paths = []
videoDir = '' # INSERT VIDEO FOLDER PATH HERE
video_buttons = []
cl_buttons = []
clPos = 0


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
            if item.name.endswith('.mkv'):
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

def playVideo(path):
    command = "omxplayer " + str(path)
    print(command)
    #os.system(command)


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
window.resizable(width=False,height=False)
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
               font=(trekFont, 15), bg='black', fg='#86DF64', padx=5, pady=5)
tricorder = tk.Label(center, text="TRICORDER",
                     font=(trekFont, 45), fg='#DAD778', bg='black')

planet_butt = tk.Button(topButtons, font=(trekFont,13), text="PLANET", bg='#86DF64', fg='black', padx=5, pady=5, command=show_planet_page)
CL_butt = tk.Button(topButtons, font=(trekFont,13), text="CAPTAIN'S LOG", bg='#86DF64', fg='black', padx=5, pady=5, command=show_captains_log_page)
stat_butt = tk.Button(topButtons, font=(trekFont,13), text="STATUS", bg='#86DF64', fg='black', padx=5, pady=5, command=show_status_page)

userLabel = tk.Label(bottomButtons, text=username, font=(trekFont,13), bg='black', fg='#DAD778', pady=9)
sensor_butt = tk.Button(bottomButtons, font=(trekFont,13), text="SENSORS", bg='#86DF64', fg='black', padx=5, pady=5)
select_butt = tk.Button(bottomButtons, font=(trekFont,13), text="SELECT", bg='#86DF64', fg='black', padx=5, pady=5)
input_butt = tk.Button(bottomButtons, font=(trekFont,13), text="INPUT", bg='#86DF64', fg='black', padx=5, pady=5)

# Create separate frames for each page
planet_page = tk.Frame(window, bg='black')
captains_log_page = tk.Frame(window, bg='black')
status_page = tk.Frame(window, bg='black')

# Planet Internal Frames
pl_header = tk.Frame(planet_page,bg='black',padx=5,pady=5)
pl_middle = tk.Frame(planet_page,bg='black')
temp_frame = tk.Frame(pl_middle,bg='#DAD778',padx=5,pady=5)
right_pl_frame = tk.Frame(pl_middle,bg='black',padx=5,pady=5)
humid_frame = tk.Frame(right_pl_frame,bg='#DAD778',pady=5)
precip_frame = tk.Frame(right_pl_frame, bg='#DAD778')
sfc_frame = tk.Frame(planet_page,bg='black', pady=10)


# Add widgets to the planet page (WEATHER)
planet_label = tk.Label(pl_header, text="Planet Conditions", font=(trekFont,27), bg='black', fg='#DAD778',padx=10)
planet_back_button = tk.Button(pl_header, font=(trekFont,15), text="Back", bg='#86DF64', fg='black', padx=5, pady=5)

temp_title = tk.Label(temp_frame, font=trekFont,text='Temperature:', bg='#DAD778',fg='black', padx=5, pady=5)
temp_label =tk.Label(temp_frame, font=(trekFont),textvariable=tempVar, padx=0, pady=5,bg='#DAD778', fg='black')
temp_symbol = tk.Label(temp_frame, font=(trekFont),text="°", padx=0, bg='#DAD778', fg='black')
farenheight = tk.Label(temp_frame, text="F",font=(trekFont, 15),padx=15, bg='#DAD778',fg='black')

humid_label = tk.Label(humid_frame, font=(trekFont),text="Humidity: ",fg='black',bg='#DAD778')
humid_var_label =tk.Label(humid_frame,font=trekFont,textvariable=humVar, fg='black', bg='#DAD778')
humidPC_label = tk.Label(humid_frame, font=trekFont,text='%', fg='black', bg='#DAD778')

precip_label = tk.Label(precip_frame, font=trekFont, text='Chance Precip: ', fg='black', bg='#DAD778')
precip_var_label = tk.Label(precip_frame, font=trekFont,textvariable=precVar, fg='black', bg='#DAD778')
precipPC_label = tk.Label(precip_frame, font=trekFont, text='%', fg='black', bg='#DAD778')

sfc_label = tk.Label(sfc_frame, font=trekFont, textvariable=sfcVar, fg='#DAD778', bg='black')
                       
# Captain's Log Internal Frames

cl_header = tk.Frame(captains_log_page, bg='black', padx=14, pady=3)
logHolder = tk.Frame(captains_log_page, bg='black', padx=14, pady=3)
logsL = tk.Frame(logHolder, bg="black")
logsR = tk.Frame(logHolder, bg="black")

# Add widgets to the captain's log page
captains_log_label = tk.Label(cl_header, text="Captain's Log Page", font=(trekFont,25), bg='black', fg='#DAD778', padx=5)
captains_log_back_button = tk.Button(cl_header, font=(trekFont), text="Back", bg='#86DF64', fg='black', padx=5, pady=5)

enumerate_videos()
alternator = 0
for video in video_paths:
    if alternator == 0:
        newButton = tk.Button(logsL,text=video.name.removesuffix('.mkv'),font=trekFont, bg= '#86DF64',fg='black', command= lambda tV=video:playVideo(tV))
        alternator = 1
    else:
        newButton = tk.Button(logsR,text=video.name.removesuffix('.mkv'),font=trekFont, bg= '#86DF64',fg='black', command= lambda tV=video:playVideo(tV))
        alternator = 0
    video_buttons.append(newButton)


# Add widgets to the status page
status_label = tk.Label(status_page, text="Status Page", font=(trekFont), bg='black', fg='#DAD778')
status_back_button = tk.Button(status_page, font=(trekFont), text="Back", bg='#86DF64', fg='black', padx=5, pady=5)

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



window.mainloop()
