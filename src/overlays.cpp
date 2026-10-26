// Full-screen layers drawn on top of whatever page is showing: RED ALERT for severe
// weather warnings, and the idle "standby scan" after the tricorder sits untouched.
#include "overlays.h"

#include <cmath>
#include <ctime>

#include "app.h"
#include "stardate.h"
#include "widgets.h"

void drawRedAlert(Gfx& g, const WeatherAlert& a, double t) {
    bool flash = std::fmod(t, 1.0) < 0.5;
    g.fill({0, 0, kW, kH}, pal::BLACK);
    // Classic alert bars, alternating.
    for (int i = 0; i < 6; ++i) {
        bool on = (i % 2 == 0) == flash;
        Color c = on ? pal::RED : mix(pal::RED, pal::BLACK, 0.7f);
        g.fill({20 + i * 115, 14, 105, 22}, c);
        g.fill({20 + i * 115, kH - 36, 105, 22}, c);
    }
    g.text("RED ALERT", kW / 2, 46, 110, flash ? pal::RED : mix(pal::RED, pal::BLACK, 0.35f), Align::Center);

    std::string ev = a.event;
    for (auto& c : ev) c = char(toupper(static_cast<unsigned char>(c)));
    g.text(ev, kW / 2, 166, g.fitSize(ev, kW - 40, 48), pal::YELLOW, Align::Center);
    std::string when;
    if (a.expires > 0) {
        tm lt;
        localtime_r(&a.expires, &lt);
        char buf[32];
        strftime(buf, sizeof buf, "UNTIL %-I:%M %p", &lt);
        when = buf;
    }
    std::string sub = g.ellipsize(a.area, 28, kW - 60);
    g.text(sub, kW / 2, 220, 28, pal::GREEN, Align::Center);
    if (!when.empty()) g.text(when, kW / 2, 254, 30, pal::YELLOW, Align::Center);

    std::string body = a.instruction.empty() ? a.headline : a.instruction;
    if (!a.description.empty()) body += "\n\n" + a.description;
    auto lines = g.wrap(body, 24, kW - 80);
    int y = 300;
    for (size_t i = 0; i < lines.size() && y < kH - 110; ++i, y += 36) g.text(lines[i], 40, y, 30, pal::YELLOW);

    g.button({kW / 2 - 150, kH - 92, 300, 46}, "ACKNOWLEDGE", 30, true);
}

void drawTransmission(Gfx& g, const SubspaceMessage& m, double t) {
    g.fill({0, 0, kW, kH}, withAlpha(pal::BLACK, 235));
    SDL_Rect box{30, 60, kW - 60, kH - 120};
    g.fill(box, pal::BLACK);
    g.outline(box, pal::YELLOW, 3);
    bool flash = std::fmod(t, 1.2) < 0.8;
    g.text("INCOMING TRANSMISSION", kW / 2, box.y + 18, 46, flash ? pal::YELLOW : pal::DIM_YELLOW, Align::Center);
    drawLights(g, kW / 2 - 51, box.y + 78, 6, t, 12, true);
    g.text("FROM: " + g.ellipsize(m.from, 28, box.w - 140), box.x + 24, box.y + 104, 28, pal::GREEN);
    auto lines = g.wrap(m.text, 30, box.w - 48);
    int y = box.y + 150;
    for (size_t i = 0; i < lines.size() && y < box.y + box.h - 80; ++i, y += 36)
        g.text(lines[i], box.x + 24, y, 30, pal::YELLOW);
    g.button({kW / 2 - 150, box.y + box.h - 66, 300, 46}, "ACKNOWLEDGE", 30, true);
}

void drawIdle(App& app, Gfx& g, double t) {
    g.fill({0, 0, kW, kH}, pal::BLACK);
    // Rotating scanner on the left.
    const float cx = 210, cy = 300, R = 170;
    std::vector<SDL_FPoint> ring;
    for (int i = 0; i <= 72; ++i) {
        float a = float(i) / 72 * 2 * float(M_PI);
        ring.push_back({cx + R * std::cos(a), cy + R * std::sin(a)});
    }
    g.polyline(ring, pal::GREEN, 2);
    for (float r : {R * 0.33f, R * 0.66f}) {
        std::vector<SDL_FPoint> c;
        for (int i = 0; i <= 48; ++i) {
            float a = float(i) / 48 * 2 * float(M_PI);
            c.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
        }
        g.polyline(c, pal::DIM_GREEN);
    }
    g.line(cx - R, cy, cx + R, cy, pal::DIM_GREEN);
    g.line(cx, cy - R, cx, cy + R, pal::DIM_GREEN);
    double sweep = std::fmod(t * 1.4, 2 * M_PI);
    for (int i = 0; i < 24; ++i) {
        double a = sweep - i * 0.035;
        g.line(cx, cy, cx + R * float(std::cos(a)), cy + R * float(std::sin(a)),
               withAlpha(pal::GREEN, uint8_t(std::max(0, 230 - i * 10))));
    }
    // Contacts that light up as the beam passes and slowly fade.
    for (int i = 0; i < 7; ++i) {
        double ang = std::fmod(i * 2.39996 + std::floor(t / 30) * 0.7, 2 * M_PI);
        double dist = 0.25 + 0.7 * std::fmod(i * 0.618 + 0.13, 1.0);
        double since = std::fmod(sweep - ang + 4 * M_PI, 2 * M_PI);
        int alpha = int(255 * std::max(0.0, 1.0 - since / 4.0));
        if (alpha <= 0) continue;
        float bx = cx + float(R * dist * std::cos(ang)), by = cy + float(R * dist * std::sin(ang));
        g.fill({int(bx) - 4, int(by) - 4, 8, 8}, withAlpha(i == 0 ? pal::YELLOW : pal::GREEN, uint8_t(alpha)));
    }

    // At a convention the standby screen invites visitors to send a message.
    std::string url = app.visitorUrl();
    if (!url.empty()) {
        int qx = 430;
        g.text("TRANSMIT TO", qx + 125, 40, 34, pal::YELLOW, Align::Center);
        g.text("THE TRICORDER", qx + 125, 80, 34, pal::YELLOW, Align::Center);
        drawQr(g, url, qx + 10, 130, 230);
        g.text("SCAN WITH PHONE", qx + 125, 372, 30, pal::GREEN, Align::Center);
        if (!app.cfg.smsNumber.empty()) {
            g.text("OR TEXT", qx + 125, 412, 26, pal::GREEN, Align::Center);
            g.text(app.cfg.smsNumber, qx + 125, 442, 30, pal::YELLOW, Align::Center);
        }
        drawLights(g, qx + 80, 480, 6, t, 12);
        return;
    }

    // Readouts on the right.
    int x = 420;
    g.text("TRICORDER", x, 40, 50, pal::YELLOW);
    g.text("STANDBY" + std::string(int(t * 2) % 4, '.'), x, 100, 32, pal::GREEN);
    time_t now = time(nullptr);
    tm lt;
    localtime_r(&now, &lt);
    char clock[16];
    strftime(clock, sizeof clock, "%H:%M:%S", &lt);
    WeatherData w = app.weather.get();
    ProbeResult p = app.prober.get();
    std::vector<std::pair<std::string, std::string>> rows = {
        {"DATE", stardateString(now)},
        {"TIME", clock},
        {"TEMP", w.valid ? std::to_string(w.temp) + "\xC2\xB0" "F" : "---"},
        {"HUMID", w.valid ? std::to_string(w.humidity) + "%" : "---"},
        {"LIFE", p.neighbours > 0 ? std::to_string(p.neighbours) : "---"},
        {"SIGNALS", std::to_string(p.nets.size())},
    };
    int y = 150;
    for (auto& r : rows) {
        g.text(r.first, x, y, 30, pal::GREEN);
        g.text(r.second, kW - 24, y, 30, pal::YELLOW, Align::Right);
        y += 46;
    }
    drawLights(g, x, y + 4, 8, t, 14);
    g.text("PRESS ANY BUTTON", kW / 2, kH - 44, 28, pal::DIM_YELLOW, Align::Center);
}
