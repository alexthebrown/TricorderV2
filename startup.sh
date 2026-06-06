#!/bin/bash

# Navigate to the desired directory
cd /home/tricorder/Desktop/Tricorder/TricorderV2 || exit 1

# Update the repository and install Python dependencies if needed
git pull --ff-only
python3 -m pip install -r requirements.txt --break-system-packages

# Ensure the application uses the primary display
export DISPLAY="${DISPLAY:-:0}"

# Run the Python program
python3 gpTricorder.py
