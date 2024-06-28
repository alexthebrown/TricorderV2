import tkinter as tk
import cv2
from PIL import Image, ImageTk

class App:
    def __init__(self, window):
        self.window = window
        self.window.title("Camera Viewer")
        
        # Create a canvas to display the video feed
        self.canvas = tk.Canvas(window, width=640, height=480)
        self.canvas.pack()

        # Initialize OpenCV VideoCapture
        self.cap = cv2.VideoCapture(0)  # Use 0 for default camera
        
        # Call the function to start displaying the video
        self.update_video()

    def update_video(self):
        ret, frame = self.cap.read()
        if ret:
            # Convert the frame from BGR to RGB
            frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            
            # Convert OpenCV frame to ImageTk format
            img = Image.fromarray(frame)
            img_tk = ImageTk.PhotoImage(image=img)
            
            # Update the canvas image
            self.canvas.create_image(0, 0, anchor=tk.NW, image=img_tk)
            self.canvas.img_tk = img_tk  # Keep a reference to prevent garbage collection
            
        # Schedule the next update after 10 ms (adjust as needed)
        self.window.after(10, self.update_video)

    def close(self):
        # Release the camera and destroy the window
        if hasattr(self, 'cap'):
            self.cap.release()
        self.window.destroy()

if __name__ == "__main__":
    window = tk.Tk()
    window.mainloop()
