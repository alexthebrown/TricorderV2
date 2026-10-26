#!/usr/bin/env bash
# Convert videos into the format a Pi Zero W plays best: H.264 (hardware decoded),
# no larger than the 720x576 composite screen, 25 fps, AAC audio, fast-start MP4.
# Run it on your desktop (much faster than on the Pi), then copy the results to the
# network drive's Videos folder.
#
#   tools/pi_transcode.sh OUTPUT_DIR input1.mkv input2.mp4 ...
set -euo pipefail
[[ $# -ge 2 ]] || { echo "usage: $0 OUTPUT_DIR FILE..."; exit 1; }
outdir=$1; shift
mkdir -p "$outdir"
for f in "$@"; do
    base=$(basename "${f%.*}")
    dst="$outdir/$base.mp4"
    echo ">> $f -> $dst"
    ffmpeg -hide_banner -loglevel warning -stats -y -i "$f" \
        -map 0:v:0 -map 0:a:0? \
        -vf "scale='min(720,iw)':'min(576,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2,fps=25" \
        -c:v libx264 -profile:v high -level 3.1 -preset slow -crf 22 -pix_fmt yuv420p \
        -c:a aac -b:a 128k -ac 2 \
        -movflags +faststart "$dst"
done
