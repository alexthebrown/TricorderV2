#! /bin/bash
# Pi launcher: update from git, rebuild if the code changed, run the tricorder.

cd /home/tricorder/Desktop/Tricorder/TricorderV2 || exit

before=$(git rev-parse HEAD 2>/dev/null)
git pull --ff-only
after=$(git rev-parse HEAD 2>/dev/null)

# One-time dependency install (skipped once everything is present)
if ! pkg-config --exists sdl2 SDL2_ttf libavformat libswscale libcurl 2>/dev/null || ! command -v cmake >/dev/null; then
    sudo apt-get install -y cmake g++ pkg-config libsdl2-dev libsdl2-ttf-dev \
        libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libcurl4-openssl-dev
fi

# Rebuilding on a Pi Zero takes a few minutes, so only do it when needed.
if [[ ! -x build/tricorder || "$before" != "$after" ]]; then
    ./build.sh Release || exit 1
fi

# Under the desktop (X11) use the main display; from the console SDL uses KMS/DRM directly.
if pgrep -x Xorg >/dev/null || pgrep -x labwc >/dev/null || pgrep -x wayfire >/dev/null; then
    export DISPLAY=:0
fi

exec ./build/tricorder "$@"
