// TricorderV2 — Star Trek tricorder for a Raspberry Pi Zero W with a composite screen.
// Also runs on a normal Linux desktop for development (keyboard = the five buttons).
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>

#include "app.h"
#include "config.h"

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

extern "C" {
#include <libavutil/log.h>
}

int main(int argc, char** argv) {
    setlocale(LC_ALL, "");
    Config cfg;
    bool pi = isRaspberryPi();
    cfg.fullscreen = pi;
    cfg.gpio = pi;
    // On the desktop, read the captain's logs from the Plex share (mounted by GNOME Files).
    if (!pi) cfg.videoDir = "smb://mycloud-0147dd.local/itsgreen1701/Plex";

    const Config defaults = cfg;
    std::string configOverride;
    {
        // First pass only to find --config; flags are re-applied after the file loads.
        Config scratch;
        if (!scratch.applyArgs(argc, argv, configOverride)) return 0;
    }
    std::string path = configOverride.empty() ? configFilePath() : configOverride;
    if (!cfg.load(path) && configOverride.empty()) cfg.load("tricorder.conf");
    std::string ignored;
    cfg.applyArgs(argc, argv, ignored);
    if (!cfg.loadedFrom.empty()) fprintf(stderr, "config: %s\n", cfg.loadedFrom.c_str());

    // Odd files on a media drive make FFmpeg very chatty; TRICORDER_DEBUG=1 shows it all.
    av_log_set_level(getenv("TRICORDER_DEBUG") ? AV_LOG_INFO : AV_LOG_FATAL);
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
#ifdef HAVE_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    int rc;
    {
        App app(cfg, defaults);
        rc = app.run();
    }
#ifdef HAVE_CURL
    curl_global_cleanup();
#endif
    SDL_Quit();
    return rc;
}
