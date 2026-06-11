#!/bin/bash

# Navigate to the desired directory
cd /home/tricorder/Desktop/Tricorder/TricorderV2 || exit 1

# Update the repository and install Python dependencies if needed
git pull --ff-only
python3 -m pip install -r requirements.txt --break-system-packages

# Ensure the application uses the primary display
export DISPLAY="${DISPLAY:-:0}"

# Nudge the pointer so the desktop/display sees activity during startup.
if command -v xdotool >/dev/null 2>&1; then
    xdotool mousemove 1 1
fi

# Configure GPIO wakeup from sleep on the expected BCM key pins.
WAKE_PINS=(17 18 27 22 23)
if [ -d /sys/class/gpio ]; then
    for WAKE_PIN in "${WAKE_PINS[@]}"; do
        if [ ! -d "/sys/class/gpio/gpio${WAKE_PIN}" ]; then
            echo "${WAKE_PIN}" > /sys/class/gpio/export 2>/dev/null || true
        fi
        if [ -e "/sys/class/gpio/gpio${WAKE_PIN}/direction" ]; then
            echo "in" > "/sys/class/gpio/gpio${WAKE_PIN}/direction" 2>/dev/null || true
        fi
        if [ -e "/sys/class/gpio/gpio${WAKE_PIN}/edge" ]; then
            echo "falling" > "/sys/class/gpio/gpio${WAKE_PIN}/edge" 2>/dev/null || true
        fi
        if [ -e "/sys/class/gpio/gpio${WAKE_PIN}/power/wakeup" ]; then
            echo "enabled" > "/sys/class/gpio/gpio${WAKE_PIN}/power/wakeup" 2>/dev/null || true
        fi
    done
fi

# Run the Python program
python3 gpTricorder.py
