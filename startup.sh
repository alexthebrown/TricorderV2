#!/bin/bash

# Navigate to the desired directory
cd /home/tricorder/Desktop/Tricorder/TricorderV2 || exit 1

# Update the repository and install Python dependencies if needed
git pull --ff-only
python3 -m pip install -r requirements.txt --break-system-packages

# Ensure the application uses the primary display
export DISPLAY="${DISPLAY:-:0}"

# Configure GPIO wakeup from sleep on all button pins.
# Pins: 17, 18, 27, 22 (buttons), 23 (enter/wake button)
GPIO_PINS=(17 18 27 22 23)

if [ -d /sys/class/gpio ]; then
    for pin in "${GPIO_PINS[@]}"; do
        if [ ! -d "/sys/class/gpio/gpio${pin}" ]; then
            echo "${pin}" > /sys/class/gpio/export 2>/dev/null || true
        fi
        if [ -e "/sys/class/gpio/gpio${pin}/direction" ]; then
            echo "in" > "/sys/class/gpio/gpio${pin}/direction" 2>/dev/null || true
        fi
        if [ -e "/sys/class/gpio/gpio${pin}/edge" ]; then
            echo "falling" > "/sys/class/gpio/gpio${pin}/edge" 2>/dev/null || true
        fi
        if [ -e "/sys/class/gpio/gpio${pin}/power/wakeup" ]; then
            echo "enabled" > "/sys/class/gpio/gpio${pin}/power/wakeup" 2>/dev/null || true
        fi
    done
fi

# Enable system wake on GPIO input
if [ -d /sys/bus/platform/drivers/gpio-keys ]; then
    for button in /sys/bus/platform/drivers/gpio-keys/*/; do
        if [ -e "${button}power/wakeup" ]; then
            echo "enabled" > "${button}power/wakeup" 2>/dev/null || true
        fi
    done
fi

# Run the Python program
python3 gpTricorder.py
