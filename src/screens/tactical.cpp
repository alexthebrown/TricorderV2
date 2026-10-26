// LIBRARY COMPUTER -> Tactical Simulation: the classic 1971 "Star Trek" game (Mike
// Mayfield's original rules) adapted for five buttons. Destroy every Klingon in the
// galaxy before time runs out; dock at starbases (>!<) to refuel.
//
// Courses use the original compass: 1 = right, 3 = up, 5 = left, 7 = down. When aiming,
// LEFT/RIGHT turn the course and UP/DOWN set warp or energy; the path is drawn on the scan.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <random>

#include "../app.h"
#include "../widgets.h"

namespace {

enum Device { Warp, SRS, LRS, Phasers, Torpedoes, DamageCtl, ShieldCtl, Computer, DeviceCount };
const char* kDevices[] = {"WARP ENGINES", "S.R. SENSORS", "L.R. SENSORS", "PHASER CONTROL",
                          "PHOTON TUBES", "DAMAGE CONTROL", "SHIELD CONTROL", "LIBRARY COMPUTER"};

enum Cell { Empty, Ship, Klingon, Base, Star };

struct KShip {
    int r, c;
    double energy;
};

class TacticalScreen : public Screen {
public:
    using Screen::Screen;

    void enter() override {
        if (!started_) newGame();
        buildButtons();
    }
    bool animating() const override { return mode_ == Mode::Aim || app.time() < trailUntil_; }
    void back() override {
        if (mode_ != Mode::Command && mode_ != Mode::Over) mode_ = Mode::Command;
        else app.go(ScreenId::Library);
    }

    void input(Button b) override {
        switch (mode_) {
            case Mode::Command: buttons_.handle(b); break;
            case Mode::Aim: aimInput(b); break;
            case Mode::Amount: amountInput(b); break;
            case Mode::Scan:
            case Mode::Map: mode_ = Mode::Command; break;
            case Mode::Over:
                if (b == Button::Enter) {
                    newGame();
                    mode_ = Mode::Command;
                }
                break;
        }
    }

    void render(Gfx& g) override {
        if (mode_ == Mode::Over) g.button(backButtonRect(), "Back", 34, false);
        drawTitle(g, "Tactical Simulation");
        drawScan(g);
        drawStatus(g);
        drawLog(g);
        if (mode_ == Mode::Command) buttons_.render(g);
        else if (mode_ == Mode::Aim) drawAimPanel(g);
        else if (mode_ == Mode::Amount) drawAmountPanel(g);
        else if (mode_ == Mode::Scan) drawLongRange(g);
        else if (mode_ == Mode::Map) drawGalaxyMap(g);
        if (mode_ == Mode::Over) {
            SDL_Rect r{kW / 2 - 240, 462, 480, 100};
            g.fill(r, pal::BLACK);
            g.outline(r, won_ ? pal::GREEN : pal::RED, 3);
            g.textIn({r.x, r.y + 6, r.w, 44}, won_ ? "MISSION ACCOMPLISHED" : "MISSION FAILED", 36,
                     won_ ? pal::GREEN : pal::RED);
            g.textIn({r.x, r.y + 52, r.w, 40}, "PRESS ENTER FOR A NEW MISSION", 22, pal::YELLOW);
        }
    }

private:
    enum class Mode { Command, Aim, Amount, Scan, Map, Over };
    enum class Action { Nav, Torpedo, Phasers, Shields };

    // ------------------------------------------------------------------ setup
    double rnd() { return std::uniform_real_distribution<double>(0, 1)(rng_); }
    int irand(int n) { return int(rnd() * n) % n; }

    void newGame() {
        rng_.seed(std::random_device{}());
        started_ = true;
        won_ = false;
        totalK_ = 0;
        int bases = 0;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                double x = rnd();
                gK_[r][c] = x > 0.98 ? 3 : x > 0.95 ? 2 : x > 0.80 ? 1 : 0;
                gB_[r][c] = rnd() > 0.96 ? 1 : 0;
                gS_[r][c] = irand(8) + 1;
                known_[r][c] = false;
                totalK_ += gK_[r][c];
                bases += gB_[r][c];
            }
        if (bases == 0) gB_[irand(8)][irand(8)] = 1;
        if (totalK_ == 0) {
            gK_[irand(8)][irand(8)] = 1;
            totalK_ = 1;
        }
        startK_ = totalK_;
        t0_ = t_ = double((irand(20) + 20) * 100);
        days_ = std::max(25 + irand(10), totalK_ + 2);
        energy_ = 3000;
        shields_ = 0;
        torps_ = 10;
        for (auto& d : damage_) d = 0;
        qr_ = irand(8);
        qc_ = irand(8);
        log_.clear();
        say("YOUR ORDERS: DESTROY THE " + std::to_string(totalK_) + " KLINGON WARSHIPS");
        say("BEFORE STARDATE " + std::to_string(int(t0_ + days_)) + ".  GOOD LUCK, CAPTAIN.");
        enterQuadrant();
        mode_ = Mode::Command;
    }

    void enterQuadrant() {
        for (auto& row : grid_)
            for (auto& c : row) c = Empty;
        ks_.clear();
        known_[qr_][qc_] = true;
        auto place = [&](Cell what) {
            for (;;) {
                int r = irand(8), c = irand(8);
                if (grid_[r][c] == Empty) {
                    grid_[r][c] = what;
                    return std::make_pair(r, c);
                }
            }
        };
        auto me = place(Ship);
        sr_ = me.first;
        sc_ = me.second;
        for (int i = 0; i < gK_[qr_][qc_]; ++i) {
            auto p = place(Klingon);
            ks_.push_back({p.first, p.second, 200 * (0.5 + rnd())});
        }
        for (int i = 0; i < gB_[qr_][qc_]; ++i) place(Base);
        for (int i = 0; i < gS_[qr_][qc_]; ++i) place(Star);
        if (!ks_.empty()) {
            say("COMBAT AREA - CONDITION RED");
            if (shields_ < 200) say("SHIELDS DANGEROUSLY LOW");
        }
        checkDocked();
    }

    void buildButtons() {
        buttons_.clear();
        buttons_.add(backButtonRect(), "Back", 34, [this] { app.go(ScreenId::Library); });
        const char* labels[] = {"NAVIGATE", "PHASERS", "TORPEDO", "SHIELDS", "LONG RANGE", "GALAXY MAP"};
        int w = (kW - 28 - 20) / 3;
        for (int i = 0; i < 6; ++i) {
            SDL_Rect r{14 + (i % 3) * (w + 10), 470 + (i / 3) * 54, w, 46};
            buttons_.add(r, labels[i], 28, [this, i] { command(i); });
        }
        buttons_.setFocus(1);
    }

    // ------------------------------------------------------------------ commands
    void command(int i) {
        switch (i) {
            case 0: startAim(Action::Nav); break;
            case 1:
                if (damage_[Phasers] > 0) return say("PHASER CONTROL IS DISABLED");
                if (ks_.empty()) return say("SENSORS SHOW NO ENEMY SHIPS IN THIS QUADRANT");
                action_ = Action::Phasers;
                amount_ = std::min(500, int(energy_));
                mode_ = Mode::Amount;
                break;
            case 2:
                if (damage_[Torpedoes] > 0) return say("PHOTON TUBES ARE NOT OPERATIONAL");
                if (torps_ <= 0) return say("ALL PHOTON TORPEDOES EXPENDED");
                startAim(Action::Torpedo);
                break;
            case 3:
                if (damage_[ShieldCtl] > 0) return say("SHIELD CONTROL INOPERABLE");
                action_ = Action::Shields;
                amount_ = int(shields_);
                mode_ = Mode::Amount;
                break;
            case 4:
                if (damage_[LRS] > 0) return say("LONG RANGE SENSORS ARE INOPERABLE");
                for (int r = qr_ - 1; r <= qr_ + 1; ++r)
                    for (int c = qc_ - 1; c <= qc_ + 1; ++c)
                        if (r >= 0 && r < 8 && c >= 0 && c < 8) known_[r][c] = true;
                mode_ = Mode::Scan;
                break;
            case 5:
                if (damage_[Computer] > 0) return say("COMPUTER DISABLED");
                mode_ = Mode::Map;
                break;
        }
    }

    void startAim(Action a) {
        action_ = a;
        if (a == Action::Torpedo && !ks_.empty()) {
            // Start aimed at the nearest Klingon; the player can adjust.
            auto k = std::min_element(ks_.begin(), ks_.end(), [&](const KShip& a, const KShip& b) {
                return distance(a.r, a.c) < distance(b.r, b.c);
            });
            course_ = courseTo(k->r, k->c);
        }
        mode_ = Mode::Aim;
    }

    double courseTo(int r, int c) const {
        double ang = std::atan2(double(sr_ - r), double(c - sc_)) * 180 / M_PI;
        if (ang < 0) ang += 360;
        double crs = 1 + ang / 45;
        return std::round(crs * 4) / 4;  // quarter-course steps
    }

    void aimInput(Button b) {
        if (b == Button::Left) course_ += 0.25;  // counter-clockwise
        else if (b == Button::Right) course_ -= 0.25;
        else if (b == Button::Up && action_ == Action::Nav) warp_ = std::min(8.0, warp_ + (warp_ < 1 ? 0.125 : 1.0));
        else if (b == Button::Down && action_ == Action::Nav) warp_ = std::max(0.125, warp_ - (warp_ <= 1 ? 0.125 : 1.0));
        else if (b == Button::Enter) {
            mode_ = Mode::Command;
            if (action_ == Action::Nav) navigate(course_, warp_);
            else fireTorpedo(course_);
        }
        if (course_ >= 9) course_ -= 8;
        if (course_ < 1) course_ += 8;
    }

    void amountInput(Button b) {
        int maxV = action_ == Action::Phasers ? int(energy_) : int(energy_ + shields_);
        if (b == Button::Up) amount_ += 100;
        else if (b == Button::Down) amount_ -= 100;
        else if (b == Button::Right) amount_ += 10;
        else if (b == Button::Left) amount_ -= 10;
        amount_ = std::clamp(amount_, 0, maxV);
        if (b == Button::Enter) {
            mode_ = Mode::Command;
            if (action_ == Action::Phasers) firePhasers(amount_);
            else setShields(amount_);
        }
    }

    // Direction vector (row, col) for a course, interpolated between compass points.
    static void vector(double course, double& dr, double& dc) {
        double ang = (course - 1) * 45 * M_PI / 180;
        dc = std::cos(ang);
        dr = -std::sin(ang);
        double m = std::max(std::fabs(dc), std::fabs(dr));  // step one sector at a time along the major axis
        dc /= m;
        dr /= m;
    }

    void navigate(double course, double warp) {
        if (damage_[Warp] > 0 && warp > 0.2) return say("WARP ENGINES ARE DAMAGED.  MAXIMUM SPEED = WARP 0.2");
        int n = int(warp * 8 + 0.5);
        if (energy_ < n + 10) {
            say("ENGINEERING: INSUFFICIENT ENERGY FOR THAT MANEUVER");
            if (shields_ > 0 && damage_[ShieldCtl] <= 0) say("DEFLECTOR CONTROL ROOM: " + std::to_string(int(shields_)) + " UNITS IN SHIELDS");
            return;
        }
        klingonsFire();  // they get a shot as you power up
        if (mode_ == Mode::Over) return;
        repairAndMishaps(warp >= 1 ? 1.0 : warp);

        double dr, dc;
        vector(course, dr, dc);
        grid_[sr_][sc_] = Empty;
        double r = sr_, c = sc_;
        bool leftQuadrant = false;
        for (int i = 0; i < n; ++i) {
            r += dr;
            c += dc;
            int ir = int(std::lround(r)), ic = int(std::lround(c));
            if (ir < 0 || ir > 7 || ic < 0 || ic > 7) {
                leftQuadrant = true;
                // Absolute galactic position after the full move.
                double ar = qr_ * 8 + sr_ + dr * n, ac = qc_ * 8 + sc_ + dc * n;
                bool edge = false;
                if (ar < 0) { ar = 0; edge = true; }
                if (ac < 0) { ac = 0; edge = true; }
                if (ar > 63) { ar = 63; edge = true; }
                if (ac > 63) { ac = 63; edge = true; }
                if (edge) say("LT. UHURA: STARFLEET FORBIDS LEAVING THE GALAXY - STOPPED AT PERIMETER");
                qr_ = int(ar) / 8;
                qc_ = int(ac) / 8;
                break;
            }
            if (grid_[ir][ic] != Empty) {
                say("WARP ENGINES SHUT DOWN AT SECTOR " + sector(int(std::lround(r - dr)), int(std::lround(c - dc))) +
                    " DUE TO BAD NAVIGATION");
                r -= dr;
                c -= dc;
                break;
            }
        }
        energy_ -= n + 10;
        if (energy_ < 0) {
            shields_ += energy_;
            energy_ = 0;
        }
        t_ += warp >= 1 ? 1.0 : 0.1 * int(10 * warp + 0.5);
        if (leftQuadrant) {
            say("ENTERING QUADRANT " + quadrant(qr_, qc_));
            enterQuadrant();
        } else {
            sr_ = int(std::lround(r));
            sc_ = int(std::lround(c));
            grid_[sr_][sc_] = Ship;
            checkDocked();
        }
        checkEnd();
    }

    void fireTorpedo(double course) {
        --torps_;
        double dr, dc;
        vector(course, dr, dc);
        double r = sr_, c = sc_;
        trail_.clear();
        say("TORPEDO TRACK:");
        for (;;) {
            r += dr;
            c += dc;
            int ir = int(std::lround(r)), ic = int(std::lround(c));
            if (ir < 0 || ir > 7 || ic < 0 || ic > 7) {
                say("TORPEDO MISSED");
                break;
            }
            trail_.push_back({ir, ic});
            Cell hit = grid_[ir][ic];
            if (hit == Klingon) {
                say("*** KLINGON DESTROYED ***");
                destroyKlingon(ir, ic);
                break;
            }
            if (hit == Star) {
                say("STAR AT " + sector(ir, ic) + " ABSORBED TORPEDO ENERGY");
                break;
            }
            if (hit == Base) {
                say("*** STARBASE DESTROYED ***  STARFLEET IS NOT PLEASED");
                grid_[ir][ic] = Empty;
                gB_[qr_][qc_] = 0;
                docked_ = false;
                break;
            }
        }
        trailUntil_ = app.time() + 1.5;
        if (totalK_ > 0) klingonsFire();
        checkEnd();
    }

    void firePhasers(int x) {
        if (x <= 0) return;
        energy_ -= x;
        double power = x;
        if (damage_[Computer] > 0) {
            power *= rnd();
            say("COMPUTER FAILURE HAMPERS ACCURACY");
        }
        double each = power / double(ks_.size());
        auto targets = ks_;
        for (const KShip& k : targets) {
            double h = (each / distance(k.r, k.c)) * (rnd() + 2);
            auto it = std::find_if(ks_.begin(), ks_.end(), [&](const KShip& o) { return o.r == k.r && o.c == k.c; });
            if (it == ks_.end()) continue;
            if (h <= 0.15 * it->energy) {
                say("SENSORS SHOW NO DAMAGE TO ENEMY AT " + sector(k.r, k.c));
                continue;
            }
            it->energy -= h;
            say(std::to_string(int(h)) + " UNIT HIT ON KLINGON AT " + sector(k.r, k.c));
            if (it->energy <= 0) {
                say("*** KLINGON DESTROYED ***");
                destroyKlingon(k.r, k.c);
            }
        }
        if (totalK_ > 0) klingonsFire();
        checkEnd();
    }

    void setShields(int x) {
        double total = energy_ + shields_;
        shields_ = x;
        energy_ = total - x;
        say("DEFLECTOR CONTROL: SHIELDS NOW AT " + std::to_string(x) + " UNITS");
    }

    void destroyKlingon(int r, int c) {
        grid_[r][c] = Empty;
        ks_.erase(std::remove_if(ks_.begin(), ks_.end(), [&](const KShip& k) { return k.r == r && k.c == c; }),
                  ks_.end());
        --gK_[qr_][qc_];
        --totalK_;
    }

    void klingonsFire() {
        if (ks_.empty()) return;
        if (docked_) {
            say("STARBASE SHIELDS PROTECT THE ENTERPRISE");
            return;
        }
        for (KShip& k : ks_) {
            if (k.energy <= 0) continue;
            double h = (k.energy / distance(k.r, k.c)) * (2 + rnd());
            shields_ -= h;
            k.energy /= 3 + rnd();
            say(std::to_string(int(h)) + " UNIT HIT ON ENTERPRISE FROM " + sector(k.r, k.c));
            if (shields_ < 0) {
                say("THE ENTERPRISE HAS BEEN DESTROYED.");
                endGame(false);
                return;
            }
            if (h >= 20 && rnd() <= 0.6 && h / std::max(1.0, shields_) > 0.02) {
                int d = irand(DeviceCount);
                damage_[d] += h / std::max(1.0, shields_) + 0.5 * rnd();
                say("DAMAGE CONTROL: " + std::string(kDevices[d]) + " DAMAGED BY THE HIT");
            }
        }
    }

    void repairAndMishaps(double time) {
        for (int d = 0; d < DeviceCount; ++d) {
            if (damage_[d] <= 0) continue;
            damage_[d] -= time;
            if (damage_[d] <= 0) {
                damage_[d] = 0;
                say("DAMAGE CONTROL: " + std::string(kDevices[d]) + " REPAIR COMPLETED");
            }
        }
        if (rnd() <= 0.2) {  // random space phenomena
            int d = irand(DeviceCount);
            if (rnd() < 0.6) {
                damage_[d] += rnd() * 5 + 1;
                say("DAMAGE CONTROL: " + std::string(kDevices[d]) + " DAMAGED BY SPACE STORM");
            } else if (damage_[d] > 0) {
                damage_[d] = std::max(0.0, damage_[d] - (rnd() * 3 + 1));
                say("DAMAGE CONTROL: " + std::string(kDevices[d]) + " STATE OF REPAIR IMPROVED");
            }
        }
    }

    void checkDocked() {
        docked_ = false;
        for (int r = sr_ - 1; r <= sr_ + 1; ++r)
            for (int c = sc_ - 1; c <= sc_ + 1; ++c)
                if (r >= 0 && r < 8 && c >= 0 && c < 8 && grid_[r][c] == Base) docked_ = true;
        if (docked_) {
            energy_ = 3000;
            torps_ = 10;
            shields_ = 0;
            for (auto& d : damage_) d = 0;
            say("DOCKED: RESUPPLIED AND REPAIRED. SHIELDS DROPPED FOR DOCKING");
        }
    }

    void checkEnd() {
        if (mode_ == Mode::Over) return;
        if (totalK_ <= 0) {
            say("THE LAST KLINGON WARSHIP HAS BEEN DESTROYED.  THE FEDERATION IS SAVED!");
            int rating = int(1000.0 * startK_ / std::max(0.1, t_ - t0_));
            say("YOUR EFFICIENCY RATING IS " + std::to_string(rating));
            endGame(true);
        } else if (t_ > t0_ + days_) {
            say("IT IS STARDATE " + std::to_string(int(t_)) + ". TIME HAS RUN OUT.");
            say(std::to_string(totalK_) + " KLINGON WARSHIPS REMAIN.");
            endGame(false);
        } else if (energy_ + shields_ <= 10 && !docked_) {
            say("FATAL ERROR: THE ENTERPRISE IS DEAD IN SPACE.");
            endGame(false);
        }
    }

    void endGame(bool won) {
        won_ = won;
        mode_ = Mode::Over;
    }

    // ------------------------------------------------------------------ helpers
    double distance(int r, int c) const { return std::max(1.0, std::hypot(double(r - sr_), double(c - sc_))); }
    static std::string sector(int r, int c) { return std::to_string(r + 1) + "," + std::to_string(c + 1); }
    static std::string quadrant(int r, int c) { return std::to_string(r + 1) + "," + std::to_string(c + 1); }
    void say(const std::string& s) {
        log_.push_back(s);
        while (log_.size() > 5) log_.pop_front();
    }
    std::string condition() const {
        if (docked_) return "DOCKED";
        if (!ks_.empty()) return "RED";
        if (energy_ < 300) return "YELLOW";
        return "GREEN";
    }

    // ------------------------------------------------------------------ drawing
    static constexpr int kCell = 44;
    static constexpr int kGX = 14, kGY = 86;

    SDL_Rect cellRect(int r, int c) const { return {kGX + c * kCell, kGY + r * kCell, kCell, kCell}; }

    void drawScan(Gfx& g) {
        SDL_Rect area{kGX, kGY, kCell * 8, kCell * 8};
        g.fill(area, Color{0x04, 0x08, 0x04});
        for (int i = 0; i <= 8; ++i) {
            g.fill({kGX + i * kCell, kGY, 1, kCell * 8}, Color{0x18, 0x30, 0x18});
            g.fill({kGX, kGY + i * kCell, kCell * 8, 1}, Color{0x18, 0x30, 0x18});
        }
        g.outline({area.x - 2, area.y - 2, area.w + 4, area.h + 4}, pal::GREEN, 2);
        if (damage_[SRS] > 0) {
            g.textIn(area, "SHORT RANGE SENSORS OUT", 26, pal::RED);
            return;
        }
        // The original's symbols: <*> Enterprise, +K+ Klingon, >!< starbase, * star.
        // (The Trek font's asterisk is a tiny superscript, so stars are drawn by hand.)
        auto star = [&](float x, float y, float s, Color col) {
            g.line(x - s, y, x + s, y, col);
            g.line(x, y - s, x, y + s, col);
            g.line(x - s * 0.7f, y - s * 0.7f, x + s * 0.7f, y + s * 0.7f, col);
            g.line(x - s * 0.7f, y + s * 0.7f, x + s * 0.7f, y - s * 0.7f, col);
        };
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                SDL_Rect cr = cellRect(r, c);
                float mx = cr.x + cr.w / 2.f, my = cr.y + cr.h / 2.f;
                switch (grid_[r][c]) {
                    case Ship:
                        g.textIn({cr.x + 2, cr.y, 14, cr.h}, "<", 20, pal::YELLOW, Align::Left);
                        g.textIn({cr.x + cr.w - 16, cr.y, 14, cr.h}, ">", 20, pal::YELLOW, Align::Right);
                        star(mx, my, 6, pal::YELLOW);
                        break;
                    case Klingon: g.textIn(cr, "+K+", 20, pal::RED); break;
                    case Base: g.textIn(cr, ">!<", 20, pal::BLUE); break;
                    case Star: star(mx, my, 7, pal::WHITE); break;
                    default: break;
                }
            }
        // Aim preview / torpedo trail, drawn over the symbols; an obstacle gets a box.
        std::vector<std::pair<int, int>> path;
        if (mode_ == Mode::Aim) path = previewPath();
        else if (app.time() < trailUntil_) path = trail_;
        Color pc = mode_ == Mode::Aim ? pal::YELLOW : pal::RED;
        for (auto& p : path) {
            SDL_Rect r = cellRect(p.first, p.second);
            if (grid_[p.first][p.second] != Empty) g.outline({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, pc, 2);
            else g.fill({r.x + r.w / 2 - 3, r.y + r.h / 2 - 3, 6, 6}, pc);
        }
        for (int i = 0; i < 8; ++i) {
            g.text(std::to_string(i + 1), kGX + i * kCell + kCell / 2, kGY + kCell * 8 + 2, 14, pal::DIM_GREEN, Align::Center);
            g.text(std::to_string(i + 1), kGX + kCell * 8 + 6, kGY + i * kCell + 14, 14, pal::DIM_GREEN);
        }
    }

    std::vector<std::pair<int, int>> previewPath() const {
        std::vector<std::pair<int, int>> path;
        double dr, dc;
        vector(course_, dr, dc);
        int n = action_ == Action::Nav ? int(warp_ * 8 + 0.5) : 99;
        double r = sr_, c = sc_;
        for (int i = 0; i < n; ++i) {
            r += dr;
            c += dc;
            int ir = int(std::lround(r)), ic = int(std::lround(c));
            if (ir < 0 || ir > 7 || ic < 0 || ic > 7) break;
            path.push_back({ir, ic});
            if (grid_[ir][ic] != Empty) break;
        }
        return path;
    }

    void drawStatus(Gfx& g) {
        int x = 396, y = 86, right = kW - 18;
        auto row = [&](const std::string& k, const std::string& v, Color vc = pal::YELLOW) {
            g.text(k, x, y, 20, pal::GREEN);
            g.text(v, right, y, 20, vc, Align::Right);
            y += 25;
        };
        char buf[32];
        snprintf(buf, sizeof buf, "%.1f", t_);
        row("STARDATE", buf);
        std::string cond = condition();
        Color cc = cond == "RED" ? pal::RED : cond == "YELLOW" ? pal::YELLOW : cond == "DOCKED" ? pal::BLUE : pal::GREEN;
        row("CONDITION", cond, cc);
        row("QUADRANT", quadrant(qr_, qc_));
        row("SECTOR", sector(sr_, sc_));
        row("PHOTON TORPEDOES", std::to_string(torps_));
        row("ENERGY", std::to_string(int(energy_)), energy_ < 300 ? pal::RED : pal::YELLOW);
        row("SHIELDS", std::to_string(int(shields_)), shields_ < 200 && !ks_.empty() ? pal::RED : pal::YELLOW);
        row("KLINGONS LEFT", std::to_string(totalK_));
        snprintf(buf, sizeof buf, "%.1f", t0_ + days_ - t_);
        row("DAYS LEFT", buf);
        int dmg = 0;
        for (double d : damage_) dmg += d > 0;
        if (dmg) row("DAMAGED SYSTEMS", std::to_string(dmg), pal::RED);
    }

    void drawLog(Gfx& g) {
        int y = 86 + 25 * 10 + 6;
        g.fill({396, y - 4, kW - 18 - 396, 1}, pal::DIM_GREEN);
        for (auto& line : log_) {
            auto parts = g.wrap(line, 16, kW - 18 - 396);
            for (auto& p : parts) {
                if (y > 452) break;
                g.text(p, 396, y, 16, p.find("***") != std::string::npos || p.find("DESTROYED") != std::string::npos
                                          ? pal::RED : pal::YELLOW);
                y += 19;
            }
        }
    }

    void panel(Gfx& g, const std::string& title, const std::string& value, const std::string& hint) {
        SDL_Rect r{14, 466, kW - 28, 100};
        g.fill(r, pal::BLACK);
        g.outline(r, pal::YELLOW, 2);
        g.text(title, r.x + 14, r.y + 8, 26, pal::GREEN);
        g.text(value, r.x + r.w - 14, r.y + 6, 34, pal::YELLOW, Align::Right);
        g.text(hint, r.x + 14, r.y + 64, 18, pal::DIM_YELLOW);
    }

    void drawAimPanel(Gfx& g) {
        char buf[64];
        if (action_ == Action::Nav) {
            snprintf(buf, sizeof buf, "COURSE %.2f  WARP %g", course_, warp_);
            panel(g, "SET COURSE", buf, "LEFT/RIGHT: TURN   UP/DOWN: WARP   ENTER: ENGAGE   BACK: CANCEL");
        } else {
            snprintf(buf, sizeof buf, "COURSE %.2f", course_);
            panel(g, "TORPEDO COURSE", buf, "LEFT/RIGHT: TURN   ENTER: FIRE   BACK: CANCEL");
        }
    }

    void drawAmountPanel(Gfx& g) {
        std::string v = std::to_string(amount_) + " UNITS";
        if (action_ == Action::Phasers)
            panel(g, "PHASERS: ENERGY TO FIRE", v, "UP/DOWN: 100   LEFT/RIGHT: 10   ENTER: FIRE   BACK: CANCEL");
        else
            panel(g, "SHIELD ENERGY", v, "UP/DOWN: 100   LEFT/RIGHT: 10   ENTER: SET   BACK: CANCEL");
    }

    std::string code(int r, int c) const {
        return std::to_string(gK_[r][c]) + std::to_string(gB_[r][c]) + std::to_string(gS_[r][c]);
    }

    void drawLongRange(Gfx& g) {
        SDL_Rect area{kGX, kGY, kCell * 8, kCell * 8};
        g.fill(area, pal::BLACK);
        g.text("LONG RANGE SCAN", area.x + area.w / 2, area.y + 14, 26, pal::YELLOW, Align::Center);
        for (int dr = -1; dr <= 1; ++dr)
            for (int dc = -1; dc <= 1; ++dc) {
                SDL_Rect r{area.x + 44 + (dc + 1) * 96, area.y + 70 + (dr + 1) * 86, 88, 76};
                int qr = qr_ + dr, qc = qc_ + dc;
                bool in = qr >= 0 && qr < 8 && qc >= 0 && qc < 8;
                g.outline(r, dr == 0 && dc == 0 ? pal::YELLOW : pal::GREEN, 2);
                g.textIn(r, in ? code(qr, qc) : "***", 30, in && gK_[qr][qc] ? pal::RED : pal::YELLOW);
            }
        g.text("KLINGONS / BASES / STARS", area.x + area.w / 2, area.y + area.h - 30, 18, pal::DIM_YELLOW, Align::Center);
        g.text("ANY BUTTON TO CLOSE", kW / 2, 520, 22, pal::DIM_YELLOW, Align::Center);
    }

    void drawGalaxyMap(Gfx& g) {
        SDL_Rect area{kGX, kGY, kCell * 8, kCell * 8};
        g.fill(area, pal::BLACK);
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) {
                SDL_Rect cell = cellRect(r, c);
                g.outline(cell, Color{0x18, 0x30, 0x18});
                if (r == qr_ && c == qc_) g.outline(cell, pal::YELLOW, 2);
                if (known_[r][c]) g.textIn(cell, code(r, c), 16, gK_[r][c] ? pal::RED : pal::GREEN);
            }
        g.text("GALACTIC RECORD - ANY BUTTON TO CLOSE", kW / 2, 520, 22, pal::DIM_YELLOW, Align::Center);
    }

    // ------------------------------------------------------------------ state
    std::mt19937 rng_;
    bool started_ = false, won_ = false, docked_ = false;
    int gK_[8][8], gB_[8][8], gS_[8][8];
    bool known_[8][8];
    Cell grid_[8][8];
    std::vector<KShip> ks_;
    int qr_ = 0, qc_ = 0, sr_ = 0, sc_ = 0;
    int totalK_ = 0, startK_ = 0, torps_ = 10, days_ = 30;
    double t0_ = 0, t_ = 0, energy_ = 3000, shields_ = 0;
    double damage_[DeviceCount] = {};
    std::deque<std::string> log_;
    ButtonGroup buttons_;
    Mode mode_ = Mode::Command;
    Action action_ = Action::Nav;
    double course_ = 1, warp_ = 1;
    int amount_ = 0;
    std::vector<std::pair<int, int>> trail_;
    double trailUntil_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeTacticalScreen(App& a) { return std::make_unique<TacticalScreen>(a); }
