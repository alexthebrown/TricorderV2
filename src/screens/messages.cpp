// LIBRARY COMPUTER -> Subspace Messages: transmissions from the local web page and the
// visitor relay. Relay messages wait (red) until cleared: open one, ENTER puts it on
// screen, LEFT rejects it. With the relay on, a QR code shows visitors where to send.
#include <algorithm>
#include <ctime>

#include "../app.h"
#include "../widgets.h"

namespace {

struct Item {
    SubspaceMessage m;
    bool pending = false;
};

class MessagesScreen : public Screen {
public:
    using Screen::Screen;
    void enter() override {
        sel_ = -1;
        open_ = false;
        first_ = 0;
    }
    void back() override {
        if (open_) open_ = false;
        else app.go(ScreenId::Library);
    }

    void input(Button b) override {
        auto items = collect();
        int n = int(items.size());
        if (open_) {
            if (sel_ < 0 || sel_ >= n) {
                open_ = false;
                return;
            }
            const Item& it = items[sel_];
            if (it.pending && b == Button::Enter) {
                app.subspace.approve(it.m.id);  // it now pops up as an incoming transmission
                open_ = false;
            } else if (it.pending && b == Button::Left) {
                app.subspace.reject(it.m.id);
                app.toast("TRANSMISSION REJECTED");
                open_ = false;
                sel_ = std::min(sel_, n - 2);
            } else if (b == Button::Enter) {
                open_ = false;
            } else if (!it.pending && b == Button::Down && sel_ + 1 < n) {
                ++sel_;
                markRead(items);
            } else if (!it.pending && b == Button::Up && sel_ > 0) {
                --sel_;
                markRead(items);
            }
            return;
        }
        switch (b) {
            case Button::Enter:
                if (sel_ < 0) app.go(ScreenId::Library);
                else if (sel_ < n) {
                    open_ = true;
                    markRead(items);
                }
                break;
            case Button::Down: case Button::Right: sel_ = n == 0 ? -1 : (sel_ + 1 >= n ? -1 : sel_ + 1); break;
            case Button::Up: case Button::Left: sel_ = n == 0 ? -1 : (sel_ < 0 ? n - 1 : sel_ - 1); break;
            default: break;
        }
        int rows = visibleRows();
        if (sel_ >= 0) {
            if (sel_ < first_) first_ = sel_;
            if (sel_ >= first_ + rows) first_ = sel_ - rows + 1;
        }
    }

    void render(Gfx& g) override {
        g.button(backButtonRect(), "Back", 34, sel_ < 0 && !open_);
        drawTitle(g, "Subspace Messages");
        std::string url = app.visitorUrl();
        bool relayOn = !url.empty();

        // Status line: local page address and relay state.
        std::string addr = app.subspaceAddress();
        std::string line = addr.empty() ? "LOCAL LINK: " + std::string(app.subspace.running() ? "NO NETWORK" : "OFF")
                                        : "LOCAL LINK: " + addr;
        g.text(g.ellipsize(line, 18, kW - 36), 18, 80, 18, addr.empty() ? pal::DIM_YELLOW : pal::GREEN);
        if (relayOn) {
            std::string st = app.relay.status();
            g.text("VISITOR RELAY: " + st, 18, 100, 18, st == "ONLINE" ? pal::GREEN : pal::RED);
        }
        std::string up = app.subspace.uploadStatus();
        if (!up.empty()) g.text(up, kW - 18, 100, 18, pal::YELLOW, Align::Right);

        auto items = collect();
        int listW = relayOn ? kW - 28 - 236 : kW - 28;
        if (relayOn) {
            int qx = kW - 18 - 220;
            drawQr(g, url, qx, 128, 220);
            g.text("SCAN TO TRANSMIT", qx + 110, 352, 22, pal::YELLOW, Align::Center);
            if (!app.cfg.smsNumber.empty()) {
                g.text("OR TEXT \"TRICORDER\"", qx + 110, 384, 18, pal::GREEN, Align::Center);
                g.text("+ MESSAGE TO", qx + 110, 406, 18, pal::GREEN, Align::Center);
                g.text(app.cfg.smsNumber, qx + 110, 430, 22, pal::YELLOW, Align::Center);
            }
        }

        if (items.empty()) {
            int cx = 14 + listW / 2;
            g.text("NO TRANSMISSIONS", cx, 230, 30, pal::YELLOW, Align::Center);
            g.text(relayOn ? "Visitors scan the code to send one" : "Open the local link on a phone to send one", cx,
                   276, 20, pal::DIM_YELLOW, Align::Center);
            return;
        }
        if (open_ && sel_ >= 0 && sel_ < int(items.size())) {
            renderOpen(g, items[sel_]);
            return;
        }
        int rows = visibleRows();
        for (int row = 0; row < rows; ++row) {
            int i = first_ + row;
            if (i >= int(items.size())) break;
            const Item& it = items[i];
            SDL_Rect r{14, 128 + row * 68, listW, 58};
            bool f = i == sel_;
            if (f) g.outline({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, pal::YELLOW, 2);
            Color bg = it.pending ? pal::RED : (it.m.read ? pal::GREEN : pal::YELLOW);
            g.bevel(r, bg, f);
            int off = f ? 1 : 0;
            std::string head = it.pending ? "AWAITING CLEARANCE" : it.m.from;
            g.text(g.ellipsize(head, 22, r.w - 130), r.x + 12 + off, r.y + 4 + off, 22, pal::BLACK);
            g.text(stamp(it.m.when), r.x + r.w - 12 + off, r.y + 6 + off, 16, mix(bg, pal::BLACK, 0.6f), Align::Right);
            // Pending text stays hidden in the list; the operator opens it to review.
            std::string body = it.pending ? "FROM " + it.m.from + " - OPEN TO REVIEW" : it.m.text;
            g.text(g.ellipsize(body, 18, r.w - 24), r.x + 12 + off, r.y + 32 + off, 18, mix(bg, pal::BLACK, 0.6f));
        }
    }

private:
    std::vector<Item> collect() const {
        std::vector<Item> v;
        for (auto& m : app.subspace.pending()) v.push_back({m, true});
        for (auto& m : app.subspace.messages()) v.push_back({m, false});
        return v;
    }

    static int visibleRows() { return 6; }

    void markRead(const std::vector<Item>& items) {
        if (sel_ < 0 || sel_ >= int(items.size()) || items[sel_].pending) return;
        auto msgs = app.subspace.messages();
        for (size_t i = 0; i < msgs.size(); ++i)
            if (msgs[i].id == items[sel_].m.id) app.subspace.markRead(i);
    }

    void renderOpen(Gfx& g, const Item& it) {
        int w = app.visitorUrl().empty() ? kW - 36 : kW - 36 - 236;
        SDL_Rect box{18, 128, w, kH - 150};
        g.outline(box, it.pending ? pal::RED : pal::YELLOW, 2);
        g.text("FROM: " + g.ellipsize(it.m.from, 26, box.w - 200), box.x + 14, box.y + 10, 26, pal::GREEN);
        g.text(stamp(it.m.when), box.x + box.w - 14, box.y + 16, 16, pal::DIM_YELLOW, Align::Right);
        auto lines = g.wrap(it.m.text, 28, box.w - 28);
        int y = box.y + 54;
        int bottom = box.y + box.h - (it.pending ? 70 : 20);
        for (size_t i = 0; i < lines.size() && y < bottom; ++i, y += 34) g.text(lines[i], box.x + 14, y, 28, pal::YELLOW);
        if (it.pending) {
            int bw = (box.w - 42) / 2;
            g.button({box.x + 14, box.y + box.h - 60, bw, 46}, "ENTER: DISPLAY", 24, true);
            SDL_Rect rej{box.x + 28 + bw, box.y + box.h - 60, bw, 46};
            g.bevel(rej, pal::RED, false);
            g.textIn(rej, "LEFT: REJECT", 24, pal::BLACK);
        }
    }

    static std::string stamp(time_t t) {
        tm lt;
        localtime_r(&t, &lt);
        char b[32];
        strftime(b, sizeof b, "%b %d %H:%M", &lt);
        std::string s = b;
        for (auto& c : s) c = char(toupper(static_cast<unsigned char>(c)));
        return s;
    }

    int sel_ = -1, first_ = 0;
    bool open_ = false;
};

}  // namespace

std::unique_ptr<Screen> makeMessagesScreen(App& a) { return std::make_unique<MessagesScreen>(a); }
