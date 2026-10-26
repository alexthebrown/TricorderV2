// SELECT -> Chronometer: stardate, ship's time and a mission stopwatch.
#include <cctype>
#include <cstdio>
#include <ctime>

#include "../app.h"
#include "../stardate.h"
#include "../widgets.h"

namespace {

class ChronoScreen : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        buttons_.clear();
        buttons_.add(backButtonRect(), "Back", 34, [this] { app.go(ScreenId::Library); });
        int px;
        auto r = layoutRow(app.gfx, {"START", "RESET"}, kH - 84, 60, 36, 14, kW - 14, 24, &px, 40);
        startIdx_ = buttons_.add(r[0], running_ ? "STOP" : "START", px, [this] { toggle(); });
        buttons_.add(r[1], "RESET", px, [this] {
            running_ = false;
            accumulated_ = 0;
            buttons_[startIdx_].label = "START";
        });
        buttons_.setFocus(startIdx_);
    }
    void input(Button b) override { buttons_.handle(b); }
    void back() override { app.go(ScreenId::Library); }
    bool animating() const override { return running_; }

    void render(Gfx& g) override {
        buttons_.render(g);
        drawTitle(g, "Chronometer");
        time_t now = time(nullptr);
        tm lt;
        localtime_r(&now, &lt);

        g.text("STARDATE", kW / 2, 92, 30, pal::GREEN, Align::Center);
        std::string sd = stardateString(now);
        g.text(sd, kW / 2, 122, g.fitSize(sd, kW - 60, 120), pal::YELLOW, Align::Center);

        char buf[64];
        strftime(buf, sizeof buf, "%H:%M:%S", &lt);
        g.text(buf, kW / 2, 262, 70, pal::GREEN, Align::Center);
        strftime(buf, sizeof buf, "%A %d %B %Y", &lt);
        std::string date = buf;
        for (auto& c : date) c = char(toupper(static_cast<unsigned char>(c)));
        g.text(date, kW / 2, 340, g.fitSize(date, kW - 60, 30), pal::YELLOW, Align::Center);

        double e = elapsed();
        int cs = int(e * 10) % 10;
        long s = long(e);
        snprintf(buf, sizeof buf, "%02ld:%02ld:%02ld.%d", s / 3600, (s / 60) % 60, s % 60, cs);
        SDL_Rect box{kW / 2 - 230, 388, 460, 80};
        g.fill(box, pal::YELLOW);
        g.text("MISSION TIMER", box.x + 10, box.y + 6, 20, pal::BLACK);
        g.textIn({box.x, box.y + 16, box.w, box.h - 16}, buf, 52, running_ ? pal::BLACK : pal::DIM_GREEN);
    }

private:
    double elapsed() const { return accumulated_ + (running_ ? app.time() - startedAt_ : 0); }
    void toggle() {
        if (running_) accumulated_ += app.time() - startedAt_;
        else startedAt_ = app.time();
        running_ = !running_;
        buttons_[startIdx_].label = running_ ? "STOP" : "START";
    }

    ButtonGroup buttons_;
    int startIdx_ = 1;
    bool running_ = false;
    double startedAt_ = 0, accumulated_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeChronoScreen(App& a) { return std::make_unique<ChronoScreen>(a); }
