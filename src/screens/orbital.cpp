// LIBRARY COMPUTER -> Orbital Tracking: where the International Space Station is now,
// its ground track, and when it next passes over you (and whether you can see it).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "../app.h"
#include "../orbit.h"
#include "../widgets.h"
#include "../worldmap_data.h"

namespace {

const SDL_Rect kMap{14, 82, kW - 28, 280};

SDL_FPoint toMap(double lat, double lon) {
    return {float(kMap.x + (lon + 180) / 360 * kMap.w), float(kMap.y + (90 - lat) / 180 * kMap.h)};
}

std::string when(time_t t) {
    tm lt;
    localtime_r(&t, &lt);
    char b[32];
    strftime(b, sizeof b, "%a %H:%M", &lt);
    std::string s = b;
    for (auto& c : s) c = char(toupper(static_cast<unsigned char>(c)));
    return s;
}

class OrbitalScreen : public Screen {
public:
    using Screen::Screen;
    void enter() override { passesAt_ = trackAt_ = 0; }
    void input(Button b) override {
        if (b == Button::Enter) app.go(ScreenId::Library);
    }
    void back() override { app.go(ScreenId::Library); }
    bool animating() const override { return false; }

    void render(Gfx& g) override {
        g.button(backButtonRect(), "Back", 34, true);
        drawTitle(g, "Orbital Tracking");
        drawMap(g);

        std::string text = app.weather.issTle();
        orbit::Tle tle;
        if (!orbit::parseTle(text, tle)) {
            g.text("NO ORBITAL DATA", kW / 2, 450, 34, pal::RED, Align::Center);
            g.text("Connect to a network once to download the station's orbit", kW / 2, 500, 20, pal::YELLOW,
                   Align::Center);
            return;
        }
        Location loc = app.weather.location();
        time_t now = time(nullptr);

        // Ground track: 40 minutes behind (dim) and 95 minutes ahead (bright), refreshed every 20 s.
        if (now - trackAt_ >= 20) {
            trackAt_ = now;
            for (int pass = 0; pass < 2; ++pass) {
                track_[pass].clear();
                int from = pass == 0 ? -40 : 0, to = pass == 0 ? 0 : 95;
                std::vector<SDL_FPoint> seg;
                double prevLon = 0;
                for (int m = from; m <= to; ++m) {
                    orbit::State s = orbit::propagate(tle, now + m * 60);
                    if (!seg.empty() && std::fabs(s.lon - prevLon) > 180) {  // wrapped across the date line
                        track_[pass].push_back(seg);
                        seg.clear();
                    }
                    seg.push_back(toMap(s.lat, s.lon));
                    prevLon = s.lon;
                }
                track_[pass].push_back(seg);
            }
        }
        for (int pass = 0; pass < 2; ++pass)
            for (auto& seg : track_[pass]) g.polyline(seg, pass == 0 ? pal::DIM_YELLOW : pal::YELLOW, 2);

        // You
        SDL_FPoint me = toMap(loc.lat, loc.lon);
        g.line(me.x - 7, me.y, me.x + 7, me.y, pal::GREEN);
        g.line(me.x, me.y - 7, me.x, me.y + 7, pal::GREEN);
        g.text("YOU", int(me.x) + 8, int(me.y) + 2, 22, pal::GREEN);

        // The station
        orbit::State s = orbit::propagate(tle, now);
        SDL_FPoint p = toMap(s.lat, s.lon);
        bool blink = std::fmod(app.time(), 1.0) < 0.6;
        g.fill({int(p.x) - 6, int(p.y) - 6, 12, 12}, blink ? pal::WHITE : pal::YELLOW);
        g.text("ISS", int(p.x) + 10, int(p.y) - 28, 26, pal::WHITE);

        // Readouts (big: the tricorder's composite screen can't show small text)
        char buf[96];
        int y = 374;
        snprintf(buf, sizeof buf, "%.0f%c %.0f%c", std::fabs(s.lat), s.lat >= 0 ? 'N' : 'S', std::fabs(s.lon),
                 s.lon >= 0 ? 'E' : 'W');
        g.text(buf, 18, y, 30, pal::YELLOW);
        snprintf(buf, sizeof buf, "%.0f KM UP", s.altKm);
        g.text(buf, 18, y + 40, 30, pal::YELLOW);
        snprintf(buf, sizeof buf, "%.0f KM/H", s.speedKmh);
        g.text(buf, 18, y + 80, 30, pal::YELLOW);
        g.text(s.sunlit ? "SUNLIT" : "IN SHADOW", 18, y + 120, 30, s.sunlit ? pal::GREEN : pal::DIM_YELLOW);

        int x = 330;
        orbit::Look look = orbit::lookAngles(s, now, loc.lat, loc.lon);
        if (look.el > 0) {
            bool on = std::fmod(app.time(), 1.0) < 0.6;
            g.text("OVERHEAD NOW", x, y, 34, on ? pal::RED : pal::YELLOW);
            snprintf(buf, sizeof buf, "LOOK %s %.0f\xC2\xB0 UP", astroDir(look.az).c_str(), look.el);
            g.text(buf, x, y + 46, 30, pal::YELLOW);
        } else {
            if (now - passesAt_ > 300) {  // ~5000 propagations: recompute every few minutes
                passes_ = orbit::passes(tle, now, 48, loc.lat, loc.lon);
                passesAt_ = now;
            }
            g.text("NEXT PASSES", x, y, 28, pal::GREEN);
            int row = 0;
            for (const auto& ps : passes_) {
                if (ps.set < now) continue;
                snprintf(buf, sizeof buf, "%s %.0f\xC2\xB0", when(ps.rise).c_str(), ps.maxEl);
                int ry = y + 38 + row * 38;
                g.text(buf, x, ry, 30, ps.visible ? pal::YELLOW : pal::DIM_YELLOW);
                if (ps.visible) g.text("SEE IT", kW - 18, ry, 26, pal::GREEN, Align::Right);
                if (++row == 3) break;
            }
            if (row == 0) g.text("NONE IN 48 HOURS", x, y + 38, 30, pal::DIM_YELLOW);
        }
        double age = orbit::tleAgeDays(tle, now);
        if (age > 5) g.text("ORBIT DATA OLD", kW - 18, kH - 34, 24, pal::RED, Align::Right);
    }

private:
    static std::string astroDir(double az) {
        static const char* pts[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        return pts[int(std::fmod(az + 22.5, 360) / 45) % 8];
    }

    void drawMap(Gfx& g) {
        g.fill(kMap, Color{0x04, 0x0C, 0x14});
        for (int lon = -150; lon < 180; lon += 30) {
            SDL_FPoint a = toMap(90, lon);
            g.fill({int(a.x), kMap.y, 1, kMap.h}, Color{0x14, 0x2A, 0x14});
        }
        for (int lat = -60; lat <= 60; lat += 30) {
            SDL_FPoint a = toMap(lat, -180);
            g.fill({kMap.x, int(a.y), kMap.w, 1}, lat == 0 ? pal::DIM_GREEN : Color{0x14, 0x2A, 0x14});
        }
        for (int r = 0; r < worldmap::kRings; ++r) {
            std::vector<SDL_FPoint> pts;
            for (int i = worldmap::kRingStart[r]; i < worldmap::kRingStart[r + 1]; ++i)
                pts.push_back(toMap(worldmap::kPoints[2 * i + 1] / 100.0, worldmap::kPoints[2 * i] / 100.0));
            g.polyline(pts, pal::GREEN);
        }
        g.outline(kMap, pal::YELLOW, 2);
    }

    std::vector<orbit::Pass> passes_;
    time_t passesAt_ = 0;
    std::vector<std::vector<SDL_FPoint>> track_[2];
    time_t trackAt_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeOrbitalScreen(App& a) { return std::make_unique<OrbitalScreen>(a); }
