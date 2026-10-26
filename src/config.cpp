#include "config.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

namespace {

struct Field {
    const char* key;
    std::function<bool(Config&, const std::string&)> set;
    std::function<std::string(const Config&)> get;
};

bool parseBool(const std::string& v, bool& out) {
    if (v == "1" || v == "true" || v == "yes" || v == "on") { out = true; return true; }
    if (v == "0" || v == "false" || v == "no" || v == "off") { out = false; return true; }
    return false;
}

#define STR_FIELD(k, m) {k, [](Config& c, const std::string& v) { c.m = v; return true; }, [](const Config& c) { return c.m; }}
#define INT_FIELD(k, m) {k, [](Config& c, const std::string& v) { try { c.m = std::stoi(v); return true; } catch (...) { return false; } }, [](const Config& c) { return std::to_string(c.m); }}
#define FLT_FIELD(k, m) {k, [](Config& c, const std::string& v) { try { c.m = std::stof(v); return true; } catch (...) { return false; } }, [](const Config& c) { char b[32]; snprintf(b, sizeof b, "%g", c.m); return std::string(b); }}
#define DBL_FIELD(k, m) {k, [](Config& c, const std::string& v) { try { c.m = std::stod(v); return true; } catch (...) { return false; } }, [](const Config& c) { char b[32]; snprintf(b, sizeof b, "%.5f", c.m); return std::string(b); }}
#define BOOL_FIELD(k, m) {k, [](Config& c, const std::string& v) { return parseBool(v, c.m); }, [](const Config& c) { return std::string(c.m ? "true" : "false"); }}

const std::vector<Field>& fields() {
    static const std::vector<Field> f = {
        STR_FIELD("username", username),
        STR_FIELD("video_dir", videoDir),
        STR_FIELD("hidden_folders", hiddenFolders),
        STR_FIELD("font", fontPath),
        STR_FIELD("weather_url", weatherUrl),
        INT_FIELD("weather_refresh_min", weatherRefreshMin),
        BOOL_FIELD("offline", offline),
        DBL_FIELD("latitude", latitude),
        DBL_FIELD("longitude", longitude),
        DBL_FIELD("idle_minutes", idleMinutes),
        INT_FIELD("web_port", webPort),
        STR_FIELD("web_pin", webPin),
        STR_FIELD("upload_dir", uploadDir),
        BOOL_FIELD("relay", relay),
        STR_FIELD("relay_server", relayServer),
        STR_FIELD("relay_topic", relayTopic),
        STR_FIELD("relay_page", relayPage),
        STR_FIELD("sms_number", smsNumber),
        BOOL_FIELD("moderate", moderate),
        BOOL_FIELD("profanity_filter", profanityFilter),
        BOOL_FIELD("fullscreen", fullscreen),
        FLT_FIELD("window_scale", windowScale),
        INT_FIELD("overscan_x", overscanX),
        INT_FIELD("overscan_y", overscanY),
        FLT_FIELD("pixel_aspect", pixelAspect),
        BOOL_FIELD("lights", lights),
        BOOL_FIELD("bold_text", boldText),
        BOOL_FIELD("boot_sequence", bootSequence),
        STR_FIELD("hw_decode", hwDecode),
        BOOL_FIELD("video_fill", videoFill),
        BOOL_FIELD("audio", audio),
        BOOL_FIELD("gpio", gpio),
        STR_FIELD("gpio_chip", gpioChip),
        INT_FIELD("pin_up", pinUp),
        INT_FIELD("pin_down", pinDown),
        INT_FIELD("pin_left", pinLeft),
        INT_FIELD("pin_right", pinRight),
        INT_FIELD("pin_enter", pinEnter),
        STR_FIELD("shutdown_cmd", shutdownCmd),
    };
    return f;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void mkdirs(const std::string& path) {
    std::string cur;
    std::stringstream ss(path);
    std::string part;
    if (!path.empty() && path[0] == '/') cur = "/";
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        cur += part + "/";
        mkdir(cur.c_str(), 0755);
    }
}

}  // namespace

bool Config::set(const std::string& key, const std::string& value) {
    for (auto& f : fields())
        if (key == f.key) return f.set(*this, value);
    return false;
}

bool Config::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        if (!set(k, v)) fprintf(stderr, "config %s:%d: bad setting '%s'\n", path.c_str(), n, k.c_str());
    }
    loadedFrom = path;
    return true;
}

bool Config::save(const std::string& path, const Config& defaults) const {
    size_t slash = path.rfind('/');
    if (slash != std::string::npos) mkdirs(path.substr(0, slash));
    std::ofstream out(path);
    if (!out) return false;
    out << "# Tricorder configuration (written by the INPUT > Configuration page)\n";
    for (auto& f : fields())
        if (f.get(*this) != f.get(defaults)) out << f.key << " = " << f.get(*this) << "\n";
    return bool(out);
}

bool Config::applyArgs(int argc, char** argv, std::string& configOverride) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string& dst) {
            if (i + 1 < argc) { dst = argv[++i]; return true; }
            fprintf(stderr, "%s needs a value\n", a.c_str());
            return false;
        };
        std::string v;
        if (a == "--help" || a == "-h") {
            printf(
                "Usage: tricorder [options]\n"
                "  --videos DIR       Captain's log video folder (path or smb://host/share/path)\n"
                "  --font FILE        TTF font to use (default: auto-detect Trek/TOS_Title)\n"
                "  --config FILE      Config file (default: %s)\n"
                "  --windowed         Run in a window\n"
                "  --fullscreen       Run fullscreen\n"
                "  --scale F          Window size multiplier (desktop)\n"
                "  --offline          No network; simulated weather\n"
                "  --no-gpio          Disable GPIO buttons\n"
                "  --gpio             Force-enable GPIO buttons\n"
                "  --no-boot          Skip boot sequence\n"
                "  --sw-decode        Force software video decoding\n"
                "  --no-audio         Disable audio\n"
                "  --set KEY=VALUE    Set any config key (see tricorder.conf.example)\n"
                "  --red-alert-test   Pretend a severe weather warning is active\n"
                "  --relay            Turn on the visitor message relay (see README)\n"
                "  --script STEPS     Automate: e.g. \"2,R,E,1.5,shot:menu.bmp,B,quit\" (seconds to wait,\n"
                "                     U/D/L/R/E/B buttons, shot:FILE saves a screenshot, quit)\n"
                "Keys: arrows/WASD = buttons, Enter/Space = enter, Esc/Backspace = back (long-press enter),\n"
                "      F11 = fullscreen, Ctrl+Q = quit\n",
                configFilePath().c_str());
            return false;
        } else if (a == "--videos") { if (next(v)) videoDir = v; }
        else if (a == "--font") { if (next(v)) fontPath = v; }
        else if (a == "--config") { if (next(v)) configOverride = v; }
        else if (a == "--windowed") fullscreen = false;
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--scale") { if (next(v) && !set("window_scale", v)) fprintf(stderr, "bad --scale\n"); }
        else if (a == "--offline") offline = true;
        else if (a == "--no-gpio") gpio = false;
        else if (a == "--gpio") gpio = true;
        else if (a == "--no-boot") bootSequence = false;
        else if (a == "--sw-decode") hwDecode = "off";
        else if (a == "--no-audio") audio = false;
        else if (a == "--script") next(script);
        else if (a == "--red-alert-test") alertTest = true;
        else if (a == "--relay") relay = true;
        else if (a == "--set") {
            if (next(v)) {
                size_t eq = v.find('=');
                if (eq == std::string::npos || !set(v.substr(0, eq), v.substr(eq + 1)))
                    fprintf(stderr, "bad --set '%s'\n", v.c_str());
            }
        } else {
            fprintf(stderr, "unknown option %s (try --help)\n", a.c_str());
        }
    }
    return true;
}

std::string piModel() {
    std::ifstream in("/proc/device-tree/model");
    std::string s;
    std::getline(in, s, '\0');
    return s;
}

bool isRaspberryPi() { return piModel().find("Raspberry Pi") != std::string::npos; }

std::string homeDir() {
    const char* h = getenv("HOME");
    return h ? h : "/tmp";
}

std::string configFilePath() {
    const char* x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : homeDir() + "/.config";
    return base + "/tricorder/tricorder.conf";
}

std::string stateDir() {
    const char* x = getenv("XDG_STATE_HOME");
    std::string dir = (x && *x ? std::string(x) : homeDir() + "/.local/state") + "/tricorder";
    mkdirs(dir);
    return dir;
}

std::string exeDir() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
    std::string p(buf);
    size_t slash = p.rfind('/');
    return slash == std::string::npos ? "." : p.substr(0, slash);
}

bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool dirExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
