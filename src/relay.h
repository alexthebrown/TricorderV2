#pragma once
// Internet relay for visitors' messages (e.g. at a convention, where the tricorder rides
// on a phone hotspot that visitors shouldn't join). Visitors post to a topic on a free
// ntfy server (https://ntfy.sh) from their own phones; the tricorder polls the topic.
// Text messages can join in through an SMS forwarder that posts to the same topic.
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct RelayMessage {
    std::string id, from, text;
    long long time = 0;
};

// Parses ntfy's newline-delimited JSON; returns only "message" events.
std::vector<RelayMessage> parseNtfy(const std::string& body);
// Turns a raw relay message into what goes on screen: masks phone numbers, strips the
// "TRICORDER" keyword that texted messages start with, trims to sane lengths.
RelayMessage tidyRelayMessage(RelayMessage m);
std::string randomTopic();

class RelayClient {
public:
    ~RelayClient() { stop(); }
    void start(const std::string& server, const std::string& topic,
               std::function<void(const RelayMessage&)> onMessage);
    void stop();
    bool running() const { return thread_.joinable(); }
    std::string status() const;
    std::string topic() const { return topic_; }
    std::string server() const { return server_; }

private:
    void loop();

    std::string server_, topic_, statePath_;
    std::function<void(const RelayMessage&)> onMessage_;
    std::thread thread_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    bool quit_ = false;
    std::string status_ = "CONNECTING";
    long long since_ = 0;
    std::set<std::string> seen_;
};
