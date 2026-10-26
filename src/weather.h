#pragma once
// Background fetcher for network data: weather.gov forecast + alerts, ISS orbit. Never blocks the UI; falls back to the last cached
// reading, then to simulated "sensor estimates" when there is no network.
#include <condition_variable>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct WeatherHour {
    std::string label;  // "3PM"
    int temp = 0;
    int precip = 0;
    std::string shortForecast;
};

struct WeatherData {
    bool valid = false;
    bool simulated = false;
    int temp = 0;      // F
    int humidity = 0;  // %
    int precip = 0;    // %
    int dewpointF = 0;
    std::string shortForecast;
    std::string wind;  // "10 mph NW"
    std::vector<WeatherHour> hours;
    time_t fetched = 0;
    std::string error;
};

struct WeatherAlert {
    std::string id, event, severity, headline, description, instruction, area;
    time_t expires = 0;
    // Warnings and anything Severe/Extreme put the tricorder on red alert.
    bool red() const {
        return severity == "Extreme" || severity == "Severe" || event.find("Warning") != std::string::npos;
    }
};

struct Location {
    double lat = 41.52, lon = -90.58;  // Quad Cities until we know better
    bool known = false;
};

// Parses a weather.gov hourly forecast document.
bool parseForecast(const std::string& body, WeatherData& out, std::string& err);
WeatherData simulatedWeather();
bool parseAlerts(const std::string& body, std::vector<WeatherAlert>& out, std::string& err);
// Centroid of a weather.gov grid point's polygon.
bool parseGridLocation(const std::string& body, Location& out);
time_t parseIsoTime(const std::string& iso);  // "2026-10-25T19:45:00-05:00" -> UTC time_t

class WeatherService {
public:
    ~WeatherService() { stop(); }
    void start(const std::string& url, int refreshMin, bool offline, double lat, double lon, bool alertTest);
    void stop();
    WeatherData get() const;
    void refreshNow();
    bool busy() const;
    std::vector<WeatherAlert> alerts() const;
    Location location() const;
    std::string issTle() const;  // latest ISS two-line elements (cached across restarts)

private:
    void loop();
    bool fetchForecast();
    void fetchAlerts();
    void ensureLocation();
    void fetchTle();

    std::string url_;
    int refreshMin_ = 10;
    bool offline_ = false;
    std::string cachePath_;
    std::thread thread_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    bool quit_ = false, refresh_ = false, busy_ = false;
    WeatherData data_;
    std::vector<WeatherAlert> alerts_;
    Location loc_;
    bool alertTest_ = false;
    std::string tle_;
    bool lastTleOk_ = false;
};

bool httpGet(const std::string& url, std::string& body, std::string& err);
