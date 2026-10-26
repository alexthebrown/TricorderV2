# TricorderV2

Software for a basic yet functional tricorder from Star Trek: a Raspberry Pi Zero W, a tiny
720x576 composite screen and five buttons. It's written in C++ (SDL2 + FFmpeg) and replaces
the original Python/Tk version, which is kept in [`legacy/`](legacy/).

It runs on the Pi **and** on a normal Linux desktop for testing. You don't need the Pi's
network drive or GPIO to try it.

## Pages

| Button | Page | What it does |
| --- | --- | --- |
| PLANET | Planet Conditions | Live weather.gov conditions as in the original page, plus wind and a 12-hour forecast strip. It works offline from a cache, or falls back to simulated "sensor estimates". |
| CAPTAIN'S LOG | Captain's Log Page | A video library browser: yellow buttons are folders, green ones play. On the desktop it reads your Plex share. Movie folders play their film directly, TV episodes continue to the next one, and it shows running times and offers to resume where you stopped. |
| STATUS | Status Report | Real diagnostics: core temperature, CPU, memory, storage, Wi-Fi link and signal, Pi under-voltage/throttling, uptime, stardate. |
| SENSORS | Sensor Page | Scanning modes styled on the prop: **Geological**, **Biological**, **Atmospheric** and **Subspace**. Each one plays a sweep animation, then shows readouts and a live scope trace. Subspace lists the real nearby Wi-Fi networks. Biological counts the devices on your LAN as "life signs". Atmospheric uses the weather data. |
| SELECT | Library Computer | A menu holding the pages below. |
| | Chronometer | Stardate, ship's time and date, and a mission timer. |
| | Stellar Cartography | Tonight's sky for your location, computed offline: a sky chart with the Sun, Moon and planets, sunrise and sunset, moon phase and rise/set, and when each planet is up. Left/right steps time by an hour, up/down by a day. |
| | Orbital Tracking | The ISS live on a world map with its ground track, altitude and speed, plus the next passes over you, marking the ones you can see with your own eyes. It works offline from the last downloaded orbit for a few days. |
| | Ship's Database | Crew, starships, species, technology and mission logs, read from [`data/library.txt`](data/library.txt). Add your own entries there. |
| | Tactical Simulation | The classic 1971 *Star Trek* game for five buttons: hunt Klingons across an 8x8 galaxy, dock at starbases, and watch your energy and stardates. |
| | Subspace Messages | Messages sent from a phone through the tricorder's web page (see below). |
| INPUT | Configuration | Overscan adjustment with an edge guide for the composite screen, hardware/software decoding, fit/fill scaling, audio, indicator lights, boot sequence, save, exit and safe **shut down**. |

On top of every page:
- **Red Alert.** When weather.gov issues a warning for your area (tornado, severe thunderstorm and so on), the tricorder goes to a flashing full-screen RED ALERT with the instructions. Test it with `--red-alert-test`.
- **Incoming transmissions** from the web page pop up as they arrive.
- **Standby scan.** After 5 minutes untouched (`idle_minutes`), it switches to a rotating scanner display. Any button wakes it.

There is also a skippable boot sequence, and blinking indicator lamps like the ones on the
prop.

## Subspace link (phone web page)

While the tricorder is on a network, open `http://<its address>:1701` on any phone or
computer. The Subspace Messages page shows the address. From there you can:
- send a message that pops up on the tricorder;
- drive it with an on-screen D-pad;
- upload a video straight into the captain's logs (`<library>/Captain's Logs`, or `upload_dir`).

Anyone on the same network can use the page. Set `web_pin` to require a PIN, or
`web_port = 0` to turn the page off.

## Visitor relay (conventions)

At a convention the tricorder rides on your phone's hotspot, which visitors shouldn't
join. Instead, turn on **VISITOR RELAY** (INPUT page, `relay = true`, or `--relay`):

- A QR code appears on the Subspace Messages page, and on the standby screen as a
  "transmit to the tricorder" attract mode. Visitors scan it with their own phone data.
- Their messages go through the free relay [ntfy.sh](https://ntfy.sh) on a long random topic
  only your tricorder knows. The tricorder checks for new ones every few seconds.
- **Nothing goes on screen until you clear it.** Relay messages wait in red on the Subspace
  Messages page: open one, ENTER to display it or LEFT to reject it. You can also review them
  from your own phone, since it's on the hotspot: open the local page (`http://<tricorder>:1701`)
  with your `web_pin`. Bad words are masked and phone numbers are never shown.
- **RELAY FREQUENCY → CHANGE** (press twice) on the INPUT page picks a fresh topic, for example at the start of
  each event, so old QR codes stop working.

**The visitor page** is hosted free on GitHub Pages at
**https://alexthebrown.github.io/TricorderV2/tricorder/**, and the QR code opens it with your topic attached
(`…/tricorder/#tricorder-…`). The file is [`docs/tricorder/index.html`](docs/tricorder/index.html).
To switch Pages on: push the repo, then go to **Settings → Pages → Deploy from a branch → `main` / `/docs`**.
It only sends messages, unlike ntfy's own web app (used if `relay_page` is empty), which shows
visitors *everyone's* raw messages. For local testing the tricorder serves the same page at
`http://localhost:1701/tricorder`.

The topic is still readable by anyone determined enough to pull it out of the QR code, so
the approval queue is the real filter. Messages expire from ntfy.sh after 12 hours.

**Text messages** can feed the same relay. Anything that POSTs to
`https://ntfy.sh/<your topic>` works. A phone automation that forwards texts beginning with
"TRICORDER" puts them into the approval queue as "TEXT MESSAGE". Set `sms_number` to show the
number on screen.

## Controls

| Tricorder button | Keyboard |
| --- | --- |
| Up / Down / Left / Right | Arrow keys or WASD |
| Enter | Enter or Space |
| Back (**hold Enter** for ~0.7 s) | Esc or Backspace |
| | F11 toggles fullscreen, Ctrl+Q quits |

Directions move to the nearest button that way and wrap around like the original. Holding a
direction auto-repeats. In the video player, the first press wakes the hidden controls:
`<<` / `>>` skip 10 s, and `Stop` (or hold Enter) goes back to the log list.

## Video playback

The old player decoded with OpenCV, converted every frame to RGB, then to a PIL image and
then a Tk image, on a fixed 30 ms timer. The new one:

* decodes on a separate thread, so the UI never waits for the decoder;
* uses the Pi's **hardware H.264 decoder** (`h264_v4l2m2m`) and falls back to software
  automatically;
* uploads YUV straight to a GPU texture, which scales it with no colour conversion on the CPU;
* times playback to real time (audio is the master clock when there's sound) and **drops
  late frames** instead of slowing down;
* when it keeps falling behind, it skips deblocking and then non-reference frames, and
  recovers when it catches up;
* supports pause, seek, resume where you left off, letterbox or crop-to-fill, and correct
  aspect ratio.

For the smoothest playback on a Pi Zero, convert your videos once on your desktop:

```bash
tools/pi_transcode.sh ~/converted  ~/Videos/*.mkv
```

That produces ≤720x576, 25 fps, H.264 + AAC files. Copy them to the Pi's
video folder (`video_dir`).

## Running on the desktop

```bash
sudo apt install cmake g++ pkg-config ffmpeg libsdl2-dev libsdl2-ttf-dev \
    libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libcurl4-openssl-dev
./build.sh
tools/make_sample_videos.sh      # test logs in sample_data/videos
./build/tricorder                # or: ./build/tricorder --offline --scale 1.5
```

On the desktop, the captain's logs come from your Plex share
`smb://mycloud-0147dd.local/itsgreen1701/Plex`, through the mount the Files app makes. If it
isn't mounted, the tricorder asks GNOME to mount it, or you can open the share once in Files.
The Shared Music and Shared Photos folders are hidden (`hidden_folders`). If the share is
unreachable, the program uses `sample_data/videos`, then `~/Videos`, and the log page says
it's on the local archive. On the Pi, `video_dir` stays a local folder.
Weather works over the internet as normal. Use `--offline` to simulate it.

Font: the original "Trek" font (`TOS_Title.ttf`) is found in `assets/fonts/`,
`~/.local/share/fonts/`, or via fontconfig. If it's missing, a system sans font is used.

Run `./build/tricorder --help` for all options. Unit tests: `ctest --test-dir build`.

**Location** for the sky chart, ISS passes and weather alerts comes from the centre of the
weather.gov grid point in `weather_url` (currently 41.47°N 91.58°W). Set `latitude` and
`longitude` to override it. It's cached, so it also works offline.

**Testing:** `--script` drives the program automatically, for example
`--script "1,R,E,2,shot:/tmp/log.bmp,quit"`. Set `TRICORDER_DEBUG=1` to see FFmpeg's log output.

## Setting up the Pi Zero W (boots straight into the tricorder, no desktop)

1. Flash **Raspberry Pi OS Lite (32-bit)** with Raspberry Pi Imager: hostname `tricorder`,
   user `tricorder`, your phone hotspot's Wi-Fi, SSH on.
2. Composite screen, in `/boot/firmware/config.txt`:
   ```
   dtoverlay=vc4-kms-v3d,composite
   enable_tvout=1
   ```
   At the end of the single line in `/boot/firmware/cmdline.txt`, add
   `video=Composite-1:720x576@50ie vc4.tv_norm=PAL quiet loglevel=3 logo.nologo vt.global_cursor_default=0 consoleblank=0`.
3. Install the build tools, clone to `/home/tricorder/Desktop/Tricorder/TricorderV2`, and run `./build.sh`.
   Expect a long build on a Zero.
4. Start at boot: `sudo cp tools/tricorder.service /etc/systemd/system/ && sudo systemctl enable tricorder`.
   **EXIT PROGRAM** drops to a console login, and **SHUT DOWN** powers off.
5. Allow the **SHUT DOWN** button: `/etc/sudoers.d/tricorder` containing
   `tricorder ALL=(root) NOPASSWD: /sbin/poweroff`.
6. Logs: `journalctl -u tricorder -f`. To update: `git pull && ./build.sh && sudo systemctl restart tricorder`.

Settings live in `~/.config/tricorder/tricorder.conf`. See
[`tricorder.conf.example`](tricorder.conf.example). Buttons are BCM 17/18/27/22/23, wired
to ground, read through the Linux GPIO character device. The user needs to be in the `gpio`
group, which the default Pi user is.

## Layout

```
src/            app shell, drawing (gfx), input + GPIO, video player, data services,
                astronomy (astro), satellite tracking (orbit), web link (subspace)
src/screens/    one file per page
data/           Ship's Database text
tools/          sample video generator, Pi transcoder, systemd unit
tests/          unit tests (JSON, forecast parsing, config, formatting)
legacy/         the original Python version
```
