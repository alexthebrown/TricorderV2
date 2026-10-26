#pragma once
// Satellite tracking from a NORAD two-line element set (TLE). Propagation is Keplerian
// plus the secular J2 (Earth oblateness) drift of the node and perigee: within a few
// tens of km for a day or two after the TLE epoch — enough for pass predictions and a
// ground track, and it works offline from a cached TLE.
#include <ctime>
#include <string>
#include <vector>

namespace orbit {

struct Tle {
    std::string name;
    double epochJd = 0;
    double ndot2 = 0;  // rev/day^2 (first derivative of mean motion / 2)
    double incl = 0, raan = 0, ecc = 0, argp = 0, meanAnom = 0;  // degrees
    double meanMotion = 0;                                       // rev/day
    bool valid = false;
};

bool parseTle(const std::string& text, Tle& out);  // accepts 2 or 3 line form

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

struct State {
    Vec3 eci;            // km, true-equator inertial frame
    double lat = 0, lon = 0, altKm = 0, speedKmh = 0;
    bool sunlit = true;
};

State propagate(const Tle& tle, time_t t);

struct Look {
    double az = 0, el = 0, rangeKm = 0;
};
Look lookAngles(const State& s, time_t t, double lat, double lon);

struct Pass {
    time_t rise = 0, peak = 0, set = 0;
    double maxEl = 0, riseAz = 0, setAz = 0;
    bool visible = false;  // sunlit satellite in a dark sky: you can see it with your eyes
};
// Passes reaching at least minEl degrees in the next `hours`.
std::vector<Pass> passes(const Tle& tle, time_t from, double hours, double lat, double lon, double minEl = 10);

double gmstDeg(time_t t);
double tleAgeDays(const Tle& tle, time_t now);

}  // namespace orbit
