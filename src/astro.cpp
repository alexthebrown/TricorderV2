#include "astro.h"

#include <algorithm>
#include <cmath>

namespace astro {

namespace {

constexpr double kDeg = M_PI / 180.0;

double rev(double x) {
    x = std::fmod(x, 360.0);
    return x < 0 ? x + 360.0 : x;
}
double sind(double x) { return std::sin(x * kDeg); }
double cosd(double x) { return std::cos(x * kDeg); }
double atan2d(double y, double x) { return std::atan2(y, x) / kDeg; }

struct Elements {
    double N, i, w, a, e, M;
};

// Days since 2000 Jan 0.0 UT, Schlyter's epoch.
double dayNumber(time_t t) { return julianDay(t) - 2451543.5; }

Elements elements(Body b, double d) {
    switch (b) {
        case Sun: return {0, 0, 282.9404 + 4.70935e-5 * d, 1.0, 0.016709 - 1.151e-9 * d, 356.0470 + 0.9856002585 * d};
        case Moon:
            return {125.1228 - 0.0529538083 * d, 5.1454, 318.0634 + 0.1643573223 * d, 60.2666, 0.054900,
                    115.3654 + 13.0649929509 * d};
        case Mercury:
            return {48.3313 + 3.24587e-5 * d, 7.0047 + 5.00e-8 * d, 29.1241 + 1.01444e-5 * d, 0.387098,
                    0.205635 + 5.59e-10 * d, 168.6562 + 4.0923344368 * d};
        case Venus:
            return {76.6799 + 2.46590e-5 * d, 3.3946 + 2.75e-8 * d, 54.8910 + 1.38374e-5 * d, 0.723330,
                    0.006773 - 1.302e-9 * d, 48.0052 + 1.6021302244 * d};
        case Mars:
            return {49.5574 + 2.11081e-5 * d, 1.8497 - 1.78e-8 * d, 286.5016 + 2.92961e-5 * d, 1.523688,
                    0.093405 + 2.516e-9 * d, 18.6021 + 0.5240207766 * d};
        case Jupiter:
            return {100.4542 + 2.76854e-5 * d, 1.3030 - 1.557e-7 * d, 273.8777 + 1.64505e-5 * d, 5.20256,
                    0.048498 + 4.469e-9 * d, 19.8950 + 0.0830853001 * d};
        case Saturn:
            return {113.6634 + 2.38980e-5 * d, 2.4886 - 1.081e-7 * d, 339.3939 + 2.97661e-5 * d, 9.55475,
                    0.055546 - 9.499e-9 * d, 316.9670 + 0.0334442282 * d};
        default: return {0, 0, 0, 1, 0, 0};
    }
}

// Position in the orbit plane -> heliocentric (geocentric for the Moon) ecliptic xyz.
void orbitXYZ(const Elements& el, double& x, double& y, double& z, double& r, double& lon) {
    double M = rev(el.M);
    double E = M + el.e / kDeg * sind(M) * (1 + el.e * cosd(M));
    for (int k = 0; k < 8; ++k) E = E - (E - el.e / kDeg * sind(E) - M) / (1 - el.e * cosd(E));
    double xv = el.a * (cosd(E) - el.e);
    double yv = el.a * std::sqrt(1 - el.e * el.e) * sind(E);
    double v = atan2d(yv, xv);
    r = std::sqrt(xv * xv + yv * yv);
    double vw = v + el.w;
    x = r * (cosd(el.N) * cosd(vw) - sind(el.N) * sind(vw) * cosd(el.i));
    y = r * (sind(el.N) * cosd(vw) + cosd(el.N) * sind(vw) * cosd(el.i));
    z = r * sind(vw) * sind(el.i);
    lon = rev(vw + el.N);
}

void sunXY(double d, double& xs, double& ys, double& lonsun, double& rs) {
    Elements s = elements(Sun, d);
    double x, y, z;
    orbitXYZ(s, x, y, z, rs, lonsun);
    lonsun = rev(lonsun);
    xs = rs * cosd(lonsun);
    ys = rs * sind(lonsun);
}

Equatorial eclipticToEquatorial(double x, double y, double z, double d) {
    double ecl = 23.4393 - 3.563e-7 * d;
    double xe = x, ye = y * cosd(ecl) - z * sind(ecl), ze = y * sind(ecl) + z * cosd(ecl);
    Equatorial q;
    q.ra = rev(atan2d(ye, xe));
    q.dec = atan2d(ze, std::sqrt(xe * xe + ye * ye));
    q.dist = std::sqrt(xe * xe + ye * ye + ze * ze);
    return q;
}

// Moon ecliptic longitude/latitude with the main perturbation terms.
void moonEcliptic(double d, double& lon, double& lat, double& r) {
    Elements m = elements(Moon, d), s = elements(Sun, d);
    double x, y, z, l0;
    orbitXYZ(m, x, y, z, r, l0);
    lon = atan2d(y, x);
    lat = atan2d(z, std::sqrt(x * x + y * y));
    double Ms = rev(s.M), Mm = rev(m.M);
    double Ls = rev(Ms + s.w), Lm = rev(Mm + m.w + m.N);
    double D = rev(Lm - Ls), F = rev(Lm - m.N);
    lon += -1.274 * sind(Mm - 2 * D) + 0.658 * sind(2 * D) - 0.186 * sind(Ms) - 0.059 * sind(2 * Mm - 2 * D) -
           0.057 * sind(Mm - 2 * D + Ms) + 0.053 * sind(Mm + 2 * D) + 0.046 * sind(2 * D - Ms) +
           0.041 * sind(Mm - Ms) - 0.035 * sind(D) - 0.031 * sind(Mm + Ms) - 0.015 * sind(2 * F - 2 * D) +
           0.011 * sind(Mm - 4 * D);
    lat += -0.173 * sind(F - 2 * D) - 0.055 * sind(Mm - F - 2 * D) - 0.046 * sind(Mm + F - 2 * D) +
           0.033 * sind(F + 2 * D) + 0.017 * sind(2 * Mm + F);
    r += -0.58 * cosd(Mm - 2 * D) - 0.46 * cosd(2 * D);
    lon = rev(lon);
}

}  // namespace

double julianDay(time_t t) { return double(t) / 86400.0 + 2440587.5; }

Equatorial position(Body b, time_t t) {
    double d = dayNumber(t);
    double xs, ys, lonsun, rs;
    sunXY(d, xs, ys, lonsun, rs);
    if (b == Sun) return eclipticToEquatorial(xs, ys, 0, d);
    if (b == Moon) {
        double lon, lat, r;
        moonEcliptic(d, lon, lat, r);
        Equatorial q = eclipticToEquatorial(r * cosd(lon) * cosd(lat), r * sind(lon) * cosd(lat), r * sind(lat), d);
        q.dist = r;
        return q;
    }
    double x, y, z, r, lon;
    orbitXYZ(elements(b, d), x, y, z, r, lon);
    // Jupiter and Saturn tug on each other enough to matter at chart scale.
    if (b == Jupiter || b == Saturn) {
        double Mj = rev(elements(Jupiter, d).M), Ms = rev(elements(Saturn, d).M);
        double plon = atan2d(y, x), plat = atan2d(z, std::sqrt(x * x + y * y));
        if (b == Jupiter)
            plon += -0.332 * sind(2 * Mj - 5 * Ms - 67.6) - 0.056 * sind(2 * Mj - 2 * Ms + 21) +
                    0.042 * sind(3 * Mj - 5 * Ms + 21) - 0.036 * sind(Mj - 2 * Ms) + 0.022 * cosd(Mj - Ms) +
                    0.023 * sind(2 * Mj - 3 * Ms + 52) - 0.016 * sind(Mj - 5 * Ms - 69);
        else {
            plon += 0.812 * sind(2 * Mj - 5 * Ms - 67.6) - 0.229 * cosd(2 * Mj - 4 * Ms - 2) +
                    0.119 * sind(Mj - 2 * Ms - 3) + 0.046 * sind(2 * Mj - 6 * Ms - 69) + 0.014 * sind(Mj - 3 * Ms + 32);
            plat += -0.020 * cosd(2 * Mj - 4 * Ms - 2) + 0.018 * sind(2 * Mj - 6 * Ms - 49);
        }
        x = r * cosd(plon) * cosd(plat);
        y = r * sind(plon) * cosd(plat);
        z = r * sind(plat);
    }
    return eclipticToEquatorial(x + xs, y + ys, z, d);
}

Horizontal toHorizontal(const Equatorial& q, time_t t, double lat, double lon) {
    double d = dayNumber(t);
    Elements s = elements(Sun, d);
    double L = rev(s.M + s.w);  // Sun's mean longitude
    double ut = std::fmod(double(t), 86400.0) / 3600.0;
    double lst = rev(L + 180 + ut * 15 + lon);
    double ha = lst - q.ra;
    double x = cosd(ha) * cosd(q.dec), y = sind(ha) * cosd(q.dec), z = sind(q.dec);
    double xh = x * sind(lat) - z * cosd(lat), zh = x * cosd(lat) + z * sind(lat);
    Horizontal h;
    h.az = rev(atan2d(y, xh) + 180);
    h.alt = atan2d(zh, std::sqrt(xh * xh + y * y));
    return h;
}

Horizontal altAz(Body b, time_t t, double lat, double lon) {
    Equatorial q = position(b, t);
    Horizontal h = toHorizontal(q, t, lat, lon);
    if (b == Moon) h.alt -= std::asin(1.0 / q.dist) / kDeg * cosd(h.alt);  // topocentric parallax
    return h;
}

RiseSet riseSet(Body b, time_t from, double lat, double lon) {
    // Standard altitudes: refraction (+ Sun's semi-diameter); the Moon's parallax is in altAz.
    double h0 = b == Sun ? -0.833 : b == Moon ? -0.583 : -0.567;
    RiseSet rs;
    const int step = 600;  // 10-minute scan, then interpolate
    double prev = altAz(b, from, lat, lon).alt - h0;
    bool everUp = prev > 0, everDown = prev <= 0;
    for (int s = step; s <= 86400; s += step) {
        time_t t = from + s;
        double cur = altAz(b, t, lat, lon).alt - h0;
        if (cur > 0) everUp = true;
        else everDown = true;
        if ((prev <= 0) != (cur <= 0)) {
            time_t cross = t - step + time_t(step * (prev / (prev - cur)));
            if (cur > 0 && !rs.rise) rs.rise = cross;
            if (cur <= 0 && !rs.set) rs.set = cross;
        }
        prev = cur;
    }
    rs.alwaysUp = everUp && !everDown;
    rs.alwaysDown = everDown && !everUp;
    return rs;
}

MoonPhase moonPhase(time_t t) {
    double d = dayNumber(t);
    double xs, ys, lonsun, rs;
    sunXY(d, xs, ys, lonsun, rs);
    double mlon, mlat, mr;
    moonEcliptic(d, mlon, mlat, mr);
    double elong = std::acos(std::clamp(cosd(mlon - lonsun) * cosd(mlat), -1.0, 1.0)) / kDeg;
    MoonPhase p;
    p.illumination = (1 - cosd(elong)) / 2;
    double sep = rev(mlon - lonsun);  // 0 new, 180 full
    p.age = sep / 360.0;
    p.waxing = sep < 180;
    if (p.illumination < 0.03) p.name = "NEW MOON";
    else if (p.illumination > 0.97) p.name = "FULL MOON";
    else if (std::fabs(p.illumination - 0.5) < 0.06) p.name = p.waxing ? "FIRST QUARTER" : "LAST QUARTER";
    else if (p.illumination < 0.5) p.name = p.waxing ? "WAXING CRESCENT" : "WANING CRESCENT";
    else p.name = p.waxing ? "WAXING GIBBOUS" : "WANING GIBBOUS";
    return p;
}

const char* bodyName(Body b) {
    static const char* n[] = {"SUN", "MOON", "MERCURY", "VENUS", "MARS", "JUPITER", "SATURN"};
    return b < BodyCount ? n[b] : "?";
}

std::string compassPoint(double az) {
    static const char* pts[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    return pts[int(rev(az) / 22.5 + 0.5) % 16];
}

}  // namespace astro
