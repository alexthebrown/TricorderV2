// Short power-on sequence: sweep line, title, system checks. Any button skips it.
#include <algorithm>
#include <cmath>

#include "../app.h"
#include "../widgets.h"

namespace {

class BootScreen : public Screen {
public:
    using Screen::Screen;
    void enter() override { t_ = 0; }
    void input(Button) override { app.go(ScreenId::MainMenu); }
    void back() override { app.go(ScreenId::MainMenu); }
    bool animating() const override { return true; }
    bool allowIdle() const override { return false; }
    void update(double dt) override {
        t_ += dt;
        if (t_ > 3.4) app.go(ScreenId::MainMenu);
    }

    void render(Gfx& g) override {
        // Scanning beam drops down the screen first.
        double sweep = std::min(1.0, t_ / 0.7);
        int y = int(sweep * kH);
        if (t_ < 0.9) {
            for (int i = 0; i < 24; ++i)
                g.fill({0, y - i * 3, kW, 3}, withAlpha(pal::GREEN, uint8_t(std::max(0, 200 - i * 9))));
        }
        auto fadeIn = [&](double start) {
            return uint8_t(std::clamp((t_ - start) / 0.4, 0.0, 1.0) * 255);
        };
        if (t_ > 0.5) {
            int px = g.fitSize("USS ENTERPRISE NCC-1701", 680, 44);
            g.text("USS ENTERPRISE NCC-1701", kW / 2, 70, px, withAlpha(pal::GREEN, fadeIn(0.5)), Align::Center);
        }
        if (t_ > 0.9) {
            int px = g.fitSize("TRICORDER", 680, 170);
            g.text("TRICORDER", kW / 2, 130, px, withAlpha(pal::YELLOW, fadeIn(0.9)), Align::Center);
        }
        static const char* checks[] = {"SENSOR ARRAY", "SUBSPACE TRANSCEIVER", "MEMORY CORE", "LIBRARY COMPUTER"};
        for (int i = 0; i < 4; ++i) {
            double at = 1.4 + i * 0.35;
            if (t_ < at) break;
            int ly = 340 + i * 38;
            g.text(checks[i], 150, ly, 28, pal::GREEN);
            bool done = t_ > at + 0.25;
            g.text(done ? "ONLINE" : "...", 570, ly, 28, done ? pal::YELLOW : pal::DIM_YELLOW, Align::Right);
        }
        SDL_Rect bar{150, 510, 420, 14};
        g.outline(bar, pal::GREEN, 2);
        g.fill({bar.x + 3, bar.y + 3, int((bar.w - 6) * std::clamp(t_ / 3.0, 0.0, 1.0)), bar.h - 6}, pal::GREEN);
        drawLights(g, kW / 2 - 45, 540, 6, t_, 10, true);
    }

private:
    double t_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeBootScreen(App& a) { return std::make_unique<BootScreen>(a); }
