#pragma once
// Drawing layer. Everything is laid out on a fixed 720x576 logical canvas (the Pi's PAL
// composite resolution) and mapped to the real output, honouring overscan margins.
#include <SDL.h>
#include <SDL_ttf.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct Config;

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

namespace pal {
// The original tricorder palette.
constexpr Color GREEN{0x86, 0xDF, 0x64};
constexpr Color YELLOW{0xDA, 0xD7, 0x78};
constexpr Color BLACK{0, 0, 0};
// Accents for the new pages (indicator lights, alerts).
constexpr Color RED{0xE8, 0x4A, 0x2E};
constexpr Color BLUE{0x5C, 0xA8, 0xE8};
constexpr Color DIM_GREEN{0x2C, 0x55, 0x22};
constexpr Color DIM_YELLOW{0x5A, 0x58, 0x30};
constexpr Color WHITE{0xFF, 0xFF, 0xFF};
}  // namespace pal

Color mix(Color a, Color b, float t);
Color withAlpha(Color c, uint8_t a);

enum class Align { Left, Center, Right };

constexpr int kW = 720;
constexpr int kH = 576;

class Gfx {
public:
    bool init(const Config& cfg, std::string& err);
    void shutdown();

    SDL_Window* window() const { return win_; }
    SDL_Renderer* renderer() const { return ren_; }
    bool vsync() const { return vsync_; }
    const std::string& fontPath() const { return fontPath_; }

    void setOverscan(int x, int y) { osX_ = x; osY_ = y; }
    void toggleFullscreen();
    bool screenshot(const std::string& path);  // BMP of the last rendered frame

    void beginFrame();
    void endFrame();

    // Primitives (logical coordinates)
    void fill(SDL_Rect r, Color c);
    void outline(SDL_Rect r, Color c, int thickness = 1);
    void line(float x1, float y1, float x2, float y2, Color c);
    void polyline(const std::vector<SDL_FPoint>& pts, Color c, int thickness = 1);
    void triangle(SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, Color col);
    void bevel(SDL_Rect r, Color bg, bool sunken, int bw = 3);
    void texture(SDL_Texture* t, const SDL_Rect* src, SDL_Rect dst);
    void clip(const SDL_Rect* r);

    // Tk-style button: green, raised; focused = sunken with a yellow ring.
    void button(SDL_Rect r, const std::string& label, int px, bool focused, bool enabled = true);

    // Text. `px` is the font size in logical pixels; y is the top of the line box.
    void text(const std::string& s, int x, int y, int px, Color c, Align a = Align::Left);
    void textIn(SDL_Rect r, const std::string& s, int px, Color c, Align a = Align::Center);
    int textWidth(const std::string& s, int px);
    int lineHeight(int px);
    // Largest size <= maxPx at which `s` fits in maxW.
    int fitSize(const std::string& s, int maxW, int maxPx, int minPx = 10);
    std::string ellipsize(const std::string& s, int px, int maxW);
    // Word-wraps to maxW; blank lines in the input are kept as paragraph breaks.
    std::vector<std::string> wrap(const std::string& s, int px, int maxW);

private:
    struct CachedText {
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0;
        uint64_t lastUsed = 0;
    };

    SDL_Rect map(SDL_Rect r) const;
    SDL_FPoint mapF(float x, float y) const;
    int physPx(int px) const;
    TTF_Font* font(int physSize, bool fallback);
    bool primaryCovers(const std::string& s);
    CachedText* renderText(const std::string& s, int px, Color c, bool fallback);
    // Splits text into runs the Trek font can draw and runs needing the fallback (e.g. the degree sign).
    std::vector<std::pair<std::string, bool>> fontRuns(const std::string& s);
    void evictText();

    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    bool vsync_ = false;
    bool fullscreen_ = false;
    int osX_ = 0, osY_ = 0;
    // Current transform logical -> physical
    float ox_ = 0, oy_ = 0, sx_ = 1, sy_ = 1;

    std::string fontPath_, fallbackPath_;
    std::unordered_map<int, TTF_Font*> fonts_, fallbackFonts_;
    std::unordered_map<std::string, CachedText> textCache_;
    std::unordered_map<std::string, int> widthCache_;
    std::unordered_map<std::string, bool> coverCache_;
    uint64_t frame_ = 0;
};

std::string findTrekFont(const std::string& configured);
std::string findFallbackFont();
