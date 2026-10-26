// Small self-contained tests for parsing and formatting code.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <ctime>
#include <string>

#include "astro.h"
#include "config.h"
#include "json.h"
#include "orbit.h"
#include "relay.h"
#include "stardate.h"
#include "subspace.h"
#include "sysinfo.h"
#include "weather.h"

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static void testJson() {
    json::Value v;
    std::string err;
    CHECK(json::parse(R"({"a":[1,2.5,-3e2],"b":{"c":"x°\n"},"d":null,"e":true})", v, &err));
    CHECK(v["a"].size() == 3);
    CHECK(v["a"][1].number() == 2.5);
    CHECK(v["a"][2].number() == -300);
    CHECK(v["b"]["c"].string() == "x\xC2\xB0\n");
    CHECK(v["d"].isNull());
    CHECK(v["e"].b);
    CHECK(v["missing"]["deeper"][3].isNull());
    CHECK(!json::parse("{\"a\":}", v, &err));
    CHECK(!json::parse("[1,2", v, &err));
    CHECK(json::parse("\"\\ud83d\\ude80\"", v, &err) && v.string() == "\xF0\x9F\x9A\x80");
}

static void testForecast() {
    const char* doc = R"({"properties":{"periods":[
      {"startTime":"2026-10-25T14:00:00-05:00","temperature":61,"shortForecast":"Sunny",
       "probabilityOfPrecipitation":{"unitCode":"wmoUnit:percent","value":5},
       "relativeHumidity":{"value":48},"dewpoint":{"value":10.0},"windSpeed":"10 mph","windDirection":"NW"},
      {"startTime":"2026-10-25T15:00:00-05:00","temperature":63,"shortForecast":"Sunny",
       "probabilityOfPrecipitation":{"value":null},"relativeHumidity":{"value":45}},
      {"startTime":"2026-10-26T00:00:00-05:00","temperature":50,"shortForecast":"Clear",
       "probabilityOfPrecipitation":{"value":20},"relativeHumidity":{"value":70}}
    ]}})";
    WeatherData w;
    std::string err;
    CHECK(parseForecast(doc, w, err));
    CHECK(w.temp == 61);
    CHECK(w.precip == 5);
    CHECK(w.humidity == 48);
    CHECK(w.dewpointF == 50);
    CHECK(w.wind == "10 mph NW");
    CHECK(w.shortForecast == "Sunny");
    CHECK(w.hours.size() == 2);
    CHECK(w.hours[0].label == "3PM");
    CHECK(w.hours[0].precip == 0);
    CHECK(w.hours[1].label == "12AM");
    CHECK(!parseForecast("{\"properties\":{}}", w, err));
    WeatherData s = simulatedWeather();
    CHECK(s.valid && s.simulated && s.hours.size() == 12);
}

static void testStardate() {
    tm t{};
    t.tm_year = 2026 - 1900;
    t.tm_mon = 0;
    t.tm_mday = 1;
    t.tm_isdst = -1;
    double sd = stardate(mktime(&t));
    CHECK(sd >= 26000.0 && sd < 26000.1);
    t.tm_mon = 6;
    t.tm_mday = 2;
    double mid = stardate(mktime(&t));
    CHECK(mid > 26490 && mid < 26510);
}

static void testFormat() {
    CHECK(formatClock(0) == "0:00");
    CHECK(formatClock(75) == "1:15");
    CHECK(formatClock(3725) == "1:02:05");
    CHECK(formatDuration(59) == "0M 59S");
    CHECK(formatDuration(3 * 3600 + 12 * 60) == "3H 12M");
}

static void testConfig() {
    Config c;
    CHECK(c.set("overscan_x", "12") && c.overscanX == 12);
    CHECK(c.set("audio", "off") && !c.audio);
    CHECK(c.set("window_scale", "1.5") && c.windowScale == 1.5f);
    CHECK(!c.set("audio", "maybe"));
    CHECK(!c.set("no_such_key", "1"));
    std::string path = "/tmp/tricorder_test.conf";
    c.username = "Test User";
    CHECK(c.save(path, Config()));
    Config d;
    CHECK(d.load(path));
    CHECK(d.username == "Test User" && d.overscanX == 12 && !d.audio);
    // Unchanged settings aren't written, so a later default change still takes effect.
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("video_dir") == std::string::npos);
    std::remove(path.c_str());
}

static time_t utc(int Y, int M, int D, int h, int m) {
    tm t{};
    t.tm_year = Y - 1900;
    t.tm_mon = M - 1;
    t.tm_mday = D;
    t.tm_hour = h;
    t.tm_min = m;
    return timegm(&t);
}

static void testAstro() {
    using namespace astro;
    // 2024 total solar eclipse seen from Dallas: Sun and Moon coincide, Moon is new.
    time_t ecl = utc(2024, 4, 8, 18, 42);
    Horizontal s = altAz(Sun, ecl, 32.78, -96.80), m = altAz(Moon, ecl, 32.78, -96.80);
    CHECK(std::fabs(s.alt - m.alt) < 0.3 && std::fabs(s.az - m.az) < 0.3);
    CHECK(s.alt > 60 && s.alt < 70);
    CHECK(moonPhase(ecl).illumination < 0.01);
    // Total lunar eclipse 2025-03-14: full moon.
    CHECK(moonPhase(utc(2025, 3, 14, 6, 58)).illumination > 0.99);
    CHECK(moonPhase(utc(2025, 3, 14, 6, 58)).name == "FULL MOON");
    // Great conjunction 2020-12-21: Jupiter and Saturn about 0.1 degree apart.
    Equatorial j = position(Jupiter, utc(2020, 12, 21, 18, 0)), sa = position(Saturn, utc(2020, 12, 21, 18, 0));
    CHECK(std::fabs(j.ra - sa.ra) < 0.3 && std::fabs(j.dec - sa.dec) < 0.3);
    // Davenport, 2026-10-25: sunrise ~12:25 UTC (7:25 CDT), sunset ~23:06 UTC (18:06 CDT).
    RiseSet rs = riseSet(Sun, utc(2026, 10, 25, 5, 0), 41.52, -90.58);
    CHECK(std::llabs(rs.rise - utc(2026, 10, 25, 12, 25)) < 5 * 60);
    CHECK(std::llabs(rs.set - utc(2026, 10, 25, 23, 6)) < 5 * 60);
    CHECK(compassPoint(0) == "N" && compassPoint(100) == "E" && compassPoint(350) == "N");
}

static void testAlerts() {
    CHECK(parseIsoTime("2026-10-25T19:45:00-05:00") == utc(2026, 10, 26, 0, 45));
    CHECK(parseIsoTime("2026-10-25T19:45:00Z") == utc(2026, 10, 25, 19, 45));
    const char* doc = R"({"features":[
      {"properties":{"id":"a1","event":"Wind Advisory","severity":"Moderate","status":"Actual",
        "headline":"Wind Advisory","description":"Gusts to 45\nmph.\n\nSecond para","ends":"2026-10-25T21:00:00-05:00"}},
      {"properties":{"id":"a2","event":"Tornado Warning","severity":"Extreme","status":"Actual","areaDesc":"Scott, IA",
        "expires":"2026-10-25T19:45:00-05:00"}},
      {"properties":{"id":"a3","event":"Test Message","severity":"Unknown","status":"Test"}}]})";
    std::vector<WeatherAlert> a;
    std::string err;
    CHECK(parseAlerts(doc, a, err));
    CHECK(a.size() == 2);
    CHECK(a[0].id == "a2" && a[0].red());  // most serious first
    CHECK(!a[1].red());
    CHECK(a[1].description == "Gusts to 45 mph.\n\nSecond para");
    Location l;
    CHECK(parseGridLocation(R"({"geometry":{"coordinates":[[[-90,41],[-91,41],[-91,42],[-90,42]]]}})", l));
    CHECK(l.known && std::fabs(l.lat - 41.5) < 1e-9 && std::fabs(l.lon + 90.5) < 1e-9);
}

static void testSubspace() {
    CHECK(urlDecode("Hello+World%21%20ok") == "Hello World! ok");
    CHECK(formField("from=Kirk&text=Beam+me%20up&pin=1701", "text") == "Beam me up");
    CHECK(formField("from=Kirk&text=x", "pin").empty());
    CHECK(safeUploadName("../../etc/Log 1.MKV") == "Log 1.MKV");
    CHECK(safeUploadName("..\\evil\\.hidden.mp4") == "hidden.mp4");
    CHECK(safeUploadName("payload.sh").empty());
    CHECK(safeUploadName("a\nb.mp4") == "ab.mp4");
}

static void testOrbit() {
    // ISS elements (2026 day 298.499); propagated position checked against a live tracker.
    const char* tle = "ISS (ZARYA)\n"
                      "1 25544U 98067A   26298.49942685  .00007074  00000+0  13616-3 0  9991\n"
                      "2 25544  51.6337  15.2393 0006903 308.0649  51.9716 15.49050058591953\n";
    orbit::Tle t;
    CHECK(orbit::parseTle(tle, t));
    CHECK(std::fabs(t.incl - 51.6337) < 1e-9 && std::fabs(t.meanMotion - 15.49050058) < 1e-6);
    time_t when = utc(2026, 10, 26, 0, 0);
    double maxLat = 0;
    for (int m = 0; m < 93; ++m) {
        orbit::State s = orbit::propagate(t, when + m * 60);
        CHECK(s.altKm > 400 && s.altKm < 440);
        CHECK(s.speedKmh > 27000 && s.speedKmh < 28000);
        maxLat = std::max(maxLat, std::fabs(s.lat));
    }
    CHECK(maxLat > 50 && maxLat < 52.5);  // ground track reaches ~ the inclination
    // One orbital period (1440 / 15.49 min) later the latitude repeats.
    double period = 86400.0 / 15.4905;
    orbit::State a = orbit::propagate(t, when), b = orbit::propagate(t, when + time_t(period));
    CHECK(std::fabs(a.lat - b.lat) < 0.5);
    CHECK(!orbit::passes(t, when, 24, 41.5, -90.6).empty());
}

static void testModeration() {
    CHECK(censor("this is sh1t") == "this is s***");
    CHECK(censor("What the FUCK") == "What the F***");
    CHECK(censor("Take the cockpit to class, Hancock") == "Take the cockpit to class, Hancock");
    CHECK(censor("dick") == "d***");
    CHECK(censor("fire retardant") == "fire retardant");
    CHECK(maskPhoneNumbers("call 563-555-0199 now") == "call [NUMBER HIDDEN] now");
    CHECK(maskPhoneNumbers("+1 (319) 555-0123") == "[NUMBER HIDDEN]");
    CHECK(maskPhoneNumbers("stardate 4523.3 at 1701") == "stardate 4523.3 at 1701");

    SubspaceServer s;  // not started: just the queue
    s.addMessage("Visitor", "hello", true, "relay");
    CHECK(s.pending().size() == 1 && s.messages().empty());
    int id = s.pending()[0].id;
    CHECK(s.approve(id) && s.pending().empty() && s.messages().size() == 1);
    SubspaceMessage m;
    CHECK(s.popIncoming(m) && m.text == "hello");
    s.addMessage("Troll", "bad", true);
    CHECK(s.reject(s.pending()[0].id) && s.pending().empty());
    CHECK(!s.approve(9999));

    auto msgs = parseNtfy("{\"id\":\"a\",\"time\":1,\"event\":\"open\"}\n"
                          "{\"id\":\"b\",\"time\":2,\"event\":\"message\",\"title\":\"+1 319 555 0123\","
                          "\"message\":\"TRICORDER: beam me up\"}\n");
    CHECK(msgs.size() == 1 && msgs[0].id == "b");
    RelayMessage t = tidyRelayMessage(msgs[0]);
    CHECK(t.from == "TEXT MESSAGE" && t.text == "beam me up");
    RelayMessage anon = tidyRelayMessage({"x", "", "hi", 0});
    CHECK(anon.from == "UNKNOWN VESSEL");
    std::string topic = randomTopic();
    CHECK(topic.size() == 24 && topic.rfind("tricorder-", 0) == 0);
}

int main() {
    testModeration();
    testSubspace();
    testOrbit();
    testAstro();
    testAlerts();
    testJson();
    testForecast();
    testStardate();
    testFormat();
    testConfig();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
