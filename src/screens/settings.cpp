// INPUT -> Configuration: tweak the device with just the five buttons.
// UP/DOWN pick a row, LEFT/RIGHT (or ENTER) change it. Changes apply immediately.
#include <algorithm>
#include <cstdlib>
#include <functional>

#include "../app.h"
#include "../widgets.h"

namespace {

struct Row {
    std::string label;
    std::function<std::string()> value;
    std::function<void(int)> change;  // -1 / +1
};

class SettingsScreen : public Screen {
public:
    explicit SettingsScreen(App& a) : Screen(a) {
        Config& c = app.cfg;
        rows_ = {
            {"OVERSCAN WIDTH", [&c] { return std::to_string(c.overscanX); },
             [this, &c](int d) { c.overscanX = std::clamp(c.overscanX + d * 4, 0, 80); applyOverscan(); }},
            {"OVERSCAN HEIGHT", [&c] { return std::to_string(c.overscanY); },
             [this, &c](int d) { c.overscanY = std::clamp(c.overscanY + d * 4, 0, 80); applyOverscan(); }},
            {"VIDEO DECODER", [&c] { return std::string(c.hwDecode == "off" ? "SOFTWARE" : "HARDWARE"); },
             [&c](int) { c.hwDecode = c.hwDecode == "off" ? "auto" : "off"; }},
            {"VIDEO SCALING", [&c] { return std::string(c.videoFill ? "FILL" : "FIT"); },
             [&c](int) { c.videoFill = !c.videoFill; }},
            {"AUDIO", [&c] { return std::string(c.audio ? "ON" : "OFF"); }, [&c](int) { c.audio = !c.audio; }},
            {"BOLD TEXT", [&c] { return std::string(c.boldText ? "ON" : "OFF"); },
             [this, &c](int) { c.boldText = !c.boldText; app.gfx.setBold(c.boldText); }},
            {"INDICATOR LIGHTS", [&c] { return std::string(c.lights ? "ON" : "OFF"); },
             [&c](int) { c.lights = !c.lights; }},
            {"BOOT SEQUENCE", [&c] { return std::string(c.bootSequence ? "ON" : "OFF"); },
             [&c](int) { c.bootSequence = !c.bootSequence; }},
            {"VISITOR RELAY", [&c] { return std::string(c.relay ? "ON" : "OFF"); },
             [this, &c](int) { app.setRelay(!c.relay); }},
            {"MODERATION", [&c] { return std::string(c.moderate ? "ON" : "OFF"); },
             [&c](int) { c.moderate = !c.moderate; }},
            // Changing the topic breaks every QR code already shown, so it takes two presses.
            {"RELAY FREQUENCY",
             [this] { return std::string(app.time() - confirmTopic_ < 3 ? "PRESS AGAIN" : "CHANGE"); },
             [this](int) {
                 if (app.time() - confirmTopic_ < 3) {
                     confirmTopic_ = -100;
                     app.newRelayTopic();
                 } else {
                     confirmTopic_ = app.time();
                     app.toast("PRESS AGAIN TO CHANGE", 3);
                 }
             }},
        };
    }

    void enter() override {
        sel_ = 0;
        confirmShutdown_ = -100;
        confirmTopic_ = -100;
        actions_.clear();
        int px;
        auto r = layoutRow(app.gfx, {"SAVE", "EXIT", "SHUT DOWN"}, kH - 74, 58, 34, 14, kW - 14, 18, &px, 30);
        actions_.add(r[0], "SAVE", px, [this] { app.saveConfig(); });
        actions_.add(r[1], "EXIT", px, [this] { app.quit(); });
        actions_.add(r[2], "SHUT DOWN", px, [this] { shutdown(); });
    }

    // sel_: 0 = Back, 1..rows = settings, rows+1 = action row
    void input(Button b) override {
        int nRows = int(rows_.size());
        int actionSel = nRows + 1;
        switch (b) {
            case Button::Up: sel_ = sel_ == 0 ? actionSel : sel_ - 1; break;
            case Button::Down: sel_ = sel_ == actionSel ? 0 : sel_ + 1; break;
            case Button::Left:
            case Button::Right:
                if (sel_ >= 1 && sel_ <= nRows) rows_[sel_ - 1].change(b == Button::Right ? 1 : -1);
                else if (sel_ == actionSel) actions_.handle(b);
                break;
            case Button::Enter:
                if (sel_ == 0) app.go(ScreenId::MainMenu);
                else if (sel_ <= nRows) rows_[sel_ - 1].change(1);
                else actions_.handle(b);
                break;
            default: break;
        }
    }

    void render(Gfx& g) override {
        g.button(backButtonRect(), "Back", 34, sel_ == 0);
        drawTitle(g, "Configuration");
        // A scrolling window of big rows: more settings than fit at a readable size.
        const int top = 90, rowH = 50, visible = 8;
        int n = int(rows_.size());
        if (sel_ >= 1 && sel_ <= n) {
            if (sel_ - 1 < first_) first_ = sel_ - 1;
            if (sel_ - 1 >= first_ + visible) first_ = sel_ - visible;
        }
        first_ = std::clamp(first_, 0, std::max(0, n - visible));
        for (int k = 0; k < visible && first_ + k < n; ++k) {
            int i = first_ + k, y = top + k * rowH;
            bool f = sel_ == i + 1;
            SDL_Rect valR{kW - 250, y + 4, 232, rowH - 10};
            g.text(rows_[i].label, 22, y + 6, 32, f ? pal::YELLOW : pal::GREEN);
            if (f) g.outline({valR.x - 4, valR.y - 4, valR.w + 8, valR.h + 8}, pal::YELLOW, 2);
            g.bevel(valR, pal::GREEN, f);
            std::string v = rows_[i].value();
            g.textIn(valR, (f ? "< " : "") + v + (f ? " >" : ""), 30, pal::BLACK);
        }
        if (first_ > 0) g.triangle({kW / 2.f - 14, top - 2.f}, {kW / 2.f + 14, top - 2.f}, {kW / 2.f, top - 14.f}, pal::YELLOW);
        if (first_ + visible < n) {
            float y = top + visible * rowH - 4.f;
            g.triangle({kW / 2.f - 14, y}, {kW / 2.f + 14, y}, {kW / 2.f, y + 12}, pal::YELLOW);
        }
        // Overscan guide: a frame at the real screen edge helps line things up.
        if (sel_ == 1 || sel_ == 2) g.outline({0, 0, kW, kH}, pal::RED, 3);
        actions_.render(g, sel_ == n + 1);
    }

private:
    void applyOverscan() { app.gfx.setOverscan(app.cfg.overscanX, app.cfg.overscanY); }

    void shutdown() {
        if (app.time() - confirmShutdown_ > 3) {
            confirmShutdown_ = app.time();
            app.toast("PRESS SHUT DOWN AGAIN", 3);
            return;
        }
        app.toast("POWERING DOWN");
        if (std::system(app.cfg.shutdownCmd.c_str()) != 0) app.toast("SHUTDOWN NOT PERMITTED (SEE README)", 4);
        else app.quit();
    }

    std::vector<Row> rows_;
    ButtonGroup actions_;
    int sel_ = 0;
    double confirmShutdown_ = -100;
    double confirmTopic_ = -100;
    int first_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeSettingsScreen(App& a) { return std::make_unique<SettingsScreen>(a); }
