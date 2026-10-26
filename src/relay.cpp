#include "relay.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

#include "config.h"
#include "json.h"
#include "subspace.h"
#include "weather.h"  // httpGet

std::vector<RelayMessage> parseNtfy(const std::string& body) {
    std::vector<RelayMessage> out;
    std::istringstream in(body);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        json::Value v;
        if (!json::parse(line, v) || v["event"].string() != "message") continue;
        RelayMessage m;
        m.id = v["id"].string();
        m.from = v["title"].string();
        m.text = v["message"].string();
        m.time = (long long)v["time"].number();
        out.push_back(m);
    }
    return out;
}

RelayMessage tidyRelayMessage(RelayMessage m) {
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n:-");
        if (a == std::string::npos) return std::string();
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    };
    // Texts arrive as "TRICORDER <message>": drop the keyword.
    std::string up = m.text.substr(0, 9);
    for (auto& c : up) c = char(toupper(static_cast<unsigned char>(c)));
    if (up == "TRICORDER") m.text = m.text.substr(9);
    std::string text;
    for (char c : m.text) text += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    m.text = trim(maskPhoneNumbers(text));
    if (m.text.size() > 300) m.text = m.text.substr(0, 300);
    std::string from = trim(maskPhoneNumbers(m.from));
    // A forwarder that only knows the sender's number leaves nothing useful after masking.
    if (from.empty() || from.find("[NUMBER HIDDEN]") != std::string::npos) from = m.from.empty() ? "UNKNOWN VESSEL" : "TEXT MESSAGE";
    if (from.size() > 40) from = from.substr(0, 40);
    m.from = from;
    return m;
}

std::string randomTopic() {
    static const char chars[] = "abcdefghijkmnpqrstuvwxyz23456789";
    std::random_device rd;
    std::string t = "tricorder-";
    for (int i = 0; i < 14; ++i) t += chars[rd() % (sizeof chars - 1)];
    return t;
}

void RelayClient::start(const std::string& server, const std::string& topic,
                        std::function<void(const RelayMessage&)> onMessage) {
    stop();
    server_ = server;
    while (!server_.empty() && server_.back() == '/') server_.pop_back();
    topic_ = topic;
    onMessage_ = std::move(onMessage);
    statePath_ = stateDir() + "/relay_since.txt";
    since_ = 0;
    std::ifstream in(statePath_);
    std::string savedTopic;
    long long t = 0;
    // Resume where we left off for the same topic, so nothing is shown twice or missed.
    if (in >> savedTopic >> t && savedTopic == topic_) since_ = t;
    if (since_ == 0) since_ = (long long)time(nullptr);
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = false;
        status_ = "CONNECTING";
    }
    thread_ = std::thread(&RelayClient::loop, this);
}

void RelayClient::stop() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

std::string RelayClient::status() const {
    std::lock_guard<std::mutex> lk(m_);
    return status_;
}

void RelayClient::loop() {
    int failures = 0;
    for (;;) {
        std::string url = server_ + "/" + topic_ + "/json?poll=1&since=" + std::to_string(since_);
        std::string body, err;
        bool ok = httpGet(url, body, err);
        if (ok) {
            failures = 0;
            for (auto& m : parseNtfy(body)) {
                if (seen_.count(m.id)) continue;  // `since` is inclusive of the last second
                seen_.insert(m.id);
                if (m.time > since_) since_ = m.time;
                onMessage_(tidyRelayMessage(m));
            }
            if (seen_.size() > 500) seen_.clear();
            // +1: on restart, don't re-show the last message we already handled.
            std::ofstream(statePath_) << topic_ << " " << since_ + 1 << "\n";
        } else {
            ++failures;
        }
        {
            std::unique_lock<std::mutex> lk(m_);
            status_ = ok ? "ONLINE" : "NO SIGNAL - RETRYING";
            // ntfy.sh allows a request every ~5 s per client; back off when offline.
            int wait = ok ? 6 : std::min(60, 6 * (1 + failures));
            cv_.wait_for(lk, std::chrono::seconds(wait), [&] { return quit_; });
            if (quit_) return;
        }
    }
}
