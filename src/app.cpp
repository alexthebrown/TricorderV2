#include "app.h"

#include <fstream>

#include "overlays.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

void Screen::back() { app.go(ScreenId::MainMenu); }

App::App(Config& c, const Config& d) : cfg(c), defaults(d) {}

App::~App() {
    for (auto& s : screens_) s.reset();
    relay.stop();
    subspace.stop();
    prober.stop();
    weather.stop();
    library.stop();
    gfx.shutdown();
}

double App::time() const {
    return double(SDL_GetPerformanceCounter() - startCounter_) / double(SDL_GetPerformanceFrequency());
}

void App::go(ScreenId id) {
    Screen* next = screens_[int(id)].get();
    if (!next) return;
    if (cur_) cur_->exit();
    cur_ = next;
    cur_->enter();
}

void App::toast(const std::string& msg, double secs) {
    toast_ = msg;
    toastUntil_ = time() + secs;
}

void App::playVideo(const std::vector<MediaItem>& list, int index) {
    playlist = list;
    playIndex = index;
    go(ScreenId::Player);
}

bool App::saveConfig() {
    bool ok = cfg.save(configFilePath(), defaults);
    toast(ok ? "CONFIGURATION SAVED" : "SAVE FAILED");
    return ok;
}

std::string App::subspaceAddress() const {
    const std::string& ip = sys.info().ip;
    if (!subspace.running() || ip.empty()) return "";
    return "http://" + ip + ":" + std::to_string(subspace.port());
}

void App::setRelay(bool on) {
    cfg.relay = on;
    if (!on) {
        relay.stop();
        return;
    }
    std::string topic = cfg.relayTopic;
    if (topic.empty()) {
        // A private topic name, made once per tricorder; only the QR code reveals it.
        std::string path = stateDir() + "/relay_topic.txt";
        std::ifstream in(path);
        in >> topic;
        if (topic.empty()) {
            topic = randomTopic();
            std::ofstream(path) << topic << "\n";
        }
    }
    relay.start(cfg.relayServer, topic, [this](const RelayMessage& m) {
        if (!m.text.empty()) subspace.addMessage(m.from, m.text, cfg.moderate, "relay");
    });
}

void App::newRelayTopic() {
    if (!cfg.relayTopic.empty()) {
        toast("TOPIC IS FIXED BY relay_topic IN THE CONFIG", 4);
        return;
    }
    std::ofstream(stateDir() + "/relay_topic.txt") << randomTopic() << "\n";
    if (cfg.relay) setRelay(true);
    toast("NEW SUBSPACE FREQUENCY SET");
}

std::string App::visitorUrl() const {
    if (!relay.running()) return "";
    if (!cfg.relayPage.empty()) return cfg.relayPage + "#" + relay.topic();
    return relay.server() + "/" + relay.topic();
}

bool App::redAlertActive() const {
    for (auto& a : weather.alerts())
        if (a.red()) return true;
    return false;
}

void App::checkAlerts() {
    if (redAlert_) return;
    for (auto& a : weather.alerts()) {
        if (!a.red() || ackedAlerts_.count(a.id)) continue;
        activeAlert_ = a;
        redAlert_ = true;
        idle_ = false;
        return;
    }
}

bool App::overlayInput(Button b) {
    if (redAlert_) {
        if (b == Button::Enter || b == Button::Back) {
            redAlert_ = false;
            ackedAlerts_.insert(activeAlert_.id);
            std::ofstream out(stateDir() + "/alerts_acknowledged.txt");
            for (auto& id : ackedAlerts_) out << id << "\n";
        }
        return true;
    }
    if (!transmissions_.empty()) {
        if (b == Button::Enter || b == Button::Back) {
            const SubspaceMessage& t = transmissions_.front();
            auto all = subspace.messages();
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].when == t.when && all[i].text == t.text) subspace.markRead(i);
            transmissions_.pop_front();
        }
        return true;
    }
    if (idle_) {  // any button wakes the tricorder without doing anything else
        idle_ = false;
        return true;
    }
    return false;
}

void App::runScript(double now, std::vector<Button>& presses) {
    while (scriptPos_ < script_.size() && now >= scriptWaitUntil_) {
        const std::string& step = script_[scriptPos_++];
        if (step.rfind("shot:", 0) == 0) {
            pendingShot_ = step.substr(5);  // taken after this frame renders
            return;
        }
        if (step == "quit") {
            running_ = false;
            return;
        }
        if (step.size() == 1) {
            switch (step[0]) {
                case 'U': presses.push_back(Button::Up); break;
                case 'D': presses.push_back(Button::Down); break;
                case 'L': presses.push_back(Button::Left); break;
                case 'R': presses.push_back(Button::Right); break;
                case 'E': presses.push_back(Button::Enter); break;
                case 'B': presses.push_back(Button::Back); break;
                default: break;
            }
            if (!(step[0] >= '0' && step[0] <= '9')) {
                scriptWaitUntil_ = now + 0.15;
                return;
            }
        }
        scriptWaitUntil_ = now + std::atof(step.c_str());
    }
}

int App::run() {
    startCounter_ = SDL_GetPerformanceCounter();
    std::string err;
    if (!gfx.init(cfg, err)) {
        fprintf(stderr, "tricorder: %s\n", err.c_str());
        return 1;
    }
    input.init(cfg);
    weather.start(cfg.weatherUrl, cfg.weatherRefreshMin, cfg.offline, cfg.latitude, cfg.longitude, cfg.alertTest);
    prober.start();
    std::vector<std::string> hidden;
    for (size_t a = 0, b; a <= cfg.hiddenFolders.size(); a = b + 1) {
        b = cfg.hiddenFolders.find(',', a);
        if (b == std::string::npos) b = cfg.hiddenFolders.size();
        std::string h = cfg.hiddenFolders.substr(a, b - a);
        while (!h.empty() && h.front() == ' ') h.erase(0, 1);
        while (!h.empty() && h.back() == ' ') h.pop_back();
        if (!h.empty()) hidden.push_back(h);
    }
    library.start(cfg.videoDir,
                  {"sample_data/videos", exeDir() + "/sample_data/videos", exeDir() + "/../sample_data/videos",
                   homeDir() + "/Videos"},
                  hidden);
    sys.update(homeDir());
    subspace.start(cfg.webPort, cfg.webPin,
                   [this] {
                       if (!cfg.uploadDir.empty()) return cfg.uploadDir;
                       std::string root = library.root();
                       return root.empty() ? root : root + "/Captain's Logs";
                   },
                   gfx.fontPath());
    subspace.setProfanityFilter(cfg.profanityFilter);
    subspace.setRelayInfo([this] {
        return relay.running() ? std::make_pair(relay.server(), relay.topic()) : std::pair<std::string, std::string>();
    });
    if (cfg.relay) setRelay(true);

    screens_[int(ScreenId::Boot)] = makeBootScreen(*this);
    screens_[int(ScreenId::MainMenu)] = makeMainMenu(*this);
    screens_[int(ScreenId::Planet)] = makePlanetScreen(*this);
    screens_[int(ScreenId::CaptainsLog)] = makeCaptainsLog(*this);
    screens_[int(ScreenId::Player)] = makePlayerScreen(*this);
    screens_[int(ScreenId::Status)] = makeStatusScreen(*this);
    screens_[int(ScreenId::Sensors)] = makeSensorScreen(*this);
    screens_[int(ScreenId::Chrono)] = makeChronoScreen(*this);
    screens_[int(ScreenId::Settings)] = makeSettingsScreen(*this);
    screens_[int(ScreenId::Library)] = makeLibraryMenu(*this);
    screens_[int(ScreenId::Cartography)] = makeCartographyScreen(*this);
    screens_[int(ScreenId::Orbital)] = makeOrbitalScreen(*this);
    screens_[int(ScreenId::Database)] = makeDatabaseScreen(*this);
    screens_[int(ScreenId::Tactical)] = makeTacticalScreen(*this);
    screens_[int(ScreenId::Messages)] = makeMessagesScreen(*this);
    go(cfg.bootSequence ? ScreenId::Boot : ScreenId::MainMenu);

    for (size_t a = 0, b; a < cfg.script.size(); a = b + 1) {
        b = cfg.script.find(',', a);
        if (b == std::string::npos) b = cfg.script.size();
        if (b > a) script_.push_back(cfg.script.substr(a, b - a));
    }

    {
        std::ifstream in(stateDir() + "/alerts_acknowledged.txt");
        std::string id;
        while (std::getline(in, id))
            if (!id.empty()) ackedAlerts_.insert(id);
    }

    double last = time();
    lastInput_ = last;
    std::vector<Button> presses;
    while (running_) {
        presses.clear();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running_ = false;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                    gfx.toggleFullscreen();
                    continue;
                }
                if (k == SDLK_q && (e.key.keysym.mod & KMOD_CTRL)) running_ = false;
            }
            input.handleEvent(e, presses);
        }
        double now = time();
        input.poll(uint32_t(now * 1000), presses);
        if (!script_.empty()) runScript(now, presses);
        for (Button rb; subspace.popButton(rb);) presses.push_back(rb);
        // Tell the operator (not the crowd) that something is waiting for clearance.
        size_t pend = subspace.pending().size();
        if (pend > lastPending_) toast("TRANSMISSION AWAITING CLEARANCE");
        lastPending_ = pend;
        for (SubspaceMessage m; subspace.popIncoming(m);) {
            transmissions_.push_back(m);
            idle_ = false;
        }

        for (Button b : presses) {
            lastInput_ = now;
            if (overlayInput(b)) continue;
            Screen* s = cur_;
            if (b == Button::Back) s->back();
            else s->input(b);
        }
        alertTimer_ -= now - last;
        if (alertTimer_ <= 0) {
            checkAlerts();
            alertTimer_ = 1.0;
        }
        if (!idle_ && cfg.idleMinutes > 0 && cur_->allowIdle() && now - lastInput_ > cfg.idleMinutes * 60.0) {
            idle_ = true;
            prober.scanNow();  // fresh life signs / subspace counts for the standby display
        }

        double dt = std::min(0.1, now - last);
        last = now;
        sysTimer_ -= dt;
        if (sysTimer_ <= 0) {
            sys.update(homeDir());  // local disk only: a network mount could stall the UI
            sysTimer_ = 1.0;
        }
        cur_->update(dt);

        gfx.beginFrame();
        if (idle_) drawIdle(*this, gfx, now);
        else cur_->render(gfx);
        if (!transmissions_.empty()) drawTransmission(gfx, transmissions_.front(), now);
        if (redAlert_) drawRedAlert(gfx, activeAlert_, now);
        if (!toast_.empty() && now < toastUntil_) {
            int px = 26;
            int w = std::min(kW - 40, gfx.textWidth(toast_, px) + 40);
            SDL_Rect r{(kW - w) / 2, kH - 92, w, 46};
            gfx.fill(r, pal::BLACK);
            gfx.outline(r, pal::YELLOW, 2);
            gfx.textIn(r, toast_, std::min(px, gfx.fitSize(toast_, w - 20, px)), pal::YELLOW);
        }
        if (!pendingShot_.empty()) {
            if (!gfx.screenshot(pendingShot_)) fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
            pendingShot_.clear();
        }
        gfx.endFrame();

        // Menus are cheap at 30 fps; video and animations get the full refresh rate.
        bool fast = cur_->animating() || idle_ || redAlert_ || !transmissions_.empty();
        double budget = fast ? (gfx.vsync() ? 0.0 : 1.0 / 60) : 1.0 / 30;
        double spent = time() - now;
        if (spent < budget) SDL_Delay(Uint32((budget - spent) * 1000));
    }
    if (cur_) cur_->exit();
    return 0;
}
