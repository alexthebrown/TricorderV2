#! /bin/bash

# Navigate to the desired directory
cd /home/tricorder/Desktop/Tricorder/TricorderV2 || exit

# Perform a git pull to update the repository
git pull

# Run dependency installer
pip install -r 'requirements.txt' --break-system-packages

# Sudo apt library installs
sudo apt install python3-opencv -y
sudo apt-get install python3-pil python3-pil.imagetk


# Export display so it runs on the main display
export DISPLAY=:0

# Run the Python Program
python gpTricorder.py