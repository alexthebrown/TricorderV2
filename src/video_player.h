#pragma once
// FFmpeg-based player built for the Pi Zero W:
//  * decoding runs on its own thread, the UI thread only uploads + draws
//  * uses the Pi's hardware H.264 decoder (h264_v4l2m2m) when available
//  * YUV frames go straight to a GPU texture (no RGB conversion, no PIL/Tk round-trip)
//  * audio is the master clock when present; late frames are dropped, and if the
//    decoder keeps falling behind it starts skipping deblocking / non-reference frames
#include <SDL.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;
}

class Gfx;

struct VideoOptions {
    bool hwDecode = true;
    bool audio = true;
    float pixelAspect = 1.0f;  // display pixel aspect (w/h) to correct for
};

class VideoPlayer {
public:
    VideoPlayer() = default;
    ~VideoPlayer() { close(); }
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    bool open(const std::string& path, const VideoOptions& opt, std::string& err);
    void close();
    bool isOpen() const { return fmt_ != nullptr; }

    // UI thread, once per frame.
    void update(SDL_Renderer* ren);
    void render(Gfx& g, SDL_Rect area, bool fill);

    void setPaused(bool p);
    bool paused() const { return paused_; }
    void seek(double seconds);  // absolute, clamped
    void seekRelative(double delta) { seek(position() + delta); }

    double position() const;
    double duration() const { return duration_; }
    bool finished() const;
    bool hasPicture() const { return tex_ != nullptr; }
    int droppedFrames() const { return dropped_; }
    int qualityLevel() const { return lateLevel_.load(); }
    std::string decoderName() const;
    bool hardware() const { return hardware_; }
    bool hasAudio() const { return audioDev_ != 0; }
    int width() const { return vw_; }
    int height() const { return vh_; }

private:
    struct QFrame {
        AVFrame* f = nullptr;
        double pts = 0;
        int serial = 0;
    };

    bool openVideoDecoder(bool tryHw, std::string& err);
    bool openAudio();
    void decodeLoop();
    bool sendAndReceive(AVCodecContext* ctx, AVPacket* pkt, bool video);
    void handleVideoFrame(AVFrame* fr);
    void handleAudioFrame(AVFrame* fr);
    void doSeek();
    void clearFrames();  // m_ held
    double audioQueuedSec() const;
    void upload(SDL_Renderer* ren, AVFrame* f);

    double clockNow() const;
    void setClock(double v);

    AVFormatContext* fmt_ = nullptr;
    AVCodecContext* vdec_ = nullptr;
    AVCodecContext* adec_ = nullptr;
    int vIdx_ = -1, aIdx_ = -1;
    double vtb_ = 0, atb_ = 0, startTime_ = 0, duration_ = 0;
    int vw_ = 0, vh_ = 0;
    double sar_ = 1.0;
    SwsContext* sws_ = nullptr;
    SwrContext* swr_ = nullptr;
    std::string decoderName_;
    std::atomic<bool> hardware_{false};
    int hwErrors_ = 0;
    bool gotVideoFrame_ = false;
    bool swFallback_ = false;
    double frameDur_ = 0.04;
    std::vector<uint8_t> audioBuf_;
    VideoOptions opt_;
    std::string path_;

    SDL_AudioDeviceID audioDev_ = 0;
    int audioRate_ = 0;
    int audioBytesPerSec_ = 0;
    double audioLatency_ = 0;

    std::thread thread_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<QFrame> frames_;
    std::atomic<bool> quit_{false};
    // protected by m_
    bool seekReq_ = false;
    double seekTarget_ = 0;
    int serial_ = 0;       // bumped by every seek request
    int decSerial_ = 0;    // serial the decode thread is producing for
    bool eof_ = false;
    double audioEndPts_ = 0;
    int audioSerial_ = -1;
    double discardBefore_ = -1;  // drop decoded output earlier than this (accurate seek)
    double lastVideoPts_ = 0;

    std::atomic<int> lateLevel_{0};

    // UI thread state
    SDL_Texture* tex_ = nullptr;
    int texW_ = 0, texH_ = 0;
    Uint32 texFmt_ = 0;
    bool paused_ = false;
    double clockOrigin_ = 0, pausedClock_ = 0;
    bool waitingFirst_ = true;
    double shownPts_ = 0;
    int dropped_ = 0;
    double lastDropTime_ = -100, lastLevelChange_ = 0;
};
