#!/usr/bin/env bash
# Generate test "captain's logs" in sample_data/videos so the tricorder can be run on a
# desktop without the Pi's network drive. Needs the ffmpeg command-line tool.
set -euo pipefail
cd "$(dirname "$0")/.."
out=sample_data/videos
mkdir -p "$out"
command -v ffmpeg >/dev/null || { echo "ffmpeg not found (sudo apt install ffmpeg)"; exit 1; }

make() {  # name seconds size codec tone
    local name=$1 secs=$2 size=$3 codec=$4 tone=$5
    local file="$out/$name.mp4"
    [[ -f $file ]] && { echo "exists: $file"; return; }
    echo "making: $file"
    local vcodec=(-c:v libx264 -preset veryfast -pix_fmt yuv420p)
    [[ $codec == mpeg4 ]] && vcodec=(-c:v mpeg4 -q:v 4)
    ffmpeg -loglevel error -y \
        -f lavfi -i "testsrc2=size=$size:rate=25" \
        -f lavfi -i "sine=frequency=$tone:sample_rate=48000:beep_factor=4" \
        -t "$secs" -shortest "${vcodec[@]}" -c:a aac -b:a 96k -movflags +faststart "$file"
}

make "Stardate 1312.4 - Where No Man Has Gone Before" 60 720x576 h264 440
make "Stardate 1513.1 - The Man Trap" 20 640x480 h264 520
make "Stardate 2124.5 - Arena (1080p stress test)" 20 1920x1080 h264 600
make "Stardate 3134.0 - Amok Time (MPEG-4)" 15 720x400 mpeg4 660
for i in $(seq 1 12); do
    make "Supplemental Log $(printf %02d "$i")" 5 480x270 h264 $((300 + i * 40))
done
echo "done: $(ls "$out" | wc -l) files in $out"
