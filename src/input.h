#pragma once
// The tricorder has five physical buttons. On the desktop the keyboard stands in for them.
#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "button.h"

struct Config;

// Reads the five buttons through the Linux GPIO character device (no libgpiod needed).
class GpioButtons {
public:
    ~GpioButtons();
    bool open(const std::string& chip, const std::vector<int>& pins, std::string& err);
    // Bit i set = button i held down (pins are active-low).
    bool read(uint32_t& heldMask);
    bool isOpen() const { return lineFd_ >= 0; }

private:
    int lineFd_ = -1;
    int count_ = 0;
};

class Input {
public:
    void init(const Config& cfg);
    void handleEvent(const SDL_Event& e, std::vector<Button>& out);
    void poll(uint32_t nowMs, std::vector<Button>& out);
    bool gpioActive() const { return gpio_.isOpen(); }

private:
    struct PinState {
        bool raw = false, stable = false;
        uint32_t rawSince = 0, pressedAt = 0, lastRepeat = 0;
        bool longFired = false;
    };
    GpioButtons gpio_;
    PinState pins_[5];
};
