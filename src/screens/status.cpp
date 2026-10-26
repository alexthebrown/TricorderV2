// STATUS: real diagnostics of the tricorder itself, dressed as ship's systems.
#include <cstdio>
#include <ctime>

#include "../app.h"
#include "../stardate.h"
#include "../widgets.h"

namespace {

std::string mb(double kb) {
    char b[32];
    snprintf(b, sizeof b, "%.0f MB", kb / 1024);
    return b;
}

std::string gb(uint64_t bytes) {
    char b[32];
    snprintf(b, sizeof b, "%.1f GB", bytes / 1e9);
    return b;
}

class StatusScreen : public Screen {
public:
    using Screen::Screen;
    void enter() override { app.prober.setActive(true); }
    void exit() override { app.prober.setActive(false); }
    void input(Button b) override {
        if (b == Button::Enter) app.go(ScreenId::MainMenu);
    }

    void render(Gfx& g) override {
        g.button(backButtonRect(), "Back", 34, true);
        drawTitle(g, "Status Report");
        const SysInfo& s = app.sys.info();
        ProbeResult p = app.prober.get();

        const int x0 = 18, gap = 10, w = (kW - 2 * x0 - gap) / 2, h = 78;
        int y = 88;
        auto box = [&](int col, const std::string& label, const std::string& value, double bar = -1,
                       Color vc = pal::BLACK) {
            drawReadout(g, {x0 + col * (w + gap), y, w, h}, label, value, 34, bar, vc);
            if (col == 1) y += h + gap;
        };

        box(0, "STARDATE", stardateString(time(nullptr)));
        box(1, "SYSTEM UPTIME", formatDuration(s.uptimeSec));

        if (s.cpuTempC >= 0) {
            char t[32];
            snprintf(t, sizeof t, "%.1f\xC2\xB0" "C", s.cpuTempC);
            box(0, "CORE TEMPERATURE", t, s.cpuTempC / 85.0, s.cpuTempC > 75 ? pal::RED : pal::BLACK);
        } else {
            box(0, "CORE TEMPERATURE", "NO SENSOR");
        }
        char load[32];
        snprintf(load, sizeof load, "%d%%  (LOAD %.2f)", s.cpuUsage < 0 ? 0 : int(s.cpuUsage * 100), s.load1);
        box(1, "COMPUTER CORE", load, s.cpuUsage < 0 ? 0 : s.cpuUsage);

        double used = double(s.memTotalKB - s.memAvailKB);
        box(0, "MEMORY BANKS", mb(used) + " / " + mb(double(s.memTotalKB)),
            s.memTotalKB ? used / double(s.memTotalKB) : 0);
        if (s.diskTotal)
            box(1, "STORAGE FREE", gb(s.diskFree) + " / " + gb(s.diskTotal),
                1.0 - double(s.diskFree) / double(s.diskTotal));
        else
            box(1, "STORAGE", "UNKNOWN", -1, pal::RED);

        std::string net = s.ip.empty() ? "NO LINK" : s.ip;
        if (!p.ssid.empty()) net = p.ssid + "  " + net;
        box(0, "SUBSPACE LINK", net, -1, s.ip.empty() ? pal::RED : pal::BLACK);
        if (s.wifiQuality >= 0)
            box(1, "SIGNAL STRENGTH", std::to_string(s.wifiQuality) + "%  " + std::to_string(s.wifiDbm) + " dBm",
                s.wifiQuality / 100.0);
        else
            box(1, "SIGNAL STRENGTH", "NO WIRELESS");

        // Power: Pi firmware reports under-voltage / throttling bits.
        std::string power = "NOMINAL";
        Color pc = pal::BLACK;
        if (!p.throttleKnown) power = "UNMONITORED";
        else if (p.throttled & 0x1) { power = "UNDER-VOLTAGE!"; pc = pal::RED; }
        else if (p.throttled & 0x4) { power = "THROTTLED"; pc = pal::RED; }
        else if (p.throttled & 0x50000) { power = "NOMINAL (BROWNOUT LOGGED)"; }
        box(0, "POWER SYSTEMS", power, -1, pc);
        bool online = !app.library.root().empty();
        box(1, "LOG LIBRARY", online ? app.library.rootLabel() : "OFFLINE", -1,
            !online || app.library.usingFallback() ? pal::RED : pal::BLACK);

        g.text(g.ellipsize(s.model + "  -  " + s.hostname, 20, kW - 36), x0, kH - 30, 20, pal::GREEN);
    }

};

}  // namespace

std::unique_ptr<Screen> makeStatusScreen(App& a) { return std::make_unique<StatusScreen>(a); }
