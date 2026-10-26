#include "input.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "config.h"

#if defined(__linux__) && __has_include(<linux/gpio.h>)
#include <linux/gpio.h>
#endif

namespace {
// Index order used for pins_ and the GPIO request.
constexpr Button kOrder[5] = {Button::Up, Button::Down, Button::Left, Button::Right, Button::Enter};
constexpr uint32_t kDebounceMs = 25;
constexpr uint32_t kRepeatDelayMs = 420;
constexpr uint32_t kRepeatMs = 140;
constexpr uint32_t kLongPressMs = 700;  // hold ENTER = back
}  // namespace

GpioButtons::~GpioButtons() {
    if (lineFd_ >= 0) close(lineFd_);
}

bool GpioButtons::open(const std::string& chip, const std::vector<int>& pins, std::string& err) {
#ifdef GPIO_V2_GET_LINE_IOCTL
    int chipFd = ::open(chip.c_str(), O_RDONLY | O_CLOEXEC);
    if (chipFd < 0) {
        err = chip + ": " + strerror(errno);
        return false;
    }
    gpio_v2_line_request req;
    memset(&req, 0, sizeof req);
    for (size_t i = 0; i < pins.size(); ++i) req.offsets[i] = unsigned(pins[i]);
    req.num_lines = unsigned(pins.size());
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
    strncpy(req.consumer, "tricorder", sizeof req.consumer - 1);
    int rc = ioctl(chipFd, GPIO_V2_GET_LINE_IOCTL, &req);
    int e = errno;
    close(chipFd);
    if (rc < 0) {
        err = std::string("GPIO line request failed: ") + strerror(e);
        return false;
    }
    lineFd_ = req.fd;
    count_ = int(pins.size());
    return true;
#else
    (void)chip;
    (void)pins;
    err = "built without GPIO v2 support";
    return false;
#endif
}

bool GpioButtons::read(uint32_t& held) {
#ifdef GPIO_V2_LINE_GET_VALUES_IOCTL
    if (lineFd_ < 0) return false;
    gpio_v2_line_values v;
    memset(&v, 0, sizeof v);
    v.mask = (1ull << count_) - 1;
    if (ioctl(lineFd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0) return false;
    held = uint32_t(~v.bits & v.mask);  // pulled up: low = pressed
    return true;
#else
    (void)held;
    return false;
#endif
}

void Input::init(const Config& cfg) {
    if (!cfg.gpio) return;
    std::string err;
    std::vector<int> pins = {cfg.pinUp, cfg.pinDown, cfg.pinLeft, cfg.pinRight, cfg.pinEnter};
    if (gpio_.open(cfg.gpioChip, pins, err))
        fprintf(stderr, "gpio: buttons on %s\n", cfg.gpioChip.c_str());
    else
        fprintf(stderr, "gpio: disabled (%s)\n", err.c_str());
}

void Input::handleEvent(const SDL_Event& e, std::vector<Button>& out) {
    if (e.type != SDL_KEYDOWN) return;
    bool rep = e.key.repeat != 0;
    switch (e.key.keysym.sym) {
        case SDLK_UP: case SDLK_w: out.push_back(Button::Up); break;
        case SDLK_DOWN: case SDLK_s: out.push_back(Button::Down); break;
        case SDLK_LEFT: case SDLK_a: out.push_back(Button::Left); break;
        case SDLK_RIGHT: case SDLK_d: out.push_back(Button::Right); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
            if (!rep) out.push_back(Button::Enter);
            break;
        case SDLK_ESCAPE: case SDLK_BACKSPACE:
            if (!rep) out.push_back(Button::Back);
            break;
        default: break;
    }
}

void Input::poll(uint32_t now, std::vector<Button>& out) {
    uint32_t held;
    if (!gpio_.isOpen() || !gpio_.read(held)) return;
    for (int i = 0; i < 5; ++i) {
        PinState& p = pins_[i];
        bool raw = held & (1u << i);
        if (raw != p.raw) {
            p.raw = raw;
            p.rawSince = now;
        }
        bool isEnter = kOrder[i] == Button::Enter;
        if (raw != p.stable && now - p.rawSince >= kDebounceMs) {
            p.stable = raw;
            if (raw) {
                p.pressedAt = p.lastRepeat = now;
                p.longFired = false;
                if (!isEnter) out.push_back(kOrder[i]);
            } else if (isEnter && !p.longFired) {
                out.push_back(Button::Enter);  // fire on release so a long hold can mean "back"
            }
        } else if (p.stable) {
            if (isEnter) {
                if (!p.longFired && now - p.pressedAt >= kLongPressMs) {
                    p.longFired = true;
                    out.push_back(Button::Back);
                }
            } else if (now - p.pressedAt >= kRepeatDelayMs && now - p.lastRepeat >= kRepeatMs) {
                p.lastRepeat = now;
                out.push_back(kOrder[i]);
            }
        }
    }
}
