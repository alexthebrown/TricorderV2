from picamera2 import Picamera2
picam2 = Picamera2()
picam2.start(show_preview=True)
picam2.title_fields = ["ExposureTime", "AnalogueGain"]