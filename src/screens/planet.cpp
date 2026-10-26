// PLANET: current conditions from weather.gov, laid out like the original page,
// plus wind, dew point and a 12-hour forecast strip.
#include <algorithm>
#include <cmath>
#include <ctime>

#include "../app.h"
#include "../widgets.h"

namespace {

class PlanetScreen : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        buttons_.clear();
        buttons_.add(backButtonRect(), "Back", 34, [this] { app.go(ScreenId::MainMenu); });
        buttons_.add({kW - 150, kH - 62, 136, 50}, "Rescan", 30, [this] {
            app.weather.refreshNow();
            scanFlash_ = 1.2;
        });
        buttons_.setFocus(0);
    }
    void input(Button b) override { buttons_.handle(b); }
    void update(double dt) override { scanFlash_ = std::max(0.0, scanFlash_ - dt); }
    bool animating() const override { return scanFlash_ > 0 || app.redAlertActive(); }

    void render(Gfx& g) override {
        buttons_.render(g);
        drawTitle(g, "Planet Conditions");
        WeatherData w = app.weather.get();
        bool busy = app.weather.busy() || scanFlash_ > 0;

        if (!w.valid) {
            int dots = int(app.time() * 3) % 4;
            g.text("SCANNING ATMOSPHERE" + std::string(dots, '.'), 40, 250, 40, pal::YELLOW);
            drawLights(g, 40, 310, 8, app.time(), 14, true);
            return;
        }

        // Temperature block (left), as in the original.
        SDL_Rect temp{18, 92, 300, 196};
        g.fill(temp, pal::YELLOW);
        g.text("Temperature:", temp.x + temp.w / 2, temp.y + 8, 32, pal::BLACK, Align::Center);
        std::string deg = std::to_string(w.temp) + "\xC2\xB0";
        int tpx = g.fitSize(deg, 200, 112);
        int tw = g.textWidth(deg, tpx);
        int tx = temp.x + (temp.w - tw - 40) / 2;
        g.text(deg, tx, temp.y + 52, tpx, pal::BLACK);
        g.text("F", tx + tw + 6, temp.y + 52 + tpx / 2, 48, pal::BLACK);

        // Right column readouts.
        int rx = 334, rw = kW - rx - 18;
        auto row = [&](int y, const std::string& s) {
            SDL_Rect r{rx, y, rw, 58};
            g.fill(r, pal::YELLOW);
            g.textIn({r.x + 12, r.y, r.w - 24, r.h}, s, g.fitSize(s, r.w - 24, 32), pal::BLACK, Align::Left);
        };
        row(92, "Chance Precip: " + std::to_string(w.precip) + "%");
        row(161, "Humidity: " + std::to_string(w.humidity) + "%");
        row(230, w.wind.empty() ? "Dew Point: " + std::to_string(w.dewpointF) + "\xC2\xB0" : "Wind: " + w.wind);

        std::string sfc = w.shortForecast;
        g.text(sfc, kW / 2, 300, g.fitSize(sfc, kW - 40, 40), pal::YELLOW, Align::Center);

        // Forecast strip: the next 6 hours, with a temperature trace and precipitation bars.
        // (Kept to 6 so the numbers stay big enough for the tricorder's small screen.)
        SDL_Rect strip{18, 350, kW - 36, 146};
        g.outline(strip, pal::GREEN, 2);
        if (!w.hours.empty()) {
            int n = int(std::min<size_t>(6, w.hours.size()));
            int lo = 999, hi = -999;
            for (int i = 0; i < n; ++i) {
                lo = std::min(lo, w.hours[i].temp);
                hi = std::max(hi, w.hours[i].temp);
            }
            if (hi == lo) hi = lo + 1;
            float cw = (strip.w - 8) / float(n);
            std::vector<SDL_FPoint> pts;
            for (int i = 0; i < n; ++i) {
                const WeatherHour& h = w.hours[i];
                float cx = strip.x + 4 + cw * (i + 0.5f);
                int barH = int(h.precip / 100.0 * 40);
                g.fill({int(cx - cw / 2 + 4), strip.y + strip.h - 26 - barH, int(cw - 8), barH},
                       withAlpha(pal::BLUE, 170));
                float ty = strip.y + 74 - (h.temp - lo) / float(hi - lo) * 30;
                pts.push_back({cx, ty});
                g.text(std::to_string(h.temp) + "\xC2\xB0", int(cx), int(ty) - 40, 32, pal::YELLOW, Align::Center);
                g.text(h.label, int(cx), strip.y + strip.h - 34, 28, pal::GREEN, Align::Center);
            }
            g.polyline(pts, pal::YELLOW, 2);
        }

        // Active NWS alerts take over the status line.
        auto alerts = app.weather.alerts();
        if (!alerts.empty()) {
            const WeatherAlert& a = alerts[0];
            std::string line = a.event;
            if (a.expires > 0) {
                tm lt;
                localtime_r(&a.expires, &lt);
                char buf[24];
                strftime(buf, sizeof buf, " UNTIL %-I:%M %p", &lt);
                line += buf;
            }
            if (alerts.size() > 1) line += "  (+" + std::to_string(alerts.size() - 1) + " MORE)";
            for (auto& c : line) c = char(toupper(static_cast<unsigned char>(c)));
            bool on = !a.red() || std::fmod(app.time(), 1.0) < 0.6;
            g.text(g.ellipsize(line, 28, kW - 200), 22, kH - 62, 28, a.red() ? (on ? pal::RED : pal::DIM_YELLOW) : pal::YELLOW);
            return;
        }

        // Status line.
        std::string status;
        if (!w.simulated && w.fetched > 0) {
            long mins = long(difftime(time(nullptr), w.fetched) / 60);
            status = mins < 1 ? "UPDATED JUST NOW" : "UPDATED " + std::to_string(mins) + "M AGO";
            if (!w.error.empty()) status += " (OFFLINE)";
        }
        if (busy) status = "SCANNING ATMOSPHERE...";
        else if (w.simulated) status = "SENSOR ESTIMATE";
        g.text(g.ellipsize(status, 28, kW - 200), 22, kH - 62, 28, w.simulated ? pal::RED : pal::GREEN);
    }

private:
    ButtonGroup buttons_;
    double scanFlash_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makePlanetScreen(App& a) { return std::make_unique<PlanetScreen>(a); }
