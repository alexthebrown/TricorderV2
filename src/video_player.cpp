#include "video_player.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "gfx.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {

double wallSec() { return double(SDL_GetPerformanceCounter()) / double(SDL_GetPerformanceFrequency()); }

bool sdlTakesFormat(int fmt) {
    if (fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_YUVJ420P) return true;
#if SDL_VERSION_ATLEAST(2, 0, 16)
    if (fmt == AV_PIX_FMT_NV12) return true;
#endif
    return false;
}

// Hardware decoders may offer DRM_PRIME etc.; we want plain memory frames.
AVPixelFormat pickSoftwareFormat(AVCodecContext*, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (sdlTakesFormat(*p)) return *p;
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(*p);
        if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *p;
    }
    return fmts[0];
}

const char* hwDecoderFor(AVCodecID id) {
    switch (id) {
        case AV_CODEC_ID_H264: return "h264_v4l2m2m";
        case AV_CODEC_ID_MPEG4: return "mpeg4_v4l2m2m";
        case AV_CODEC_ID_MPEG2VIDEO: return "mpeg2_v4l2m2m";
        case AV_CODEC_ID_HEVC: return "hevc_v4l2m2m";
        default: return nullptr;
    }
}

std::string averr(int e) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(e, buf, sizeof buf);
    return buf;
}

}  // namespace

std::string VideoPlayer::decoderName() const {
    std::lock_guard<std::mutex> lk(m_);
    return decoderName_;
}

bool VideoPlayer::open(const std::string& path, const VideoOptions& opt, std::string& err) {
    close();
    path_ = path;
    opt_ = opt;

    int r = avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr);
    if (r < 0) {
        err = averr(r);
        fmt_ = nullptr;
        return false;
    }
    if ((r = avformat_find_stream_info(fmt_, nullptr)) < 0) {
        err = averr(r);
        close();
        return false;
    }
    vIdx_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIdx_ < 0) {
        err = "no video stream";
        close();
        return false;
    }
    if (!openVideoDecoder(opt.hwDecode, err)) {
        close();
        return false;
    }
    if (opt.audio) {
        aIdx_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, vIdx_, nullptr, 0);
        if (aIdx_ >= 0 && !openAudio()) aIdx_ = -1;
    }
    for (unsigned i = 0; i < fmt_->nb_streams; ++i)
        if (int(i) != vIdx_ && int(i) != aIdx_) fmt_->streams[i]->discard = AVDISCARD_ALL;

    startTime_ = fmt_->start_time != AV_NOPTS_VALUE ? fmt_->start_time / double(AV_TIME_BASE) : 0;
    duration_ = fmt_->duration > 0 ? fmt_->duration / double(AV_TIME_BASE) : 0;
    AVRational fr = av_guess_frame_rate(fmt_, fmt_->streams[vIdx_], nullptr);
    frameDur_ = fr.num > 0 && fr.den > 0 ? av_q2d(av_inv_q(fr)) : 0.04;

    quit_ = false;
    eof_ = false;
    serial_ = decSerial_ = 0;
    waitingFirst_ = true;
    paused_ = false;
    shownPts_ = 0;
    setClock(0);
    thread_ = std::thread(&VideoPlayer::decodeLoop, this);
    if (audioDev_) SDL_PauseAudioDevice(audioDev_, 0);
    fprintf(stderr, "video: %s %dx%d via %s%s, audio %s\n", path.c_str(), vw_, vh_, decoderName().c_str(),
            hardware_ ? " (hardware)" : "", audioDev_ ? "on" : "off");
    return true;
}

bool VideoPlayer::openVideoDecoder(bool tryHw, std::string& err) {
    AVStream* st = fmt_->streams[vIdx_];
    AVCodecContext* ctx = nullptr;
    const AVCodec* codec = nullptr;
    bool hw = false;

    if (tryHw) {
        const char* name = hwDecoderFor(st->codecpar->codec_id);
        codec = name ? avcodec_find_decoder_by_name(name) : nullptr;
        if (codec) {
            ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(ctx, st->codecpar);
            ctx->pkt_timebase = st->time_base;
            ctx->get_format = pickSoftwareFormat;
            if (avcodec_open2(ctx, codec, nullptr) == 0) hw = true;
            else avcodec_free_context(&ctx);
        }
    }
    if (!hw) {
        codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) {
            err = "unsupported video codec";
            return false;
        }
        ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(ctx, st->codecpar);
        ctx->pkt_timebase = st->time_base;
        ctx->thread_count = 0;  // auto (1 on a Pi Zero, all cores on the desktop)
        ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        int r = avcodec_open2(ctx, codec, nullptr);
        if (r < 0) {
            err = "video decoder: " + averr(r);
            avcodec_free_context(&ctx);
            return false;
        }
    }
    {
        std::lock_guard<std::mutex> lk(m_);
        vdec_ = ctx;
        decoderName_ = codec->name;
    }
    hardware_ = hw;
    vtb_ = av_q2d(st->time_base);
    vw_ = ctx->width;
    vh_ = ctx->height;
    AVRational sar = av_guess_sample_aspect_ratio(fmt_, st, nullptr);
    sar_ = sar.num > 0 && sar.den > 0 ? av_q2d(sar) : 1.0;
    return true;
}

bool VideoPlayer::openAudio() {
    AVStream* st = fmt_->streams[aIdx_];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return false;
    adec_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(adec_, st->codecpar);
    adec_->pkt_timebase = st->time_base;
    if (avcodec_open2(adec_, codec, nullptr) < 0) {
        avcodec_free_context(&adec_);
        return false;
    }
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        avcodec_free_context(&adec_);
        return false;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = std::clamp(adec_->sample_rate, 8000, 48000);
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    audioDev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!audioDev_) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        avcodec_free_context(&adec_);
        return false;
    }
    audioRate_ = have.freq;
    audioBytesPerSec_ = have.freq * 2 * 2;
    audioLatency_ = have.samples / double(have.freq);

#if LIBSWRESAMPLE_VERSION_INT >= AV_VERSION_INT(4, 5, 100)
    AVChannelLayout outL, inL;
    av_channel_layout_default(&outL, 2);
    if (adec_->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&inL, adec_->ch_layout.nb_channels);
    else
        av_channel_layout_copy(&inL, &adec_->ch_layout);
    int r = swr_alloc_set_opts2(&swr_, &outL, AV_SAMPLE_FMT_S16, audioRate_, &inL, adec_->sample_fmt,
                                adec_->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&inL);
    av_channel_layout_uninit(&outL);
    if (r < 0) swr_ = nullptr;
#else
    int64_t inLayout = adec_->channel_layout ? int64_t(adec_->channel_layout)
                                             : av_get_default_channel_layout(adec_->channels);
    swr_ = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, audioRate_, inLayout,
                              adec_->sample_fmt, adec_->sample_rate, 0, nullptr);
#endif
    if (!swr_ || swr_init(swr_) < 0) {
        swr_free(&swr_);
        SDL_CloseAudioDevice(audioDev_);
        audioDev_ = 0;
        avcodec_free_context(&adec_);
        return false;
    }
    atb_ = av_q2d(st->time_base);
    return true;
}

void VideoPlayer::close() {
    if (thread_.joinable()) {
        quit_ = true;
        cv_.notify_all();
        thread_.join();
    }
    if (audioDev_) {
        SDL_CloseAudioDevice(audioDev_);
        audioDev_ = 0;
    }
    {
        std::lock_guard<std::mutex> lk(m_);
        clearFrames();
    }
    sws_freeContext(sws_);
    sws_ = nullptr;
    swr_free(&swr_);
    avcodec_free_context(&vdec_);
    avcodec_free_context(&adec_);
    if (fmt_) avformat_close_input(&fmt_);
    if (tex_) SDL_DestroyTexture(tex_);
    tex_ = nullptr;
    texW_ = texH_ = 0;
    vIdx_ = aIdx_ = -1;
    hardware_ = false;
    hwErrors_ = 0;
    gotVideoFrame_ = false;
    swFallback_ = false;
    lateLevel_ = 0;
    dropped_ = 0;
    serial_ = decSerial_ = 0;
    seekReq_ = false;
    eof_ = false;
    audioSerial_ = -1;
    discardBefore_ = -1;
    lastVideoPts_ = 0;
    duration_ = 0;
}

void VideoPlayer::clearFrames() {
    for (auto& q : frames_) av_frame_free(&q.f);
    frames_.clear();
}

double VideoPlayer::audioQueuedSec() const {
    return audioDev_ ? SDL_GetQueuedAudioSize(audioDev_) / double(audioBytesPerSec_) : 0;
}

// ---------------------------------------------------------------- decode thread

void VideoPlayer::decodeLoop() {
    AVPacket* pkt = av_packet_alloc();
    int appliedLevel = 0;
    while (!quit_) {
        {
            std::unique_lock<std::mutex> lk(m_);
            if (seekReq_) {
                lk.unlock();
                doSeek();
                continue;
            }
            if (eof_) {
                cv_.wait_for(lk, std::chrono::milliseconds(50));
                continue;
            }
            auto full = [&] {
                size_t n = frames_.size();
                double aq = audioQueuedSec();
                if (n >= 30) return true;
                if (audioDev_ && aq > 2.0) return true;
                bool audioHungry = audioDev_ && aq < 0.3;
                return n >= 8 && !audioHungry;
            };
            while (!quit_ && !seekReq_ && full()) cv_.wait_for(lk, std::chrono::milliseconds(10));
            if (quit_ || seekReq_) continue;
        }

        int lvl = lateLevel_;
        if (lvl != appliedLevel) {
            // Falling behind: trade picture quality for speed (mostly matters for software decode).
            vdec_->skip_loop_filter = lvl >= 1 ? AVDISCARD_ALL : AVDISCARD_DEFAULT;
            vdec_->skip_frame = lvl >= 2 ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
            if (lvl >= 1) vdec_->flags2 |= AV_CODEC_FLAG2_FAST;
            else vdec_->flags2 &= ~AV_CODEC_FLAG2_FAST;
            appliedLevel = lvl;
        }

        int r = av_read_frame(fmt_, pkt);
        if (r == AVERROR(EAGAIN)) {
            SDL_Delay(5);
            continue;
        }
        if (r < 0) {
            sendAndReceive(vdec_, nullptr, true);  // drain
            if (adec_) sendAndReceive(adec_, nullptr, false);
            std::lock_guard<std::mutex> lk(m_);
            if (!seekReq_) eof_ = true;
            continue;
        }
        if (pkt->stream_index == vIdx_) sendAndReceive(vdec_, pkt, true);
        else if (pkt->stream_index == aIdx_ && adec_) sendAndReceive(adec_, pkt, false);
        av_packet_unref(pkt);

        if (swFallback_) {
            // The hardware decoder opened but won't decode this file; switch to software.
            swFallback_ = false;
            fprintf(stderr, "video: hardware decoder failed, falling back to software\n");
            AVCodecContext* old;
            {
                std::lock_guard<std::mutex> lk(m_);
                old = vdec_;
                vdec_ = nullptr;
            }
            avcodec_free_context(&old);
            std::string err;
            if (!openVideoDecoder(false, err)) {
                fprintf(stderr, "video: %s\n", err.c_str());
                std::lock_guard<std::mutex> lk(m_);
                eof_ = true;
                break;
            }
            appliedLevel = 0;
            std::lock_guard<std::mutex> lk(m_);
            seekReq_ = true;
            seekTarget_ = std::max(0.0, lastVideoPts_);
            ++serial_;
        }
    }
    av_packet_free(&pkt);
}

bool VideoPlayer::sendAndReceive(AVCodecContext* ctx, AVPacket* pkt, bool video) {
    if (!ctx) return false;
    AVFrame* fr = av_frame_alloc();
    for (int tries = 0;; ++tries) {
        int s = avcodec_send_packet(ctx, pkt);
        for (;;) {
            int rr = avcodec_receive_frame(ctx, fr);
            if (rr < 0) break;
            if (video) handleVideoFrame(fr);
            else handleAudioFrame(fr);
            av_frame_unref(fr);
            if (quit_) break;
        }
        if (s != AVERROR(EAGAIN)) {
            if (s < 0 && s != AVERROR_EOF && video && hardware_ && !gotVideoFrame_ && ++hwErrors_ > 8)
                swFallback_ = true;
            break;
        }
        if (quit_ || tries > 500) break;
        SDL_Delay(1);  // hardware decoder's input queue is full; give it a moment
    }
    av_frame_free(&fr);
    return true;
}

void VideoPlayer::handleVideoFrame(AVFrame* fr) {
    gotVideoFrame_ = true;
    int64_t ts = fr->best_effort_timestamp;
    double pts = ts == AV_NOPTS_VALUE ? lastVideoPts_ + frameDur_ : ts * vtb_ - startTime_;
    lastVideoPts_ = pts;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (seekReq_ || pts < discardBefore_) return;
    }

    AVFrame* out = av_frame_alloc();
    if (sdlTakesFormat(fr->format) && !hardware_) {
        av_frame_move_ref(out, fr);
    } else if (sdlTakesFormat(fr->format)) {
        // Copy out of the hardware decoder's small buffer pool so it never starves.
        out->format = fr->format;
        out->width = fr->width;
        out->height = fr->height;
        if (av_frame_get_buffer(out, 32) < 0 || av_frame_copy(out, fr) < 0) {
            av_frame_free(&out);
            return;
        }
    } else {
        sws_ = sws_getCachedContext(sws_, fr->width, fr->height, AVPixelFormat(fr->format), fr->width,
                                    fr->height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        out->format = AV_PIX_FMT_YUV420P;
        out->width = fr->width;
        out->height = fr->height;
        if (!sws_ || av_frame_get_buffer(out, 32) < 0) {
            av_frame_free(&out);
            return;
        }
        sws_scale(sws_, fr->data, fr->linesize, 0, fr->height, out->data, out->linesize);
    }

    std::lock_guard<std::mutex> lk(m_);
    if (decSerial_ != serial_) {
        av_frame_free(&out);
        return;
    }
    frames_.push_back({out, pts, decSerial_});
}

void VideoPlayer::handleAudioFrame(AVFrame* fr) {
    if (!audioDev_ || !swr_) return;
    double dur = fr->nb_samples / double(fr->sample_rate > 0 ? fr->sample_rate : 48000);
    double pts;
    {
        std::lock_guard<std::mutex> lk(m_);
        int64_t ts = fr->best_effort_timestamp;
        pts = ts == AV_NOPTS_VALUE ? audioEndPts_ : ts * atb_ - startTime_;
        if (seekReq_ || decSerial_ != serial_ || pts + dur < discardBefore_) return;
    }
    int outMax = swr_get_out_samples(swr_, fr->nb_samples);
    if (outMax <= 0) return;
    audioBuf_.resize(size_t(outMax) * 4);
    uint8_t* outp = audioBuf_.data();
    int n = swr_convert(swr_, &outp, outMax, const_cast<const uint8_t**>(fr->extended_data), fr->nb_samples);
    if (n <= 0) return;
    std::lock_guard<std::mutex> lk(m_);
    if (decSerial_ != serial_) return;
    SDL_QueueAudio(audioDev_, audioBuf_.data(), Uint32(n * 4));
    audioEndPts_ = pts + dur;
    audioSerial_ = decSerial_;
}

void VideoPlayer::doSeek() {
    double target;
    int s;
    {
        std::lock_guard<std::mutex> lk(m_);
        target = seekTarget_;
        s = serial_;
    }
    int64_t ts = int64_t((target + startTime_) * AV_TIME_BASE);
    if (avformat_seek_file(fmt_, -1, INT64_MIN, ts, ts, 0) < 0)
        av_seek_frame(fmt_, -1, ts, AVSEEK_FLAG_BACKWARD);
    if (vdec_) avcodec_flush_buffers(vdec_);
    if (adec_) avcodec_flush_buffers(adec_);
    {
        std::lock_guard<std::mutex> lk(m_);
        clearFrames();
        if (audioDev_) SDL_ClearQueuedAudio(audioDev_);
        audioEndPts_ = target;
        audioSerial_ = -1;
        discardBefore_ = target - 0.001;
        lastVideoPts_ = target;
        decSerial_ = s;
        if (serial_ == s) seekReq_ = false;  // otherwise another seek arrived meanwhile
        eof_ = false;
    }
    cv_.notify_all();
}

// ---------------------------------------------------------------- UI thread

double VideoPlayer::clockNow() const { return paused_ ? pausedClock_ : wallSec() - clockOrigin_; }

void VideoPlayer::setClock(double v) {
    pausedClock_ = v;
    clockOrigin_ = wallSec() - v;
}

void VideoPlayer::setPaused(bool p) {
    if (p == paused_ || !fmt_) return;
    if (p) {
        pausedClock_ = clockNow();
        paused_ = true;
    } else {
        paused_ = false;
        setClock(pausedClock_);
    }
    if (audioDev_) SDL_PauseAudioDevice(audioDev_, p ? 1 : 0);
}

void VideoPlayer::seek(double sec) {
    if (!fmt_) return;
    double maxT = duration_ > 1 ? duration_ - 0.5 : std::max(sec, 0.0);
    sec = std::clamp(sec, 0.0, maxT);
    {
        std::lock_guard<std::mutex> lk(m_);
        seekReq_ = true;
        seekTarget_ = sec;
        ++serial_;
        clearFrames();
        if (audioDev_) SDL_ClearQueuedAudio(audioDev_);
    }
    cv_.notify_all();
    waitingFirst_ = true;
    shownPts_ = sec;
    setClock(sec);
}

double VideoPlayer::position() const {
    if (waitingFirst_) return shownPts_;
    double t = clockNow();
    return duration_ > 0 ? std::clamp(t, 0.0, duration_) : std::max(0.0, t);
}

bool VideoPlayer::finished() const {
    if (!fmt_) return true;
    std::lock_guard<std::mutex> lk(m_);
    return eof_ && !seekReq_ && frames_.empty() && audioQueuedSec() <= 0.0;
}

void VideoPlayer::update(SDL_Renderer* ren) {
    if (!fmt_) return;
    AVFrame* show = nullptr;
    double showPts = 0;
    {
        std::lock_guard<std::mutex> lk(m_);
        while (!frames_.empty() && frames_.front().serial != serial_) {
            av_frame_free(&frames_.front().f);
            frames_.pop_front();
        }
        if (waitingFirst_ && !frames_.empty()) {
            waitingFirst_ = false;
            setClock(frames_.front().pts);
        }
        double now = clockNow();

        // Audio is the master clock: steer the wall clock gently toward it.
        if (audioDev_ && !paused_ && !waitingFirst_ && audioSerial_ == serial_) {
            double q = audioQueuedSec();
            if (q > 0) {
                double ac = audioEndPts_ - q - audioLatency_;
                double diff = ac - now;
                if (std::fabs(diff) > 0.4) setClock(ac);
                else if (std::fabs(diff) > 0.01) setClock(now + diff * 0.1);
                now = clockNow();
            }
        }

        while (!frames_.empty()) {
            QFrame& f = frames_.front();
            if (f.pts > now + 0.002) break;
            if (frames_.size() > 1 && frames_[1].pts <= now) {
                // A newer frame is already due: skip this one rather than fall behind.
                av_frame_free(&f.f);
                frames_.pop_front();
                ++dropped_;
                lastDropTime_ = wallSec();
                continue;
            }
            show = f.f;
            showPts = f.pts;
            frames_.pop_front();
            break;
        }
    }
    cv_.notify_one();

    if (show) {
        upload(ren, show);
        shownPts_ = showPts;
        av_frame_free(&show);
    }

    double wall = wallSec();
    int lvl = lateLevel_;
    if (wall - lastDropTime_ < 1.0 && wall - lastLevelChange_ > 2.0 && lvl < 2) {
        lateLevel_ = lvl + 1;
        lastLevelChange_ = wall;
    } else if (lvl > 0 && wall - lastDropTime_ > 6.0 && wall - lastLevelChange_ > 6.0) {
        lateLevel_ = lvl - 1;
        lastLevelChange_ = wall;
    }
}

void VideoPlayer::upload(SDL_Renderer* ren, AVFrame* f) {
#if SDL_VERSION_ATLEAST(2, 0, 16)
    Uint32 want = f->format == AV_PIX_FMT_NV12 ? SDL_PIXELFORMAT_NV12 : SDL_PIXELFORMAT_IYUV;
#else
    Uint32 want = SDL_PIXELFORMAT_IYUV;
#endif
    if (!tex_ || texW_ != f->width || texH_ != f->height || texFmt_ != want) {
        if (tex_) SDL_DestroyTexture(tex_);
        tex_ = SDL_CreateTexture(ren, want, SDL_TEXTUREACCESS_STREAMING, f->width, f->height);
        if (!tex_) {
            fprintf(stderr, "video texture: %s\n", SDL_GetError());
            return;
        }
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(tex_, SDL_ScaleModeLinear);
#endif
        texW_ = f->width;
        texH_ = f->height;
        texFmt_ = want;
    }
#if SDL_VERSION_ATLEAST(2, 0, 16)
    if (want == SDL_PIXELFORMAT_NV12) {
        SDL_UpdateNVTexture(tex_, nullptr, f->data[0], f->linesize[0], f->data[1], f->linesize[1]);
        return;
    }
#endif
    SDL_UpdateYUVTexture(tex_, nullptr, f->data[0], f->linesize[0], f->data[1], f->linesize[1], f->data[2],
                         f->linesize[2]);
}

void VideoPlayer::render(Gfx& g, SDL_Rect area, bool fill) {
    if (!tex_ || texH_ == 0 || area.h == 0) return;
    double aspect = texW_ * sar_ / texH_ / std::max(0.1f, opt_.pixelAspect);
    double areaAspect = area.w / double(area.h);
    SDL_Rect dst = area, src{0, 0, texW_, texH_};
    if (!fill) {
        if (aspect > areaAspect) {
            dst.h = int(area.w / aspect);
            dst.y += (area.h - dst.h) / 2;
        } else {
            dst.w = int(area.h * aspect);
            dst.x += (area.w - dst.w) / 2;
        }
    } else if (aspect > areaAspect) {
        src.w = int(texW_ * areaAspect / aspect);
        src.x = (texW_ - src.w) / 2;
    } else {
        src.h = int(texH_ * aspect / areaAspect);
        src.y = (texH_ - src.h) / 2;
    }
    g.texture(tex_, &src, dst);
}
