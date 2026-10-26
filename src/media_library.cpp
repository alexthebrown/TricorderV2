#include "media_library.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "config.h"

extern "C" {
#include <libavformat/avformat.h>
}

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

// Plex "inline extras" naming: "Gag Reel -featurette.mkv" etc.
bool isExtra(const std::string& stem) {
    static const char* tags[] = {"-behindthescenes", "-deleted", "-featurette", "-interview", "-scene",
                                 "-short", "-trailer", "-other", "-sample"};
    std::string s = lower(stem);
    for (auto t : tags)
        if (endsWith(s, t)) return true;
    return s == "sample";
}

bool isHiddenName(const std::string& name) {
    static const char* junk[] = {"@eaDir", "#recycle", "#snapshot", "lost+found", "Plex Versions", "$RECYCLE.BIN",
                                 "System Volume Information"};
    if (name.empty() || name[0] == '.') return true;
    for (auto j : junk)
        if (name == j) return true;
    return false;
}

double probeDuration(const std::string& path) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return 0;
    if (fmt->duration <= 0) avformat_find_stream_info(fmt, nullptr);
    double d = fmt->duration > 0 ? fmt->duration / double(AV_TIME_BASE) : 0;
    avformat_close_input(&fmt);
    return d;
}

std::string run(const std::string& cmd) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    std::string out;
    char buf[256];
    while (fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
    return out;
}

}  // namespace

bool isVideoFile(const std::string& path) {
    fs::path p(path);
    std::string ext = lower(p.extension().string());
    static const char* exts[] = {".mp4", ".m4v", ".mkv", ".mov", ".avi", ".webm", ".mpg", ".mpeg", ".ts", ".wmv"};
    for (auto e : exts)
        if (ext == e) return true;
    if (!ext.empty() && ext.size() <= 5) return false;
    // Some files on the drive have lost their extension: sniff the container header.
    unsigned char h[12] = {0};
    std::ifstream in(path, std::ios::binary);
    if (!in.read(reinterpret_cast<char*>(h), sizeof h)) return false;
    if (h[0] == 0x1A && h[1] == 0x45 && h[2] == 0xDF && h[3] == 0xA3) return true;  // Matroska/WebM
    if (memcmp(h + 4, "ftyp", 4) == 0) return true;                                 // MP4/MOV
    if (memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "AVI ", 4) == 0) return true;    // AVI
    return false;
}

std::string sortKey(const std::string& name) {
    std::string s = lower(name);
    for (const char* art : {"the ", "a ", "an "}) {
        size_t n = strlen(art);
        if (s.size() > n && s.compare(0, n, art) == 0) return s.substr(n);
    }
    return s;
}

std::string resolveSmbUrl(const std::string& url, std::string* shareOut, std::string* hostOut) {
    if (url.rfind("smb://", 0) != 0) return "";
    std::string rest = url.substr(6);
    size_t s1 = rest.find('/');
    if (s1 == std::string::npos) return "";
    std::string host = rest.substr(0, s1);
    std::string tail = rest.substr(s1 + 1);
    size_t s2 = tail.find('/');
    std::string share = tail.substr(0, s2);
    std::string sub = s2 == std::string::npos ? "" : tail.substr(s2 + 1);
    while (!sub.empty() && sub.back() == '/') sub.pop_back();
    if (shareOut) *shareOut = share;
    if (hostOut) *hostOut = host;

    std::string uid = std::to_string(getuid());
    std::vector<std::string> mounts = {
        "/run/user/" + uid + "/gvfs/smb-share:server=" + host + ",share=" + share,
        "/run/user/" + uid + "/gvfs/smb-share:server=" + lower(host) + ",share=" + lower(share),
        "/mnt/" + share,
        homeDir() + "/networkdrive/" + share,
        homeDir() + "/networkdrive",
    };
    for (auto& m : mounts) {
        std::string p = sub.empty() ? m : m + "/" + sub;
        if (dirExists(p)) return p;
    }
    return "";
}

void MediaLibrary::start(const std::string& videoDir, const std::vector<std::string>& fallbacks,
                         const std::vector<std::string>& hiddenFolders) {
    configured_ = videoDir;
    fallbacks_ = fallbacks;
    hidden_.insert(hiddenFolders.begin(), hiddenFolders.end());
    resumePath_ = stateDir() + "/resume.txt";
    std::ifstream in(resumePath_);
    double pos;
    std::string path;
    while (in >> pos && std::getline(in >> std::ws, path)) resume_[path] = pos;
    thread_ = std::thread(&MediaLibrary::worker, this);
    open("");
}

void MediaLibrary::stop() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void MediaLibrary::open(const std::string& folder) {
    {
        std::lock_guard<std::mutex> lk(m_);
        request_ = folder;
        hasRequest_ = true;
        loading_ = true;
        ++generation_;
    }
    cv_.notify_all();
}

bool MediaLibrary::loading() const {
    std::lock_guard<std::mutex> lk(m_);
    return loading_;
}

std::vector<MediaItem> MediaLibrary::items() const {
    std::lock_guard<std::mutex> lk(m_);
    return items_;
}

std::string MediaLibrary::folder() const {
    std::lock_guard<std::mutex> lk(m_);
    return folder_;
}

std::string MediaLibrary::root() const {
    std::lock_guard<std::mutex> lk(m_);
    return root_;
}

bool MediaLibrary::usingFallback() const {
    std::lock_guard<std::mutex> lk(m_);
    return fallback_;
}

std::string MediaLibrary::status() const {
    std::lock_guard<std::mutex> lk(m_);
    return status_;
}

std::string MediaLibrary::rootLabel() const {
    std::lock_guard<std::mutex> lk(m_);
    if (fallback_) return "LOCAL ARCHIVE";
    std::string name = fs::path(root_).filename().string();
    if (name.find("smb-share:") == 0) name = name.substr(name.find("share=") + 6);
    for (auto& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));
    return name.empty() ? "LOGS" : name;
}

double MediaLibrary::resumePosition(const std::string& path) const {
    std::lock_guard<std::mutex> lk(m_);
    auto it = resume_.find(path);
    return it == resume_.end() ? 0 : it->second;
}

void MediaLibrary::setResumePosition(const std::string& path, double pos) {
    std::lock_guard<std::mutex> lk(m_);
    if (pos <= 0) resume_.erase(path);
    else resume_[path] = pos;
    saveResume();
}

void MediaLibrary::saveResume() const {
    std::ofstream out(resumePath_);
    for (auto& kv : resume_) out << kv.second << " " << kv.first << "\n";
}

bool MediaLibrary::resolveRoot() {
    std::string path, status;
    bool fb = false;
    if (configured_.rfind("smb://", 0) == 0) {
        std::string share, host;
        path = resolveSmbUrl(configured_, &share, &host);
        if (path.empty() && dirExists("/run/user/" + std::to_string(getuid())) &&
            !run("command -v gio 2>/dev/null").empty()) {
            // Desktop: ask GNOME to mount it (works when the password is in the keyring).
            run("timeout 15 gio mount 'smb://" + host + "/" + share + "' </dev/null >/dev/null 2>&1");
            path = resolveSmbUrl(configured_);
        }
        if (path.empty()) status = "NETWORK DRIVE NOT MOUNTED";
    } else if (dirExists(configured_)) {
        path = configured_;
    } else {
        status = "VIDEO FOLDER NOT FOUND";
    }
    if (path.empty()) {
        for (auto& f : fallbacks_)
            if (dirExists(f)) {
                path = f;
                fb = true;
                break;
            }
    }
    std::lock_guard<std::mutex> lk(m_);
    root_ = path;
    fallback_ = fb;
    status_ = status;
    return !path.empty();
}

std::vector<MediaItem> MediaLibrary::listFolder(const std::string& dir, bool& ok) {
    std::vector<MediaItem> folders, files;
    std::error_code ec;
    ok = false;
    fs::directory_iterator it(dir, ec), end;
    if (ec) return {};
    ok = true;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        std::string name = it->path().filename().string();
        if (isHiddenName(name) || hidden_.count(name)) continue;
        std::error_code e2;
        if (it->is_directory(e2)) {
            MediaItem m;
            m.path = it->path().string();
            m.name = name;
            m.folder = true;
            folders.push_back(m);
        } else if (it->is_regular_file(e2) && isVideoFile(it->path().string())) {
            MediaItem m;
            m.path = m.playPath = it->path().string();
            // Strip a real extension; extensionless files ("Mr. Robot - S01E01") keep their whole name.
            bool realExt = it->path().has_extension() && it->path().extension().string().size() <= 5;
            m.name = realExt ? it->path().stem().string() : name;
            files.push_back(m);
        }
    }
    // TV episodes all start with "Show (Year) - ": drop the shared prefix.
    if (files.size() >= 2) {
        size_t dash = files[0].name.find(" - ");
        if (dash != std::string::npos) {
            std::string prefix = files[0].name.substr(0, dash + 3);
            bool all = std::all_of(files.begin(), files.end(), [&](const MediaItem& m) {
                return m.name.size() > prefix.size() && m.name.compare(0, prefix.size(), prefix) == 0;
            });
            if (all)
                for (auto& m : files) m.name = m.name.substr(prefix.size());
        }
    }
    auto byName = [](const MediaItem& a, const MediaItem& b) { return sortKey(a.name) < sortKey(b.name); };
    std::sort(folders.begin(), folders.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    folders.insert(folders.end(), files.begin(), files.end());
    return folders;
}

void MediaLibrary::worker() {
    for (;;) {
        std::string req;
        int gen;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [&] { return quit_ || hasRequest_; });
            if (quit_) return;
            req = request_;
            hasRequest_ = false;
            gen = generation_;
        }
        auto stale = [&] {
            std::lock_guard<std::mutex> lk(m_);
            return quit_ || gen != generation_;
        };

        // Opening the root re-checks the drive, so it is picked up once it comes online.
        if (req.empty() || root().empty()) resolveRoot();
        std::string dir = req.empty() ? root() : req;
        bool ok = false;
        std::vector<MediaItem> list;
        if (!dir.empty()) list = listFolder(dir, ok);
        for (auto& m : list) {
            std::lock_guard<std::mutex> lk(m_);
            auto d = durations_.find(m.playPath);
            if (d != durations_.end()) m.duration = d->second;
        }
        {
            std::lock_guard<std::mutex> lk(m_);
            if (gen != generation_) continue;
            folder_ = dir;
            items_ = list;
            loading_ = false;
            if (!dir.empty() && !ok) status_ = "UNABLE TO READ FOLDER";
        }

        // Fill in details one item at a time: what's inside each folder, then running times.
        for (size_t i = 0; i < list.size() && !stale(); ++i) {
            MediaItem m = list[i];
            if (m.folder) {
                bool sok;
                auto inside = listFolder(m.path, sok);
                int videos = 0, mains = 0;
                std::string main;
                for (auto& x : inside) {
                    if (x.folder) continue;
                    ++videos;
                    if (!isExtra(fs::path(x.path).stem().string())) {
                        ++mains;
                        main = x.path;
                    }
                }
                m.videoCount = int(inside.size());
                // A movie folder (one feature + extras) plays straight away.
                if (mains == 1 && int(inside.size()) - videos <= 2) m.playPath = main;
            }
            if (m.playable() && m.duration < 0) {
                double d;
                {
                    std::lock_guard<std::mutex> lk(m_);
                    auto it = durations_.find(m.playPath);
                    d = it != durations_.end() ? it->second : -1;
                }
                if (d < 0) d = probeDuration(m.playPath);
                m.duration = d;
            }
            std::lock_guard<std::mutex> lk(m_);
            if (gen != generation_) break;
            if (m.playable()) durations_[m.playPath] = m.duration;
            if (i < items_.size() && items_[i].path == m.path) items_[i] = m;
        }
    }
}
