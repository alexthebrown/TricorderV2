#pragma once
// Real readings for the STATUS and SENSORS pages: cheap /proc and /sys reads on the UI
// thread, and slow probes (Wi-Fi scan, neighbours, firmware) on a background thread.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct SysInfo {
    double cpuTempC = -1;
    double cpuUsage = -1;  // 0..1
    double load1 = 0;
    long memTotalKB = 0, memAvailKB = 0;
    double uptimeSec = 0;
    uint64_t diskTotal = 0, diskFree = 0;
    std::string ip;
    std::string hostname;
    int wifiQuality = -1;  // 0..100, -1 unknown
    int wifiDbm = 0;
    std::string model;
};

class SysMonitor {
public:
    void update(const std::string& diskPath);  // call ~1/s
    const SysInfo& info() const { return info_; }

private:
    SysInfo info_;
    uint64_t prevIdle_ = 0, prevTotal_ = 0;
};

struct WifiNet {
    std::string ssid;
    int signal = 0;  // 0..100
    int channel = 0;
    bool inUse = false;
    bool secure = false;
};

struct ProbeResult {
    bool wifiReal = false;  // false = nmcli unavailable; list is simulated
    std::vector<WifiNet> nets;
    int neighbours = -1;   // devices seen on the LAN ("life signs")
    std::string ssid;      // network we're connected to
    bool throttleKnown = false;
    uint32_t throttled = 0;  // vcgencmd get_throttled bits
    uint64_t generation = 0;
};

class Prober {
public:
    ~Prober() { stop(); }
    void start();
    void stop();
    void setActive(bool a);  // only scan while a page needs it
    void scanNow();
    ProbeResult get() const;
    bool scanning() const { return scanning_; }

private:
    void loop();
    std::thread thread_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    bool quit_ = false, active_ = false, now_ = false;
    std::atomic<bool> scanning_{false};
    ProbeResult res_;
};

std::string formatDuration(double sec);  // "3H 12M" / "12M 04S"
std::string formatClock(double sec);     // "1:02:03" / "12:04"
