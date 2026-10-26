#include "weather.h"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "config.h"
#include "json.h"

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

bool httpGet(const std::string& url, std::string& body, std::string& err) {
#ifdef HAVE_CURL
    CURL* c = curl_easy_init();
    if (!c) {
        err = "curl init failed";
        return false;
    }
    body.clear();
    curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Accept: application/geo+json");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    // weather.gov rejects requests without a User-Agent.
    curl_easy_setopt(c, CURLOPT_USERAGENT, "TricorderV2 (github.com/TricorderV2)");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, +[](char* p, size_t s, size_t n, void* ud) {
        static_cast<std::string*>(ud)->append(p, s * n);
        return s * n;
    });
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) {
        err = curl_easy_strerror(rc);
        return false;
    }
    if (status != 200) {
        err = "HTTP " + std::to_string(status);
        return false;
    }
    return true;
#else
    (void)url;
    (void)body;
    err = "built without libcurl";
    return false;
#endif
}

namespace {

std::string hourLabel(const std::string& iso) {
    // "2026-10-25T14:00:00-05:00"
    if (iso.size() < 13) return "--";
    int h = std::atoi(iso.substr(11, 2).c_str());
    int h12 = h % 12 == 0 ? 12 : h % 12;
    return std::to_string(h12) + (h < 12 ? "AM" : "PM");
}

}  // namespace

bool parseForecast(const std::string& body, WeatherData& out, std::string& err) {
    json::Value doc;
    if (!json::parse(body, doc, &err)) return false;
    const json::Value& periods = doc["properties"]["periods"];
    if (periods.size() == 0) {
        err = "no forecast periods";
        return false;
    }
    const json::Value& p0 = periods[0];
    out = WeatherData();
    out.valid = true;
    out.temp = int(std::lround(p0["temperature"].number()));
    out.precip = int(std::lround(p0["probabilityOfPrecipitation"]["value"].number()));
    out.humidity = int(std::lround(p0["relativeHumidity"]["value"].number()));
    double dewC = p0["dewpoint"]["value"].number(-999);
    out.dewpointF = dewC > -900 ? int(std::lround(dewC * 9 / 5 + 32)) : 0;
    out.shortForecast = p0["shortForecast"].string();
    std::string ws = p0["windSpeed"].string(), wd = p0["windDirection"].string();
    out.wind = ws.empty() ? "" : ws + " " + wd;
    for (size_t i = 1; i < periods.size() && out.hours.size() < 12; ++i) {
        const json::Value& p = periods[i];
        WeatherHour h;
        h.label = hourLabel(p["startTime"].string());
        h.temp = int(std::lround(p["temperature"].number()));
        h.precip = int(std::lround(p["probabilityOfPrecipitation"]["value"].number()));
        h.shortForecast = p["shortForecast"].string();
        out.hours.push_back(h);
    }
    return true;
}

WeatherData simulatedWeather() {
    // Plausible readings that follow the time of day, for offline use and desktop testing.
    time_t now = time(nullptr);
    tm lt;
    localtime_r(&now, &lt);
    WeatherData w;
    w.valid = true;
    w.simulated = true;
    auto tempAt = [&](int hour) {
        return int(std::lround(58 + 12 * std::sin((hour - 9) / 24.0 * 2 * M_PI)));
    };
    w.temp = tempAt(lt.tm_hour);
    w.humidity = 55 + int(10 * std::cos(lt.tm_hour / 24.0 * 2 * M_PI));
    w.precip = 10 + (lt.tm_yday * 7) % 30;
    w.dewpointF = w.temp - 12;
    w.shortForecast = w.precip > 30 ? "Chance Light Rain" : "Partly Cloudy";
    w.wind = "8 mph NW";
    for (int i = 1; i <= 12; ++i) {
        int h = (lt.tm_hour + i) % 24;
        WeatherHour wh;
        int h12 = h % 12 == 0 ? 12 : h % 12;
        wh.label = std::to_string(h12) + (h < 12 ? "AM" : "PM");
        wh.temp = tempAt(h);
        wh.precip = std::max(0, w.precip + int(15 * std::sin(i * 0.7)));
        w.hours.push_back(wh);
    }
    w.fetched = now;
    return w;
}

time_t parseIsoTime(const std::string& iso) {
    int Y, M, D, h, m, sec = 0;
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) < 5) return 0;
    tm t{};
    t.tm_year = Y - 1900;
    t.tm_mon = M - 1;
    t.tm_mday = D;
    t.tm_hour = h;
    t.tm_min = m;
    t.tm_sec = sec;
    time_t utc = timegm(&t);
    // Offset: "...Z", "+hh:mm" or "-hh:mm" after the seconds.
    size_t tpos = iso.find('T');
    size_t sign = iso.find_first_of("+-", tpos == std::string::npos ? 0 : tpos);
    if (sign != std::string::npos && sign + 5 < iso.size() + 1) {
        int oh = 0, om = 0;
        if (sscanf(iso.c_str() + sign + 1, "%d:%d", &oh, &om) >= 1) {
            int off = (oh * 60 + om) * 60;
            utc += iso[sign] == '+' ? -off : off;
        }
    }
    return utc;
}

namespace {
// NWS text is hard-wrapped at ~70 columns; keep only the blank-line paragraph breaks.
std::string unwrap(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\n') { out += s[i]; continue; }
        if (i + 1 < s.size() && s[i + 1] == '\n') { out += "\n\n"; while (i + 1 < s.size() && s[i + 1] == '\n') ++i; }
        else out += ' ';
    }
    return out;
}
}  // namespace

bool parseAlerts(const std::string& body, std::vector<WeatherAlert>& out, std::string& err) {
    json::Value doc;
    if (!json::parse(body, doc, &err)) return false;
    out.clear();
    const json::Value& feats = doc["features"];
    for (size_t i = 0; i < feats.size(); ++i) {
        const json::Value& p = feats[i]["properties"];
        WeatherAlert a;
        a.id = p["id"].string();
        a.event = p["event"].string();
        a.severity = p["severity"].string();
        a.headline = p["headline"].string();
        a.description = unwrap(p["description"].string());
        a.instruction = unwrap(p["instruction"].string());
        a.area = p["areaDesc"].string();
        std::string ends = p["ends"].string();
        a.expires = parseIsoTime(ends.empty() ? p["expires"].string() : ends);
        if (p["status"].string("Actual") != "Actual") continue;  // skip tests/exercises
        out.push_back(a);
    }
    // Most serious first.
    std::stable_sort(out.begin(), out.end(), [](const WeatherAlert& a, const WeatherAlert& b) { return a.red() > b.red(); });
    return true;
}

bool parseGridLocation(const std::string& body, Location& out) {
    json::Value doc;
    std::string err;
    if (!json::parse(body, doc, &err)) return false;
    const json::Value& ring = doc["geometry"]["coordinates"][0];
    if (ring.size() == 0) return false;
    double sx = 0, sy = 0;
    for (size_t i = 0; i < ring.size(); ++i) {
        sx += ring[i][0].number();
        sy += ring[i][1].number();
    }
    out.lon = sx / ring.size();
    out.lat = sy / ring.size();
    out.known = true;
    return true;
}

void WeatherService::start(const std::string& url, int refreshMin, bool offline, double lat, double lon,
                           bool alertTest) {
    url_ = url;
    refreshMin_ = std::max(1, refreshMin);
    offline_ = offline;
    alertTest_ = alertTest;
    cachePath_ = stateDir() + "/weather.json";
    {
        std::ifstream ti(stateDir() + "/iss.tle");
        std::stringstream ts;
        ts << ti.rdbuf();
        tle_ = ts.str();
    }
    if (lat != 0 || lon != 0) {
        loc_ = {lat, lon, true};
    } else {
        std::ifstream li(stateDir() + "/location.txt");
        double a, b;
        if (li >> a >> b) loc_ = {a, b, true};
    }
    if (alertTest_) {
        WeatherAlert a;
        a.id = "test-alert";
        a.event = "Tornado Warning";
        a.severity = "Extreme";
        a.area = "Scott, IA; Rock Island, IL";
        a.headline = "Tornado Warning issued for Scott County (TEST)";
        a.description = "THIS IS A TEST. A severe thunderstorm capable of producing a tornado was located near "
                        "Davenport, moving northeast at 35 mph.";
        a.instruction = "TAKE COVER NOW! Move to a basement or an interior room on the lowest floor.";
        a.expires = time(nullptr) + 45 * 60;
        alerts_.push_back(a);
    }
    // Show the last good reading immediately, even before the network comes up.
    std::ifstream in(cachePath_);
    if (in) {
        std::stringstream ss;
        ss << in.rdbuf();
        WeatherData d;
        std::string err;
        if (parseForecast(ss.str(), d, err)) {
            struct stat st;
            d.fetched = stat(cachePath_.c_str(), &st) == 0 ? st.st_mtime : 0;
            data_ = d;
        }
    }
    thread_ = std::thread(&WeatherService::loop, this);
}

void WeatherService::stop() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

WeatherData WeatherService::get() const {
    std::lock_guard<std::mutex> lk(m_);
    return data_;
}

bool WeatherService::busy() const {
    std::lock_guard<std::mutex> lk(m_);
    return busy_;
}

void WeatherService::refreshNow() {
    {
        std::lock_guard<std::mutex> lk(m_);
        refresh_ = true;
    }
    cv_.notify_all();
}

std::vector<WeatherAlert> WeatherService::alerts() const {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<WeatherAlert> live;
    time_t now = time(nullptr);
    for (auto& a : alerts_)
        if (a.expires == 0 || a.expires > now) live.push_back(a);
    return live;
}

std::string WeatherService::issTle() const {
    std::lock_guard<std::mutex> lk(m_);
    return tle_;
}

void WeatherService::fetchTle() {
    if (offline_) return;
    std::string body, err;
    if (httpGet("https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE", body, err) &&
        body.find("\n1 25544") != std::string::npos) {
        std::ofstream(stateDir() + "/iss.tle") << body;
        std::lock_guard<std::mutex> lk(m_);
        tle_ = body;
        lastTleOk_ = true;
    } else {
        lastTleOk_ = false;
        fprintf(stderr, "iss tle: %s (keeping the saved orbit)\n", err.empty() ? "bad response" : err.c_str());
    }
}

Location WeatherService::location() const {
    std::lock_guard<std::mutex> lk(m_);
    return loc_;
}

bool WeatherService::fetchForecast() {
    {
        std::lock_guard<std::mutex> lk(m_);
        busy_ = true;
    }
    WeatherData d;
    std::string body, err;
    bool ok = false;
    if (offline_) {
        err = "offline mode";
    } else if (httpGet(url_, body, err) && parseForecast(body, d, err)) {
        d.fetched = time(nullptr);
        ok = true;
        std::ofstream(cachePath_) << body;
    }
    std::lock_guard<std::mutex> lk(m_);
    if (ok) {
        data_ = d;
    } else if (!data_.valid || data_.simulated) {
        data_ = simulatedWeather();
        data_.error = err;
    } else {
        data_.error = err;  // keep showing the last real reading
    }
    busy_ = false;
    if (!ok) fprintf(stderr, "weather: %s\n", err.c_str());
    return ok;
}

void WeatherService::ensureLocation() {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (loc_.known || offline_) return;
    }
    // ".../gridpoints/DVN/33,63/forecast/hourly" -> ".../gridpoints/DVN/33,63"
    std::string grid = url_;
    size_t f = grid.find("/forecast");
    if (f == std::string::npos) return;
    grid = grid.substr(0, f);
    std::string body, err;
    Location l;
    if (httpGet(grid, body, err) && parseGridLocation(body, l)) {
        std::ofstream(stateDir() + "/location.txt") << l.lat << " " << l.lon << "\n";
        std::lock_guard<std::mutex> lk(m_);
        loc_ = l;
    }
}

void WeatherService::fetchAlerts() {
    if (offline_ || alertTest_) return;
    Location l = location();
    if (!l.known) return;
    char url[160];
    snprintf(url, sizeof url, "https://api.weather.gov/alerts/active?point=%.4f,%.4f", l.lat, l.lon);
    std::string body, err;
    std::vector<WeatherAlert> a;
    if (httpGet(url, body, err) && parseAlerts(body, a, err)) {
        std::lock_guard<std::mutex> lk(m_);
        alerts_ = a;
    } else {
        fprintf(stderr, "alerts: %s\n", err.c_str());
    }
}

void WeatherService::loop() {
    using clock = std::chrono::steady_clock;
    auto nextForecast = clock::now(), nextAlerts = clock::now(), nextTle = clock::now();
    {
        // CelesTrak refuses clients that re-download the same elements within ~2 hours,
        // so a cached copy younger than 12 hours is used as-is.
        struct stat st;
        if (stat((stateDir() + "/iss.tle").c_str(), &st) == 0) {
            long age = long(time(nullptr) - st.st_mtime);
            if (age < 12 * 3600) nextTle += std::chrono::seconds(12 * 3600 - age);
        }
    }
    for (;;) {
        auto now = clock::now();
        bool forced;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (quit_) return;
            forced = refresh_;
            refresh_ = false;
        }
        if (forced || now >= nextForecast) {
            bool ok = fetchForecast();
            // Retry sooner after a failure (Wi-Fi may still be coming up).
            nextForecast = now + std::chrono::minutes(ok || offline_ ? refreshMin_ : 1);
        }
        ensureLocation();
        if (forced || now >= nextAlerts) {
            fetchAlerts();
            nextAlerts = now + std::chrono::minutes(3);  // warnings matter: check often
        }
        if (now >= nextTle) {
            fetchTle();
            // Orbits drift: refresh twice a day. After a failure wait 3 hours, past CelesTrak's limit.
            nextTle = now + std::chrono::hours(lastTleOk_ ? 12 : 3);
        }
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait_until(lk, std::min({nextForecast, nextAlerts, nextTle}), [&] { return quit_ || refresh_; });
        if (quit_) return;
    }
}
