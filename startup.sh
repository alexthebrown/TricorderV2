#!/bin/bash

# Navigate to the desired directory
cd /home/tricorder/Desktop/Tricorder/TricorderV2 || exit 1

# Update the repository and install Python dependencies if needed
git pull --ff-only
python3 -m pip install -r requirements.txt --break-system-packages

# Ensure the application uses the primary display
export DISPLAY="${DISPLAY:-:0}"

# Configure GPIO wakeup from sleep on the expected BCM pin.
# Change WAKE_PIN if a different BCM GPIO is used for your wake button.
WAKE_PIN=23
if [ -d /sys/class/gpio ]; then
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
fi

# Run the Python program
python3 gpTricorder.py
