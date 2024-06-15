import tkinter as tk
import requests as rq
import pathlib as pl
import os
import RPi.GPIO as GPIO
import multiprocessing
import time


class Weather:
    def __init__(self, temp, precip, humidity, sfc):
        self.temp = temp
        self.humidity = humidity
        self.precip = precip
        self.sfc = sfc


class TricorderApp:
    def __init__(self, root, username):
        self.root = root
        self.username = username
        self.current_page = "mm"
        self.cl_pos = 0
        self.video_paths = []
        self.cl_buttons = []
        self.trek_font = "Trek"

        self.init_gpio()
        self.init_vars()
        self.init_main_menu()
        self.init_planet_page()
        self.init_captains_log_page()
        self.init_status_page()
        self.enumerate_videos()

        self.show_main_menu()

        self.root.bind("<Right>", self.highlight_next_button)
        self.root.bind("<Left>", self.highlight_previous_button)
        self.root.bind("<Up>", self.highlight_previous_button)
        self.root.bind("<Down>", self.highlight_next_button)
        self.root.bind("<Return>", self.handle_enter)
        self.root.after(2000, self.hat)

    def init_gpio(self):
        GPIO.setmode(GPIO.BCM)
        GPIO.setup(17, GPIO.IN)  # Up button
        GPIO.setup(18, GPIO.IN)  # Down button
        GPIO.setup(27, GPIO.IN)  # Left button
        GPIO.setup(22, GPIO.IN)  # Right button
        GPIO.setup(23, GPIO.IN)  # Enter button

    def init_vars(self):
        self.temp_var = tk.StringVar()
        self.hum_var = tk.StringVar()
        self.sfc_var = tk.StringVar()
        self.prec_var = tk.StringVar()

    def init_main_menu(self):
        self.header = tk.Frame(self.root)
        self.center = tk.Frame(self.root)
        self.top_buttons = tk.Frame(self.root)
        self.bottom_buttons = tk.Frame(self.root, bg='black')

        self.top_label = tk.Label(
            self.header, text="USS ENTERPRISE NCC-1701 STANDARD ISSUE",
            font=(self.trek_font, 42), bg='black', fg='#86DF64', padx=5, pady=10
        )
        self.tricorder_label = tk.Label(
            self.center, text="TRICORDER", font=(self.trek_font, 135),
            fg='#DAD778', bg='black'
        )
        self.planet_button = tk.Button(
            self.top_buttons, font=(self.trek_font, 39), text="PLANET",
            bg='#86DF64', fg='black', padx=5, pady=5, command=self.show_planet_page
        )
        self.cl_button = tk.Button(
            self.top_buttons, font=(self.trek_font, 39), text="CAPTAIN'S LOG",
            bg='#86DF64', fg='black', padx=5, pady=5, command=self.show_captains_log_page
        )
        self.status_button = tk.Button(
            self.top_buttons, font=(self.trek_font, 39), text="STATUS",
            bg='#86DF64', fg='black', padx=5, pady=5, command=self.show_status_page
        )
        self.user_label = tk.Label(
            self.bottom_buttons, text=self.username, font=(self.trek_font, 39),
            bg='black', fg='#DAD778', pady=9
        )
        self.sensor_button = tk.Button(
            self.bottom_buttons, font=(self.trek_font, 39), text="SENSORS",
            bg='#86DF64', fg='black', padx=5, pady=5
        )
        self.select_button = tk.Button(
            self.bottom_buttons, font=(self.trek_font, 39), text="SELECT",
            bg='#86DF64', fg='black', padx=5, pady=5
        )
        self.input_button = tk.Button(
            self.bottom_buttons, font=(self.trek_font, 39), text="INPUT",
            bg='#86DF64', fg='black', padx=5, pady=5
        )

    def init_planet_page(self):
        self.planet_page = tk.Frame(self.root, bg='black')
        self.pl_header = tk.Frame(self.planet_page, bg='black', padx=5, pady=5)
        self.pl_middle = tk.Frame(self.planet_page, bg='black')
        self.temp_frame = tk.Frame(self.pl_middle, bg='#DAD778', padx=5, pady=5)
        self.right_pl_frame = tk.Frame(self.pl_middle, bg='black', padx=5, pady=5)
        self.humid_frame = tk.Frame(self.right_pl_frame, bg='#DAD778', pady=5)
        self.precip_frame = tk.Frame(self.right_pl_frame, bg='#DAD778')
        self.sfc_frame = tk.Frame(self.planet_page, bg='black', pady=10)

        self.planet_label = tk.Label(
            self.pl_header, text="Planet Conditions", font=(self.trek_font, 81),
            bg='black', fg='#DAD778', padx=10
        )
        self.planet_back_button = tk.Button(
            self.pl_header, font=(self.trek_font, 45), text="Back", bg='#86DF64',
            fg='black', padx=5, pady=5, command=self.show_main_menu
        )
        self.temp_title = tk.Label(
            self.temp_frame, font=(self.trek_font, 30), text='Temperature:',
            bg='#DAD778', fg='black', padx=5, pady=5
        )
        self.temp_label = tk.Label(
            self.temp_frame, font=(self.trek_font, 30), textvariable=self.temp_var,
            padx=0, pady=5, bg='#DAD778', fg='black'
        )
        self.temp_symbol = tk.Label(
            self.temp_frame, font=(self.trek_font, 30), text="°", padx=0,
            bg='#DAD778', fg='black'
        )
        self.fahrenheit_label = tk.Label(
            self.temp_frame, text="F", font=(self.trek_font, 45), padx=15,
            bg='#DAD778', fg='black'
        )
        self.humid_label = tk.Label(
            self.humid_frame, font=(self.trek_font, 30), text="Humidity:",
            fg='black', bg='#DAD778'
        )
        self.humid_var_label = tk.Label(
            self.humid_frame, font=(self.trek_font, 30), textvariable=self.hum_var,
            fg='black', bg='#DAD778'
        )
        self.humid_pc_label = tk.Label(
            self.humid_frame, font=(self.trek_font, 30), text='%', fg='black',
            bg='#DAD778'
        )
        self.precip_label = tk.Label(
            self.precip_frame, font=(self.trek_font, 30), text='Chance Precip:',
            fg='black', bg='#DAD778'
        )
        self.precip_var_label = tk.Label(
            self.precip_frame, font=(self.trek_font, 30), textvariable=self.prec_var,
            fg='black', bg='#DAD778'
        )
        self.precip_pc_label = tk.Label(
            self.precip_frame, font=(self.trek_font, 30), text='%', fg='black',
            bg='#DAD778'
        )
        self.sfc_label = tk.Label(
            self.sfc_frame, font=(self.trek_font, 30), textvariable=self.sfc_var,
            fg='#DAD778', bg='black'
        )

    def init_captains_log_page(self):
        self.captains_log_page = tk.Frame(self.root, bg='black')
        self.cl_header = tk.Frame(self.captains_log_page, bg='black', padx=14, pady=3)
        self.log_holder = tk.Frame(self.captains_log_page, bg='black', padx=14, pady=3)
        self.logs_left = tk.Frame(self.log_holder, bg="black")
        self.logs_right = tk.Frame(self.log_holder, bg="black")

        self.captains_log_label = tk.Label(
            self.cl_header, text="Captain's Log Page", font=(self.trek_font, 75),
            bg='black', fg='#DAD778', padx=5
        )
        self.captains_log_back_button = tk.Button(
            self.cl_header, font=(self.trek_font, 30), text="Back", bg='#86DF64',
            fg='black', padx=5, pady=5, command=self.show_main_menu
        )

    def init_status_page(self):
        self.status_page = tk.Frame(self.root, bg='black')
        self.status_label = tk.Label(
            self.status_page, text="Status Page", font=(self.trek_font, 30),
            bg='black', fg='#DAD778'
        )
        self.status_back_button = tk.Button(
            self.status_page, font=(self.trek_font, 30), text="Back", bg='#86DF64',
            fg='black', padx=5, pady=5, command=self.show_main_menu
        )

    def highlight_next_button(self, event):
        if self.cl_pos < len(self.cl_buttons) - 1:
            self.cl_buttons[self.cl_pos].config(bg="#86DF64", fg='black')
            self.cl_pos += 1
            self.cl_buttons[self.cl_pos].config(bg="black", fg='#86DF64')

    def highlight_previous_button(self, event):
        if self.cl_pos > 0:
            self.cl_buttons[self.cl_pos].config(bg="#86DF64", fg='black')
            self.cl_pos -= 1
            self.cl_buttons[self.cl_pos].config(bg="black", fg='#86DF64')

    def handle_enter(self, event):
        self.cl_buttons[self.cl_pos].invoke()

    def show_main_menu(self):
        self.clear_screen()
        self.header.pack()
        self.center.pack()
        self.top_buttons.pack()
        self.bottom_buttons.pack()

        self.top_label.pack()
        self.tricorder_label.pack()
        self.planet_button.pack(side=tk.LEFT, padx=2, pady=2)
        self.cl_button.pack(side=tk.LEFT, padx=2, pady=2)
        self.status_button.pack(side=tk.LEFT, padx=2, pady=2)
        self.user_label.pack(side=tk.LEFT, padx=2, pady=2)
        self.sensor_button.pack(side=tk.LEFT, padx=2, pady=2)
        self.select_button.pack(side=tk.LEFT, padx=2, pady=2)
        self.input_button.pack(side=tk.LEFT, padx=2, pady=2)

    def show_planet_page(self):
        self.clear_screen()
        self.planet_page.pack()
        self.pl_header.pack()
        self.pl_middle.pack()
        self.temp_frame.pack(side=tk.LEFT)
        self.right_pl_frame.pack(side=tk.LEFT)
        self.sfc_frame.pack()

        self.planet_label.pack(side=tk.LEFT)
        self.planet_back_button.pack(side=tk.RIGHT)
        self.temp_title.pack(side=tk.LEFT)
        self.temp_label.pack(side=tk.LEFT)
        self.temp_symbol.pack(side=tk.LEFT)
        self.fahrenheit_label.pack(side=tk.LEFT)
        self.humid_label.pack(side=tk.LEFT)
        self.humid_var_label.pack(side=tk.LEFT)
        self.humid_pc_label.pack(side=tk.LEFT)
        self.precip_label.pack(side=tk.LEFT)
        self.precip_var_label.pack(side=tk.LEFT)
        self.precip_pc_label.pack(side=tk.LEFT)
        self.sfc_label.pack()

        weather = self.get_weather()
        self.update_weather(weather)

    def show_captains_log_page(self):
        self.clear_screen()
        self.captains_log_page.pack()
        self.cl_header.pack()
        self.log_holder.pack()
        self.logs_left.pack(side=tk.LEFT)
        self.logs_right.pack(side=tk.LEFT)

        self.captains_log_label.pack(side=tk.LEFT)
        self.captains_log_back_button.pack(side=tk.RIGHT)

        for i, path in enumerate(self.video_paths[:len(self.video_paths) // 2]):
            button = tk.Button(
                self.logs_left, text=path, font=(self.trek_font, 20),
                bg="#86DF64", fg='black', command=lambda p=path: self.play_video(p)
            )
            button.pack(fill=tk.BOTH, pady=2)
            self.cl_buttons.append(button)

        for i, path in enumerate(self.video_paths[len(self.video_paths) // 2:]):
            button = tk.Button(
                self.logs_right, text=path, font=(self.trek_font, 20),
                bg="#86DF64", fg='black', command=lambda p=path: self.play_video(p)
            )
            button.pack(fill=tk.BOTH, pady=2)
            self.cl_buttons.append(button)

    def show_status_page(self):
        self.clear_screen()
        self.status_page.pack()
        self.status_label.pack()
        self.status_back_button.pack()

    def enumerate_videos(self):
        video_directory = "videos"
        if os.path.exists(video_directory):
            self.video_paths = [
                pl.PurePath(video_directory, v) for v in os.listdir(video_directory)
                if v.endswith('.mp4') and os.path.isfile(pl.PurePath(video_directory, v))
            ]
        else:
            os.makedirs(video_directory)

    def get_weather(self):
        response = rq.get("http://10.0.0.186:5000/")
        weather_data = response.json()
        return Weather(
            weather_data["temp"],
            weather_data["precip"],
            weather_data["humidity"],
            weather_data["sfc"]
        )

    def update_weather(self, weather):
        self.temp_var.set(weather.temp)
        self.hum_var.set(weather.humidity)
        self.prec_var.set(weather.precip)
        self.sfc_var.set(weather.sfc)

    def hat(self):
        self.root.after(2000, self.hat)

    def play_video(self, path):
        os.system(f'omxplayer {path}')

    def clear_screen(self):
        for widget in self.root.winfo_children():
            widget.pack_forget()


if __name__ == "__main__":
    root = tk.Tk()
    root.geometry("320x240")
    app = TricorderApp(root, "Captain Kirk")
    root.mainloop()
