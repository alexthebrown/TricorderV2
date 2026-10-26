// Video player page: full-screen picture with the original Play / Pause / Stop
// buttons (plus skip back/forward) on an overlay that hides itself while playing.
#include <algorithm>

#include "../app.h"
#include "../sysinfo.h"
#include "../video_player.h"
#include "../widgets.h"

namespace {

constexpr double kOverlaySecs = 3.5;
constexpr double kSkipSecs = 10;
constexpr double kNextSecs = 6;

class PlayerScreen : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        buildControls();
        load();
    }

    void load() {
        item_ = app.playlist.empty() ? MediaItem{} : app.playlist[app.playIndex];
        error_.clear();
        endedAt_ = -1;
        double resume = app.library.resumePosition(item_.playPath);
        if (resume > 15) {
            mode_ = Mode::Prompt;
            prompt_.clear();
            prompt_.add({110, 300, 240, 64}, "Resume " + formatClock(resume), 32, [this, resume] { start(resume); });
            prompt_.add({370, 300, 240, 64}, "From Start", 32, [this] { start(0); });
            prompt_.setFocus(0);
        } else {
            start(0);
        }
    }

    void exit() override { closeVideo(); }

    void closeVideo() {
        if (player_.isOpen()) {
            double pos = player_.position(), dur = player_.duration();
            bool nearEnd = dur > 0 && pos > dur - 20;
            app.library.setResumePosition(item_.playPath, (pos > 15 && !nearEnd && endedAt_ < 0) ? pos : 0);
        }
        player_.close();
    }

    // Episodes continue into the next file in the folder (not movie to movie).
    bool hasNext() const {
        int n = app.playIndex + 1;
        return n < int(app.playlist.size()) && !app.playlist[n].folder;
    }
    void playNext() {
        closeVideo();
        ++app.playIndex;
        load();
    }

    bool animating() const override { return mode_ == Mode::Playing; }
    bool allowIdle() const override { return false; }

    void back() override { stop(); }

    void input(Button b) override {
        if (mode_ == Mode::Prompt) {
            prompt_.handle(b);
            return;
        }
        if (mode_ == Mode::Error) {
            if (b == Button::Enter) stop();
            return;
        }
        if (endedAt_ >= 0) {  // ENTER skips the countdown to the next episode
            if (b == Button::Enter && hasNext()) playNext();
            return;
        }
        bool wasHidden = !overlayVisible();
        lastInput_ = app.time();
        if (wasHidden) return;  // first press just wakes the controls
        controls_.handle(b);
    }

    void update(double) override {
        if (mode_ != Mode::Playing) return;
        player_.update(app.gfx.renderer());
        if (endedAt_ < 0 && player_.finished()) {
            endedAt_ = app.time();
            app.library.setResumePosition(item_.playPath, 0);
        }
        if (endedAt_ >= 0 && app.time() - endedAt_ > (hasNext() ? kNextSecs : 2.0)) {
            if (hasNext()) playNext();
            else stop();
        }
    }

    void render(Gfx& g) override {
        if (mode_ == Mode::Prompt) {
            g.text(item_.name, kW / 2, 150, g.fitSize(item_.name, kW - 40, 48), pal::YELLOW, Align::Center);
            g.text("Log entry previously interrupted", kW / 2, 222, 30, pal::GREEN, Align::Center);
            prompt_.render(g);
            return;
        }
        if (mode_ == Mode::Error) {
            drawTitle(g, "Captain's Log", 20);
            g.text("UNABLE TO ACCESS LOG ENTRY", kW / 2, 200, 40, pal::RED, Align::Center);
            g.text(g.ellipsize(item_.name, 28, kW - 40), kW / 2, 260, 28, pal::YELLOW, Align::Center);
            g.text(g.ellipsize(error_, 22, kW - 40), kW / 2, 300, 22, pal::GREEN, Align::Center);
            g.button({kW / 2 - 70, 380, 140, 56}, "Back", 32, true);
            return;
        }

        player_.render(g, {0, 0, kW, kH}, app.cfg.videoFill);
        if (!player_.hasPicture()) {
            g.text("ACCESSING LOG" + std::string(int(app.time() * 3) % 4, '.'), kW / 2, 260, 36, pal::YELLOW,
                   Align::Center);
        }
        if (endedAt_ >= 0) {
            SDL_Rect r{kW / 2 - 260, kH / 2 - 60, 520, hasNext() ? 120 : 80};
            g.fill(r, pal::BLACK);
            g.outline(r, pal::YELLOW, 2);
            g.textIn({r.x, r.y, r.w, 80}, "END OF LOG ENTRY", 40, pal::YELLOW);
            if (hasNext()) {
                int secs = std::max(0, int(kNextSecs - (app.time() - endedAt_) + 0.99));
                std::string next = "NEXT: " + app.playlist[app.playIndex + 1].name + " IN " + std::to_string(secs);
                g.text(g.ellipsize(next, 24, r.w - 30), kW / 2, r.y + 74, 24, pal::GREEN, Align::Center);
            }
            return;
        }
        if (!overlayVisible()) return;

        // Title + decoder info
        g.fill({0, 0, kW, 44}, withAlpha(pal::BLACK, 170));
        g.text(g.ellipsize(item_.name, 28, kW - 220), 14, 8, 28, pal::YELLOW);
        std::string info = std::string(player_.hardware() ? "HW " : "SW ") + std::to_string(player_.width()) + "x" +
                           std::to_string(player_.height());
        if (player_.droppedFrames() > 0) info += "  -" + std::to_string(player_.droppedFrames());
        g.text(info, kW - 14, 14, 18, player_.qualityLevel() > 0 ? pal::RED : pal::GREEN, Align::Right);

        // Progress + controls
        g.fill({0, kH - 116, kW, 116}, withAlpha(pal::BLACK, 190));
        double pos = player_.position(), dur = player_.duration();
        SDL_Rect bar{18, kH - 104, kW - 36, 12};
        g.outline(bar, pal::GREEN, 2);
        if (dur > 0) g.fill({bar.x + 2, bar.y + 2, int((bar.w - 4) * std::clamp(pos / dur, 0.0, 1.0)), bar.h - 4}, pal::YELLOW);
        std::string t = formatClock(pos) + " / " + (dur > 0 ? formatClock(dur) : "--:--");
        g.text(t, kW - 18, kH - 88, 22, pal::YELLOW, Align::Right);
        if (player_.paused()) g.text("PAUSED", 18, kH - 88, 22, pal::RED);
        controls_.render(g);
    }

private:
    enum class Mode { Prompt, Playing, Error };

    bool overlayVisible() const {
        return player_.paused() || app.time() - lastInput_ < kOverlaySecs || !player_.hasPicture();
    }

    void buildControls() {
        controls_.clear();
        auto r = layoutRow(app.gfx, {"<<", "Play", "Pause", ">>", "Stop"}, kH - 62, 50, 30, 14, kW - 14, 14,
                           nullptr, 26);
        controls_.add(r[0], "<<", 30, [this] { player_.seekRelative(-kSkipSecs); });
        controls_.add(r[1], "Play", 30, [this] { player_.setPaused(false); });
        controls_.add(r[2], "Pause", 30, [this] { player_.setPaused(true); });
        controls_.add(r[3], ">>", 30, [this] { player_.seekRelative(kSkipSecs); });
        controls_.add(r[4], "Stop", 30, [this] { stop(); });
        controls_.setFocus(1);
    }

    void start(double at) {
        VideoOptions o;
        o.hwDecode = app.cfg.hwDecode != "off";
        o.audio = app.cfg.audio;
        o.pixelAspect = app.cfg.pixelAspect;
        if (!player_.open(item_.playPath, o, error_)) {
            mode_ = Mode::Error;
            return;
        }
        if (at > 0) player_.seek(at);
        mode_ = Mode::Playing;
        lastInput_ = app.time();
    }

    void stop() { app.go(ScreenId::CaptainsLog); }

    MediaItem item_;
    VideoPlayer player_;
    ButtonGroup controls_, prompt_;
    Mode mode_ = Mode::Playing;
    std::string error_;
    double lastInput_ = 0;
    double endedAt_ = -1;
};

}  // namespace

std::unique_ptr<Screen> makePlayerScreen(App& a) { return std::make_unique<PlayerScreen>(a); }
