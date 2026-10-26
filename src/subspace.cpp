#include "subspace.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "config.h"

namespace {

constexpr size_t kMaxMessages = 50;
constexpr long long kMaxUpload = 16LL << 30;  // 16 GB

const char* kPage = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tricorder Subspace Link</title>
<style>
@font-face{font-family:Trek;src:url(/font.ttf)}
:root{--g:#86DF64;--y:#DAD778;--r:#E84A2E}
body{background:#000;color:var(--y);font-family:Trek,"Arial Narrow",sans-serif;margin:0;padding:16px;max-width:560px;margin:auto}
h1{color:var(--y);font-size:34px;margin:4px 0 0}h2{color:var(--g);font-size:22px;margin:22px 0 8px;letter-spacing:1px}
.sub{color:var(--g);font-size:15px}
button{background:var(--g);color:#000;border:3px outset #c3efb1;font:inherit;font-size:20px;padding:8px 14px;cursor:pointer}
button:active{border-style:inset}
input,textarea{width:100%;box-sizing:border-box;background:#111;color:var(--y);border:2px solid var(--g);font:inherit;font-size:18px;padding:8px;margin:4px 0}
.pad{display:grid;grid-template-columns:repeat(3,72px);gap:8px;justify-content:center}
.pad button{height:60px;font-size:24px;padding:0}
.msg{border-left:4px solid var(--g);padding:4px 10px;margin:8px 0;color:var(--y)}
.msg b{color:var(--g)}.msg small{color:#777}
progress{width:100%;height:16px;accent-color:var(--g)}
#status{color:var(--r);min-height:1.2em}
</style></head><body>
<h1>TRICORDER</h1><div class="sub">SUBSPACE LINK ESTABLISHED</div>
<div id="pinbox" style="display:none"><h2>ACCESS CODE</h2><input id="pin" type="password" inputmode="numeric" placeholder="PIN"></div>
<h2>SEND TRANSMISSION</h2>
<input id="from" maxlength="40" placeholder="From (e.g. Starfleet Command)">
<textarea id="text" rows="3" maxlength="500" placeholder="Message"></textarea>
<button onclick="send()">TRANSMIT</button>
<h2>REMOTE CONTROL</h2>
<div class="pad"><span></span><button onclick="btn('up')">&#9650;</button><span></span>
<button onclick="btn('left')">&#9664;</button><button onclick="btn('enter')">OK</button><button onclick="btn('right')">&#9654;</button>
<button onclick="btn('back')" style="font-size:16px">BACK</button><button onclick="btn('down')">&#9660;</button><span></span></div>
<h2>UPLOAD CAPTAIN'S LOG</h2>
<input id="file" type="file" accept="video/*,.mkv">
<button onclick="upload()">UPLOAD</button> <progress id="prog" value="0" max="100" style="display:none"></progress>
<div id="status"></div>
<div id="modbox" style="display:none"><h2 style="color:var(--r)">AWAITING CLEARANCE</h2><div id="pending"></div></div>
<h2>RECENT TRANSMISSIONS</h2><div id="log"></div>
<script>
const $=id=>document.getElementById(id);
$('from').value=localStorage.from||'';$('pin').value=localStorage.pin||'';
function q(){localStorage.pin=$('pin').value;return 'pin='+encodeURIComponent($('pin').value)}
function say(t){$('status').textContent=t}
async function post(url,body){const r=await fetch(url,{method:'POST',body});if(!r.ok)say(r.status==403?'ACCESS DENIED - CHECK PIN':'ERROR '+r.status);return r.ok}
async function send(){localStorage.from=$('from').value;
 const b=new URLSearchParams({from:$('from').value,text:$('text').value,pin:$('pin').value});
 if(await post('/api/message',b)){$('text').value='';say('TRANSMISSION SENT');refresh()}}
function btn(b){post('/api/button?b='+b+'&'+q())}
function upload(){const f=$('file').files[0];if(!f)return;const x=new XMLHttpRequest();
 x.open('POST','/api/upload?name='+encodeURIComponent(f.name)+'&'+q());
 $('prog').style.display='';x.upload.onprogress=e=>{$('prog').value=e.loaded/e.total*100};
 x.onload=()=>{say(x.status==200?'LOG STORED: '+f.name:(x.status==403?'ACCESS DENIED':'UPLOAD FAILED: '+x.responseText))};
 x.onerror=()=>say('UPLOAD FAILED');say('UPLOADING...');x.send(f)}
async function refresh(){const s=await (await fetch('/api/state')).json();$('pinbox').style.display=s.pin?'':'none';
 $('log').innerHTML=s.messages.map(m=>'<div class="msg"><b></b> <small></small><div></div></div>').join('');
 [...$('log').children].forEach((d,i)=>{const m=s.messages[i];d.querySelector('b').textContent=m.from;
  d.querySelector('small').textContent=new Date(m.when*1000).toLocaleString();d.querySelector('div').textContent=m.text})}
async function mod(id,ok){await post('/api/moderate?id='+id+'&ok='+ok+'&'+q());pend();refresh()}
async function pend(){const r=await fetch('/api/pending?'+q());if(!r.ok){$('modbox').style.display='none';return}
 const list=await r.json();$('modbox').style.display=list.length?'':'none';
 $('pending').innerHTML=list.map(m=>'<div class="msg" style="border-color:var(--r)"><b></b><div></div>'+
  '<button onclick="mod('+m.id+',1)">DISPLAY</button> <button onclick="mod('+m.id+',0)" style="background:var(--r)">REJECT</button></div>').join('');
 [...$('pending').children].forEach((d,i)=>{d.querySelector('b').textContent=list[i].from;d.querySelector('div').textContent=list[i].text})}
refresh();pend();setInterval(refresh,10000);setInterval(pend,4000);
</script></body></html>)HTML";

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += char(c);
                }
        }
    }
    return o;
}

bool sendAll(int fd, const char* p, size_t n) {
    while (n > 0) {
        ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) return false;
        p += w;
        n -= size_t(w);
    }
    return true;
}

void respond(int fd, int code, const std::string& type, const std::string& body) {
    const char* status = code == 200 ? "OK" : code == 403 ? "Forbidden" : code == 404 ? "Not Found"
                       : code == 429 ? "Too Many Requests"
                       : code == 413 ? "Payload Too Large" : code == 400 ? "Bad Request" : "Error";
    std::string h = "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\nContent-Type: " + type +
                    "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    sendAll(fd, h.data(), h.size());
    sendAll(fd, body.data(), body.size());
}

// Strip CR/LF/tabs and other control characters from user-supplied text.
std::string clean(const std::string& s, size_t maxLen, bool keepNewlines) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '\n' && keepNewlines) o += ' ';
        else if (c >= 0x20 && c != 0x7F) o += char(c);
        if (o.size() >= maxLen) break;
    }
    return o;
}

}  // namespace

std::string censor(const std::string& text) {
    // Roots caught anywhere inside a word, and short words only caught on their own.
    // Roots are only the unambiguous ones, so "cockpit", "Moby Dick" and "fire retardant" survive.
    static const char* roots[] = {"fuck", "shit", "cunt", "bitch", "pussy", "asshole", "bastard",
                                  "slut", "whore", "faggot", "nigger", "nigga", "twat", "wank", "porn"};
    static const char* words[] = {"ass", "dick", "cock", "tits", "dildo", "cum", "jizz", "nazi", "kys",
                                  "fag", "retard", "retarded", "penis", "vagina", "dicks", "cocks"};
    std::string out = text;
    size_t i = 0;
    while (i < out.size()) {
        if (!isalnum(static_cast<unsigned char>(out[i])) && out[i] != '@' && out[i] != '$') {
            ++i;
            continue;
        }
        size_t j = i;
        std::string norm;
        while (j < out.size() && (isalnum(static_cast<unsigned char>(out[j])) || out[j] == '@' || out[j] == '$' ||
                                  out[j] == '*')) {
            char c = char(tolower(static_cast<unsigned char>(out[j])));
            switch (c) {  // undo common l33t substitutions
                case '0': c = 'o'; break;
                case '1': case '!': c = 'i'; break;
                case '3': c = 'e'; break;
                case '4': case '@': c = 'a'; break;
                case '5': case '$': c = 's'; break;
                case '7': c = 't'; break;
                default: break;
            }
            norm += c;
            ++j;
        }
        bool bad = false;
        for (auto r : roots) bad = bad || norm.find(r) != std::string::npos;
        for (auto w : words) bad = bad || norm == w || norm == std::string(w) + "s";
        if (bad)
            for (size_t k = i + 1; k < j; ++k) out[k] = '*';
        i = j;
    }
    return out;
}

std::string maskPhoneNumbers(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        // A run of digits with the usual separators, containing 7+ digits, is a phone number.
        size_t j = i;
        int digits = 0;
        auto sep = [&](char c) { return c == '-' || c == ' ' || c == '.' || c == '(' || c == ')' || c == '+'; };
        while (j < s.size() && (isdigit(static_cast<unsigned char>(s[j])) ||
                                (sep(s[j]) && (j > i || s[j] == '+' || s[j] == '(')))) {
            digits += isdigit(static_cast<unsigned char>(s[j])) ? 1 : 0;
            ++j;
        }
        if ((isdigit(static_cast<unsigned char>(s[i])) || s[i] == '+' || s[i] == '(') && digits >= 7) {
            while (j > i && !isdigit(static_cast<unsigned char>(s[j - 1]))) --j;  // keep trailing separators
            out += "[NUMBER HIDDEN]";
            i = j;
        } else {
            out += s[i++];
        }
    }
    return out;
}

std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                 isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            o += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            o += s[i];
        }
    }
    return o;
}

std::string formField(const std::string& body, const std::string& key) {
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t amp = body.find('&', pos);
        std::string pair = body.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos && urlDecode(pair.substr(0, eq)) == key) return urlDecode(pair.substr(eq + 1));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return "";
}

std::string safeUploadName(const std::string& name) {
    std::string base = name;
    size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    std::string o;
    for (unsigned char c : base)
        if (c >= 0x20 && c != 0x7F && c != ':' && c != '*' && c != '?' && c != '"' && c != '<' && c != '>' && c != '|')
            o += char(c);
    while (!o.empty() && (o[0] == '.' || o[0] == ' ')) o.erase(0, 1);
    if (o.size() > 200) o = o.substr(o.size() - 200);
    std::string lower = o;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(tolower(c)); });
    static const char* exts[] = {".mp4", ".m4v", ".mkv", ".mov", ".avi", ".webm", ".mpg", ".mpeg"};
    for (auto e : exts) {
        size_t n = strlen(e);
        if (lower.size() > n && lower.compare(lower.size() - n, n, e) == 0) return o;
    }
    return "";
}

bool SubspaceServer::start(int port, const std::string& pin, std::function<std::string()> uploadDir,
                           const std::string& fontPath) {
    pin_ = pin;
    uploadDir_ = std::move(uploadDir);
    fontPath_ = fontPath;
    statePath_ = stateDir() + "/messages.txt";
    load();
    if (port <= 0) return false;
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(uint16_t(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || listen(fd, 8) < 0) {
        fprintf(stderr, "subspace: port %d: %s\n", port, strerror(errno));
        close(fd);
        return false;
    }
    listenFd_ = fd;
    port_ = port;
    quit_ = false;
    thread_ = std::thread(&SubspaceServer::loop, this);
    fprintf(stderr, "subspace: listening on port %d\n", port);
    return true;
}

void SubspaceServer::stop() {
    quit_ = true;
    if (thread_.joinable()) thread_.join();
    if (listenFd_ >= 0) close(listenFd_);
    listenFd_ = -1;
}

void SubspaceServer::loop() {
    while (!quit_) {
        pollfd p{listenFd_, POLLIN, 0};
        if (poll(&p, 1, 250) <= 0) continue;
        int c = accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (c < 0) continue;
        timeval tv{15, 0};
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        handle(c);
        close(c);
    }
}

void SubspaceServer::handle(int fd) {
    // Read the request head.
    std::string buf;
    char tmp[4096];
    size_t headEnd;
    while ((headEnd = buf.find("\r\n\r\n")) == std::string::npos) {
        if (buf.size() > 32768) return;
        ssize_t n = recv(fd, tmp, sizeof tmp, 0);
        if (n <= 0) return;
        buf.append(tmp, size_t(n));
    }
    std::string head = buf.substr(0, headEnd), body = buf.substr(headEnd + 4);
    std::istringstream hs(head);
    std::string method, target, version;
    hs >> method >> target >> version;
    long long contentLength = 0;
    std::string line;
    std::getline(hs, line);
    while (std::getline(hs, line)) {
        std::string l = line;
        std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return char(tolower(c)); });
        if (l.rfind("content-length:", 0) == 0) contentLength = atoll(l.c_str() + 15);
    }
    std::string path = target, query;
    size_t qm = target.find('?');
    if (qm != std::string::npos) {
        path = target.substr(0, qm);
        query = target.substr(qm + 1);
    }
    auto authorized = [&](const std::string& form) { return pin_.empty() || formField(form, "pin") == pin_; };

    sockaddr_in peer{};
    socklen_t plen = sizeof peer;
    getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &plen);
    char ipbuf[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof ipbuf);
    std::string ip = ipbuf;

    if (method == "GET" && path == "/") {
        respond(fd, 200, "text/html; charset=utf-8", kPage);
    } else if (method == "GET" && (path == "/visitor" || path == "/visitor/")) {
        // The same page that GitHub Pages hosts for visitors (docs/subspace/index.html).
        for (const std::string& p : {exeDir() + "/../docs/subspace/index.html", exeDir() + "/docs/subspace/index.html",
                                     std::string("docs/subspace/index.html")}) {
            std::ifstream in(p);
            if (!in) continue;
            std::stringstream ss;
            ss << in.rdbuf();
            return respond(fd, 200, "text/html; charset=utf-8", ss.str());
        }
        respond(fd, 404, "text/plain", "visitor page not found (docs/subspace/index.html)");
    } else if (method == "GET" && path == "/api/relay") {
        std::function<std::pair<std::string, std::string>()> info;
        {
            std::lock_guard<std::mutex> lk(m_);
            info = relayInfo_;
        }
        auto st = info ? info() : std::pair<std::string, std::string>();
        if (st.second.empty()) return respond(fd, 404, "text/plain", "relay off");
        respond(fd, 200, "application/json",
                "{\"server\":\"" + jsonEscape(st.first) + "\",\"topic\":\"" + jsonEscape(st.second) + "\"}");
    } else if (method == "GET" && path == "/font.ttf") {
        std::ifstream in(fontPath_, std::ios::binary);
        if (!in) return respond(fd, 404, "text/plain", "no font");
        std::stringstream ss;
        ss << in.rdbuf();
        respond(fd, 200, "font/ttf", ss.str());
    } else if (method == "GET" && path == "/api/state") {
        std::string js = "{\"pin\":" + std::string(pin_.empty() ? "false" : "true") + ",\"messages\":[";
        auto msgs = messages();
        for (size_t i = 0; i < msgs.size() && i < 20; ++i) {
            if (i) js += ",";
            js += "{\"when\":" + std::to_string(msgs[i].when) + ",\"from\":\"" + jsonEscape(msgs[i].from) +
                  "\",\"text\":\"" + jsonEscape(msgs[i].text) + "\"}";
        }
        js += "]}";
        respond(fd, 200, "application/json", js);
    } else if (method == "POST" && path == "/api/message") {
        while ((long long)body.size() < std::min(contentLength, 8192LL)) {
            ssize_t n = recv(fd, tmp, sizeof tmp, 0);
            if (n <= 0) break;
            body.append(tmp, size_t(n));
        }
        if (!authorized(body)) return respond(fd, 403, "text/plain", "denied");
        std::string from = clean(formField(body, "from"), 40, false), text = clean(formField(body, "text"), 500, true);
        if (text.empty()) return respond(fd, 400, "text/plain", "empty");
        {
            std::lock_guard<std::mutex> lk(m_);
            time_t now = time(nullptr);
            if (now - lastPost_[ip] < 4) return respond(fd, 429, "text/plain", "slow down");
            lastPost_[ip] = now;
        }
        addMessage(from.empty() ? "UNKNOWN VESSEL" : from, text);
        respond(fd, 200, "text/plain", "ok");
    } else if (method == "GET" && path == "/api/pending") {
        if (!authorized(query)) return respond(fd, 403, "text/plain", "denied");
        std::string js = "[";
        auto list = pending();
        for (size_t i = 0; i < list.size(); ++i) {
            if (i) js += ",";
            js += "{\"id\":" + std::to_string(list[i].id) + ",\"when\":" + std::to_string(list[i].when) +
                  ",\"from\":\"" + jsonEscape(list[i].from) + "\",\"text\":\"" + jsonEscape(list[i].text) + "\"}";
        }
        respond(fd, 200, "application/json", js + "]");
    } else if (method == "POST" && path == "/api/moderate") {
        if (!authorized(query)) return respond(fd, 403, "text/plain", "denied");
        int id = atoi(formField(query, "id").c_str());
        bool ok = formField(query, "ok") == "1" ? approve(id) : reject(id);
        respond(fd, ok ? 200 : 404, "text/plain", ok ? "ok" : "gone");
    } else if (method == "POST" && path == "/api/button") {
        if (!authorized(query)) return respond(fd, 403, "text/plain", "denied");
        std::string b = formField(query, "b");
        Button bt;
        if (b == "up") bt = Button::Up;
        else if (b == "down") bt = Button::Down;
        else if (b == "left") bt = Button::Left;
        else if (b == "right") bt = Button::Right;
        else if (b == "enter") bt = Button::Enter;
        else if (b == "back") bt = Button::Back;
        else return respond(fd, 400, "text/plain", "bad button");
        {
            std::lock_guard<std::mutex> lk(m_);
            if (buttons_.size() < 16) buttons_.push_back(bt);
        }
        respond(fd, 200, "text/plain", "ok");
    } else if (method == "POST" && path == "/api/upload") {
        if (!authorized(query)) return respond(fd, 403, "text/plain", "denied");
        std::string name = safeUploadName(formField(query, "name"));
        if (name.empty()) return respond(fd, 400, "text/plain", "only video files can be uploaded");
        if (contentLength <= 0 || contentLength > kMaxUpload) return respond(fd, 413, "text/plain", "too large");
        std::string dir = uploadDir_ ? uploadDir_() : "";
        if (dir.empty()) return respond(fd, 500, "text/plain", "no log library available");
        mkdir(dir.c_str(), 0755);
        std::string dest = dir + "/" + name;
        for (int i = 2; fileExists(dest); ++i) {  // never overwrite an existing log
            size_t dot = name.rfind('.');
            dest = dir + "/" + name.substr(0, dot) + " (" + std::to_string(i) + ")" + name.substr(dot);
        }
        std::string part = dir + "/.uploading-" + name;
        FILE* f = fopen(part.c_str(), "wb");
        if (!f) return respond(fd, 500, "text/plain", std::string("cannot write: ") + strerror(errno));
        long long got = 0;
        bool ok = true;
        if (!body.empty()) {
            ok = fwrite(body.data(), 1, body.size(), f) == body.size();
            got += (long long)body.size();
        }
        std::vector<char> chunk(1 << 16);
        int lastPct = -1;
        while (ok && got < contentLength) {
            ssize_t n = recv(fd, chunk.data(), size_t(std::min<long long>((long long)chunk.size(), contentLength - got)), 0);
            if (n <= 0) {
                ok = false;
                break;
            }
            ok = fwrite(chunk.data(), 1, size_t(n), f) == size_t(n);
            got += n;
            int pct = int(got * 100 / contentLength);
            if (pct != lastPct) {
                lastPct = pct;
                std::lock_guard<std::mutex> lk(m_);
                uploadStatus_ = "RECEIVING LOG " + std::to_string(pct) + "%";
            }
        }
        ok = (fclose(f) == 0) && ok && got == contentLength;
        {
            std::lock_guard<std::mutex> lk(m_);
            uploadStatus_.clear();
        }
        if (!ok || rename(part.c_str(), dest.c_str()) != 0) {
            unlink(part.c_str());
            return respond(fd, 500, "text/plain", "transfer interrupted");
        }
        size_t slash = dest.rfind('/');
        addMessage("SUBSPACE LINK", "New captain's log received: " + dest.substr(slash + 1));
        respond(fd, 200, "text/plain", "ok");
    } else {
        respond(fd, 404, "text/plain", "not found");
    }
}

void SubspaceServer::addMessage(const std::string& from, const std::string& text, bool moderated,
                                const std::string& source) {
    SubspaceMessage m;
    m.when = time(nullptr);
    std::string f = clean(from, 40, false), t = clean(text, 500, true);
    m.from = filter_ ? censor(f) : f;
    m.text = filter_ ? censor(t) : t;
    m.source = source;
    std::lock_guard<std::mutex> lk(m_);
    m.id = nextId_++;
    if (moderated) {
        if (pending_.size() >= 30) pending_.pop_front();  // a flood can't grow without bound
        pending_.push_back(m);
        save();
        return;
    }
    messages_.push_front(m);
    while (messages_.size() > kMaxMessages) messages_.pop_back();
    incoming_.push_back(m);
    save();
}

std::vector<SubspaceMessage> SubspaceServer::pending() const {
    std::lock_guard<std::mutex> lk(m_);
    return {pending_.begin(), pending_.end()};
}

bool SubspaceServer::approve(int id) {
    std::lock_guard<std::mutex> lk(m_);
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->id != id) continue;
        SubspaceMessage m = *it;
        pending_.erase(it);
        messages_.push_front(m);
        while (messages_.size() > kMaxMessages) messages_.pop_back();
        incoming_.push_back(m);
        save();
        return true;
    }
    return false;
}

bool SubspaceServer::reject(int id) {
    std::lock_guard<std::mutex> lk(m_);
    for (auto it = pending_.begin(); it != pending_.end(); ++it)
        if (it->id == id) {
            pending_.erase(it);
            save();
            return true;
        }
    return false;
}

std::vector<SubspaceMessage> SubspaceServer::messages() const {
    std::lock_guard<std::mutex> lk(m_);
    return {messages_.begin(), messages_.end()};
}

int SubspaceServer::unread() const {
    std::lock_guard<std::mutex> lk(m_);
    return int(std::count_if(messages_.begin(), messages_.end(), [](const SubspaceMessage& m) { return !m.read; }));
}

void SubspaceServer::markRead(size_t i) {
    std::lock_guard<std::mutex> lk(m_);
    if (i < messages_.size() && !messages_[i].read) {
        messages_[i].read = true;
        save();
    }
}

bool SubspaceServer::popIncoming(SubspaceMessage& m) {
    std::lock_guard<std::mutex> lk(m_);
    if (incoming_.empty()) return false;
    m = incoming_.front();
    incoming_.pop_front();
    return true;
}

bool SubspaceServer::popButton(Button& b) {
    std::lock_guard<std::mutex> lk(m_);
    if (buttons_.empty()) return false;
    b = buttons_.front();
    buttons_.pop_front();
    return true;
}

std::string SubspaceServer::uploadStatus() const {
    std::lock_guard<std::mutex> lk(m_);
    return uploadStatus_;
}

namespace {

void readMessages(const std::string& path, std::deque<SubspaceMessage>& out, int& nextId) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        // when \t read \t from \t text
        std::istringstream ss(line);
        std::string when, read, from, text;
        if (!std::getline(ss, when, '\t') || !std::getline(ss, read, '\t') || !std::getline(ss, from, '\t')) continue;
        std::getline(ss, text);
        SubspaceMessage m;
        m.when = time_t(atoll(when.c_str()));
        m.read = read == "1";
        m.from = from;
        m.text = text;
        m.id = nextId++;
        out.push_back(m);
    }
}

void writeMessages(const std::string& path, const std::deque<SubspaceMessage>& list) {
    std::ofstream out(path);
    for (auto& m : list) out << m.when << '\t' << (m.read ? 1 : 0) << '\t' << m.from << '\t' << m.text << '\n';
}

}  // namespace

void SubspaceServer::load() {
    readMessages(statePath_, messages_, nextId_);
    // Messages still awaiting clearance survive a restart too.
    readMessages(statePath_ + ".pending", pending_, nextId_);
}

void SubspaceServer::save() const {
    if (statePath_.empty()) return;
    writeMessages(statePath_, messages_);
    writeMessages(statePath_ + ".pending", pending_);
}
