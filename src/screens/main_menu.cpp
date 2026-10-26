// The original front page: header, giant TRICORDER, two rows of green buttons.
#include <cmath>
#include <ctime>

#include "../app.h"
#include "../stardate.h"
#include "../widgets.h"

namespace {

class MainMenu : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        int keep = buttons_.size() ? buttons_.focus() : 0;
        buttons_.clear();
        Gfx& g = app.gfx;
        int px;
        auto top = layoutRow(g, {"PLANET", "CAPTAIN'S LOG", "STATUS"}, 296, 70, 44, 14, kW - 14, 14, &px);
        buttons_.add(top[0], "PLANET", px, [this] { app.go(ScreenId::Planet); });
        buttons_.add(top[1], "CAPTAIN'S LOG", px, [this] { app.go(ScreenId::CaptainsLog); });
        buttons_.add(top[2], "STATUS", px, [this] { app.go(ScreenId::Status); });

        // Second row starts with the user's name, like the original.
        int nameW = std::min(220, g.textWidth(app.cfg.username, 36)) + 16;
        auto bot = layoutRow(g, {"SENSORS", "SELECT", "INPUT"}, 392, 70, px, 14 + nameW, kW - 14, 14, &px);
        nameRect_ = {14, 392, bot[0].x - 14 - 10, 70};
        buttons_.add(bot[0], "SENSORS", px, [this] { app.go(ScreenId::Sensors); });
        buttons_.add(bot[1], "SELECT", px, [this] { app.go(ScreenId::Library); });
        buttons_.add(bot[2], "INPUT", px, [this] { app.go(ScreenId::Settings); });
        buttons_.setFocus(keep);
    }

    void input(Button b) override { buttons_.handle(b); }
    void back() override {}  // already home
    bool animating() const override { return false; }

    void render(Gfx& g) override {
        const char* header = "USS ENTERPRISE NCC-1701 STANDARD ISSUE";
        g.text(header, kW / 2, 18, g.fitSize(header, 690, 40), pal::GREEN, Align::Center);
        g.text("TRICORDER", kW / 2, 62, g.fitSize("TRICORDER", 690, 190), pal::YELLOW, Align::Center);

        buttons_.render(g);
        int namePx = g.fitSize(app.cfg.username, nameRect_.w, 36);
        g.textIn(nameRect_, app.cfg.username, namePx, pal::YELLOW, Align::Center);

        time_t now = time(nullptr);
        tm lt;
        localtime_r(&now, &lt);
        char clock[16];
        strftime(clock, sizeof clock, "%H:%M", &lt);
        g.text("STARDATE " + stardateString(now), 20, 512, 26, pal::GREEN);
        g.text(clock, kW - 20, 512, 26, pal::GREEN, Align::Right);
        size_t pending = app.subspace.pending().size();
        if (app.redAlertActive()) {
            bool on = std::fmod(app.time(), 1.0) < 0.6;
            g.text("WEATHER ALERT - SEE PLANET", kW / 2, 512, 24, on ? pal::RED : mix(pal::RED, pal::BLACK, 0.5f),
                   Align::Center);
        } else if (pending > 0) {
            bool on = std::fmod(app.time(), 1.2) < 0.8;
            std::string m = std::to_string(pending) + (pending == 1 ? " TRANSMISSION" : " TRANSMISSIONS") + " AWAITING CLEARANCE";
            g.text(m, kW / 2, 514, g.fitSize(m, 400, 22), on ? pal::YELLOW : pal::DIM_YELLOW, Align::Center);
        } else if (app.cfg.lights) {
            drawLights(g, kW / 2 - 51, 520, 6, app.time());
        }
    }

private:
    ButtonGroup buttons_;
    SDL_Rect nameRect_{};
};

}  // namespace

std::unique_ptr<Screen> makeMainMenu(App& a) { return std::make_unique<MainMenu>(a); }
