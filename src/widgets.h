#pragma once
#include <functional>
#include <string>
#include <vector>

#include "gfx.h"
#include "input.h"

struct UIButton {
    SDL_Rect r{};
    std::string label;
    int px = 40;
    std::function<void()> action;
    bool enabled = true;
};

// A set of focusable buttons. Arrow keys move focus to the nearest button in that
// direction (wrapping like the original when there is none); ENTER activates.
class ButtonGroup {
public:
    int add(SDL_Rect r, const std::string& label, int px, std::function<void()> action);
    void clear() { buttons_.clear(); focus_ = 0; }
    bool handle(Button b);
    void render(Gfx& g, bool showFocus = true) const;
    int focus() const { return focus_; }
    void setFocus(int i) { if (i >= 0 && i < int(buttons_.size())) focus_ = i; }
    UIButton& operator[](int i) { return buttons_[i]; }
    int size() const { return int(buttons_.size()); }
    int neighbour(int from, Button dir) const;

private:
    std::vector<UIButton> buttons_;
    int focus_ = 0;
};

// Lays out a centred row of buttons sized to their labels, shrinking the font to fit.
std::vector<SDL_Rect> layoutRow(Gfx& g, const std::vector<std::string>& labels, int y, int h, int px, int x0,
                                int x1, int gap, int* outPx = nullptr, int padX = 18);

// Standard page header: [Back] + yellow title, as on every page of the original.
SDL_Rect backButtonRect();
void drawTitle(Gfx& g, const std::string& title, int x0 = 150);

// Blinking indicator lamps like the ones on the prop's face plate.
void drawLights(Gfx& g, int x, int y, int count, double t, int size = 12, bool busy = false);

// Yellow "readout" box with a black label/value (planet & status pages).
void drawReadout(Gfx& g, SDL_Rect r, const std::string& label, const std::string& value, int valuePx,
                 double bar = -1, Color valueColor = pal::BLACK);

void drawScope(Gfx& g, SDL_Rect r, const std::vector<float>& samples, Color trace, const std::string& caption);

// QR code (black modules on the tricorder's yellow) fitted into a size x size square.
void drawQr(Gfx& g, const std::string& text, int x, int y, int size);
