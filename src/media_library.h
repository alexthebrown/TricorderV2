#pragma once
// Captain's log entries: a browsable folder tree of videos (a Plex library on the
// network drive, or a local folder). Listing, mounting and probing all happen on a
// background thread so a slow or offline drive never freezes the UI.
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct MediaItem {
    std::string path;      // file or folder on disk
    std::string name;      // display name
    bool folder = false;
    std::string playPath;  // what ENTER plays: the file itself, or a movie folder's main video
    int videoCount = -1;   // folders: videos inside (-1 = not looked yet)
    double duration = -1;  // seconds of playPath (-1 = not probed, 0 = unknown)
    bool playable() const { return !playPath.empty(); }
};

class MediaLibrary {
public:
    ~MediaLibrary() { stop(); }
    // videoDir may be a local path or smb://host/share/path.
    void start(const std::string& videoDir, const std::vector<std::string>& fallbacks,
               const std::vector<std::string>& hiddenFolders);
    void stop();

    void open(const std::string& folder);  // absolute folder path; "" = library root
    void refresh() { open(folder()); }
    bool loading() const;
    std::vector<MediaItem> items() const;
    std::string folder() const;     // folder currently listed
    std::string root() const;       // resolved library root ("" while unresolved/offline)
    bool usingFallback() const;
    std::string rootLabel() const;  // e.g. "PLEX" or "LOCAL ARCHIVE"
    std::string status() const;     // why the root is unavailable, if it is

    double resumePosition(const std::string& path) const;
    void setResumePosition(const std::string& path, double pos);  // pos <= 0 clears

private:
    void worker();
    bool resolveRoot();  // worker thread
    std::vector<MediaItem> listFolder(const std::string& dir, bool& ok);
    void saveResume() const;

    std::string configured_;
    std::vector<std::string> fallbacks_;
    std::set<std::string> hidden_;

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::thread thread_;
    bool quit_ = false;
    std::string request_;        // folder requested by the UI
    bool hasRequest_ = false;
    std::atomic<int> generation_{0};
    bool loading_ = false;

    std::string root_, folder_, status_;
    bool fallback_ = false;
    std::vector<MediaItem> items_;
    std::map<std::string, double> durations_;
    std::map<std::string, double> resume_;
    std::string resumePath_;
};

// Maps smb://host/share/path to where it is mounted locally (GNOME gvfs on a desktop,
// /mnt/<share> from tools/setup_network_drive.sh on the Pi). Returns "" if not mounted.
std::string resolveSmbUrl(const std::string& url, std::string* share = nullptr, std::string* host = nullptr);
bool isVideoFile(const std::string& path);
std::string sortKey(const std::string& name);
