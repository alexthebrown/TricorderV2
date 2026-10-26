// LIBRARY COMPUTER -> Ship's Database: browse categories and entries from data/library.txt
// (plus ~/.config/tricorder/library.txt). UP/DOWN scroll long entries.
#include <algorithm>
#include <fstream>

#include "../app.h"
#include "../widgets.h"

namespace {

struct Entry {
    std::string title, text;
};
struct Category {
    std::string name;
    std::vector<Entry> entries;
};

void parseLibrary(const std::string& path, std::vector<Category>& cats) {
    std::ifstream in(path);
    std::string line;
    Category* cat = nullptr;
    Entry* e = nullptr;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] == '#') continue;
        if (line.size() > 2 && line.front() == '[' && line.back() == ']') {
            std::string name = line.substr(1, line.size() - 2);
            auto it = std::find_if(cats.begin(), cats.end(), [&](const Category& c) { return c.name == name; });
            if (it == cats.end()) {
                cats.push_back({name, {}});
                cat = &cats.back();
            } else {
                cat = &*it;
            }
            e = nullptr;
        } else if (line.rfind("== ", 0) == 0 && cat) {
            cat->entries.push_back({line.substr(3), ""});
            e = &cat->entries.back();
        } else if (e) {
            if (!e->text.empty() || !line.empty()) e->text += line + "\n";
        }
    }
}

class DatabaseScreen : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        if (cats_.empty()) {
            for (const std::string& p : {exeDir() + "/../data/library.txt", exeDir() + "/data/library.txt",
                                         std::string("data/library.txt")}) {
                if (fileExists(p)) {
                    parseLibrary(p, cats_);
                    break;
                }
            }
            std::string user = configFilePath();
            parseLibrary(user.substr(0, user.rfind('/')) + "/library.txt", cats_);
        }
        level_ = 0;
        sel_ = 0;
    }

    void back() override {
        if (level_ == 0) app.go(ScreenId::Library);
        else {
            --level_;
            sel_ = level_ == 0 ? cat_ : entry_;
        }
    }

    void input(Button b) override {
        if (level_ == 2) {  // reading
            if (b == Button::Down) scroll_ = std::min(scroll_ + 1, maxScroll_);
            else if (b == Button::Up) scroll_ = std::max(scroll_ - 1, 0);
            else if (b == Button::Right || b == Button::Left) {
                // flip to the next/previous entry in the category
                int n = int(cats_[cat_].entries.size());
                entry_ = (entry_ + (b == Button::Right ? 1 : n - 1)) % n;
                scroll_ = 0;
            } else if (b == Button::Enter) back();
            return;
        }
        int n = level_ == 0 ? int(cats_.size()) : int(cats_[cat_].entries.size());
        int cols = level_ == 0 ? 1 : 2;
        switch (b) {
            case Button::Down: sel_ = sel_ + cols < n ? sel_ + cols : (sel_ < 0 ? 0 : -1); break;
            case Button::Up: sel_ = sel_ < 0 ? n - 1 : (sel_ - cols >= 0 ? sel_ - cols : -1); break;
            case Button::Right: sel_ = sel_ + 1 < n ? sel_ + 1 : -1; break;
            case Button::Left: sel_ = sel_ < 0 ? n - 1 : sel_ - 1; break;
            case Button::Enter:
                if (sel_ < 0) back();
                else if (level_ == 0) {
                    cat_ = sel_;
                    level_ = 1;
                    sel_ = 0;
                } else {
                    entry_ = sel_;
                    level_ = 2;
                    scroll_ = 0;
                }
                break;
            default: break;
        }
    }

    void render(Gfx& g) override {
        g.button(backButtonRect(), "Back", 34, sel_ < 0 && level_ < 2);
        drawTitle(g, "Ship's Database");
        if (cats_.empty()) {
            g.text("DATABASE FILE NOT FOUND", kW / 2, 250, 34, pal::RED, Align::Center);
            g.text("Expected data/library.txt", kW / 2, 300, 22, pal::YELLOW, Align::Center);
            return;
        }
        if (level_ == 0) {
            g.text("SELECT CATEGORY", 18, 84, 22, pal::GREEN);
            for (int i = 0; i < int(cats_.size()); ++i) {
                SDL_Rect r{120, 118 + i * 72, kW - 240, 60};
                g.button(r, cats_[i].name, 34, i == sel_);
                std::string n = std::to_string(cats_[i].entries.size());
                g.text(n, r.x + r.w + 14, r.y + 16, 22, pal::DIM_YELLOW);
            }
        } else if (level_ == 1) {
            const Category& c = cats_[cat_];
            g.text(c.name, 18, 84, 22, pal::GREEN);
            int colW = (kW - 28 - 14) / 2;
            for (int i = 0; i < int(c.entries.size()) && i < 14; ++i) {
                SDL_Rect r{14 + (i % 2) * (colW + 14), 118 + (i / 2) * 62, colW, 52};
                g.button(r, c.entries[i].title, 26, i == sel_);
            }
        } else {
            const Category& c = cats_[cat_];
            const Entry& e = c.entries[entry_];
            g.text(c.name + "  " + std::to_string(entry_ + 1) + " / " + std::to_string(c.entries.size()), 18, 84, 22,
                   pal::GREEN);
            SDL_Rect box{18, 116, kW - 36, kH - 140};
            g.outline(box, pal::YELLOW, 2);
            g.text(g.ellipsize(e.title, 34, box.w - 32), box.x + 16, box.y + 10, 34, pal::YELLOW);
            auto lines = g.wrap(e.text, 26, box.w - 32);
            const int lh = 32, visible = (box.h - 70) / lh;
            maxScroll_ = std::max(0, int(lines.size()) - visible);
            scroll_ = std::min(scroll_, maxScroll_);
            for (int i = 0; i < visible && scroll_ + i < int(lines.size()); ++i)
                g.text(lines[scroll_ + i], box.x + 16, box.y + 58 + i * lh, 26, pal::GREEN);
            if (scroll_ > 0) g.triangle({kW - 50.f, box.y + 24.f}, {kW - 30.f, box.y + 24.f}, {kW - 40.f, box.y + 12.f}, pal::YELLOW);
            if (scroll_ < maxScroll_)
                g.triangle({kW - 50.f, box.y + box.h - 22.f}, {kW - 30.f, box.y + box.h - 22.f}, {kW - 40.f, box.y + box.h - 10.f}, pal::YELLOW);
        }
    }

private:
    std::vector<Category> cats_;
    int level_ = 0;  // 0 categories, 1 entries, 2 reading
    int sel_ = 0, cat_ = 0, entry_ = 0, scroll_ = 0, maxScroll_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeDatabaseScreen(App& a) { return std::make_unique<DatabaseScreen>(a); }
