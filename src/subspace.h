#pragma once
// "Subspace" web link: a tiny HTTP server (default port 1701) serving a page for phones
// on the same network. It can send messages to the tricorder, press its buttons
// remotely, and upload new captain's logs into the video library.
#include <atomic>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "button.h"

struct SubspaceMessage {
    int id = 0;
    time_t when = 0;
    std::string from, text;
    std::string source;  // "web" (local page), "relay" (internet relay, incl. forwarded texts)
    bool read = false;
};

class SubspaceServer {
public:
    ~SubspaceServer() { stop(); }
    // uploadDir is asked for the destination each time a file arrives.
    bool start(int port, const std::string& pin, std::function<std::string()> uploadDir, const std::string& fontPath);
    void stop();
    bool running() const { return listenFd_ >= 0; }
    int port() const { return port_; }

    std::vector<SubspaceMessage> messages() const;  // newest first
    int unread() const;
    void markRead(size_t index);  // index into messages()
    bool popIncoming(SubspaceMessage& m);  // newly arrived, for the on-screen alert
    bool popButton(Button& b);             // remote button presses
    std::string uploadStatus() const;      // "RECEIVING X 42%" while an upload runs

    // moderated: hold for approval instead of showing straight away.
    void addMessage(const std::string& from, const std::string& text, bool moderated = false,
                    const std::string& source = "web");
    void setProfanityFilter(bool on) { filter_ = on; }
    // Lets /tricorder (the visitor page, also served locally for testing) find the relay.
    void setRelayInfo(std::function<std::pair<std::string, std::string>()> serverAndTopic) {
        std::lock_guard<std::mutex> lk(m_);
        relayInfo_ = std::move(serverAndTopic);
    }
    std::vector<SubspaceMessage> pending() const;  // awaiting clearance, oldest first
    bool approve(int id);
    bool reject(int id);

private:
    void loop();
    void handle(int fd);
    void load();
    void save() const;  // m_ held

    int listenFd_ = -1;
    int port_ = 0;
    std::string pin_, fontPath_, statePath_;
    std::function<std::string()> uploadDir_;
    std::thread thread_;
    std::atomic<bool> quit_{false};
    mutable std::mutex m_;
    std::deque<SubspaceMessage> messages_;  // newest first
    std::deque<SubspaceMessage> incoming_;
    std::deque<Button> buttons_;
    std::deque<SubspaceMessage> pending_;
    std::string uploadStatus_;
    std::atomic<bool> filter_{true};
    int nextId_ = 1;
    std::map<std::string, time_t> lastPost_;  // per-address flood limit
    std::function<std::pair<std::string, std::string>()> relayInfo_;
};

// Helpers exposed for unit tests.
std::string urlDecode(const std::string& s);
std::string formField(const std::string& body, const std::string& key);  // from a=b&c=d
std::string safeUploadName(const std::string& name);                     // "" if not allowed
std::string censor(const std::string& text);         // masks common profanity
std::string maskPhoneNumbers(const std::string& s);  // never put a visitor's number on screen
