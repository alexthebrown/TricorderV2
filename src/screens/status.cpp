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
        else if (b == Button::Right || b == Button::Down) page_ = (page_ + 1) % 2;
        else if (b == Button::Left || b == Button::Up) page_ = (page_ + 1) % 2;
    }

    void render(Gfx& g) override {
        g.button(backButtonRect(), "Back", 34, true);
        drawTitle(g, "Status Report");
        const SysInfo& s = app.sys.info();
        ProbeResult p = app.prober.get();

        struct Box {
            std::string label, value;
            double bar = -1;
            Color color = pal::BLACK;
        };
        std::vector<Box> boxes;
        boxes.push_back({"STARDATE", stardateString(time(nullptr))});
        boxes.push_back({"UPTIME", formatDuration(s.uptimeSec)});
        if (s.cpuTempC >= 0) {
            char t[32];
            snprintf(t, sizeof t, "%.1f\xC2\xB0" "C", s.cpuTempC);
            boxes.push_back({"CORE TEMP", t, s.cpuTempC / 85.0, s.cpuTempC > 75 ? pal::RED : pal::BLACK});
        } else {
            boxes.push_back({"CORE TEMP", "NO SENSOR"});
        }
        char load[32];
        snprintf(load, sizeof load, "%d%%", s.cpuUsage < 0 ? 0 : int(s.cpuUsage * 100));
        boxes.push_back({"COMPUTER CORE", load, s.cpuUsage < 0 ? 0 : s.cpuUsage});
        double used = double(s.memTotalKB - s.memAvailKB);
        boxes.push_back({"MEMORY", mb(used) + " / " + mb(double(s.memTotalKB)),
                         s.memTotalKB ? used / double(s.memTotalKB) : 0});
        if (s.diskTotal)
            boxes.push_back({"STORAGE FREE", gb(s.diskFree), 1.0 - double(s.diskFree) / double(s.diskTotal)});
        else
            boxes.push_back({"STORAGE", "UNKNOWN", -1, pal::RED});

        // Page 2
        boxes.push_back({"SUBSPACE LINK", s.ip.empty() ? "NO LINK" : (p.ssid.empty() ? s.ip : p.ssid), -1,
                         s.ip.empty() ? pal::RED : pal::BLACK});
        if (s.wifiQuality >= 0)
            boxes.push_back({"SIGNAL", std::to_string(s.wifiQuality) + "%", s.wifiQuality / 100.0});
        else
            boxes.push_back({"SIGNAL", "NO WIRELESS"});
        // Power: Pi firmware reports under-voltage / throttling bits.
        Box power{"POWER", "NOMINAL"};
        if (!p.throttleKnown) power.value = "UNMONITORED";
        else if (p.throttled & 0x1) { power.value = "LOW VOLTAGE!"; power.color = pal::RED; }
        else if (p.throttled & 0x4) { power.value = "THROTTLED"; power.color = pal::RED; }
        else if (p.throttled & 0x50000) power.value = "NOMINAL*";  // a brownout happened earlier
        boxes.push_back(power);
        bool online = !app.library.root().empty();
        boxes.push_back({"LOG LIBRARY", online ? app.library.rootLabel() : "OFFLINE", -1,
                         !online || app.library.usingFallback() ? pal::RED : pal::BLACK});
        boxes.push_back({"ADDRESS", s.ip.empty() ? "--" : s.ip});
        boxes.push_back({"UNIT", s.hostname});

        // Six big boxes a page: small text doesn't survive the tricorder's composite screen.
        const int x0 = 18, gap = 12, w = (kW - 2 * x0 - gap) / 2, h = 118, top = 88;
        for (int i = 0; i < 6; ++i) {
            int idx = page_ * 6 + i;
            if (idx >= int(boxes.size())) break;
            const Box& b = boxes[idx];
            drawReadout(g, {x0 + (i % 2) * (w + gap), top + (i / 2) * (h + gap), w, h}, b.label, b.value, 46,
                        b.bar, b.color);
        }
        g.text(page_ == 0 ? "PAGE 1 OF 2   NEXT >" : "< PREV   PAGE 2 OF 2", kW / 2, kH - 46, 28, pal::DIM_YELLOW,
               Align::Center);
    }

private:
    int page_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeStatusScreen(App& a) { return std::make_unique<StatusScreen>(a); }
