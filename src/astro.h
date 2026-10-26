#pragma once
// Offline astronomy: Sun, Moon and naked-eye planet positions, rise/set times and the
// Moon's phase. Uses Paul Schlyter's low-precision orbital elements ("How to compute
// planetary positions"): good to a few arc-minutes, plenty for a sky chart.
#include <ctime>
#include <string>
#include <vector>

namespace astro {

enum Body { Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn, BodyCount };

struct Equatorial {
    double ra = 0, dec = 0;  // degrees
    double dist = 0;         // AU (Moon: Earth radii)
};

struct Horizontal {
    double alt = 0, az = 0;  // degrees; az from north through east
};

double julianDay(time_t t);
Equatorial position(Body b, time_t t);
Horizontal toHorizontal(const Equatorial& eq, time_t t, double lat, double lon);
Horizontal altAz(Body b, time_t t, double lat, double lon);  // includes lunar parallax

struct RiseSet {
    time_t rise = 0, set = 0;  // 0 = doesn't happen in the next 24 h
    bool alwaysUp = false, alwaysDown = false;
};
// Next rise and set within 24 h after `from`.
RiseSet riseSet(Body b, time_t from, double lat, double lon);

struct MoonPhase {
    double illumination = 0;  // 0..1
    double age = 0;           // 0..1 through the cycle (0 = new, 0.5 = full)
    bool waxing = true;
    std::string name;         // "WAXING GIBBOUS"
};
MoonPhase moonPhase(time_t t);

const char* bodyName(Body b);
std::string compassPoint(double az);  // "NNE"

}  // namespace astro
