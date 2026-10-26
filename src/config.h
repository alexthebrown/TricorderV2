#pragma once
#include <string>

// All user-tunable settings. Defaults match the original Python tricorder on the Pi.
// Load order: built-in defaults -> config file -> command line.
struct Config {
    std::string username = "Scott Thunder";
    std::string videoDir = "/home/tricorder/networkdrive/Videos";  // desktop default set in main()
    std::string hiddenFolders = "Shared Music,Shared Photos";       // comma list, hidden in the log browser
    std::string fontPath;  // empty = auto-detect the "Trek" font (TOS_Title.ttf)
    std::string weatherUrl = "https://api.weather.gov/gridpoints/DVN/33,63/forecast/hourly";
    int weatherRefreshMin = 10;
    bool offline = false;  // never touch the network; simulate weather
    double latitude = 0, longitude = 0;  // 0,0 = take it from the weather.gov grid point
    bool alertTest = false;              // --red-alert-test: inject a fake severe weather warning

    double idleMinutes = 5;// idle scanning mode after this long untouched (0 = off)
    int webPort = 1701;    // subspace message / remote page (0 = off)
    std::string webPin;    // optional PIN required by the web page
    std::string uploadDir; // where uploaded logs go ("" = <library>/Captain's Logs)

    // Visitor relay (conventions): messages from anyone's phone via a free ntfy server.
    bool relay = false;
    std::string relayServer = "https://ntfy.sh";
    std::string relayTopic;   // "" = a random private topic, generated once and kept
    std::string relayPage;    // themed page visitors open (topic appended as #topic); "" = ntfy's web app
    std::string smsNumber;    // shown on screen if texts are forwarded to the relay
    bool moderate = true;     // relay messages wait for your approval
    bool profanityFilter = true;

    // Display
    bool fullscreen = false;   // default: on for Pi, off elsewhere
    float windowScale = 1.0f;  // desktop window size multiplier
    int overscanX = 0;         // shrink UI inward to fight composite overscan
    int overscanY = 0;
    float pixelAspect = 1.0f;  // 1.0667 for PAL composite on a 4:3 tube/panel
    bool lights = true;        // blinking indicator lights
    bool bootSequence = true;

    // Video
    std::string hwDecode = "auto";  // auto | off
    bool videoFill = false;         // false = letterbox, true = crop to fill
    bool audio = true;

    // Buttons (BCM numbering, active-low with pull-ups, like the old RPi.GPIO setup)
    bool gpio = false;  // default: on for Pi, off elsewhere
    std::string gpioChip = "/dev/gpiochip0";
    int pinUp = 17, pinDown = 18, pinLeft = 27, pinRight = 22, pinEnter = 23;

    std::string shutdownCmd = "sudo -n /sbin/poweroff";

    std::string script;  // --script: automated button presses / screenshots (testing)

    std::string loadedFrom;  // file the settings came from (informational)

    bool load(const std::string& path);
    // Writes only settings that differ from `defaults`, so later default changes still apply.
    bool save(const std::string& path, const Config& defaults) const;
    bool set(const std::string& key, const std::string& value);
    // Returns false if the program should exit (e.g. --help).
    bool applyArgs(int argc, char** argv, std::string& configOverride);
};

bool isRaspberryPi();
std::string piModel();
std::string homeDir();
std::string configFilePath();  // ~/.config/tricorder/tricorder.conf
std::string stateDir();        // ~/.local/state/tricorder (created on demand)
std::string exeDir();
bool fileExists(const std::string& p);
bool dirExists(const std::string& p);
