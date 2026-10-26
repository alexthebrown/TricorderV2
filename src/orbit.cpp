#include "orbit.h"

#include <cmath>
#include <cstdlib>
#include <sstream>

#include "astro.h"

namespace orbit {

namespace {

constexpr double kDeg = M_PI / 180.0;
constexpr double kMu = 398600.4418;  // km^3/s^2
constexpr double kRe = 6378.137;     // km
constexpr double kJ2 = 1.08262668e-3;
constexpr double kFlat = 1 / 298.257223563;

double field(const std::string& line, size_t col, size_t len) {
    if (line.size() < col - 1 + len) return 0;
    return std::atof(line.substr(col - 1, len).c_str());
}

double julian(time_t t) { return double(t) / 86400.0 + 2440587.5; }

Vec3 sunDirection(time_t t) {
    astro::Equatorial s = astro::position(astro::Sun, t);
    return {std::cos(s.dec * kDeg) * std::cos(s.ra * kDeg), std::cos(s.dec * kDeg) * std::sin(s.ra * kDeg),
            std::sin(s.dec * kDeg)};
}

}  // namespace

bool parseTle(const std::string& text, Tle& out) {
    std::istringstream in(text);
    std::string line, l1, l2, name;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.size() >= 69 && line[0] == '1' && line[1] == ' ') l1 = line;
        else if (line.size() >= 69 && line[0] == '2' && line[1] == ' ') l2 = line;
        else if (!line.empty() && l1.empty()) name = line;
    }
    if (l1.empty() || l2.empty()) return false;
    Tle t;
    t.name = name;
    int yy = int(field(l1, 19, 2));
    double day = field(l1, 21, 12);
    int year = yy < 57 ? 2000 + yy : 1900 + yy;
    // Julian day of Jan 0.0 of the epoch year, plus the fractional day of year.
    int a = (14 - 1) / 12, y = year + 4800 - a, m = 1 + 12 * a - 3;
    double jdJan1 = 1 + (153 * m + 2) / 5 + 365.0 * y + y / 4 - y / 100 + y / 400 - 32045 - 0.5;
    t.epochJd = jdJan1 - 1 + day;
    t.ndot2 = field(l1, 34, 10);
    t.incl = field(l2, 9, 8);
    t.raan = field(l2, 18, 8);
    t.ecc = field(l2, 27, 7) * 1e-7;
    t.argp = field(l2, 35, 8);
    t.meanAnom = field(l2, 44, 8);
    t.meanMotion = field(l2, 53, 11);
    t.valid = t.meanMotion > 0;
    out = t;
    return t.valid;
}

double gmstDeg(time_t t) {
    double d = julian(t) - 2451545.0;
    double g = std::fmod(280.46061837 + 360.98564736629 * d, 360.0);
    return g < 0 ? g + 360 : g;
}

double tleAgeDays(const Tle& tle, time_t now) { return julian(now) - tle.epochJd; }

State propagate(const Tle& tle, time_t t) {
    State s;
    double dtMin = (julian(t) - tle.epochJd) * 1440.0;
    double n0 = tle.meanMotion * 2 * M_PI / 86400.0;  // rad/s
    double a = std::cbrt(kMu / (n0 * n0));
    double e = tle.ecc, inc = tle.incl * kDeg;
    double p = a * (1 - e * e);
    double k = 1.5 * kJ2 * (kRe / p) * (kRe / p) * n0;  // rad/s
    double si = std::sin(inc);
    double raanDot = -k * std::cos(inc), argpDot = k * (2 - 2.5 * si * si);
    double mDot = n0 + k * std::sqrt(1 - e * e) * (1 - 1.5 * si * si);
    double dtSec = dtMin * 60, dtDay = dtMin / 1440.0;
    double raan = tle.raan * kDeg + raanDot * dtSec;
    double argp = tle.argp * kDeg + argpDot * dtSec;
    double M = tle.meanAnom * kDeg + mDot * dtSec + 2 * M_PI * tle.ndot2 * dtDay * dtDay;
    M = std::fmod(M, 2 * M_PI);
    double E = M;
    for (int i = 0; i < 10; ++i) E -= (E - e * std::sin(E) - M) / (1 - e * std::cos(E));
    double nu = 2 * std::atan2(std::sqrt(1 + e) * std::sin(E / 2), std::sqrt(1 - e) * std::cos(E / 2));
    double r = a * (1 - e * std::cos(E));
    double xp = r * std::cos(nu), yp = r * std::sin(nu);
    double cO = std::cos(raan), sO = std::sin(raan), cw = std::cos(argp), sw = std::sin(argp), ci = std::cos(inc);
    s.eci.x = xp * (cO * cw - sO * sw * ci) - yp * (cO * sw + sO * cw * ci);
    s.eci.y = xp * (sO * cw + cO * sw * ci) + yp * (cO * cw * ci - sO * sw);
    s.eci.z = xp * (sw * si) + yp * (cw * si);

    double theta = gmstDeg(t) * kDeg;
    double rxy = std::hypot(s.eci.x, s.eci.y);
    double lon = std::atan2(s.eci.y, s.eci.x) - theta;
    lon = std::remainder(lon, 2 * M_PI);
    double latc = std::atan2(s.eci.z, rxy);
    s.lat = std::atan(std::tan(latc) / ((1 - kFlat) * (1 - kFlat))) / kDeg;  // geodetic (good enough)
    s.lon = lon / kDeg;
    double rad = std::sqrt(rxy * rxy + s.eci.z * s.eci.z);
    double localR = kRe * (1 - kFlat * std::sin(latc) * std::sin(latc));
    s.altKm = rad - localR;
    s.speedKmh = std::sqrt(kMu * (2 / rad - 1 / a)) * 3600;

    // Earth's shadow as a cylinder: behind Earth (away from the Sun) and within its radius.
    Vec3 sun = sunDirection(t);
    double along = s.eci.x * sun.x + s.eci.y * sun.y + s.eci.z * sun.z;
    double px = s.eci.x - along * sun.x, py = s.eci.y - along * sun.y, pz = s.eci.z - along * sun.z;
    s.sunlit = !(along < 0 && std::sqrt(px * px + py * py + pz * pz) < kRe);
    return s;
}

Look lookAngles(const State& s, time_t t, double lat, double lon) {
    double theta = gmstDeg(t) * kDeg;
    // Satellite in Earth-fixed coordinates.
    double c = std::cos(theta), sn = std::sin(theta);
    double x = c * s.eci.x + sn * s.eci.y, y = -sn * s.eci.x + c * s.eci.y, z = s.eci.z;
    double la = lat * kDeg, lo = lon * kDeg;
    double ox = kRe * std::cos(la) * std::cos(lo), oy = kRe * std::cos(la) * std::sin(lo), oz = kRe * std::sin(la);
    double rx = x - ox, ry = y - oy, rz = z - oz;
    double S = std::sin(la) * std::cos(lo) * rx + std::sin(la) * std::sin(lo) * ry - std::cos(la) * rz;
    double E = -std::sin(lo) * rx + std::cos(lo) * ry;
    double Z = std::cos(la) * std::cos(lo) * rx + std::cos(la) * std::sin(lo) * ry + std::sin(la) * rz;
    Look l;
    l.rangeKm = std::sqrt(rx * rx + ry * ry + rz * rz);
    l.el = std::asin(Z / l.rangeKm) / kDeg;
    l.az = std::atan2(E, -S) / kDeg;
    if (l.az < 0) l.az += 360;
    return l;
}

std::vector<Pass> passes(const Tle& tle, time_t from, double hours, double lat, double lon, double minEl) {
    std::vector<Pass> out;
    if (!tle.valid) return out;
    const int step = 30;
    Pass cur;
    bool up = false;
    double prevEl = lookAngles(propagate(tle, from), from, lat, lon).el;
    for (time_t t = from; t <= from + time_t(hours * 3600); t += step) {
        State s = propagate(tle, t);
        Look l = lookAngles(s, t, lat, lon);
        if (!up && l.el > 0) {
            up = true;
            cur = Pass();
            // Interpolate the horizon crossing between samples.
            cur.rise = prevEl < 0 ? t - time_t(step * l.el / (l.el - prevEl)) : t;
            cur.riseAz = l.az;
        }
        if (up) {
            if (l.el > cur.maxEl) {
                cur.maxEl = l.el;
                cur.peak = t;
            }
            if (l.el > minEl && s.sunlit && astro::altAz(astro::Sun, t, lat, lon).alt < -6) cur.visible = true;
            if (l.el <= 0) {
                up = false;
                cur.set = t - time_t(step * l.el / (l.el - prevEl));
                cur.setAz = l.az;
                if (cur.maxEl >= minEl && cur.rise > from) out.push_back(cur);
            }
        }
        prevEl = l.el;
    }
    return out;
}

}  // namespace orbit
