import picamera
def generate_frames():
    with picamera.PiCamera() as camera:
        camera.resolution = (640,480)
        camera.framerate  = 24
        stream = io.BytesIO()

        for