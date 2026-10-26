// LIBRARY COMPUTER -> Stellar Cartography: tonight's sky for your location, computed
// offline. LEFT/RIGHT step time by an hour, UP/DOWN by a day, ENTER returns to now.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "../app.h"
#include "../astro.h"
#include "../widgets.h"

namespace {

using namespace astro;

Color bodyColor(Body b) {
    switch (b) {
        case Sun: return pal::YELLOW;
        case Moon: return pal::WHITE;
        case Mercury: return Color{0xB0, 0xA8, 0x98};
        case Venus: return Color{0xF4, 0xF0, 0xD8};
        case Mars: return pal::RED;
        case Jupiter: return Color{0xE8, 0xC8, 0x90};
        case Saturn: return Color{0xD8, 0xC0, 0x60};
        default: return pal::GREEN;
    }
}

std::string hhmm(time_t t) {
    tm lt;
    localtime_r(&t, &lt);
    char b[16];
    strftime(b, sizeof b, "%H:%M", &lt);
    return b;
}

// Moon disc with the correct lit fraction (lit on the right while waxing).
void drawMoon(Gfx& g, float cx, float cy, float r, const MoonPhase& p) {
    for (int y = -int(r); y <= int(r); ++y) {
        float w = std::sqrt(std::max(0.f, r * r - float(y * y)));
        float t = w * float(1 - 2 * p.illumination);  // terminator offset
        g.fill({int(cx - w), int(cy + y), int(2 * w), 1}, Color{0x30, 0x30, 0x30});
        float a = p.waxing ? t : -w, b = p.waxing ? w : -t;
        if (b > a) g.fill({int(cx + a), int(cy + y), int(b - a), 1}, Color{0xE8, 0xE8, 0xD8});
    }
}

class CartographyScreen : public Screen {
public:
    using Screen::Screen;
    void enter() override { offset_ = 0; }
    void input(Button b) override {
        switch (b) {
            case Button::Left: offset_ -= 3600; break;
            case Button::Right: offset_ += 3600; break;
            case Button::Up: offset_ += 86400; break;
            case Button::Down: offset_ -= 86400; break;
            case Button::Enter:
                if (offset_ != 0) offset_ = 0;
                else app.go(ScreenId::Library);
                break;
            default: break;
        }
    }
    void back() override { app.go(ScreenId::Library); }

    void render(Gfx& g) override {
        g.button(backButtonRect(), "Back", 34, offset_ == 0);
        drawTitle(g, "Stellar Cartography");
        Location loc = app.weather.location();
        time_t t = time(nullptr) + offset_;

        // Sky dome: zenith in the middle, horizon at the rim, north up, east left.
        const float cx = 180, cy = 318, R = 154;
        auto ring = [&](float r, Color c, int th) {
            std::vector<SDL_FPoint> pts;
            for (int i = 0; i <= 90; ++i) {
                float a = float(i) / 90 * 2 * float(M_PI);
                pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
            }
            g.polyline(pts, c, th);
        };
        Horizontal sunH = altAz(Sun, t, loc.lat, loc.lon);
        bool day = sunH.alt > -6;
        if (day) {  // tint the dome by daylight
            for (int y = -int(R); y <= int(R); y += 2) {
                float w = std::sqrt(R * R - float(y * y));
                g.fill({int(cx - w), int(cy + y), int(2 * w), 2}, sunH.alt > 0 ? Color{0x10, 0x28, 0x40} : Color{0x0C, 0x14, 0x26});
            }
        }
        ring(R * 2 / 3, pal::DIM_GREEN, 1);
        ring(R / 3, pal::DIM_GREEN, 1);
        ring(R, pal::GREEN, 2);
        g.text("N", int(cx), int(cy - R - 24), 22, pal::YELLOW, Align::Center);
        g.text("S", int(cx), int(cy + R + 2), 22, pal::YELLOW, Align::Center);
        g.text("E", int(cx - R - 18), int(cy - 11), 22, pal::YELLOW, Align::Center);
        g.text("W", int(cx + R + 18), int(cy - 11), 22, pal::YELLOW, Align::Center);

        auto project = [&](const Horizontal& h) {
            float r = R * float(90 - h.alt) / 90;
            return SDL_FPoint{cx - r * float(std::sin(h.az * M_PI / 180)), cy - r * float(std::cos(h.az * M_PI / 180))};
        };
        MoonPhase phase = moonPhase(t);
        for (int b = 0; b < BodyCount; ++b) {
            Horizontal h = altAz(Body(b), t, loc.lat, loc.lon);
            if (h.alt < 0) continue;
            SDL_FPoint p = project(h);
            if (b == Moon) drawMoon(g, p.x, p.y, 9, phase);
            else {
                int s = b == Sun ? 9 : 4;
                g.fill({int(p.x) - s, int(p.y) - s, 2 * s, 2 * s}, bodyColor(Body(b)));
            }
            std::string label = bodyName(Body(b));
            g.text(label, int(p.x) + 12, int(p.y) - 12, 22, bodyColor(Body(b)));
        }

        // Readouts
        int x = 372, y = 86, right = kW - 18;
        char buf[64];
        tm lt;
        localtime_r(&t, &lt);
        strftime(buf, sizeof buf, "%a %H:%M", &lt);
        std::string when = buf;
        for (auto& c : when) c = char(toupper(static_cast<unsigned char>(c)));
        g.text(when, x, y, 30, offset_ ? pal::RED : pal::GREEN);
        if (offset_) {
            long h = offset_ / 3600;
            snprintf(buf, sizeof buf, "%+ldH", h);
            g.text(buf, right, y, 30, pal::RED, Align::Right);
        }
        y += 42;

        refreshTimes(t, loc);
        const RiseSet& sun = cache_.sunPrev;
        const RiseSet& sunNext = cache_.rs[Sun];
        // Sunrise/sunset as two big lines with drawn up/down arrows (no tiny "RISE"/"SET" words).
        auto arrowRow = [&](const std::string& label, time_t up, time_t down, Color c) {
            g.text(label, x, y, 28, c);
            int ax = right - 190;
            g.triangle({ax + 0.f, y + 24.f}, {ax + 16.f, y + 24.f}, {ax + 8.f, y + 8.f}, c);
            g.text(up ? hhmm(up) : "--", ax + 22, y, 28, c);
            ax += 98;
            g.triangle({ax + 0.f, y + 8.f}, {ax + 16.f, y + 8.f}, {ax + 8.f, y + 24.f}, c);
            g.text(down ? hhmm(down) : "--", ax + 22, y, 28, c);
            y += 40;
        };
        arrowRow("SUN", sun.rise, sunNext.set, pal::YELLOW);

        const RiseSet& mr = cache_.rs[Moon];
        arrowRow("MOON", mr.rise, mr.set, pal::WHITE);
        // Moon block
        drawMoon(g, float(x + 26), float(y + 28), 26, phase);
        g.text(g.ellipsize(phase.name, 28, right - x - 64), x + 64, y, 28, pal::YELLOW);
        snprintf(buf, sizeof buf, "%.0f%% LIT", phase.illumination * 100);
        g.text(buf, x + 64, y + 30, 26, pal::GREEN);
        y += 66;

        g.fill({x, y, right - x, 2}, pal::DIM_GREEN);
        y += 10;
        for (int b = Mercury; b <= Saturn; ++b) {
            Horizontal h = altAz(Body(b), t, loc.lat, loc.lon);
            g.fill({x, y + 9, 12, 12}, bodyColor(Body(b)));
            g.text(bodyName(Body(b)), x + 20, y, 28, pal::GREEN);
            std::string s;
            Color c = pal::YELLOW;
            if (h.alt > 0) {
                snprintf(buf, sizeof buf, "UP %s", compassPoint(h.az).c_str());
                s = buf;
                if (sunH.alt > -6) c = pal::DIM_YELLOW;  // above the horizon but lost in daylight
            } else {
                const RiseSet& r = cache_.rs[b];
                s = r.rise ? "\xE2\x86\x91" + hhmm(r.rise) : "DOWN";
                c = pal::DIM_YELLOW;
            }
            g.text(s, right, y, 28, c, Align::Right);
            y += 38;
        }

        g.text("< > HOUR   ^ v DAY", x, kH - 40, 24, pal::DIM_YELLOW);
    }

private:
    // Rise/set searches are a few thousand position evaluations: recompute once a minute.
    struct Times {
        long key = -1;
        RiseSet sunPrev;
        RiseSet rs[BodyCount];
    };
    void refreshTimes(time_t t, const Location& loc) {
        long key = long(t / 60);
        if (key == cache_.key) return;
        cache_.key = key;
        cache_.sunPrev = riseSet(Sun, t - 12 * 3600, loc.lat, loc.lon);
        for (int b = 0; b < BodyCount; ++b) cache_.rs[b] = riseSet(Body(b), t, loc.lat, loc.lon);
    }

    Times cache_;
    long offset_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeCartographyScreen(App& a) { return std::make_unique<CartographyScreen>(a); }
