#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "config.h"

Color mix(Color a, Color b, float t) {
    auto l = [t](uint8_t x, uint8_t y) { return uint8_t(std::lround(x + (y - x) * t)); };
    return {l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
}

Color withAlpha(Color c, uint8_t a) {
    c.a = a;
    return c;
}

namespace {

std::string popenLine(const std::string& cmd) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    char buf[1024];
    std::string out;
    if (fgets(buf, sizeof buf, p)) out = buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// Decode one UTF-8 code point, advancing i.
uint32_t nextCodepoint(const std::string& s, size_t& i) {
    unsigned char c = s[i++];
    if (c < 0x80) return c;
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    while (extra-- > 0 && i < s.size()) cp = (cp << 6) | (s[i++] & 0x3F);
    return cp;
}

}  // namespace

std::string findTrekFont(const std::string& configured) {
    if (!configured.empty() && fileExists(configured)) return configured;
    const std::string home = homeDir();
    const std::string candidates[] = {
        exeDir() + "/assets/fonts/TOS_Title.ttf",
        exeDir() + "/../assets/fonts/TOS_Title.ttf",
        "assets/fonts/TOS_Title.ttf",
        home + "/.local/share/fonts/TOS_Title.ttf",
        home + "/.fonts/TOS_Title.ttf",
        "/usr/local/share/fonts/TOS_Title.ttf",
        "/usr/share/fonts/truetype/TOS_Title.ttf",
    };
    for (auto& c : candidates)
        if (fileExists(c)) return c;
    // The Python version asked Tk for the font family "Trek".
    std::string r = popenLine("fc-match -f '%{family}|%{file}' Trek 2>/dev/null");
    size_t bar = r.find('|');
    if (bar != std::string::npos && r.substr(0, bar).find("Trek") != std::string::npos) {
        std::string f = r.substr(bar + 1);
        if (fileExists(f)) return f;
    }
    return "";
}

std::string findFallbackFont() {
    const std::string candidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
    };
    for (auto& c : candidates)
        if (fileExists(c)) return c;
    std::string f = popenLine("fc-match -f '%{file}' 'sans:bold' 2>/dev/null");
    return fileExists(f) ? f : "";
}

bool Gfx::init(const Config& cfg, std::string& err) {
    fontPath_ = findTrekFont(cfg.fontPath);
    fallbackPath_ = findFallbackFont();
    if (fontPath_.empty()) {
        fprintf(stderr, "Trek font (TOS_Title.ttf) not found; using %s. Put it in assets/fonts/.\n",
                fallbackPath_.c_str());
        fontPath_ = fallbackPath_;
    }
    if (fontPath_.empty()) {
        err = "no usable TTF font found (install fonts-dejavu-core or set font= in the config)";
        return false;
    }
    fprintf(stderr, "font: %s (fallback %s)\n", fontPath_.c_str(), fallbackPath_.c_str());

    if (TTF_Init() != 0) {
        err = std::string("TTF_Init: ") + TTF_GetError();
        return false;
    }

    fullscreen_ = cfg.fullscreen;
    int w = int(kW * cfg.windowScale), h = int(kH * cfg.windowScale);
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (fullscreen_) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    win_ = SDL_CreateWindow("Tricorder", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!win_) {
        err = std::string("SDL_CreateWindow: ") + SDL_GetError();
        return false;
    }
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED);
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
    if (!ren_) {
        err = std::string("SDL_CreateRenderer: ") + SDL_GetError();
        return false;
    }
    SDL_RendererInfo info;
    SDL_GetRendererInfo(ren_, &info);
    vsync_ = info.flags & SDL_RENDERER_PRESENTVSYNC;
    fprintf(stderr, "renderer: %s%s\n", info.name, vsync_ ? " (vsync)" : "");
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    if (fullscreen_) SDL_ShowCursor(SDL_DISABLE);
    setOverscan(cfg.overscanX, cfg.overscanY);
    return true;
}

void Gfx::shutdown() {
    for (auto& kv : textCache_) SDL_DestroyTexture(kv.second.tex);
    textCache_.clear();
    for (auto& kv : fonts_) TTF_CloseFont(kv.second);
    for (auto& kv : fallbackFonts_) TTF_CloseFont(kv.second);
    fonts_.clear();
    fallbackFonts_.clear();
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
    ren_ = nullptr;
    win_ = nullptr;
    TTF_Quit();
}

void Gfx::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(win_, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_ShowCursor(fullscreen_ ? SDL_DISABLE : SDL_ENABLE);
}

bool Gfx::screenshot(const std::string& path) {
    int w, h;
    SDL_GetRendererOutputSize(ren_, &w, &h);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    bool ok = SDL_RenderReadPixels(ren_, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0 &&
              SDL_SaveBMP(s, path.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}

void Gfx::beginFrame() {
    ++frame_;
    int ow, oh;
    SDL_GetRendererOutputSize(ren_, &ow, &oh);
    // Fit the 720x576 canvas into the output, keeping aspect, then inset by overscan.
    float s = std::min(ow / float(kW), oh / float(kH));
    float cw = kW * s, ch = kH * s;
    float x0 = (ow - cw) / 2, y0 = (oh - ch) / 2;
    float insetX = osX_ * s, insetY = osY_ * s;
    ox_ = x0 + insetX;
    oy_ = y0 + insetY;
    sx_ = (cw - 2 * insetX) / kW;
    sy_ = (ch - 2 * insetY) / kH;

    SDL_RenderSetClipRect(ren_, nullptr);
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
    SDL_RenderClear(ren_);
}

void Gfx::endFrame() {
    SDL_RenderPresent(ren_);
    if (frame_ % 120 == 0) evictText();
}

SDL_Rect Gfx::map(SDL_Rect r) const {
    int x1 = int(std::lround(ox_ + r.x * sx_)), y1 = int(std::lround(oy_ + r.y * sy_));
    int x2 = int(std::lround(ox_ + (r.x + r.w) * sx_)), y2 = int(std::lround(oy_ + (r.y + r.h) * sy_));
    return {x1, y1, x2 - x1, y2 - y1};
}

SDL_FPoint Gfx::mapF(float x, float y) const { return {ox_ + x * sx_, oy_ + y * sy_}; }

int Gfx::physPx(int px) const { return std::max(6, int(std::lround(px * sy_))); }

void Gfx::fill(SDL_Rect r, Color c) {
    SDL_Rect m = map(r);
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren_, &m);
}

void Gfx::outline(SDL_Rect r, Color c, int t) {
    fill({r.x, r.y, r.w, t}, c);
    fill({r.x, r.y + r.h - t, r.w, t}, c);
    fill({r.x, r.y + t, t, r.h - 2 * t}, c);
    fill({r.x + r.w - t, r.y + t, t, r.h - 2 * t}, c);
}

void Gfx::line(float x1, float y1, float x2, float y2, Color c) {
    SDL_FPoint a = mapF(x1, y1), b = mapF(x2, y2);
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLineF(ren_, a.x, a.y, b.x, b.y);
}

void Gfx::polyline(const std::vector<SDL_FPoint>& pts, Color c, int thickness) {
    if (pts.size() < 2) return;
    std::vector<SDL_FPoint> m(pts.size());
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, c.a);
    for (int t = 0; t < thickness; ++t) {
        for (size_t i = 0; i < pts.size(); ++i) m[i] = mapF(pts[i].x, pts[i].y + t);
        SDL_RenderDrawLinesF(ren_, m.data(), int(m.size()));
    }
}

void Gfx::triangle(SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, Color col) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
    SDL_Color sc{col.r, col.g, col.b, col.a};
    SDL_Vertex v[3] = {{mapF(a.x, a.y), sc, {0, 0}}, {mapF(b.x, b.y), sc, {0, 0}}, {mapF(c.x, c.y), sc, {0, 0}}};
    SDL_RenderGeometry(ren_, nullptr, v, 3, nullptr, 0);
#else
    line(a.x, a.y, b.x, b.y, col);
    line(b.x, b.y, c.x, c.y, col);
    line(c.x, c.y, a.x, a.y, col);
#endif
}

void Gfx::bevel(SDL_Rect r, Color bg, bool sunken, int bw) {
    Color light = mix(bg, pal::WHITE, 0.55f), dark = mix(bg, pal::BLACK, 0.55f);
    Color tl = sunken ? dark : light, br = sunken ? light : dark;
    fill(r, bg);
    fill({r.x, r.y, r.w, bw}, tl);
    fill({r.x, r.y, bw, r.h}, tl);
    fill({r.x, r.y + r.h - bw, r.w, bw}, br);
    fill({r.x + r.w - bw, r.y + bw, bw, r.h - bw}, br);
}

void Gfx::texture(SDL_Texture* t, const SDL_Rect* src, SDL_Rect dst) {
    SDL_Rect m = map(dst);
    SDL_RenderCopy(ren_, t, src, &m);
}

void Gfx::clip(const SDL_Rect* r) {
    if (!r) {
        SDL_RenderSetClipRect(ren_, nullptr);
        return;
    }
    SDL_Rect m = map(*r);
    SDL_RenderSetClipRect(ren_, &m);
}

void Gfx::button(SDL_Rect r, const std::string& label, int px, bool focused, bool enabled) {
    Color bg = enabled ? pal::GREEN : mix(pal::GREEN, pal::BLACK, 0.5f);
    if (focused) outline({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, pal::YELLOW, 2);
    bevel(r, bg, focused);
    int size = std::min(px, fitSize(label, r.w - 12, px));
    SDL_Rect tr = r;
    if (focused) { tr.x += 1; tr.y += 2; }
    textIn(tr, label, size, pal::BLACK);
}

TTF_Font* Gfx::font(int size, bool fallback) {
    auto& cache = fallback ? fallbackFonts_ : fonts_;
    auto it = cache.find(size);
    if (it != cache.end()) return it->second;
    const std::string& path = fallback ? fallbackPath_ : fontPath_;
    TTF_Font* f = path.empty() ? nullptr : TTF_OpenFont(path.c_str(), size);
    if (!f && fallback) f = font(size, false);
    if (!f) fprintf(stderr, "TTF_OpenFont(%s, %d): %s\n", path.c_str(), size, TTF_GetError());
    cache[size] = f;
    return f;
}

bool Gfx::primaryCovers(const std::string& s) {
    if (fallbackPath_.empty() || fallbackPath_ == fontPath_) return true;
    auto it = coverCache_.find(s);
    if (it != coverCache_.end()) return it->second;
    TTF_Font* f = font(32, false);
    bool ok = true;
    for (size_t i = 0; f && i < s.size() && ok;) {
        uint32_t cp = nextCodepoint(s, i);
        if (cp == ' ') continue;
#if SDL_TTF_VERSION_ATLEAST(2, 0, 18)
        ok = TTF_GlyphIsProvided32(f, cp);
#else
        ok = cp < 0x10000 && TTF_GlyphIsProvided(f, Uint16(cp));
#endif
    }
    if (coverCache_.size() > 4000) coverCache_.clear();
    coverCache_[s] = ok;
    return ok;
}

int Gfx::lineHeight(int px) {
    TTF_Font* f = font(physPx(px), false);
    return f ? int(std::lround(TTF_FontHeight(f) / sy_)) : px;
}

int Gfx::textWidth(const std::string& s, int px) {
    if (s.empty()) return 0;
    int pp = physPx(px);
    std::string key = std::to_string(pp) + '\x1f' + s;
    auto it = widthCache_.find(key);
    if (it != widthCache_.end()) return int(std::lround(it->second / sx_));
    int w = 0;
    for (auto& run : fontRuns(s)) {
        int rw = 0, rh = 0;
        if (TTF_Font* f = font(pp, run.second)) TTF_SizeUTF8(f, run.first.c_str(), &rw, &rh);
        w += rw;
    }
    if (widthCache_.size() > 8000) widthCache_.clear();
    widthCache_[key] = w;
    return int(std::lround(w / sx_));
}

int Gfx::fitSize(const std::string& s, int maxW, int maxPx, int minPx) {
    if (s.empty()) return maxPx;
    // Width scales ~linearly with size: estimate, then step down until it fits.
    int w = textWidth(s, maxPx);
    if (w <= maxW) return maxPx;
    int px = std::max(minPx, int(maxPx * float(maxW) / w));
    while (px > minPx && textWidth(s, px) > maxW) px -= std::max(1, px / 20);
    return std::max(px, minPx);
}

std::string Gfx::ellipsize(const std::string& s, int px, int maxW) {
    if (textWidth(s, px) <= maxW) return s;
    std::string t = s;
    while (!t.empty()) {
        // drop one UTF-8 code point
        size_t n = t.size() - 1;
        while (n > 0 && (static_cast<unsigned char>(t[n]) & 0xC0) == 0x80) --n;
        t.erase(n);
        if (textWidth(t + "...", px) <= maxW) return t + "...";
    }
    return "...";
}

std::vector<std::pair<std::string, bool>> Gfx::fontRuns(const std::string& s) {
    std::vector<std::pair<std::string, bool>> runs;
    if (primaryCovers(s)) {
        runs.emplace_back(s, false);
        return runs;
    }
    for (size_t i = 0; i < s.size();) {
        size_t start = i;
        nextCodepoint(s, i);
        std::string ch = s.substr(start, i - start);
        bool fb = ch != " " && !primaryCovers(ch);
        if (runs.empty() || runs.back().second != fb) runs.emplace_back(ch, fb);
        else runs.back().first += ch;
    }
    return runs;
}

std::vector<std::string> Gfx::wrap(const std::string& s, int px, int maxW) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = nl == std::string::npos ? s.size() + 1 : nl + 1;
        if (para.empty()) {
            if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
            continue;
        }
        std::string line, word;
        size_t i = 0;
        while (i <= para.size()) {
            if (i == para.size() || para[i] == ' ') {
                if (!word.empty()) {
                    std::string trial = line.empty() ? word : line + " " + word;
                    if (!line.empty() && textWidth(trial, px) > maxW) {
                        lines.push_back(line);
                        line = word;
                    } else {
                        line = trial;
                    }
                    word.clear();
                }
            } else {
                word += para[i];
            }
            ++i;
        }
        if (!line.empty()) lines.push_back(ellipsize(line, px, maxW));
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

Gfx::CachedText* Gfx::renderText(const std::string& s, int px, Color c, bool fallback) {
    int pp = physPx(px);
    char tag[32];
    snprintf(tag, sizeof tag, "%d:%d:%02x%02x%02x%02x:", pp, int(fallback), c.r, c.g, c.b, c.a);
    std::string key = tag + s;
    auto it = textCache_.find(key);
    if (it != textCache_.end()) {
        it->second.lastUsed = frame_;
        return &it->second;
    }
    TTF_Font* f = font(pp, fallback);
    if (!f) return nullptr;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), SDL_Color{c.r, c.g, c.b, c.a});
    if (!surf) return nullptr;
    CachedText ct;
    ct.tex = SDL_CreateTextureFromSurface(ren_, surf);
    ct.w = surf->w;
    ct.h = surf->h;
    ct.lastUsed = frame_;
    SDL_FreeSurface(surf);
    if (!ct.tex) return nullptr;
    return &(textCache_[key] = ct);
}

void Gfx::evictText() {
    for (auto it = textCache_.begin(); it != textCache_.end();) {
        if (frame_ - it->second.lastUsed > 240) {
            SDL_DestroyTexture(it->second.tex);
            it = textCache_.erase(it);
        } else {
            ++it;
        }
    }
}

void Gfx::text(const std::string& s, int x, int y, int px, Color c, Align a) {
    if (s.empty()) return;
    auto runs = fontRuns(s);
    std::vector<CachedText*> parts;
    float w = 0;
    for (auto& run : runs) {
        CachedText* t = renderText(run.first, px, c, run.second);
        if (!t) return;
        parts.push_back(t);
        w += float(t->w);
    }
    SDL_FPoint p = mapF(float(x), float(y));
    if (a == Align::Center) p.x -= w / 2;
    else if (a == Align::Right) p.x -= w;
    int baseH = parts[0]->h;
    for (size_t i = 0; i < parts.size(); ++i) {
        CachedText* t = parts[i];
        // Fallback glyphs come from a different font; align them to the top of the line.
        int dy = runs[i].second ? std::max(0, (baseH - t->h) / 4) : 0;
        SDL_Rect dst{int(std::lround(p.x)), int(std::lround(p.y)) + dy, t->w, t->h};
        SDL_RenderCopy(ren_, t->tex, nullptr, &dst);
        p.x += float(t->w);
    }
}

void Gfx::textIn(SDL_Rect r, const std::string& s, int px, Color c, Align a) {
    int lh = lineHeight(px);
    int y = r.y + (r.h - lh) / 2;
    int x = a == Align::Left ? r.x : a == Align::Center ? r.x + r.w / 2 : r.x + r.w;
    text(s, x, y, px, c, a);
}
