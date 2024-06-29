import RPi.GPIO as GPIO
import time

GPIO.setmode(GPIO.BCM)

# Set up the GPIO pins with pull-up resistors
GPIO.setup(17, GPIO.IN, pull_up_down=GPIO.PUD_UP)  # Up button
GPIO.setup(18, GPIO.IN, pull_up_down=GPIO.PUD_UP)  # Down button
GPIO.setup(27, GPIO.IN, pull_up_down=GPIO.PUD_UP)  # Left button
GPIO.setup(22, GPIO.IN, pull_up_down=GPIO.PUD_UP)  # Right button
GPIO.setup(23, GPIO.IN, pull_up_down=GPIO.PUD_UP)  # Enter button

try:
    while True:
        if GPIO.input(17) == GPIO.LOW:
            print("Up button pressed")
        if GPIO.input(18) == GPIO.LOW:
            print("Down button pressed")
        if GPIO.input(27) == GPIO.LOW:
            print("Left button pressed")
        if GPIO.input(22) == GPIO.LOW:
            print("Right button pressed")
        if GPIO.input(23) == GPIO.LOW:
            print("Enter button pressed")
        # Add a small delay to debounce
        time.sleep(0.1)
except KeyboardInterrupt:
    GPIO.cleanup()
