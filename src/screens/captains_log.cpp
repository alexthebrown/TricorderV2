// CAPTAIN'S LOG: two columns of buttons, as in the original, now browsing a folder tree
// (e.g. a Plex library). Yellow buttons are folders, green ones play. Movie folders
// play their film directly; running times and resume bars fill in as they're found.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

#include "../app.h"
#include "../sysinfo.h"
#include "../widgets.h"

namespace {

constexpr int kCols = 2;
constexpr int kRows = 6;
constexpr int kTop = 120;
constexpr int kRowH = 68;
constexpr int kBtnH = 58;

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

class CaptainsLog : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        // Re-list on entry: picks up new files, and retries the drive if it was offline.
        app.library.refresh();
        waiting_ = true;
    }

    void update(double dt) override {
        refresh_ -= dt;
        if (refresh_ > 0) return;
        refresh_ = 0.3;
        if (app.library.loading()) return;
        auto fresh = app.library.items();
        std::string folder = app.library.folder();
        if (waiting_ || folder != folder_) {
            folder_ = folder;
            items_ = fresh;
            waiting_ = false;
            restoreSelection();
        } else if (fresh.size() == items_.size()) {
            items_ = fresh;  // details (durations, movie folders) arriving
        }
    }

    void back() override {
        // Up one folder; at the top, back to the main menu.
        std::string root = app.library.root();
        if (folder_.empty() || folder_ == root || waiting_) {
            remember();
            app.go(ScreenId::MainMenu);
            return;
        }
        remember();
        app.library.open(std::filesystem::path(folder_).parent_path().string());
        waiting_ = true;
    }

    void input(Button b) override {
        int n = int(items_.size());
        if (b == Button::Enter) {
            if (sel_ < 0) back();
            else if (!waiting_) activate(sel_);
            return;
        }
        if (n == 0 || waiting_) return;
        if (sel_ < 0) {  // on Back
            sel_ = (b == Button::Down || b == Button::Right) ? 0 : n - 1;
        } else {
            switch (b) {
                case Button::Right: sel_ = sel_ + 1 < n ? sel_ + 1 : -1; break;
                case Button::Left: sel_ = sel_ - 1; break;
                case Button::Down:
                    sel_ = sel_ + kCols < n ? sel_ + kCols : (sel_ / kCols == (n - 1) / kCols ? -1 : n - 1);
                    break;
                case Button::Up:
                    sel_ = sel_ - kCols;
                    if (sel_ < 0) sel_ = -1;
                    break;
                default: break;
            }
        }
        ensureVisible();
    }

    void render(Gfx& g) override {
        g.button(backButtonRect(), "Back", 34, sel_ < 0);
        drawTitle(g, "Captain's Log Page");

        std::string root = app.library.root();
        std::string crumb = app.library.rootLabel();
        if (!folder_.empty() && folder_.size() > root.size() && folder_.compare(0, root.size(), root) == 0) {
            std::string rel = folder_.substr(root.size());
            for (auto& c : rel)
                if (c == '/') c = '\x01';
            size_t p;
            while ((p = rel.find('\x01')) != std::string::npos) rel.replace(p, 1, " / ");
            crumb += rel;
        }
        crumb = upper(crumb);
        Color subCol = app.library.usingFallback() || root.empty() ? pal::RED : pal::GREEN;
        if (app.library.usingFallback()) crumb += "  -  NETWORK DRIVE OFFLINE";
        // Keep the deepest part of a long path visible.
        int px = 28;
        if (g.textWidth(crumb, px) > kW - 80) {
            while (g.textWidth("..." + crumb, px) > kW - 80 && crumb.size() > 4) crumb.erase(0, 1);
            crumb = "..." + crumb;
        }
        g.text(crumb, 18, 80, px, subCol);

        if (waiting_) {
            g.text("ACCESSING LIBRARY" + std::string(int(app.time() * 3) % 4, '.'), kW / 2, 250, 36, pal::YELLOW,
                   Align::Center);
            drawLights(g, kW / 2 - 51, 310, 6, app.time(), 12, true);
            return;
        }
        if (items_.empty()) {
            std::string why = app.library.status();
            g.text(why.empty() ? "NO LOG ENTRIES FOUND" : why, kW / 2, 200, 34, pal::RED, Align::Center);
            g.text(g.ellipsize(app.cfg.videoDir, 24, kW - 40), kW / 2, 256, 24, pal::YELLOW, Align::Center);
            if (app.cfg.videoDir.rfind("smb://", 0) == 0)
                g.text("Open the share once in the Files app so it is mounted", kW / 2, 290, 20, pal::YELLOW,
                       Align::Center);
            return;
        }

        int colW = (kW - 28 - 14) / kCols;
        for (int row = 0; row < kRows; ++row) {
            for (int c = 0; c < kCols; ++c) {
                int i = (first_ + row) * kCols + c;
                if (i >= int(items_.size())) break;
                drawItem(g, items_[i], {14 + c * (colW + 14), kTop + row * kRowH, colW, kBtnH}, i == sel_);
            }
        }

        int totalRows = (int(items_.size()) + kCols - 1) / kCols;
        if (first_ > 0) g.triangle({kW - 46.f, 104.f}, {kW - 18.f, 104.f}, {kW - 32.f, 90.f}, pal::YELLOW);
        if (first_ + kRows < totalRows) {
            float y = kTop + kRows * kRowH - 6.f;
            g.triangle({kW / 2.f - 14, y}, {kW / 2.f + 14, y}, {kW / 2.f, y + 12}, pal::YELLOW);
        }
        if (sel_ >= 0) {
            char pos[32];
            snprintf(pos, sizeof pos, "%d / %zu", sel_ + 1, items_.size());
            g.text(pos, kW - 18, kTop + kRows * kRowH + 2, 24, pal::DIM_YELLOW, Align::Right);
        }
    }

private:
    void drawItem(Gfx& g, const MediaItem& m, SDL_Rect r, bool f) {
        // Folders are yellow; anything that plays (a video, or a movie folder) is green.
        bool isFolder = m.folder && !m.playable();
        Color bg = isFolder ? pal::YELLOW : pal::GREEN;
        if (f) g.outline({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, isFolder ? pal::GREEN : pal::YELLOW, 2);
        g.bevel(r, bg, f);
        int off = f ? 1 : 0;
        std::string tag;
        if (isFolder) tag = m.videoCount >= 0 ? std::to_string(m.videoCount) + " >" : ">";
        else if (m.duration > 0) tag = formatClock(m.duration);
        int tagW = tag.empty() ? 0 : g.textWidth(tag, 24) + 10;
        std::string name = g.ellipsize(m.name, 32, r.w - 24 - tagW);
        g.textIn({r.x + 12 + off, r.y + off, r.w - 24 - tagW, r.h}, name, 32, pal::BLACK, Align::Left);
        if (!tag.empty())
            g.textIn({r.x + off, r.y + off, r.w - 10, r.h}, tag, 24, pal::BLACK,
                     Align::Right);
        if (m.playable() && m.duration > 0) {
            double resume = app.library.resumePosition(m.playPath);
            if (resume > 0)
                g.fill({r.x + 4, r.y + r.h - 8, int((r.w - 8) * std::min(1.0, resume / m.duration)), 4}, pal::BLACK);
        }
    }

    void activate(int i) {
        const MediaItem& m = items_[i];
        if (m.playable()) {
            remember();
            // The player continues through the plain files that follow (episodes in a season).
            std::vector<MediaItem> list;
            int index = 0;
            for (int k = 0; k < int(items_.size()); ++k) {
                if (k == i) index = int(list.size());
                if (items_[k].playable()) list.push_back(items_[k]);
            }
            app.playVideo(list, index);
        } else if (m.folder) {
            remember();
            app.library.open(m.path);
            waiting_ = true;
        }
    }

    // Each folder keeps its own selection, so going back up lands where you were.
    void remember() {
        if (!folder_.empty()) memory_[folder_] = {sel_, first_};
    }
    void restoreSelection() {
        auto it = memory_.find(folder_);
        if (it != memory_.end()) {
            sel_ = std::min(it->second.first, int(items_.size()) - 1);
            first_ = it->second.second;
        } else {
            sel_ = items_.empty() ? -1 : 0;
            first_ = 0;
        }
        ensureVisible();
    }
    void ensureVisible() {
        if (sel_ < 0) return;
        int row = sel_ / kCols;
        if (row < first_) first_ = row;
        if (row >= first_ + kRows) first_ = row - kRows + 1;
    }

    std::vector<MediaItem> items_;
    std::string folder_;
    std::map<std::string, std::pair<int, int>> memory_;
    bool waiting_ = true;
    int sel_ = -1;  // -1 = Back button
    int first_ = 0;
    double refresh_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeCaptainsLog(App& a) { return std::make_unique<CaptainsLog>(a); }
