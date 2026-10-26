#pragma once
#include <deque>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "config.h"
#include "gfx.h"
#include "input.h"
#include "media_library.h"
#include "relay.h"
#include "subspace.h"
#include "sysinfo.h"
#include "weather.h"

enum class ScreenId {
    Boot, MainMenu, Planet, CaptainsLog, Player, Status, Sensors, Settings,
    Library, Chrono, Cartography, Orbital, Database, Tactical, Messages,
    Count
};

class App;

class Screen {
public:
    explicit Screen(App& a) : app(a) {}
    virtual ~Screen() = default;
    virtual void enter() {}
    virtual void exit() {}
    virtual void input(Button b) = 0;
    virtual void back();  // long-press ENTER / Esc; default returns to the main menu
    virtual void update(double dt) { (void)dt; }
    virtual void render(Gfx& g) = 0;
    virtual bool animating() const { return false; }  // true = render at full frame rate
    virtual bool allowIdle() const { return true; }   // may the standby scan take over?

protected:
    App& app;
};

class App {
public:
    App(Config& cfg, const Config& defaults);
    ~App();
    int run();

    void go(ScreenId id);
    void toast(const std::string& msg, double secs = 2.5);
    void quit() { running_ = false; }
    // Plays list[index]; following plain files in the list continue automatically (TV episodes).
    void playVideo(const std::vector<MediaItem>& list, int index);
    double time() const;  // seconds since start
    bool saveConfig();
    bool redAlertActive() const;  // any unexpired red-level alert (acknowledged or not)

    Config& cfg;
    const Config defaults;  // before any config file, for saving only what changed
    Gfx gfx;
    Input input;
    WeatherService weather;
    SysMonitor sys;
    Prober prober;
    MediaLibrary library;
    SubspaceServer subspace;
    RelayClient relay;
    int unreadMessages() const { return subspace.unread(); }
    void setRelay(bool on);          // start/stop the visitor relay
    void newRelayTopic();            // fresh private topic (e.g. per event): old QR codes stop working
    std::string visitorUrl() const;  // what the QR code points to ("" when the relay is off)
    std::string subspaceAddress() const;  // "http://192.168.1.5:1701" or "" when offline
    std::vector<MediaItem> playlist;  // handed to the player on enter()
    int playIndex = 0;

private:
    void runScript(double now, std::vector<Button>& presses);
    void checkAlerts();
    bool overlayInput(Button b);  // true if an overlay consumed the press

    std::set<std::string> ackedAlerts_;
    bool redAlert_ = false;
    WeatherAlert activeAlert_;
    double alertTimer_ = 0;
    double lastInput_ = 0;
    bool idle_ = false;
    std::deque<SubspaceMessage> transmissions_;  // incoming messages waiting to be shown
    size_t lastPending_ = 0;

    std::vector<std::string> script_;
    size_t scriptPos_ = 0;
    double scriptWaitUntil_ = 0;
    std::string pendingShot_;
    std::unique_ptr<Screen> screens_[int(ScreenId::Count)];
    Screen* cur_ = nullptr;
    bool running_ = true;
    std::string toast_;
    double toastUntil_ = 0;
    double sysTimer_ = 0;
    uint64_t startCounter_ = 0;
};

// Screen factories (screens/*.cpp)
std::unique_ptr<Screen> makeBootScreen(App&);
std::unique_ptr<Screen> makeMainMenu(App&);
std::unique_ptr<Screen> makePlanetScreen(App&);
std::unique_ptr<Screen> makeCaptainsLog(App&);
std::unique_ptr<Screen> makePlayerScreen(App&);
std::unique_ptr<Screen> makeStatusScreen(App&);
std::unique_ptr<Screen> makeSensorScreen(App&);
std::unique_ptr<Screen> makeChronoScreen(App&);
std::unique_ptr<Screen> makeSettingsScreen(App&);
std::unique_ptr<Screen> makeLibraryMenu(App&);
std::unique_ptr<Screen> makeCartographyScreen(App&);
std::unique_ptr<Screen> makeOrbitalScreen(App&);
std::unique_ptr<Screen> makeDatabaseScreen(App&);
std::unique_ptr<Screen> makeTacticalScreen(App&);
std::unique_ptr<Screen> makeMessagesScreen(App&);
