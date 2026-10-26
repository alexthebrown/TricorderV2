#pragma once
#include <cstdio>
#include <ctime>
#include <string>

// "Stardate" in the TNG style (1000 units per year, fraction through the year),
// counted from 2000 so today reads like 26xxx.x.
inline double stardate(time_t t) {
    tm lt;
    localtime_r(&t, &lt);
    int y = lt.tm_year + 1900;
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    double day = lt.tm_yday + (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) / 86400.0;
    return (y - 2000) * 1000.0 + day / (leap ? 366.0 : 365.0) * 1000.0;
}

inline std::string stardateString(time_t t) {
    char buf[32];
    snprintf(buf, sizeof buf, "%.1f", stardate(t));
    return buf;
}
