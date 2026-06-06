#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <thread>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>

namespace fs = std::filesystem;

static const int WINDOW_WIDTH = 720;
static const int WINDOW_HEIGHT = 576;
static const char* VIDEO_DIR = "videos";
static const char* FONT_PATHS[] = {
    "/home/tricorder/.fonts/Trek.ttf",
    "/home/tricorder/.local/share/fonts/Trek.ttf",
    "/home/tricorder/Desktop/Tricorder/TricorderV2/fonts/Trek.ttf",
    "/home/tricorder/Desktop/Tricorder/TricorderV2/Trek.ttf",
    "/home/tricorder/TricorderV2/fonts/Trek.ttf",
    "/home/tricorder/TricorderV2/Trek.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/freefont/FreeSans.ttf"
};

enum class Page {
    Main,
    Planet,
    CaptainsLog,
    Media,
    Sensor,
    Player,
    Status,
    DutyRoster
};

struct WeatherInfo {
    bool valid = false;
    int temperature = 0;
    int humidity = 0;
    int precip = 0;
    std::string shortForecast = "";
    std::string detailedForecast = "";
    std::string windSpeed = "";
    std::string windDirection = "";
    std::string status = "Not loaded";
};

struct Button {
    SDL_Rect rect;
    std::string label;
    std::function<void()> action;
    bool selected = false;
};

struct RosterEvent {
    std::string date;
    std::string start;
    std::string title;
    std::string location;
    std::string details;
    time_t timestamp = 0;
};

class App {
public:
    App();
    ~App();
    bool initialize();
    int run();

private:
    bool initializeSDL();
    bool initializeFonts();
    void loadVideos();
    void rebuildCaptainsLogButtons();
    void setPage(Page page);
    void layoutButtons();
    void render();
    void renderButtons(const std::vector<Button>& buttons);
    void renderText(const std::string& text, int x, int y, SDL_Color color, TTF_Font* font);
    int renderWrappedText(const std::string& text, int x, int y, int maxWidth, SDL_Color color, TTF_Font* font, int linePadding = 4);
    int measureWrappedHeight(const std::string& text, int maxWidth, TTF_Font* font, int linePadding = 4) const;
    SDL_Texture* createTextTexture(const std::string& text, SDL_Color color, TTF_Font* font, int& w, int& h);
    void handleEvent(const SDL_Event& event);
    void navigate(int direction);
    void invokeSelected();
    void submitInput();
    std::string getStatusText() const;
    static bool fileExists(const std::string& path);

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* fontLarge_ = nullptr;
    TTF_Font* fontMedium_ = nullptr;
    TTF_Font* fontSmall_ = nullptr;
    Page currentPage_ = Page::Main;
    std::vector<Button> mainButtons_;
    std::vector<Button> planetButtons_;
    std::vector<Button> captainsLogButtons_;
    std::vector<Button> mediaButtons_;
    std::vector<Button> sensorButtons_;
    std::vector<Button> playerButtons_;
    std::vector<Button> statusButtons_;
    std::vector<Button> rosterButtons_;
    std::vector<std::string> videoFiles_;
    std::vector<std::string> logEntries_;
    std::vector<RosterEvent> rosterEvents_;
    std::vector<Button>* currentButtons_ = nullptr;
    size_t selectedIndex_ = 0;
    size_t rosterPage_ = 0;
    size_t rosterPageSize_ = 5;
    std::string rosterStatus_ = "Duty roster not loaded.";
    bool rosterValid_ = false;
    int selectedVideoIndex_ = -1;
    bool playingVideo_ = false;
    bool gpioAvailable_ = false;
    std::map<int, std::string> gpioValuePaths_;
    std::map<int, int> gpioStates_;
    WeatherInfo weather_;
    bool running_ = false;

    bool initializeGPIO();
    void pollGPIO();
    bool readGPIOValue(const std::string& path, int& value) const;
    bool fetchWeather();
    bool fetchRoster();
    std::vector<std::string> extractTextBlocks(const std::string& html) const;
    std::vector<RosterEvent> parseRosterEvents(const std::vector<std::string>& lines) const;
    bool parseEventDate(const std::string& text, std::tm& tm) const;
    bool parseEventTime(const std::string& text, int& hour, int& minute) const;
    std::string normalizeWhitespace(const std::string& text) const;
    std::string trim(const std::string& text) const;
    std::string runCommand(const std::string& command) const;
    std::string findJsonStringValue(const std::string& json, const std::string& key) const;
    int findJsonIntValue(const std::string& json, const std::string& key) const;
    std::vector<std::string> findJsonStringValues(const std::string& json, const std::string& key, int maxCount) const;
    std::vector<int> findJsonIntValues(const std::string& json, const std::string& key, int maxCount) const;
    std::string getWeatherText() const;
    std::string getUptime() const;
    std::string getLoadAverage() const;
    std::string getDiskUsage() const;
};

App::App() {
}

App::~App() {
    if (fontSmall_) TTF_CloseFont(fontSmall_);
    if (fontMedium_) TTF_CloseFont(fontMedium_);
    if (fontLarge_) TTF_CloseFont(fontLarge_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    TTF_Quit();
    SDL_Quit();
}

bool App::initialize() {
    if (!initializeSDL()) return false;
    if (!initializeFonts()) return false;
    loadVideos();

    gpioAvailable_ = initializeGPIO();
    weather_.status = "Not loaded";
    running_ = true;

    mainButtons_ = {
        {SDL_Rect{60, 170, 180, 80}, "PLANET", [this]() { setPage(Page::Planet); }, false},
        {SDL_Rect{260, 170, 180, 80}, "LOGS", [this]() { setPage(Page::CaptainsLog); }, false},
        {SDL_Rect{460, 170, 180, 80}, "SENSORS", [this]() { setPage(Page::Sensor); }, false},
        {SDL_Rect{60, 280, 180, 80}, "STATUS", [this]() { setPage(Page::Status); }, false},
        {SDL_Rect{260, 280, 180, 80}, "MEDIA", [this]() { setPage(Page::Media); }, false},
        {SDL_Rect{460, 280, 180, 80}, "DUTY ROSTER", [this]() { setPage(Page::DutyRoster); }, false}
    };

    planetButtons_ = {
        {SDL_Rect{20, 20, 150, 50}, "BACK", [this]() { setPage(Page::Main); }, false},
        {SDL_Rect{200, 20, 150, 50}, "REFRESH", [this]() { fetchWeather(); setPage(Page::Planet); }, false}
    };
    sensorButtons_ = {{SDL_Rect{20, 20, 150, 50}, "BACK", [this]() { setPage(Page::Main); }, false}};
    statusButtons_ = {{SDL_Rect{20, 20, 150, 50}, "BACK", [this]() { setPage(Page::Main); }, false}};
    rosterButtons_ = {
        {SDL_Rect{20, 20, 150, 50}, "BACK", [this]() { setPage(Page::Main); }, false},
        {SDL_Rect{220, 20, 150, 50}, "REFRESH", [this]() { fetchRoster(); setPage(Page::DutyRoster); }, false},
        {SDL_Rect{420, 20, 150, 50}, "PREV", [this]() { if (rosterPage_ > 0) --rosterPage_; }, false},
        {SDL_Rect{520, 20, 150, 50}, "NEXT", [this]() { if ((rosterPage_ + 1) * rosterPageSize_ < rosterEvents_.size()) ++rosterPage_; }, false}
    };
    logEntries_ = {
        "Captain's log entry 1: Sensor sweep complete.",
        "Captain's log entry 2: Navigation chart updated.",
        "Captain's log entry 3: Hostile anomaly detected off-sensor port.",
        "Captain's log entry 4: Energy signature stable."
    };
    playerButtons_ = {
        {SDL_Rect{20, 20, 150, 50}, "BACK", [this]() { setPage(Page::Media); }, false},
        {SDL_Rect{220, 490, 140, 60}, "PLAY", [this]() { if (selectedVideoIndex_ >= 0) playingVideo_ = true; }, false},
        {SDL_Rect{380, 490, 140, 60}, "PAUSE", [this]() { playingVideo_ = false; }, false},
        {SDL_Rect{540, 490, 140, 60}, "STOP", [this]() { playingVideo_ = false; selectedVideoIndex_ = -1; setPage(Page::Media); }, false}
    };
    rebuildCaptainsLogButtons();
    setPage(Page::Main);
    return true;
}

bool App::initializeSDL() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init error: " << SDL_GetError() << "\n";
        return false;
    }
    window_ = SDL_CreateWindow("Tricorder", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
    if (!window_) {
        std::cerr << "SDL_CreateWindow error: " << SDL_GetError() << "\n";
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        std::cerr << "SDL_CreateRenderer error: " << SDL_GetError() << "\n";
        return false;
    }
    SDL_RenderSetLogicalSize(renderer_, WINDOW_WIDTH, WINDOW_HEIGHT);
    return true;
}

bool App::initializeFonts() {
    if (TTF_Init() != 0) {
        std::cerr << "TTF_Init error: " << TTF_GetError() << "\n";
        return false;
    }
    std::string fontPathStorage;
    const char* fontPath = nullptr;
    if (const char* envFont = std::getenv("TRICORDER_FONT")) {
        if (fileExists(envFont)) {
            fontPathStorage = envFont;
            fontPath = fontPathStorage.c_str();
        } else {
            std::cerr << "TRICORDER_FONT is set but file not found: " << envFont << "\n";
        }
    }
    if (!fontPath) {
        for (const char* path : FONT_PATHS) {
            if (fileExists(path)) {
                fontPathStorage = path;
                fontPath = fontPathStorage.c_str();
                break;
            }
        }
    }
    if (!fontPath) {
        std::vector<fs::path> searchDirs = {
            "/usr/share/fonts",
            "/usr/local/share/fonts",
            "/Library/Fonts",
            "/System/Library/Fonts"
        };
        if (const char* home = std::getenv("HOME")) {
            searchDirs.push_back(fs::path(home) / "Library" / "Fonts");
        }
        for (const auto& dir : searchDirs) {
            if (!fs::exists(dir) || !fs::is_directory(dir)) {
                continue;
            }
            for (auto& entry : fs::recursive_directory_iterator(dir)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                auto ext = entry.path().extension().string();
                if (ext == ".ttf" || ext == ".TTF" || ext == ".otf" || ext == ".OTF") {
                    fontPathStorage = entry.path().string();
                    fontPath = fontPathStorage.c_str();
                    break;
                }
            }
            if (fontPath) {
                break;
            }
        }
    }
    if (!fontPath) {
        std::cerr << "Could not find a TTF font in standard locations.\n";
        return false;
    }
    fontLarge_ = TTF_OpenFont(fontPath, 42);
    fontMedium_ = TTF_OpenFont(fontPath, 28);
    fontSmall_ = TTF_OpenFont(fontPath, 20);
    if (!fontLarge_ || !fontMedium_ || !fontSmall_) {
        std::cerr << "TTF_OpenFont error: " << TTF_GetError() << "\n";
        return false;
    }
    std::cout << "Using font: " << fontPath << "\n";
    return true;
}

bool App::initializeGPIO() {
    if (!fs::exists("/sys/class/gpio")) {
        return false;
    }

    const std::vector<int> pins = {17, 18, 27, 22, 23};
    for (int pin : pins) {
        fs::path valuePath = fs::path("/sys/class/gpio/gpio") / std::to_string(pin) / "value";
        if (!fs::exists(valuePath)) {
            continue;
        }
        gpioValuePaths_[pin] = valuePath.string();
        int value = 1;
        if (!readGPIOValue(gpioValuePaths_[pin], value)) {
            value = 1;
        }
        gpioStates_[pin] = value;
    }
    return !gpioValuePaths_.empty();
}

bool App::readGPIOValue(const std::string& path, int& value) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    char c = '1';
    file >> c;
    if (file.fail()) {
        return false;
    }
    value = (c == '0' ? 0 : 1);
    return true;
}

void App::pollGPIO() {
    for (const auto& kv : gpioValuePaths_) {
        int current = 1;
        if (!readGPIOValue(kv.second, current)) {
            continue;
        }
        int previous = gpioStates_[kv.first];
        if (previous == 1 && current == 0) {
            if (kv.first == 17 || kv.first == 27) {
                navigate(-1);
            } else if (kv.first == 18 || kv.first == 22) {
                navigate(1);
            } else if (kv.first == 23) {
                invokeSelected();
            }
        }
        gpioStates_[kv.first] = current;
    }
}

void App::loadVideos() {
    videoFiles_.clear();
    try {
        fs::path path(VIDEO_DIR);
        if (!fs::exists(path)) {
            fs::create_directories(path);
        }
        if (fs::exists(path) && fs::is_directory(path)) {
            for (auto& entry : fs::directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    std::string name = entry.path().filename().string();
                    if (entry.path().extension() == ".mp4") {
                        videoFiles_.push_back(name);
                    }
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Video scan failed: " << e.what() << "\n";
    }
    rebuildCaptainsLogButtons();
}

void App::rebuildCaptainsLogButtons() {
    captainsLogButtons_.clear();
    captainsLogButtons_.push_back({SDL_Rect{20, 20, 150, 50}, "BACK", [this]() { setPage(Page::Main); }, false});
    int y = 100;
    for (size_t i = 0; i < videoFiles_.size(); ++i) {
        std::string label = std::to_string(i + 1) + ". " + videoFiles_[i];
        captainsLogButtons_.push_back({SDL_Rect{40, y, 640, 50}, label, [this, i]() { selectedVideoIndex_ = static_cast<int>(i); setPage(Page::Player); playingVideo_ = true; }, false});
        y += 60;
    }
    if (videoFiles_.empty()) {
        captainsLogButtons_.push_back({SDL_Rect{40, y, 640, 50}, "No videos found.", []() {}, false});
    }
}

void App::setPage(Page page) {
    currentPage_ = page;
    if (page == Page::Planet && !weather_.valid) {
        fetchWeather();
    }
    if (page == Page::DutyRoster && !rosterValid_) {
        fetchRoster();
    }
    layoutButtons();
    selectedIndex_ = 0;
    if (currentButtons_ && !currentButtons_->empty()) {
        for (auto& button : *currentButtons_) button.selected = false;
        (*currentButtons_)[selectedIndex_].selected = true;
    }
}

void App::layoutButtons() {
    switch (currentPage_) {
    case Page::Main:
        currentButtons_ = &mainButtons_;
        break;
    case Page::Planet:
        currentButtons_ = &planetButtons_;
        break;
    case Page::CaptainsLog:
        currentButtons_ = &captainsLogButtons_;
        break;
    case Page::Media:
        currentButtons_ = &mediaButtons_;
        break;
    case Page::Sensor:
        currentButtons_ = &sensorButtons_;
        break;
    case Page::Player:
        currentButtons_ = &playerButtons_;
        break;
    case Page::Status:
        currentButtons_ = &statusButtons_;
        break;
    case Page::DutyRoster:
        currentButtons_ = &rosterButtons_;
        break;
    }
}

void App::render() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    SDL_Color titleColor = {134, 223, 100, 255};
    SDL_Color textColor = {218, 215, 120, 255};

    renderText("USS ENTERPRISE NCC-1701 STANDARD ISSUE", 24, 12, titleColor, fontSmall_);
    renderText("TRICORDER", 220, 70, textColor, fontLarge_);

    switch (currentPage_) {
    case Page::Main:
        renderButtons(mainButtons_);
        break;
    case Page::Planet: {
        renderButtons(planetButtons_);
        renderText("Planet Conditions", 40, 110, textColor, fontMedium_);
        if (weather_.valid) {
            renderText("Forecast:", 40, 160, textColor, fontSmall_);
            renderText(weather_.shortForecast, 180, 160, textColor, fontSmall_);
            renderText("Detailed:", 40, 190, textColor, fontSmall_);
            renderText(weather_.detailedForecast, 180, 190, textColor, fontSmall_);
            renderText("Temperature:", 40, 240, textColor, fontSmall_);
            renderText(std::to_string(weather_.temperature) + "°F", 180, 240, textColor, fontSmall_);
            renderText("Humidity:", 40, 270, textColor, fontSmall_);
            renderText(std::to_string(weather_.humidity) + "%", 180, 270, textColor, fontSmall_);
            renderText("Precipitation:", 40, 300, textColor, fontSmall_);
            renderText(std::to_string(weather_.precip) + "%", 180, 300, textColor, fontSmall_);
            renderText("Wind:", 40, 330, textColor, fontSmall_);
            renderText(weather_.windDirection + " " + weather_.windSpeed, 180, 330, textColor, fontSmall_);
        } else {
            renderText("Weather not yet loaded.", 40, 160, textColor, fontSmall_);
            renderText("Press REFRESH to query API.", 40, 190, textColor, fontSmall_);
            renderText("Status: " + weather_.status, 40, 220, textColor, fontSmall_);
        }
        break;
    }
    case Page::CaptainsLog:
        renderButtons(captainsLogButtons_);
        renderText("Captain's Log", 40, 90, textColor, fontMedium_);
        for (size_t i = 0; i < logEntries_.size(); ++i) {
            renderText(std::to_string(i + 1) + ". " + logEntries_[i], 40, 150 + static_cast<int>(i) * 40, textColor, fontSmall_);
        }
        break;
    case Page::Media:
        renderButtons(mediaButtons_);
        renderText("Media Library", 40, 90, textColor, fontMedium_);
        if (videoFiles_.empty()) {
            renderText("No video files found in the local videos folder.", 40, 150, textColor, fontSmall_);
            renderText("Drop .mp4 files into ./videos and refresh.", 40, 180, textColor, fontSmall_);
        } else {
            for (size_t i = 0; i < videoFiles_.size() && i < 8; ++i) {
                renderText(std::to_string(i + 1) + ". " + videoFiles_[i], 40, 150 + static_cast<int>(i) * 40, textColor, fontSmall_);
            }
            renderText("Use number keys to select a file in future builds.", 40, 470, textColor, fontSmall_);
        }
        break;
    case Page::Sensor:
        renderButtons(sensorButtons_);
        renderText("Sensor Dashboard", 40, 90, textColor, fontMedium_);
        renderText("Uptime:", 40, 150, textColor, fontSmall_);
        renderText(getUptime(), 220, 150, textColor, fontSmall_);
        renderText("Load Avg:", 40, 190, textColor, fontSmall_);
        renderText(getLoadAverage(), 220, 190, textColor, fontSmall_);
        renderText("CPU Cores:", 40, 230, textColor, fontSmall_);
        renderText(std::to_string(std::thread::hardware_concurrency()), 220, 230, textColor, fontSmall_);
        renderText("Disk Free:", 40, 270, textColor, fontSmall_);
        renderText(getDiskUsage(), 220, 270, textColor, fontSmall_);
        break;
    case Page::Player: {
        renderButtons(playerButtons_);
        renderText("Video Player", 40, 90, textColor, fontMedium_);
        std::string info = selectedVideoIndex_ >= 0 && selectedVideoIndex_ < static_cast<int>(videoFiles_.size()) ? videoFiles_[selectedVideoIndex_] : "No video selected.";
        renderText("Selected: " + info, 40, 150, textColor, fontSmall_);
        renderText(playingVideo_ ? "Status: Playing" : "Status: Paused", 40, 190, textColor, fontSmall_);
        renderText("Playback support coming soon.", 40, 230, textColor, fontSmall_);
        break;
    }
    case Page::Status:
        renderButtons(statusButtons_);
        renderText("System Status", 40, 90, textColor, fontMedium_);
        renderText(getStatusText(), 40, 150, textColor, fontSmall_);
        break;
    case Page::DutyRoster: {
        renderButtons(rosterButtons_);
        renderText("Duty Roster", 40, 90, textColor, fontMedium_);
        renderText("TrekFest Event Schedule", 40, 130, textColor, fontSmall_);
        if (!rosterValid_) {
            renderText(rosterStatus_, 40, 170, textColor, fontSmall_);
        } else if (rosterEvents_.empty()) {
            renderText("No upcoming TrekFest events found.", 40, 170, textColor, fontSmall_);
            renderText(rosterStatus_, 40, 200, textColor, fontSmall_);
        } else {
            // compute available vertical space and determine the start/end indices for current page
            int startY = 210;
            int bottomMargin = 40;
            int availableHeight = WINDOW_HEIGHT - startY - bottomMargin;
            int maxWidth = WINDOW_WIDTH - 80;
            // simulate paging to find start index for rosterPage_
            size_t start = 0;
            for (size_t p = 0; p < rosterPage_; ++p) {
                int used = 0;
                size_t idx = start;
                while (idx < rosterEvents_.size()) {
                    const RosterEvent& e = rosterEvents_[idx];
                    int h = measureWrappedHeight(e.date + " " + e.start + " - " + e.title, maxWidth, fontSmall_, 6);
                    if (!e.location.empty()) h += measureWrappedHeight(std::string("Location: ") + e.location, maxWidth - 20, fontSmall_, 4);
                    if (!e.details.empty()) h += measureWrappedHeight(e.details, maxWidth - 20, fontSmall_, 4);
                    h += 6; // separator
                    if (used + h > availableHeight) break;
                    used += h;
                    ++idx;
                }
                if (idx == start) idx = start + 1; // ensure forward progress
                start = idx;
                if (start >= rosterEvents_.size()) break;
            }
            // now compute end index from start
            size_t end = start;
            int used = 0;
            while (end < rosterEvents_.size()) {
                const RosterEvent& e = rosterEvents_[end];
                int h = measureWrappedHeight(e.date + " " + e.start + " - " + e.title, maxWidth, fontSmall_, 6);
                if (!e.location.empty()) h += measureWrappedHeight(std::string("Location: ") + e.location, maxWidth - 20, fontSmall_, 4);
                if (!e.details.empty()) h += measureWrappedHeight(e.details, maxWidth - 20, fontSmall_, 4);
                h += 6;
                if (used + h > availableHeight) break;
                used += h;
                ++end;
            }
            if (start >= rosterEvents_.size()) {
                // clamp to last page if we've gone past end
                rosterPage_ = 0;
                start = 0;
                end = 0;
                used = 0;
                while (end < rosterEvents_.size()) {
                    const RosterEvent& e = rosterEvents_[end];
                    int h = measureWrappedHeight(e.date + " " + e.start + " - " + e.title, maxWidth, fontSmall_, 6);
                    if (!e.location.empty()) h += measureWrappedHeight(std::string("Location: ") + e.location, maxWidth - 20, fontSmall_, 4);
                    if (!e.details.empty()) h += measureWrappedHeight(e.details, maxWidth - 20, fontSmall_, 4);
                    h += 6;
                    if (used + h > availableHeight) break;
                    used += h;
                    ++end;
                }
            }
            renderText("Showing events " + std::to_string(start + 1) + " to " + std::to_string(end) + " of " + std::to_string(rosterEvents_.size()), 40, 170, textColor, fontSmall_);
            int y = 210;
            for (size_t i = start; i < end; ++i) {
                const RosterEvent& event = rosterEvents_[i];
                int maxWidth = WINDOW_WIDTH - 80; // left/right margins
                std::string header = event.date + " " + event.start + " - " + event.title;
                int used = renderWrappedText(header, 40, y, maxWidth, textColor, fontSmall_, 6);
                y += used;
                if (!event.location.empty()) {
                    used = renderWrappedText(std::string("Location: ") + event.location, 60, y, maxWidth - 20, textColor, fontSmall_, 4);
                    y += used;
                }
                if (!event.details.empty()) {
                    used = renderWrappedText(event.details, 60, y, maxWidth - 20, textColor, fontSmall_, 4);
                    y += used;
                }
                y += 6;
            }
        }
        break;
    }
    }

    SDL_RenderPresent(renderer_);
}

void App::renderButtons(const std::vector<Button>& buttons) {
    for (const Button& button : buttons) {
        SDL_Color bg = button.selected ? SDL_Color{218, 215, 120, 255} : SDL_Color{134, 223, 100, 255};
        SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, bg.a);
        SDL_RenderFillRect(renderer_, &button.rect);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderDrawRect(renderer_, &button.rect);
        int textW = 0, textH = 0;
        SDL_Texture* texture = createTextTexture(button.label, {0, 0, 0, 255}, fontSmall_, textW, textH);
        if (texture) {
            SDL_Rect dst{button.rect.x + (button.rect.w - textW) / 2, button.rect.y + (button.rect.h - textH) / 2, textW, textH};
            SDL_RenderCopy(renderer_, texture, nullptr, &dst);
            SDL_DestroyTexture(texture);
        }
    }
}

void App::renderText(const std::string& text, int x, int y, SDL_Color color, TTF_Font* font) {
    std::istringstream stream(text);
    std::string line;
    int lineHeight = TTF_FontLineSkip(font);
    int currentY = y;
    while (std::getline(stream, line)) {
        int w = 0, h = 0;
        SDL_Texture* texture = createTextTexture(line, color, font, w, h);
        if (!texture) break;
        SDL_Rect dst{x, currentY, w, h};
        SDL_RenderCopy(renderer_, texture, nullptr, &dst);
        SDL_DestroyTexture(texture);
        currentY += lineHeight;
    }
}

SDL_Texture* App::createTextTexture(const std::string& text, SDL_Color color, TTF_Font* font, int& w, int& h) {
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) return nullptr;
    w = surface->w;
    h = surface->h;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_FreeSurface(surface);
    return texture;
}

void App::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
            if (currentPage_ == Page::Main) {
                running_ = false;
            } else {
                setPage(Page::Main);
            }
            break;
        case SDLK_UP:
        case SDLK_LEFT:
            navigate(-1);
            break;
        case SDLK_DOWN:
        case SDLK_RIGHT:
            navigate(1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            invokeSelected();
            break;
        case SDLK_BACKSPACE:
            break;
            break;
        default:
            break;
        }
    }
    (void)event;
}

void App::navigate(int direction) {
    if (!currentButtons_ || currentButtons_->empty()) return;
    size_t count = currentButtons_->size();
    currentButtons_->at(selectedIndex_).selected = false;
    selectedIndex_ = (selectedIndex_ + count + (direction > 0 ? 1 : -1)) % count;
    currentButtons_->at(selectedIndex_).selected = true;
}

void App::invokeSelected() {
    if (!currentButtons_ || currentButtons_->empty()) return;
    Button& button = currentButtons_->at(selectedIndex_);
    if (button.action) button.action();
}

void App::submitInput() {
    // No-op: Input page is replaced by Duty Roster.
}

std::string App::getStatusText() const {
    std::ostringstream out;
    time_t now = time(nullptr);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&now));
    out << "Current Time: " << buffer << "\n";
    out << "Video Count: " << videoFiles_.size() << "\n";
    out << "Weather: " << weather_.status << "\n";
    out << "GPIO Support: " << (gpioAvailable_ ? "Enabled" : "Disabled") << "\n";
    out << "App Version: TricorderV2 SDL2";
    return out.str();
}

std::string App::getUptime() const {
    std::string uptime = runCommand("uptime");
    if (uptime.empty()) return "Unavailable";
    size_t pos = uptime.find("up ");
    if (pos != std::string::npos) {
        size_t end = uptime.find(",", pos);
        if (end != std::string::npos) {
            return uptime.substr(pos + 3, end - pos - 3);
        }
    }
    return uptime;
}

std::string App::getLoadAverage() const {
    std::string load = runCommand("uptime");
    if (load.empty()) return "Unavailable";
    size_t pos = load.find("load average:");
    if (pos != std::string::npos) {
        return load.substr(pos + 13);
    }
    return load;
}

std::string App::getDiskUsage() const {
    std::string usage = runCommand("df -h . | tail -1 | awk '{printf \"%s available (%s)\", $4, $5}'");
    if (usage.empty()) return "Unavailable";
    return usage;
}

bool App::fileExists(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

std::string App::runCommand(const std::string& command) const {
    std::string result;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return result;
    }
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

std::string App::findJsonStringValue(const std::string& json, const std::string& key) const {
    size_t pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = json.find_first_not_of(" \t\n\r", pos + 1);
    if (pos == std::string::npos || json[pos] != '"') return "";
    size_t start = pos + 1;
    size_t end = json.find('"', start);
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
}

int App::findJsonIntValue(const std::string& json, const std::string& key) const {
    size_t pos = json.find(key);
    if (pos == std::string::npos) return -1;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return -1;
    pos = json.find_first_not_of(" \t\n\r", pos + 1);
    if (pos == std::string::npos) return -1;
    size_t end = pos;
    if (json[end] == '+' || json[end] == '-') end++;
    while (end < json.size() && isdigit(static_cast<unsigned char>(json[end]))) {
        end++;
    }
    if (end == pos) return -1;
    try {
        return std::stoi(json.substr(pos, end - pos));
    } catch (...) {
        return -1;
    }
}

bool App::fetchWeather() {
    std::string data = runCommand("curl -s -L 'https://api.weather.gov/gridpoints/DVN/33,63/forecast/hourly'");
    if (data.empty()) {
        weather_.valid = false;
        weather_.status = "Network failed";
        return false;
    }
    int temp = findJsonIntValue(data, "\"temperature\"");
    int humidity = findJsonIntValue(data, "\"relativeHumidity\"");
    int precip = findJsonIntValue(data, "\"probabilityOfPrecipitation\"");
    std::string forecast = findJsonStringValue(data, "\"shortForecast\"");
    std::string detailed = findJsonStringValue(data, "\"detailedForecast\"");
    std::string windSpeed = findJsonStringValue(data, "\"windSpeed\"");
    std::string windDirection = findJsonStringValue(data, "\"windDirection\"");
    if (forecast.empty()) {
        weather_.valid = false;
        weather_.status = "Weather parse failed";
        return false;
    }
    weather_.temperature = temp;
    weather_.humidity = humidity;
    weather_.precip = precip;
    weather_.shortForecast = forecast;
    weather_.detailedForecast = detailed.empty() ? forecast : detailed;
    weather_.windSpeed = windSpeed;
    weather_.windDirection = windDirection;
    weather_.valid = true;
    weather_.status = "OK";
    return true;
}

bool App::fetchRoster() {
    std::string html = runCommand("curl -sL 'https://trekfest.org/event-schedule'");
    if (html.empty()) {
        rosterValid_ = false;
        rosterStatus_ = "Unable to load TrekFest schedule.";
        return false;
    }
    std::vector<std::string> lines = extractTextBlocks(html);
    rosterEvents_ = parseRosterEvents(lines);
    if (rosterEvents_.empty()) {
        rosterValid_ = true;
        rosterStatus_ = "No upcoming TrekFest events found.";
        rosterPage_ = 0;
        return true;
    }
    rosterValid_ = true;
    rosterPage_ = 0;
    rosterStatus_ = "Loaded " + std::to_string(rosterEvents_.size()) + " upcoming events.";
    return true;
}

std::vector<std::string> App::extractTextBlocks(const std::string& html) const {
    std::string text;
    bool inTag = false;
    for (size_t i = 0; i < html.size(); ++i) {
        char c = html[i];
        if (c == '<') {
            inTag = true;
            text.push_back('\n');
            continue;
        }
        if (c == '>') {
            inTag = false;
            continue;
        }
        if (inTag) continue;
        if (c == '&') {
            if (html.compare(i, 6, "&nbsp;") == 0) {
                text.push_back(' ');
                i += 5;
                continue;
            }
            if (html.compare(i, 5, "&amp;") == 0) {
                text.push_back('&');
                i += 4;
                continue;
            }
            if (html.compare(i, 4, "&gt;") == 0) {
                text.push_back('>');
                i += 3;
                continue;
            }
            if (html.compare(i, 4, "&lt;") == 0) {
                text.push_back('<');
                i += 3;
                continue;
            }
        }
        text.push_back(c);
    }
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        std::string normalized = normalizeWhitespace(line);
        if (!normalized.empty()) {
            lines.push_back(normalized);
        }
    }
    return lines;
}

std::vector<RosterEvent> App::parseRosterEvents(const std::vector<std::string>& lines) const {
    std::vector<RosterEvent> events;
    std::string currentDate;
    std::tm currentTm = {};
    bool haveDate = false;
    std::regex dateHeader(R"((Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday):\s*([A-Za-z]+)\s+(\d{1,2}),\s*(\d{4}))", std::regex::icase);
    std::regex timeMarker(R"((\d{1,2}):(\d{2})\s*(AM|PM))", std::regex::icase);
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        std::smatch dateMatch;
        if (std::regex_search(line, dateMatch, dateHeader)) {
            currentDate = trim(line);
            if (parseEventDate(line, currentTm)) {
                haveDate = true;
            }
            continue;
        }
        if (line.find("■■■") != std::string::npos || line.find("■") != std::string::npos) {
            int hour = 0, minute = 0;
            if (!parseEventTime(line, hour, minute)) {
                continue;
            }
            RosterEvent event;
            event.date = haveDate ? currentDate : "TrekFest";
            event.start = normalizeWhitespace(line);
            if (std::smatch m; std::regex_search(line, m, timeMarker)) {
                event.start = m.str(0);
            }
            std::string title;
            std::string location;
            std::string details;
            size_t j = i + 1;
            while (j < lines.size()) {
                const std::string& next = lines[j];
                if (next.find("■■■") != std::string::npos || next.find("■") != std::string::npos) break;
                if (std::regex_search(next, dateHeader)) break;
                if (!title.empty() && next.empty()) break;
                if (title.empty()) {
                    title = next;
                } else if (location.empty()) {
                    location = next;
                } else {
                    if (!details.empty()) {
                        details += " ";
                    }
                    details += next;
                }
                ++j;
            }
            if (title.find("→") != std::string::npos) {
                auto pos = title.find("→");
                location = trim(title.substr(pos + 2));
                title = trim(title.substr(0, pos));
            }
            // Heuristics: move parenthetical location or separators from title into location
            size_t popen = title.find('(');
            size_t pclose = title.find(')');
            if (popen != std::string::npos && pclose != std::string::npos && pclose > popen) {
                std::string inside = trim(title.substr(popen + 1, pclose - popen - 1));
                if (!inside.empty() && inside.size() < 60 && location.empty()) {
                    location = inside;
                    title = trim(title.substr(0, popen));
                }
            }
            // split common separators
            size_t atpos = title.find(" @ ");
            if (atpos == std::string::npos) atpos = title.find(" @");
            if (atpos == std::string::npos) atpos = title.find("@ ");
            if (atpos != std::string::npos) {
                if (location.empty()) location = trim(title.substr(atpos + 1));
                title = trim(title.substr(0, atpos));
            }
            size_t dash = title.find(" - ");
            if (dash != std::string::npos && location.empty()) {
                location = trim(title.substr(dash + 3));
                title = trim(title.substr(0, dash));
            }
            // clean up location prefixes
            if (!location.empty()) {
                if (location.rfind("Location:", 0) == 0) {
                    location = trim(location.substr(9));
                }
                if (location.rfind("At ", 0) == 0) {
                    location = trim(location.substr(3));
                }
            }
            event.title = trim(title.empty() ? "TrekFest Event" : title);
            event.location = trim(location);
            event.details = trim(details);
            if (haveDate) {
                std::tm eventTm = currentTm;
                eventTm.tm_hour = hour;
                eventTm.tm_min = minute;
                eventTm.tm_sec = 0;
                event.timestamp = std::mktime(&eventTm);
            }
            if (event.timestamp == 0) {
                event.timestamp = std::time(nullptr);
            }
            time_t now = std::time(nullptr);
            if (event.timestamp >= now - 60 * 60) {
                events.push_back(event);
            }
            i = j - 1;
        }
    }
    std::sort(events.begin(), events.end(), [](const RosterEvent& a, const RosterEvent& b) {
        return a.timestamp < b.timestamp;
    });
    return events;
}

bool App::parseEventDate(const std::string& text, std::tm& tm) const {
    std::regex dateRegex(R"((Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday):\s*([A-Za-z]+)\s+(\d{1,2}),\s*(\d{4}))", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(text, match, dateRegex)) {
        return false;
    }
    static const std::vector<std::string> months = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    std::string monthText = match[2].str();
    int day = std::stoi(match[3].str());
    int year = std::stoi(match[4].str());
    int month = 0;
    std::string lowerMonth = monthText;
    std::transform(lowerMonth.begin(), lowerMonth.end(), lowerMonth.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    for (size_t i = 0; i < months.size(); ++i) {
        std::string compareMonth = months[i];
        std::transform(compareMonth.begin(), compareMonth.end(), compareMonth.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (lowerMonth == compareMonth) {
            month = static_cast<int>(i);
            break;
        }
    }
    if (month < 0 || month >= 12) {
        return false;
    }
    tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month;
    tm.tm_mday = day;
    return true;
}

bool App::parseEventTime(const std::string& text, int& hour, int& minute) const {
    std::regex timeRegex(R"((\d{1,2}):(\d{2})\s*(AM|PM|am|pm))");
    std::smatch match;
    if (!std::regex_search(text, match, timeRegex)) {
        return false;
    }
    hour = std::stoi(match[1].str());
    minute = std::stoi(match[2].str());
    std::string ampm = match[3].str();
    for (auto& c : ampm) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (ampm == "PM" && hour < 12) hour += 12;
    if (ampm == "AM" && hour == 12) hour = 0;
    return true;
}

std::string App::normalizeWhitespace(const std::string& text) const {
    std::string result;
    bool lastSpace = false;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastSpace) {
                result.push_back(' ');
                lastSpace = true;
            }
        } else {
            result.push_back(c);
            lastSpace = false;
        }
    }
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

std::string App::trim(const std::string& text) const {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        start++;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        end--;
    }
    return text.substr(start, end - start);
}

int App::renderWrappedText(const std::string& text, int x, int y, int maxWidth, SDL_Color color, TTF_Font* font, int linePadding) {
    if (!font) return 0;
    int lineHeight = TTF_FontHeight(font) + linePadding;
    std::istringstream words(text);
    std::string word;
    std::string line;
    int totalHeight = 0;
    while (words >> word) {
        std::string test = line.empty() ? word : line + " " + word;
        int w = 0, h = 0;
        if (TTF_SizeUTF8(font, test.c_str(), &w, &h) != 0) {
            // fallback: render current line
            if (!line.empty()) {
                int tw = 0, th = 0;
                SDL_Texture* tex = createTextTexture(line, color, font, tw, th);
                if (tex) {
                    SDL_Rect dst{ x, y + totalHeight, tw, th };
                    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                    SDL_DestroyTexture(tex);
                }
                totalHeight += lineHeight;
                line.clear();
            }
            line = word;
            continue;
        }
        if (w > maxWidth && !line.empty()) {
            int tw = 0, th = 0;
            SDL_Texture* tex = createTextTexture(line, color, font, tw, th);
            if (tex) {
                SDL_Rect dst{ x, y + totalHeight, tw, th };
                SDL_RenderCopy(renderer_, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
            }
            totalHeight += lineHeight;
            line = word;
        } else {
            line = test;
        }
    }
    if (!line.empty()) {
        int tw = 0, th = 0;
        SDL_Texture* tex = createTextTexture(line, color, font, tw, th);
        if (tex) {
            SDL_Rect dst{ x, y + totalHeight, tw, th };
            SDL_RenderCopy(renderer_, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
        totalHeight += lineHeight;
    }
    return totalHeight;
}

int App::measureWrappedHeight(const std::string& text, int maxWidth, TTF_Font* font, int linePadding) const {
    if (!font) return 0;
    int lineHeight = TTF_FontHeight(font) + linePadding;
    std::istringstream words(text);
    std::string word;
    std::string line;
    int totalHeight = 0;
    while (words >> word) {
        std::string test = line.empty() ? word : line + " " + word;
        int w = 0, h = 0;
        if (TTF_SizeUTF8(font, test.c_str(), &w, &h) != 0) {
            if (!line.empty()) {
                totalHeight += lineHeight;
                line.clear();
            }
            line = word;
            continue;
        }
        if (w > maxWidth && !line.empty()) {
            totalHeight += lineHeight;
            line = word;
        } else {
            line = test;
        }
    }
    if (!line.empty()) totalHeight += lineHeight;
    return totalHeight;
}

int App::run() {
    while (running_) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running_ = false;
            } else {
                handleEvent(event);
            }
        }

        if (gpioAvailable_) {
            pollGPIO();
        }

        render();
        SDL_Delay(16);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    App app;
    if (!app.initialize()) {
        return 1;
    }
    return app.run();
}
