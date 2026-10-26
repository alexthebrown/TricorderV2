// SELECT -> Library Computer: the menu for the reference and utility pages.
#include "../app.h"
#include "../widgets.h"

namespace {

class LibraryMenu : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        int keep = buttons_.size() ? buttons_.focus() : 1;
        buttons_.clear();
        buttons_.add(backButtonRect(), "Back", 34, [this] { app.go(ScreenId::MainMenu); });
        struct Item {
            const char* label;
            ScreenId id;
        };
        static const Item items[] = {
            {"CHRONOMETER", ScreenId::Chrono},        {"STELLAR CARTOGRAPHY", ScreenId::Cartography},
            {"ORBITAL TRACKING", ScreenId::Orbital},  {"SHIP'S DATABASE", ScreenId::Database},
            {"TACTICAL SIMULATION", ScreenId::Tactical}, {"SUBSPACE MESSAGES", ScreenId::Messages},
        };
        const int colW = (kW - 28 - 16) / 2, h = 82, top = 116;
        for (int i = 0; i < 6; ++i) {
            SDL_Rect r{14 + (i % 2) * (colW + 16), top + (i / 2) * (h + 22), colW, h};
            ScreenId id = items[i].id;
            buttons_.add(r, items[i].label, 34, [this, id] { app.go(id); });
        }
        buttons_.setFocus(keep);
    }
    void input(Button b) override { buttons_.handle(b); }

    void render(Gfx& g) override {
        buttons_.render(g);
        drawTitle(g, "Library Computer");
        g.text("SELECT FUNCTION", 18, 84, 22, pal::GREEN);
        int unread = app.unreadMessages();
        size_t pending = app.subspace.pending().size();
        if (pending > 0) {
            g.text(std::to_string(pending) + " AWAITING CLEARANCE", kW - 18, 84, 22, pal::RED, Align::Right);
        } else if (unread > 0) {
            std::string m = std::to_string(unread) + (unread == 1 ? " NEW TRANSMISSION" : " NEW TRANSMISSIONS");
            g.text(m, kW - 18, 84, 22, pal::RED, Align::Right);
        }
        if (app.cfg.lights) drawLights(g, kW / 2 - 51, kH - 40, 6, app.time());
    }

private:
    ButtonGroup buttons_;
};

}  // namespace

std::unique_ptr<Screen> makeLibraryMenu(App& a) { return std::make_unique<LibraryMenu>(a); }
