#include "widgets.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "third_party/qrcodegen.hpp"

int ButtonGroup::add(SDL_Rect r, const std::string& label, int px, std::function<void()> action) {
    UIButton b;
    b.r = r;
    b.label = label;
    b.px = px;
    b.action = std::move(action);
    buttons_.push_back(std::move(b));
    return int(buttons_.size()) - 1;
}

int ButtonGroup::neighbour(int from, Button dir) const {
    int n = int(buttons_.size());
    if (n == 0) return -1;
    const SDL_Rect& c = buttons_[from].r;
    float cx = c.x + c.w / 2.f, cy = c.y + c.h / 2.f;
    int best = -1;
    float bestScore = std::numeric_limits<float>::max();
    for (int i = 0; i < n; ++i) {
        if (i == from || !buttons_[i].enabled) continue;
        const SDL_Rect& o = buttons_[i].r;
        float dx = o.x + o.w / 2.f - cx, dy = o.y + o.h / 2.f - cy;
        float primary, secondary;
        switch (dir) {
            case Button::Right: primary = dx; secondary = std::fabs(dy); break;
            case Button::Left: primary = -dx; secondary = std::fabs(dy); break;
            case Button::Down: primary = dy; secondary = std::fabs(dx); break;
            case Button::Up: primary = -dy; secondary = std::fabs(dx); break;
            default: return from;
        }
        bool horizontal = dir == Button::Left || dir == Button::Right;
        // Must actually lie in that direction (rows: same-ish line for left/right).
        if (primary <= 4) continue;
        if (horizontal && secondary > std::max(c.h, o.h)) continue;
        float score = primary + secondary * 2.5f;
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (best >= 0) return best;
    // Nothing that way: wrap through the list in reading order, like the original.
    bool fwd = dir == Button::Right || dir == Button::Down;
    for (int k = 1; k < n; ++k) {
        int i = ((from + (fwd ? k : -k)) % n + n) % n;
        if (buttons_[i].enabled) return i;
    }
    return from;
}

bool ButtonGroup::handle(Button b) {
    if (buttons_.empty()) return false;
    if (b == Button::Enter) {
        UIButton& u = buttons_[focus_];
        if (u.enabled && u.action) u.action();
        return true;
    }
    if (b == Button::Back) return false;
    focus_ = neighbour(focus_, b);
    return true;
}

void ButtonGroup::render(Gfx& g, bool showFocus) const {
    for (int i = 0; i < int(buttons_.size()); ++i) {
        const UIButton& u = buttons_[i];
        g.button(u.r, u.label, u.px, showFocus && i == focus_, u.enabled);
    }
}

std::vector<SDL_Rect> layoutRow(Gfx& g, const std::vector<std::string>& labels, int y, int h, int px, int x0,
                                int x1, int gap, int* outPx, int padX) {
    int avail = x1 - x0;
    int size = px;
    for (;;) {
        int total = gap * int(labels.size() - 1);
        for (auto& l : labels) total += g.textWidth(l, size) + 2 * padX;
        if (total <= avail || size <= 12) {
            std::vector<SDL_Rect> out;
            int x = x0 + std::max(0, (avail - total) / 2);
            for (auto& l : labels) {
                int w = g.textWidth(l, size) + 2 * padX;
                out.push_back({x, y, w, h});
                x += w + gap;
            }
            if (outPx) *outPx = size;
            return out;
        }
        size -= 2;
    }
}

SDL_Rect backButtonRect() { return {14, 14, 118, 58}; }

void drawTitle(Gfx& g, const std::string& title, int x0) {
    int maxW = kW - x0 - 14;
    int px = g.fitSize(title, maxW, 64);
    g.textIn({x0, 10, maxW, 66}, title, px, pal::YELLOW, Align::Left);
}

void drawLights(Gfx& g, int x, int y, int count, double t, int size, bool busy) {
    static const Color cols[] = {pal::RED, pal::GREEN, pal::YELLOW, pal::BLUE};
    double speed = busy ? 9.0 : 2.2;
    for (int i = 0; i < count; ++i) {
        // Each lamp has its own rhythm so the pattern never looks mechanical.
        double phase = std::sin(t * speed * (0.6 + 0.37 * ((i * 7) % 5)) + i * 1.7);
        bool on = phase > (busy ? -0.2 : 0.25);
        Color c = cols[i % 4];
        SDL_Rect r{x + i * (size + 6), y, size, size};
        g.fill(r, on ? c : mix(c, pal::BLACK, 0.78f));
        if (on) g.outline({r.x - 1, r.y - 1, r.w + 2, r.h + 2}, withAlpha(c, 90));
    }
}

void drawReadout(Gfx& g, SDL_Rect r, const std::string& label, const std::string& value, int valuePx, double bar,
                 Color valueColor) {
    g.fill(r, pal::YELLOW);
    int labelPx = std::min(24, r.h / 3);
    g.text(label, r.x + 10, r.y + 6, labelPx, pal::BLACK);
    int vy = r.y + 6 + g.lineHeight(labelPx) - 2;
    int barH = bar >= 0 ? 8 : 0;
    int vh = r.y + r.h - vy - barH - 6;
    int px = std::min(valuePx, g.fitSize(value, r.w - 20, valuePx));
    g.textIn({r.x + 10, vy, r.w - 20, vh}, value, px, valueColor, Align::Left);
    if (bar >= 0) {
        SDL_Rect track{r.x + 10, r.y + r.h - 14, r.w - 20, 8};
        g.fill(track, mix(pal::YELLOW, pal::BLACK, 0.3f));
        g.fill({track.x, track.y, int(track.w * std::clamp(bar, 0.0, 1.0)), track.h},
               bar > 0.85 ? pal::RED : pal::DIM_GREEN);
    }
}

void drawScope(Gfx& g, SDL_Rect r, const std::vector<float>& samples, Color trace, const std::string& caption) {
    g.fill(r, pal::BLACK);
    for (int i = 1; i < 6; ++i) {
        int x = r.x + r.w * i / 6;
        g.fill({x, r.y + 1, 1, r.h - 2}, pal::DIM_GREEN);
    }
    for (int i = 1; i < 4; ++i) {
        int y = r.y + r.h * i / 4;
        g.fill({r.x + 1, y, r.w - 2, 1}, pal::DIM_GREEN);
    }
    g.outline(r, pal::YELLOW, 2);
    if (samples.size() >= 2) {
        std::vector<SDL_FPoint> pts;
        pts.reserve(samples.size());
        float mid = r.y + r.h / 2.f, amp = r.h / 2.f - 6;
        for (size_t i = 0; i < samples.size(); ++i)
            pts.push_back({r.x + 3 + (r.w - 6) * float(i) / float(samples.size() - 1),
                           mid - std::clamp(samples[i], -1.f, 1.f) * amp});
        g.polyline(pts, trace, 2);
    }
    if (!caption.empty()) g.text(caption, r.x + 8, r.y + 6, 18, pal::YELLOW);
}

void drawQr(Gfx& g, const std::string& text, int x, int y, int size) {
    static std::map<std::string, qrcodegen::QrCode> cache;
    auto it = cache.find(text);
    if (it == cache.end()) {
        if (cache.size() > 8) cache.clear();
        it = cache.emplace(text, qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM)).first;
    }
    const qrcodegen::QrCode& qr = it->second;
    const int quiet = 3, n = qr.getSize() + 2 * quiet;
    int m = std::max(1, size / n);  // whole pixels per module keep it crisp and scannable
    int total = m * n, ox = x + (size - total) / 2, oy = y + (size - total) / 2;
    g.fill({ox, oy, total, total}, pal::YELLOW);
    for (int r = 0; r < qr.getSize(); ++r)
        for (int c = 0; c < qr.getSize(); ++c)
            if (qr.getModule(c, r)) g.fill({ox + (c + quiet) * m, oy + (r + quiet) * m, m, m}, pal::BLACK);
}
