#include "sysinfo.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#include "config.h"

namespace {

std::string readFirstLine(const char* path) {
    std::ifstream in(path);
    std::string s;
    std::getline(in, s);
    return s;
}

std::string run(const char* cmd) {
    FILE* p = popen(cmd, "r");
    if (!p) return "";
    std::string out;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
    return out;
}

// Split one line of `nmcli -t` output (':' separated, '\:' escaped).
std::vector<std::string> splitTerse(const std::string& line) {
    std::vector<std::string> f(1);
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) f.back() += line[++i];
        else if (line[i] == ':') f.emplace_back();
        else f.back() += line[i];
    }
    return f;
}

}  // namespace

void SysMonitor::update(const std::string& diskPath) {
    if (info_.model.empty()) {
        info_.model = piModel();
        if (info_.model.empty()) info_.model = "Desktop simulator";
        char hn[256] = {0};
        gethostname(hn, sizeof hn - 1);
        info_.hostname = hn;
    }

    std::string t = readFirstLine("/sys/class/thermal/thermal_zone0/temp");
    info_.cpuTempC = t.empty() ? -1 : std::atof(t.c_str()) / 1000.0;

    {
        std::ifstream in("/proc/stat");
        std::string cpu;
        uint64_t v[10] = {0};
        in >> cpu;
        for (auto& x : v) in >> x;
        uint64_t idle = v[3] + v[4], total = 0;
        for (auto x : v) total += x;
        if (prevTotal_ && total > prevTotal_)
            info_.cpuUsage = 1.0 - double(idle - prevIdle_) / double(total - prevTotal_);
        prevIdle_ = idle;
        prevTotal_ = total;
    }

    info_.load1 = std::atof(readFirstLine("/proc/loadavg").c_str());
    info_.uptimeSec = std::atof(readFirstLine("/proc/uptime").c_str());

    {
        std::ifstream in("/proc/meminfo");
        std::string key;
        long val;
        std::string unit;
        while (in >> key >> val >> unit) {
            if (key == "MemTotal:") info_.memTotalKB = val;
            else if (key == "MemAvailable:") info_.memAvailKB = val;
        }
    }

    struct statvfs vfs;
    if (!diskPath.empty() && statvfs(diskPath.c_str(), &vfs) == 0) {
        info_.diskTotal = uint64_t(vfs.f_blocks) * vfs.f_frsize;
        info_.diskFree = uint64_t(vfs.f_bavail) * vfs.f_frsize;
    }

    info_.ip.clear();
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) == 0) {
        for (ifaddrs* i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK)) continue;
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, buf, sizeof buf);
            info_.ip = buf;
            break;
        }
        freeifaddrs(ifs);
    }

    // /proc/net/wireless: "wlan0: 0000   54.  -56.  -256 ..."
    info_.wifiQuality = -1;
    std::ifstream wl("/proc/net/wireless");
    std::string line;
    for (int n = 0; std::getline(wl, line); ++n) {
        if (n < 2) continue;
        std::istringstream ss(line);
        std::string iface, status;
        double link = 0, level = 0;
        if (ss >> iface >> status >> link >> level) {
            info_.wifiQuality = std::clamp(int(link * 100 / 70), 0, 100);
            info_.wifiDbm = int(level);
            break;
        }
    }
}

void Prober::start() { thread_ = std::thread(&Prober::loop, this); }

void Prober::stop() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void Prober::setActive(bool a) {
    {
        std::lock_guard<std::mutex> lk(m_);
        active_ = a;
        if (a) now_ = true;
    }
    cv_.notify_all();
}

void Prober::scanNow() {
    {
        std::lock_guard<std::mutex> lk(m_);
        now_ = true;
    }
    cv_.notify_all();
}

ProbeResult Prober::get() const {
    std::lock_guard<std::mutex> lk(m_);
    return res_;
}

void Prober::loop() {
    bool haveNmcli = !run("command -v nmcli 2>/dev/null").empty();
    bool haveVcgencmd = !run("command -v vcgencmd 2>/dev/null").empty();
    uint64_t gen = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait_for(lk, std::chrono::seconds(15), [&] { return quit_ || now_; });
            if (quit_) return;
            bool go = now_ || active_;
            now_ = false;
            if (!go) continue;
        }
        scanning_ = true;
        ProbeResult r;

        if (haveNmcli) {
            std::string out = run("nmcli -t -f IN-USE,SSID,SIGNAL,CHAN,SECURITY dev wifi list 2>/dev/null");
            std::istringstream ss(out);
            std::string line;
            while (std::getline(ss, line)) {
                auto f = splitTerse(line);
                if (f.size() < 5) continue;
                WifiNet n;
                n.inUse = f[0] == "*";
                n.ssid = f[1].empty() ? "(HIDDEN)" : f[1];
                n.signal = std::atoi(f[2].c_str());
                n.channel = std::atoi(f[3].c_str());
                n.secure = !f[4].empty() && f[4] != "--";
                if (n.inUse) r.ssid = n.ssid;
                r.nets.push_back(n);
            }
            r.wifiReal = !r.nets.empty();
            std::sort(r.nets.begin(), r.nets.end(), [](const WifiNet& a, const WifiNet& b) { return a.signal > b.signal; });
        }
        if (!r.wifiReal) {
            // No scanner available: invent a handful of plausible "subspace" sources.
            static const char* names[] = {"STARFLEET-COMM", "USS-FARRAGUT", "K-7 STATION", "DEEP SPACE RELAY", "ROMULAN?"};
            int phase = int(time(nullptr) / 10);
            for (int i = 0; i < 5; ++i) {
                WifiNet n;
                n.ssid = names[i];
                n.signal = std::clamp(80 - i * 14 + int(8 * std::sin(phase * 0.9 + i)), 5, 100);
                n.channel = 1 + (i * 5) % 11;
                n.secure = i != 2;
                r.nets.push_back(n);
            }
        }

        // Life signs: hosts our ARP table has resolved, plus ourselves.
        std::ifstream arp("/proc/net/arp");
        std::string line;
        int count = 0;
        std::getline(arp, line);
        while (std::getline(arp, line)) {
            std::istringstream ss(line);
            std::string ip, hw, flags;
            if (ss >> ip >> hw >> flags && flags != "0x0") ++count;
        }
        r.neighbours = count + 1;

        std::string thr = readFirstLine("/sys/devices/platform/soc/soc:firmware/get_throttled");
        if (thr.empty() && haveVcgencmd) {
            std::string o = run("vcgencmd get_throttled 2>/dev/null");  // throttled=0x50000
            size_t eq = o.find('=');
            if (eq != std::string::npos) thr = o.substr(eq + 1);
        }
        if (!thr.empty()) {
            r.throttleKnown = true;
            r.throttled = uint32_t(strtoul(thr.c_str(), nullptr, 16));
        }

        r.generation = ++gen;
        {
            std::lock_guard<std::mutex> lk(m_);
            res_ = r;
        }
        scanning_ = false;
    }
}

std::string formatDuration(double sec) {
    long s = long(sec);
    char buf[64];
    if (s >= 86400) snprintf(buf, sizeof buf, "%ldD %ldH", s / 86400, (s % 86400) / 3600);
    else if (s >= 3600) snprintf(buf, sizeof buf, "%ldH %02ldM", s / 3600, (s % 3600) / 60);
    else snprintf(buf, sizeof buf, "%ldM %02ldS", s / 60, s % 60);
    return buf;
}

std::string formatClock(double sec) {
    if (sec < 0) sec = 0;
    long s = long(sec);
    char buf[32];
    if (s >= 3600) snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", s / 3600, (s % 3600) / 60, s % 60);
    else snprintf(buf, sizeof buf, "%ld:%02ld", s / 60, s % 60);
    return buf;
}
