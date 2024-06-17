import tkinter as tk
from tkinter import Canvas, Button
import cv2
from PIL import Image, ImageTk

class VideoPlayer(tk.Tk):
    def __init__(self, video_path):
        super().__init__()
        self.title("Video Player")

        # Open the video file to get its dimensions
        self.cap = cv2.VideoCapture(video_path)
        self.width = int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        self.height = int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

        # Set the window and canvas dimensions to match the video
        self.geometry(f"{self.width}x{self.height+50}")
        self.canvas = Canvas(self, width=self.width, height=self.height)
        self.canvas.pack()

        # Add control buttons
        self.play_button = Button(self, text="Play", command=self.play_video)
        self.play_button.pack(side=tk.LEFT, padx=10, pady=10)
        self.pause_button = Button(self, text="Pause", command=self.pause_video)
        self.pause_button.pack(side=tk.LEFT, padx=10, pady=10)
        self.stop_button = Button(self, text="Stop", command=self.stop_video)
        self.stop_button.pack(side=tk.LEFT, padx=10, pady=10)

        self.is_paused = False
        self.is_stopped = False

        self.update_frame()

    def update_frame(self):
        if not self.is_paused and not self.is_stopped:
            ret, frame = self.cap.read()
            if ret:
                frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                frame = Image.fromarray(frame)
                frame = ImageTk.PhotoImage(frame)
                
                self.canvas.create_image(0, 0, anchor=tk.NW, image=frame)
                self.canvas.image = frame
        
        if not self.is_stopped:
            self.after(30, self.update_frame)

    def play_video(self):
        self.is_paused = False
        self.is_stopped = False
        self.update_frame()

    def pause_video(self):
        self.is_paused = True

    def stop_video(self):
        self.is_stopped = True
        self.cap.release()
        self.canvas.delete("all")

    def on_close(self):
        self.is_stopped = True
        self.cap.release()
        self.destroy()

if __name__ == "__main__":
    video_path = "/Users/alexbrown/Downloads/stds9tat2.mp4"  # Specified video file path
    app = VideoPlayer(video_path)
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()
