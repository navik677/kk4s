// kk4s: video playback with FFmpeg - what krmovie.dll does on Windows.
//
// tTJSNI_VideoOverlay (visual/win32/VideoOvlImpl.cpp) drives an
// iTVPVideoOverlay. This one demuxes and decodes on its own thread, paces
// frames on a wall clock, feeds the sound to the engine's mixer and tells the
// overlay about frames (EC_UPDATE) and the end (EC_COMPLETE) through its event
// queue, which runs the handlers on the main thread.
//   layer mode:   the newest frame is copied into the overlay's textures on the
//                 main thread (GetFrontBuffer, called from the EC_UPDATE handler)
//   overlay mode: frames go to kk4s::VideoScreen*, drawn over the game picture
//                 by the presenter in sdl2/stubs.cpp
#include "tjsCommHead.h"
#include "StorageIntf.h"
#include "LayerBitmapIntf.h"
#include "WaveMixer.h"
#include "VideoOvlImpl.h"
#include "krmovie.h"
#include "kk4s_platform.h"
#include "kk4s_video.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

int AvioRead(void* opaque, uint8_t* buf, int size) {
    tTJSBinaryStream* s = static_cast<tTJSBinaryStream*>(opaque);
    tjs_uint n = s->Read(buf, (tjs_uint)size);
    return n ? (int)n : AVERROR_EOF;
}

int64_t AvioSeek(void* opaque, int64_t offset, int whence) {
    tTJSBinaryStream* s = static_cast<tTJSBinaryStream*>(opaque);
    if (whence & AVSEEK_SIZE) return (int64_t)s->GetSize();
    whence &= ~AVSEEK_FORCE;
    tjs_int w = whence == SEEK_CUR ? TJS_BS_SEEK_CUR : whence == SEEK_END ? TJS_BS_SEEK_END : TJS_BS_SEEK_SET;
    return (int64_t)s->Seek(offset, w);
}

// Decoded audio -> interleaved signed 16 bit, at most 2 channels.
void AppendS16(const AVFrame* f, int channels, std::vector<int16_t>& out) {
    int src_ch = f->ch_layout.nb_channels;
    int n = f->nb_samples;
    size_t base = out.size();
    out.resize(base + (size_t)n * channels);
    int16_t* dst = out.data() + base;
    auto sample = [&](int ch, int i) -> float {
        if (ch >= src_ch) ch = src_ch - 1;
        switch (f->format) {
            case AV_SAMPLE_FMT_S16: return ((const int16_t*)f->data[0])[i * src_ch + ch] / 32768.f;
            case AV_SAMPLE_FMT_S16P: return ((const int16_t*)f->data[ch])[i] / 32768.f;
            case AV_SAMPLE_FMT_S32: return ((const int32_t*)f->data[0])[i * src_ch + ch] / 2147483648.f;
            case AV_SAMPLE_FMT_S32P: return ((const int32_t*)f->data[ch])[i] / 2147483648.f;
            case AV_SAMPLE_FMT_FLT: return ((const float*)f->data[0])[i * src_ch + ch];
            case AV_SAMPLE_FMT_FLTP: return ((const float*)f->data[ch])[i];
            case AV_SAMPLE_FMT_DBL: return (float)((const double*)f->data[0])[i * src_ch + ch];
            case AV_SAMPLE_FMT_DBLP: return (float)((const double*)f->data[ch])[i];
            case AV_SAMPLE_FMT_U8: return (((const uint8_t*)f->data[0])[i * src_ch + ch] - 128) / 128.f;
            case AV_SAMPLE_FMT_U8P: return (((const uint8_t*)f->data[ch])[i] - 128) / 128.f;
            default: return 0.f;
        }
    };
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < channels; ++c) {
            float v = sample(c, i) * 32767.f;
            dst[i * channels + c] = (int16_t)(v > 32767.f ? 32767 : v < -32768.f ? -32768 : v);
        }
}

class tKK4SVideoPlayer : public iTVPVideoOverlay {
public:
    tKK4SVideoPlayer(tTJSNI_VideoOverlay* owner, bool layer_mode) : Owner(owner), LayerMode(layer_mode) {}

    bool Open(const ttstr& name);

    // iTVPVideoOverlay
    void AddRef() override { ++Ref; }
    void Release() override { if (--Ref == 0) delete this; }
    void SetWindow(tTJSNI_Window*) override {}
    void SetMessageDrainWindow(void*) override {}
    void SetRect(int l, int t, int r, int b) override {
        RectL = l; RectT = t; RectR = r; RectB = b;
        if (!LayerMode) kk4s::VideoScreenRect(this, l, t, r, b, ScreenVisible);
    }
    void SetVisible(bool b) override {
        ScreenVisible = b;
        if (!LayerMode) kk4s::VideoScreenRect(this, RectL, RectT, RectR, RectB, b);
    }
    void Play() override;
    void Stop() override;
    void Pause() override;
    void SetPosition(uint64_t tick) override { RequestSeek((double)tick); }
    void GetPosition(uint64_t* tick) override { *tick = (uint64_t)NowMs(); }
    void GetStatus(tTVPVideoStatus* status) override {
        std::lock_guard<std::mutex> lock(Mutex);
        *status = Ended ? vsEnded : WantPlay ? vsPlaying : vsStopped;
    }
    void Rewind() override { RequestSeek(0); }
    void SetFrame(int f) override { RequestSeek(Fps > 0 ? f * 1000.0 / Fps : 0); }
    void GetFrame(int* f) override { *f = Fps > 0 ? (int)(NowMs() * Fps / 1000.0 + 0.5) : 0; }
    void GetFPS(double* f) override { *f = Fps; }
    void GetNumberOfFrame(int* f) override { *f = (int)(DurationMs * Fps / 1000.0); }
    void GetTotalTime(int64_t* t) override { *t = (int64_t)DurationMs; }
    void GetVideoSize(long* w, long* h) override { *w = Width; *h = Height; }
    tTVPBaseTexture* GetFrontBuffer() override;
    void SetVideoBuffer(tTVPBaseTexture* b1, tTVPBaseTexture* b2, long) override { Buff[0] = b1; Buff[1] = b2; }
    void SetStopFrame(int frame) override { StopFrame = frame; }
    void GetStopFrame(int* frame) override { *frame = StopFrame; }
    void SetDefaultStopFrame() override { StopFrame = -1; }
    void SetPlayRate(double rate) override { std::lock_guard<std::mutex> lock(Mutex); RebaseClockLocked(); Rate = rate > 0 ? rate : 1.0; }
    void GetPlayRate(double* rate) override { *rate = Rate; }
    void SetAudioBalance(long b) override { Balance = b; if (Sound) Sound->SetPan(b / 10000.f); }
    void GetAudioBalance(long* b) override { *b = Balance; }
    void SetAudioVolume(long v) override { Volume = v; if (Sound) Sound->SetVolume(v / 100000.f); }
    void GetAudioVolume(long* v) override { *v = Volume; }
    void GetNumberOfAudioStream(unsigned long* n) override { *n = AudioIndex >= 0 ? 1 : 0; }
    void SelectAudioStream(unsigned long) override {}
    void GetEnableAudioStreamNum(long* n) override { *n = AudioIndex >= 0 ? 0 : -1; }
    void DisableAudioStream() override {}
    void GetNumberOfVideoStream(unsigned long* n) override { *n = 1; }
    void SelectVideoStream(unsigned long) override {}
    void GetEnableVideoStreamNum(long* n) override { *n = 0; }
    void SetMixingBitmap(tTVPBaseTexture*, float) override {}
    void ResetMixingBitmap() override {}
    void SetMixingMovieAlpha(float) override {}
    void GetMixingMovieAlpha(float* a) override { *a = 1; }
    void SetMixingMovieBGColor(unsigned long) override {}
    void GetMixingMovieBGColor(unsigned long* c) override { *c = 0; }
    void PresentVideoImage() override {}
    void GetContrastRangeMin(float* v) override { *v = 0; }
    void GetContrastRangeMax(float* v) override { *v = 0; }
    void GetContrastDefaultValue(float* v) override { *v = 0; }
    void GetContrastStepSize(float* v) override { *v = 0; }
    void GetContrast(float* v) override { *v = 0; }
    void SetContrast(float) override {}
    void GetBrightnessRangeMin(float* v) override { *v = 0; }
    void GetBrightnessRangeMax(float* v) override { *v = 0; }
    void GetBrightnessDefaultValue(float* v) override { *v = 0; }
    void GetBrightnessStepSize(float* v) override { *v = 0; }
    void GetBrightness(float* v) override { *v = 0; }
    void SetBrightness(float) override {}
    void GetHueRangeMin(float* v) override { *v = 0; }
    void GetHueRangeMax(float* v) override { *v = 0; }
    void GetHueDefaultValue(float* v) override { *v = 0; }
    void GetHueStepSize(float* v) override { *v = 0; }
    void GetHue(float* v) override { *v = 0; }
    void SetHue(float) override {}
    void GetSaturationRangeMin(float* v) override { *v = 0; }
    void GetSaturationRangeMax(float* v) override { *v = 0; }
    void GetSaturationDefaultValue(float* v) override { *v = 0; }
    void GetSaturationStepSize(float* v) override { *v = 0; }
    void GetSaturation(float* v) override { *v = 0; }
    void SetSaturation(float) override {}
    void SetLoopSegement(int begin, int end) override {
        // whole-file loops are done by the overlay (EC_COMPLETE -> Rewind),
        // segment loops in layer / mixer mode by its EC_UPDATE handler
        std::lock_guard<std::mutex> lock(Mutex);
        LoopBeginMs = begin > 0 && Fps > 0 ? begin * 1000.0 / Fps : 0;
        LoopEndFrame = LayerMode ? -1 : end;
    }

private:
    ~tKK4SVideoPlayer();

    void ThreadMain();
    void RequestSeek(double ms);
    double NowMs(); // takes Mutex
    double NowMsLocked() const;
    void RebaseClockLocked();
    void Post(int code, intptr_t lparam);
    bool DecodeVideoPacket(AVPacket* pkt, std::unique_lock<std::mutex>& lock);
    bool ShowFrame(AVFrame* f, std::unique_lock<std::mutex>& lock);
    void DecodeAudioPacket(AVPacket* pkt);
    void DoSeekLocked(std::unique_lock<std::mutex>& lock);

    std::atomic<int> Ref{1};
    tTJSNI_VideoOverlay* Owner;
    bool LayerMode;

    tTJSBinaryStream* Stream = nullptr;
    AVIOContext* Avio = nullptr;
    AVFormatContext* Fmt = nullptr;
    AVCodecContext* VDec = nullptr;
    AVCodecContext* ADec = nullptr;
    SwsContext* Sws = nullptr;
    int VideoIndex = -1, AudioIndex = -1;
    int Width = 0, Height = 0;
    double Fps = 30, DurationMs = 0;
    iTVPSoundBuffer* Sound = nullptr;
    int AudioChannels = 0;
    long Volume = 100000, Balance = 0;
    int StopFrame = -1;

    // shared with the thread (Mutex)
    std::mutex Mutex;
    std::condition_variable Cond;
    std::thread Thread;
    bool Quit = false, WantPlay = false, Ended = false, Idle = true;
    bool SeekPending = false;
    double SeekTargetMs = 0, SkipBeforeMs = -1;
    double BaseMs = 0, Rate = 1.0; // clock: BaseMs at StartTick while playing
    Clock::time_point StartTick;
    double LoopBeginMs = 0;
    int LoopEndFrame = -1;

    // newest picture (Mutex)
    std::vector<uint8_t> Picture;
    bool PictureNew = false;
    tTVPBaseTexture* Buff[2] = {nullptr, nullptr};
    int Front = 1;

    int RectL = 0, RectT = 0, RectR = 0, RectB = 0;
    bool ScreenVisible = false;
};

bool tKK4SVideoPlayer::Open(const ttstr& name) {
    TVPInitDirectSound(); // the mixer exists only after the first sound otherwise
    Stream = TVPCreateStream(name, TJS_BS_READ);
    const int kAvioBuf = 1 << 16;
    uint8_t* buf = (uint8_t*)av_malloc(kAvioBuf);
    Avio = avio_alloc_context(buf, kAvioBuf, 0, Stream, AvioRead, nullptr, AvioSeek);
    Fmt = avformat_alloc_context();
    Fmt->pb = Avio;
    std::string hint = name.AsNarrowStdString(); // extension helps format probing
    if (avformat_open_input(&Fmt, hint.c_str(), nullptr, nullptr) < 0) {
        Fmt = nullptr; // freed by avformat_open_input on failure
        kk4s::Log("video: %s: unknown format", hint.c_str());
        return false;
    }
    if (avformat_find_stream_info(Fmt, nullptr) < 0) kk4s::Log("video: %s: no stream info", hint.c_str());
    VideoIndex = av_find_best_stream(Fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AudioIndex = av_find_best_stream(Fmt, AVMEDIA_TYPE_AUDIO, -1, VideoIndex, nullptr, 0);
    if (VideoIndex < 0) {
        kk4s::Log("video: %s: no video stream", hint.c_str());
        return false;
    }
    auto open_codec = [&](int index) -> AVCodecContext* {
        AVStream* st = Fmt->streams[index];
        const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!codec) return nullptr;
        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(ctx, st->codecpar);
        ctx->thread_count = 2;
        if (avcodec_open2(ctx, codec, nullptr) < 0) { avcodec_free_context(&ctx); return nullptr; }
        return ctx;
    };
    VDec = open_codec(VideoIndex);
    if (!VDec) {
        kk4s::Log("video: %s: no decoder for %s", hint.c_str(), avcodec_get_name(Fmt->streams[VideoIndex]->codecpar->codec_id));
        return false;
    }
    Width = VDec->width;
    Height = VDec->height;
    AVStream* vs = Fmt->streams[VideoIndex];
    AVRational fr = vs->avg_frame_rate.num ? vs->avg_frame_rate : vs->r_frame_rate;
    if (fr.num && fr.den) Fps = av_q2d(fr);
    if (Fmt->duration > 0) DurationMs = Fmt->duration / 1000.0;
    else if (vs->duration > 0) DurationMs = vs->duration * av_q2d(vs->time_base) * 1000.0;

    if (AudioIndex >= 0) ADec = open_codec(AudioIndex);
    if (ADec) {
        AudioChannels = ADec->ch_layout.nb_channels >= 2 ? 2 : 1;
        tTVPWaveFormat wf;
        memset(&wf, 0, sizeof(wf));
        wf.SamplesPerSec = ADec->sample_rate;
        wf.Channels = AudioChannels;
        wf.BitsPerSample = 16;
        wf.BytesPerSample = 2;
        wf.Seekable = false;
        Sound = TVPCreateSoundBuffer(wf, 4);
        if (Sound) Sound->SetVolume(Volume / 100000.f);
    }
    kk4s::Log("video: %s: %dx%d %.2f fps, %.1f s, %s%s%s", hint.c_str(), Width, Height, Fps, DurationMs / 1000.0,
              avcodec_get_name(VDec->codec_id), ADec ? " + " : "", ADec ? avcodec_get_name(ADec->codec_id) : "");
    if (Width <= 0 || Height <= 0) return false;
    Picture.assign((size_t)Width * Height * 4, 0);
    Thread = std::thread([this] { ThreadMain(); });
    return true;
}

tKK4SVideoPlayer::~tKK4SVideoPlayer() {
    {
        std::lock_guard<std::mutex> lock(Mutex);
        Quit = true;
    }
    Cond.notify_all();
    if (Thread.joinable()) Thread.join();
    if (!LayerMode) kk4s::VideoScreenRemove(this);
    if (Sound) { Sound->Stop(); Sound->Release(); }
    if (Sws) sws_freeContext(Sws);
    if (VDec) avcodec_free_context(&VDec);
    if (ADec) avcodec_free_context(&ADec);
    if (Fmt) avformat_close_input(&Fmt);
    if (Avio) { av_freep(&Avio->buffer); avio_context_free(&Avio); }
    delete Stream;
}

double tKK4SVideoPlayer::NowMsLocked() const {
    if (!WantPlay || Ended) return BaseMs;
    return BaseMs + std::chrono::duration<double, std::milli>(Clock::now() - StartTick).count() * Rate;
}

double tKK4SVideoPlayer::NowMs() {
    std::lock_guard<std::mutex> lock(Mutex);
    return NowMsLocked();
}

void tKK4SVideoPlayer::RebaseClockLocked() {
    BaseMs = NowMsLocked();
    StartTick = Clock::now();
}

void tKK4SVideoPlayer::Post(int code, intptr_t lparam) {
    NativeEvent ev(WM_GRAPHNOTIFY);
    ev.WParam = code;
    ev.LParam = lparam;
    Owner->PostEvent(ev);
}

void tKK4SVideoPlayer::Play() {
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (Ended) { // play again from the start
            Ended = false;
            SeekPending = true;
            SeekTargetMs = 0;
            BaseMs = 0;
        }
        if (!WantPlay) {
            WantPlay = true;
            StartTick = Clock::now();
        }
    }
    if (Sound) Sound->Play();
    Cond.notify_all();
}

void tKK4SVideoPlayer::Pause() {
    std::unique_lock<std::mutex> lock(Mutex);
    if (WantPlay) {
        BaseMs = NowMsLocked();
        WantPlay = false;
    }
    if (Sound) Sound->Pause();
    Cond.notify_all();
    // wait until the thread sleeps, so no event is posted after this returns
    Cond.wait_for(lock, std::chrono::milliseconds(500), [this] { return Idle || Quit; });
}

void tKK4SVideoPlayer::Stop() {
    Pause();
    RequestSeek(0);
}

void tKK4SVideoPlayer::RequestSeek(double ms) {
    {
        std::lock_guard<std::mutex> lock(Mutex);
        SeekPending = true;
        SeekTargetMs = ms < 0 ? 0 : ms;
        BaseMs = SeekTargetMs;
        StartTick = Clock::now();
        Ended = false;
    }
    Cond.notify_all();
}

tTVPBaseTexture* tKK4SVideoPlayer::GetFrontBuffer() {
    // main thread (EC_UPDATE handler): upload the newest picture
    std::lock_guard<std::mutex> lock(Mutex);
    if (PictureNew && Buff[0]) {
        Front = Buff[1] ? 1 - Front : 0;
        Buff[Front]->Update(Picture.data(), Width * 4, 0, 0, Width, Height);
        PictureNew = false;
    }
    return Buff[Front] ? Buff[Front] : Buff[0];
}

void tKK4SVideoPlayer::DoSeekLocked(std::unique_lock<std::mutex>& lock) {
    double target = SeekTargetMs;
    SeekPending = false;
    lock.unlock();
    AVStream* vs = Fmt->streams[VideoIndex];
    int64_t ts = (int64_t)(target / 1000.0 / av_q2d(vs->time_base));
    if (vs->start_time != AV_NOPTS_VALUE) ts += vs->start_time;
    av_seek_frame(Fmt, VideoIndex, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(VDec);
    if (ADec) avcodec_flush_buffers(ADec);
    if (Sound) Sound->Reset();
    lock.lock();
    SkipBeforeMs = target - 1;
}

void tKK4SVideoPlayer::DecodeAudioPacket(AVPacket* pkt) {
    if (!ADec || !Sound) return;
    if (avcodec_send_packet(ADec, pkt) < 0) return;
    AVFrame* f = av_frame_alloc();
    std::vector<int16_t> pcm;
    while (avcodec_receive_frame(ADec, f) == 0) {
        double ms = f->pts != AV_NOPTS_VALUE ? f->pts * av_q2d(Fmt->streams[AudioIndex]->time_base) * 1000.0 : -1;
        if (ms >= 0 && ms < SkipBeforeMs) continue; // before the seek target
        AppendS16(f, AudioChannels, pcm);
    }
    av_frame_free(&f);
    if (!pcm.empty()) Sound->AppendBuffer(pcm.data(), (unsigned)(pcm.size() * sizeof(int16_t)));
}

// Show one decoded frame at its time. Returns false if the thread should drop
// what it is doing (seek, stop, quit).
bool tKK4SVideoPlayer::ShowFrame(AVFrame* f, std::unique_lock<std::mutex>& lock) {
    AVStream* vs = Fmt->streams[VideoIndex];
    int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    double ms = pts != AV_NOPTS_VALUE ? pts * av_q2d(vs->time_base) * 1000.0 : NowMsLocked();
    if (vs->start_time != AV_NOPTS_VALUE) ms -= vs->start_time * av_q2d(vs->time_base) * 1000.0;
    if (ms < SkipBeforeMs) return true;
    SkipBeforeMs = -1;
    int frame = (int)(ms * Fps / 1000.0 + 0.5);
    // wait for the frame's time (a pause, seek or quit wakes us up)
    while (!Quit && !SeekPending) {
        if (!WantPlay) {
            Idle = true;
            Cond.notify_all();
            Cond.wait(lock);
            Idle = false;
            continue;
        }
        double now = NowMsLocked();
        if (now + 1 >= ms) break;
        Cond.wait_for(lock, std::chrono::microseconds((long long)((ms - now) * 1000 / Rate)));
    }
    if (Quit || SeekPending) return false;
    bool late = NowMsLocked() - ms > 120; // decode but skip the picture when far behind
    if (!late) {
        lock.unlock();
        Sws = sws_getCachedContext(Sws, f->width, f->height, (AVPixelFormat)f->format, Width, Height,
                                   AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        std::vector<uint8_t> rgba((size_t)Width * Height * 4);
        uint8_t* dst[4] = {rgba.data(), nullptr, nullptr, nullptr};
        int dst_pitch[4] = {Width * 4, 0, 0, 0};
        if (Sws) sws_scale(Sws, f->data, f->linesize, 0, f->height, dst, dst_pitch);
        if (!LayerMode) kk4s::VideoScreenFrame(this, rgba.data(), Width, Height);
        lock.lock();
        Picture.swap(rgba);
        PictureNew = true;
    }
    if (Quit || SeekPending || !WantPlay) return !(Quit || SeekPending);
    Post(EC_UPDATE, frame);
    if (StopFrame >= 0 && frame >= StopFrame) {
        StopFrame = -1;
        Ended = true;
        BaseMs = ms;
        Post(EC_COMPLETE, 0);
        return false;
    }
    if (LoopEndFrame > 0 && frame >= LoopEndFrame) {
        SeekPending = true;
        SeekTargetMs = LoopBeginMs;
        BaseMs = LoopBeginMs;
        StartTick = Clock::now();
        return false;
    }
    return true;
}

bool tKK4SVideoPlayer::DecodeVideoPacket(AVPacket* pkt, std::unique_lock<std::mutex>& lock) {
    lock.unlock();
    int rc = avcodec_send_packet(VDec, pkt);
    lock.lock();
    if (rc < 0 && rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) return true;
    AVFrame* f = av_frame_alloc();
    bool go_on = true;
    while (go_on) {
        lock.unlock();
        rc = avcodec_receive_frame(VDec, f);
        lock.lock();
        if (rc < 0) break;
        go_on = ShowFrame(f, lock);
        av_frame_unref(f);
    }
    av_frame_free(&f);
    return go_on;
}

void tKK4SVideoPlayer::ThreadMain() {
    kk4s::PlaceWorkerThread("video");
    kk4s::TrackThread();
    AVPacket* pkt = av_packet_alloc();
    std::unique_lock<std::mutex> lock(Mutex);
    bool at_eof = false;
    while (!Quit) {
        if (SeekPending) {
            DoSeekLocked(lock);
            at_eof = false;
            continue;
        }
        if (!WantPlay || Ended) {
            Idle = true;
            Cond.notify_all();
            Cond.wait(lock);
            Idle = false;
            continue;
        }
        if (at_eof) {
            // let the sound finish, then report the end once
            lock.unlock();
            bool sound_left = Sound && Sound->GetLatencySamples() > 0;
            lock.lock();
            if (sound_left && NowMsLocked() < DurationMs + 500) {
                Cond.wait_for(lock, std::chrono::milliseconds(20));
                continue;
            }
            Ended = true;
            BaseMs = NowMsLocked();
            Post(EC_COMPLETE, 0);
            continue;
        }
        lock.unlock();
        int rc = av_read_frame(Fmt, pkt);
        lock.lock();
        if (rc < 0) {
            DecodeVideoPacket(nullptr, lock); // drain the decoder
            at_eof = true;
            continue;
        }
        if (pkt->stream_index == VideoIndex) {
            DecodeVideoPacket(pkt, lock);
        } else if (pkt->stream_index == AudioIndex) {
            lock.unlock();
            DecodeAudioPacket(pkt);
            lock.lock();
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    Idle = true;
    Cond.notify_all();
    lock.unlock();
    kk4s::UntrackThread();
}

} // namespace

// Called by tTJSNI_VideoOverlay::Open. nullptr if the file cannot be played.
iTVPVideoOverlay* TVPCreateKK4SVideoPlayer(tTJSNI_VideoOverlay* owner, const ttstr& name, bool layer_mode) {
    tKK4SVideoPlayer* p = new tKK4SVideoPlayer(owner, layer_mode);
    bool ok = false;
    try {
        ok = p->Open(name);
    } catch (...) {
        ok = false;
    }
    if (!ok) {
        p->Release();
        return nullptr;
    }
    return p;
}
