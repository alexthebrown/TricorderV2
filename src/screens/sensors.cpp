// SENSORS: the prop's scanning modes. Each scan plays a short sweep, then shows
// readings. Real data where the Pi has it (Wi-Fi = subspace, LAN hosts = life signs,
// weather = atmosphere); plausible simulated readings for the rest.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../app.h"
#include "../widgets.h"

namespace {

enum class Mode { None, Geo, Bio, Atmos, Subspace };
constexpr double kScanSecs = 2.2;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 12345) {}
    float next() {
        s = s * 1664525u + 1013904223u;
        return (s >> 8) / float(1 << 24);
    }
};

class SensorScreen : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        buttons_.clear();
        buttons_.add(backButtonRect(), "Back", 34, [this] { app.go(ScreenId::MainMenu); });
        int px;
        auto r = layoutRow(app.gfx, {"GEOLOGICAL", "BIOLOGICAL", "ATMOSPHERIC", "SUBSPACE"}, 88, 52, 34, 14, kW - 14,
                           10, &px, 10);
        buttons_.add(r[0], "GEOLOGICAL", px, [this] { scan(Mode::Geo); });
        buttons_.add(r[1], "BIOLOGICAL", px, [this] { scan(Mode::Bio); });
        buttons_.add(r[2], "ATMOSPHERIC", px, [this] { scan(Mode::Atmos); });
        buttons_.add(r[3], "SUBSPACE", px, [this] { scan(Mode::Subspace); });
        buttons_.setFocus(mode_ == Mode::None ? 1 : int(mode_));
        app.prober.setActive(true);
    }
    void exit() override { app.prober.setActive(false); }
    void input(Button b) override { buttons_.handle(b); }
    bool animating() const override { return true; }
    void update(double dt) override { t_ += dt; }

    void render(Gfx& g) override {
        buttons_.render(g);
        drawTitle(g, "Sensor Page");
        bool scanning = mode_ != Mode::None && t_ - scanStart_ < kScanSecs;

        SDL_Rect panel{18, 156, 330, kH - 156 - 18};
        SDL_Rect scope{362, 156, kW - 362 - 18, 230};
        SDL_Rect extra{362, 398, kW - 362 - 18, kH - 398 - 18};

        if (mode_ == Mode::None) {
            g.text("SELECT SCAN MODE", kW / 2, 250, 44, pal::YELLOW, Align::Center);
            drawLights(g, kW / 2 - 63, 320, 8, app.time());
            return;
        }

        // Scope trace
        std::vector<float> wave(120);
        for (int i = 0; i < 120; ++i) wave[i] = sample(i / 120.0);
        drawScope(g, scope, scanning ? std::vector<float>{} : wave, pal::GREEN, modeName());
        if (scanning) {
            double p = (t_ - scanStart_) / kScanSecs;
            int x = scope.x + int(scope.w * std::fmod(p * 2, 1.0));
            for (int i = 0; i < 18; ++i)
                g.fill({x - i * 3, scope.y + 2, 3, scope.h - 4}, withAlpha(pal::GREEN, uint8_t(std::max(0, 180 - i * 10))));
            g.text("SCANNING" + std::string(int(t_ * 4) % 4, '.'), panel.x, panel.y + 10, 40, pal::YELLOW);
            SDL_Rect bar{panel.x, panel.y + 70, panel.w, 16};
            g.outline(bar, pal::GREEN, 2);
            g.fill({bar.x + 3, bar.y + 3, int((bar.w - 6) * std::min(1.0, p)), bar.h - 6}, pal::GREEN);
            drawLights(g, panel.x, panel.y + 110, 8, t_, 16, true);
            g.outline(extra, pal::DIM_GREEN, 2);
            return;
        }

        switch (mode_) {
            case Mode::Geo: renderGeo(g, panel, extra); break;
            case Mode::Bio: renderBio(g, panel, extra); break;
            case Mode::Atmos: renderAtmos(g, panel, extra); break;
            case Mode::Subspace: renderSubspace(g, panel, extra, scope); break;
            default: break;
        }
    }

private:
    void scan(Mode m) {
        mode_ = m;
        scanStart_ = t_;
        ++scanCount_;
        if (m == Mode::Subspace || m == Mode::Bio) app.prober.scanNow();
    }

    const char* modeName() const {
        switch (mode_) {
            case Mode::Geo: return "SEISMIC";
            case Mode::Bio: return "BIO-RHYTHM";
            case Mode::Atmos: return "BAROMETRIC";
            case Mode::Subspace: return "SUBSPACE SPECTRUM";
            default: return "";
        }
    }

    float sample(double x) const {
        double t = t_;
        switch (mode_) {
            case Mode::Geo:
                return float(0.25 * std::sin(x * 40 + t * 3) + 0.15 * std::sin(x * 97 - t * 7) +
                             (std::fmod(x * 3 + t * 0.4, 1.0) < 0.05 ? 0.6 * std::sin(x * 300) : 0));
            case Mode::Bio: {
                // ECG-ish: a sharp spike every beat.
                double beat = std::fmod(x * 3 - t * 1.2, 1.0);
                if (beat < 0) beat += 1;
                double v = 0.08 * std::sin(x * 30);
                if (beat > 0.10 && beat < 0.14) v += 0.25;
                if (beat > 0.20 && beat < 0.23) v -= 0.25;
                if (beat > 0.23 && beat < 0.26) v += 0.95;
                if (beat > 0.26 && beat < 0.30) v -= 0.4;
                if (beat > 0.42 && beat < 0.52) v += 0.2 * std::sin((beat - 0.42) / 0.1 * M_PI);
                return float(v);
            }
            case Mode::Atmos:
                return float(0.5 * std::sin(x * 6.28 * 2 + t * 0.8) * (0.8 + 0.2 * std::sin(t * 0.3)) +
                             0.08 * std::sin(x * 80 + t * 5));
            case Mode::Subspace:
                return float(0.6 * std::sin(x * 120 + t * 20) * std::sin(x * 7 + t) );
            default: return 0;
        }
    }

    void line(Gfx& g, SDL_Rect panel, int& y, const std::string& label, const std::string& value,
              Color vc = pal::YELLOW) {
        g.text(label, panel.x, y, 22, pal::GREEN);
        g.text(value, panel.x + panel.w, y, 22, vc, Align::Right);
        y += 32;
    }

    void barRow(Gfx& g, SDL_Rect r, int& y, const std::string& label, float v, Color c) {
        g.text(label, r.x + 10, y, 20, pal::GREEN);
        SDL_Rect b{r.x + 150, y + 4, r.w - 160, 14};
        g.fill(b, pal::DIM_GREEN);
        g.fill({b.x, b.y, int(b.w * std::clamp(v, 0.f, 1.f)), b.h}, c);
        y += 28;
    }

    void renderGeo(Gfx& g, SDL_Rect panel, SDL_Rect extra) {
        Rng r(scanCount_);
        float si = 0.35f + r.next() * 0.25f, fe = 0.1f + r.next() * 0.2f, c = 0.05f + r.next() * 0.1f;
        float dil = r.next();
        int y = panel.y;
        char buf[64];
        g.text("GEOLOGICAL SURVEY", panel.x, y, 30, pal::YELLOW);
        y += 46;
        snprintf(buf, sizeof buf, "%.2f g/cc", 2.4 + r.next() * 3.2);
        line(g, panel, y, "DENSITY", buf);
        snprintf(buf, sizeof buf, "%.0f m", 20 + r.next() * 400);
        line(g, panel, y, "BEDROCK DEPTH", buf);
        bool quake = r.next() > 0.8;
        line(g, panel, y, "SEISMIC ACTIVITY", quake ? "ELEVATED" : "LOW", quake ? pal::RED : pal::YELLOW);
        snprintf(buf, sizeof buf, "%.0f K", 280 + r.next() * 40);
        line(g, panel, y, "CRUST TEMP", buf);
        line(g, panel, y, "DILITHIUM", dil > 0.85 ? "DETECTED!" : "TRACE", dil > 0.85 ? pal::RED : pal::YELLOW);
        line(g, panel, y, "TOPALINE", r.next() > 0.5 ? "TRACE" : "NONE");

        g.outline(extra, pal::YELLOW, 2);
        int ey = extra.y + 10;
        barRow(g, extra, ey, "SILICATES", si, pal::YELLOW);
        barRow(g, extra, ey, "IRON", fe, pal::RED);
        barRow(g, extra, ey, "CARBON", c, pal::GREEN);
        barRow(g, extra, ey, "DILITHIUM", dil * 0.15f, pal::BLUE);
        barRow(g, extra, ey, "OTHER", 1 - si - fe - c, mix(pal::YELLOW, pal::BLACK, 0.4f));
    }

    void renderBio(Gfx& g, SDL_Rect panel, SDL_Rect extra) {
        ProbeResult p = app.prober.get();
        int life = std::max(1, p.neighbours);
        Rng r(scanCount_);
        int y = panel.y;
        char buf[64];
        g.text("BIOLOGICAL SCAN", panel.x, y, 30, pal::YELLOW);
        y += 46;
        line(g, panel, y, "LIFE SIGNS", std::to_string(life), life > 1 ? pal::YELLOW : pal::GREEN);
        line(g, panel, y, "CLASSIFICATION", "HUMANOID");
        line(g, panel, y, "BIOCHEMISTRY", "CARBON-BASED");
        int bpm = 62 + int(r.next() * 20) + int(3 * std::sin(t_ * 0.5));
        line(g, panel, y, "HEART RATE", std::to_string(bpm) + " BPM");
        snprintf(buf, sizeof buf, "%.1f\xC2\xB0" "C", 36.6 + r.next() * 0.8);
        line(g, panel, y, "BODY TEMP", buf);
        line(g, panel, y, "CONDITION", "NOMINAL");

        g.outline(extra, pal::YELLOW, 2);
        g.text("LIFE FORMS IN RANGE", extra.x + 10, extra.y + 8, 20, pal::GREEN);
        int n = std::min(life, 24);
        for (int i = 0; i < n; ++i) {
            int cx = extra.x + 22 + (i % 12) * 26, cy = extra.y + 50 + (i / 12) * 40;
            bool blink = std::sin(t_ * 3 + i) > 0;
            Color c = i == 0 ? pal::YELLOW : pal::GREEN;
            g.fill({cx - 5, cy - 14, 10, 10}, blink ? c : mix(c, pal::BLACK, 0.4f));   // head
            g.fill({cx - 7, cy - 2, 14, 18}, blink ? c : mix(c, pal::BLACK, 0.4f));    // body
        }
        g.text(p.neighbours < 0 ? "SCANNING LOCAL NETWORK..." : "SOURCE: LOCAL NETWORK", extra.x + 10,
               extra.y + extra.h - 28, 18, pal::DIM_YELLOW);
    }

    void renderAtmos(Gfx& g, SDL_Rect panel, SDL_Rect extra) {
        WeatherData w = app.weather.get();
        Rng r(scanCount_);
        int y = panel.y;
        char buf[64];
        g.text("ATMOSPHERIC SCAN", panel.x, y, 30, pal::YELLOW);
        y += 46;
        line(g, panel, y, "TEMPERATURE", w.valid ? std::to_string(w.temp) + "\xC2\xB0" "F" : "---");
        line(g, panel, y, "HUMIDITY", w.valid ? std::to_string(w.humidity) + "%" : "---");
        line(g, panel, y, "PRECIPITATION", w.valid ? std::to_string(w.precip) + "%" : "---");
        snprintf(buf, sizeof buf, "%.0f mb", 1002 + r.next() * 22);
        line(g, panel, y, "PRESSURE", buf);
        line(g, panel, y, "WIND", w.wind.empty() ? "CALM" : w.wind);
        line(g, panel, y, "BREATHABLE", "YES", pal::GREEN);

        g.outline(extra, pal::YELLOW, 2);
        int ey = extra.y + 10;
        barRow(g, extra, ey, "NITROGEN", 0.78f, pal::BLUE);
        barRow(g, extra, ey, "OXYGEN", 0.21f, pal::GREEN);
        barRow(g, extra, ey, "ARGON", 0.0093f + 0.02f, pal::YELLOW);
        barRow(g, extra, ey, "CO2", 0.0004f + 0.01f, pal::RED);
        barRow(g, extra, ey, "H2O VAPOR", w.valid ? w.humidity / 100.f * 0.04f + 0.01f : 0.02f, pal::BLUE);
    }

    void renderSubspace(Gfx& g, SDL_Rect panel, SDL_Rect extra, SDL_Rect scope) {
        ProbeResult p = app.prober.get();
        int y = panel.y;
        g.text("SUBSPACE SIGNALS", panel.x, y, 30, pal::YELLOW);
        y += 46;
        if (p.generation == 0) {
            g.text("LISTENING" + std::string(int(t_ * 3) % 4, '.'), panel.x, y, 24, pal::GREEN);
        }
        for (size_t i = 0; i < p.nets.size() && i < 9; ++i) {
            const WifiNet& n = p.nets[i];
            Color c = n.inUse ? pal::YELLOW : pal::GREEN;
            std::string name = g.ellipsize(n.ssid, 20, panel.w - 70);
            g.text(name, panel.x, y, 20, c);
            SDL_Rect b{panel.x + panel.w - 60, y + 4, 60, 12};
            g.fill(b, pal::DIM_GREEN);
            g.fill({b.x, b.y, b.w * n.signal / 100, b.h}, n.signal > 60 ? pal::GREEN : n.signal > 30 ? pal::YELLOW : pal::RED);
            y += 30;
        }
        // Channel spectrum over the scope trace.
        for (const WifiNet& n : p.nets) {
            if (n.channel <= 0) continue;
            int ch = n.channel <= 14 ? n.channel : 14 + (n.channel % 30);
            float fx = scope.x + 10 + (scope.w - 20) * (std::min(ch, 44) / 45.f);
            int h = int((scope.h - 30) * n.signal / 100.f);
            g.fill({int(fx) - 4, scope.y + scope.h - 4 - h, 8, h}, withAlpha(n.inUse ? pal::YELLOW : pal::BLUE, 200));
        }

        g.outline(extra, pal::YELLOW, 2);
        int ey = extra.y + 10;
        char buf[64];
        snprintf(buf, sizeof buf, "SOURCES DETECTED: %zu", p.nets.size());
        g.text(buf, extra.x + 10, ey, 20, pal::YELLOW);
        ey += 30;
        if (!p.ssid.empty()) {
            g.text("LOCKED: " + g.ellipsize(p.ssid, 20, extra.w - 110), extra.x + 10, ey, 20, pal::GREEN);
            ey += 30;
        }
        int secure = 0;
        for (auto& n : p.nets) secure += n.secure;
        snprintf(buf, sizeof buf, "ENCRYPTED: %d", secure);
        g.text(buf, extra.x + 10, ey, 20, pal::GREEN);
        g.text(p.wifiReal ? "SOURCE: WIRELESS RECEIVER" : "SIMULATED - NO RECEIVER", extra.x + 10,
               extra.y + extra.h - 28, 18, p.wifiReal ? pal::DIM_YELLOW : pal::RED);
    }

    ButtonGroup buttons_;
    Mode mode_ = Mode::None;
    double t_ = 0, scanStart_ = -10;
    uint32_t scanCount_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeSensorScreen(App& a) { return std::make_unique<SensorScreen>(a); }
