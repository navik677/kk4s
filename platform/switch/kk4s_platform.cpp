// kk4s platform layer: Nintendo Switch (libnx).
#include "kk4s_platform.h"
#include "kk4s_internal.h"
#include "kk4s_settings.h"

#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>
#include <SDL.h>

#ifndef KK4S_VERSION
#define KK4S_VERSION "dev"
#endif

// No __nx_applet_exit_mode override: the default (0) returns to hbloader on
// exit, which then starts the NRO set by envSetNextLoad (back to the launcher)
// or hbmenu. Mode 1 (from upstream krkrs) closed the whole application in
// title takeover, so "exit to the launcher" quit kk4s instead.
extern int __system_argc;
extern char** __system_argv;

namespace kk4s {

using namespace detail;

static bool g_inited = false;
static bool g_psm = false; // psm (battery) service opened
static bool g_nxlink = false;

std::string RootDir() { return "sdmc:/switch/kk4s"; }

MemoryInfo QueryMemory() {
    MemoryInfo info{0, 0, false};
    u64 v = 0;
    if (R_SUCCEEDED(svcGetInfo(&v, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0))) info.total = v;
    if (R_SUCCEEDED(svcGetInfo(&v, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0))) info.used = v;
    AppletType at = appletGetAppletType();
    info.applet_mode = (at != AppletType_Application && at != AppletType_SystemApplication);
    return info;
}

void Init(int, char**) {
    if (g_inited) return;
    g_inited = true;

    romfsInit();
    socketInitializeDefault();
    // Only redirect stdio over the network when launched with `nxlink -s`.
    g_nxlink = nxlinkStdio() >= 0;

    MakeDir("sdmc:/switch");
    InitDataDirsAndLog();
    LogBanner("Nintendo Switch");
    Log("nxlink: %s", g_nxlink ? "connected" : "no");
}

// ---------------------------------------------------------------------------
// pthread_detach. libsysbase's pthread_detach calls __syscall_thread_detach,
// which libnx leaves undefined ("unsupported"), so a detached thread was never
// closed: its stack stayed mapped over heap pages. SDL detaches its threads,
// and so does the engine for every sound and timer thread it deletes. hbloader
// starts the next NRO in the same process with the same heap, and the launcher
// then crashed in malloc on those locked pages. Here detached threads are
// remembered and closed once they have ended (libnx threadClose: unmaps and
// frees the stack); Shutdown() handles the rest.
struct LibnxPthread { Thread thr; void* rc; }; // libnx' struct __pthread_t
static std::mutex g_detached_mutex;
static std::vector<LibnxPthread*> g_detached;

static bool ThreadEnded(const Thread& t) {
    return R_SUCCEEDED(svcWaitSynchronizationSingle(t.handle, 0));
}

// Close detached threads that have ended. Caller holds g_detached_mutex.
static unsigned ReapDetachedLocked() {
    unsigned closed = 0;
    for (size_t i = 0; i < g_detached.size();) {
        LibnxPthread* t = g_detached[i];
        if (ThreadEnded(t->thr) && R_SUCCEEDED(threadClose(&t->thr))) {
            free(t); // __libnx_alloc is malloc
            g_detached.erase(g_detached.begin() + i);
            ++closed;
        } else {
            ++i;
        }
    }
    return closed;
}

extern "C" int __syscall_thread_detach(struct __pthread_t* thread) {
    if (!thread || thread == (struct __pthread_t*)~(uintptr_t)0) return 0;
    std::lock_guard<std::mutex> lock(g_detached_mutex);
    ReapDetachedLocked();
    g_detached.push_back(reinterpret_cast<LibnxPthread*>(thread));
    return 0;
}

// Tracked threads (see TrackThread in kk4s_platform.h).
static std::mutex g_threads_mutex;
static std::vector<Thread*> g_threads;

void TrackThread() {
    Thread* t = threadGetSelf();
    if (!t) return;
    std::lock_guard<std::mutex> lock(g_threads_mutex);
    g_threads.push_back(t);
}

void UntrackThread() {
    Thread* t = threadGetSelf();
    std::lock_guard<std::mutex> lock(g_threads_mutex);
    for (size_t i = 0; i < g_threads.size(); ++i)
        if (g_threads[i] == t) { g_threads.erase(g_threads.begin() + i); break; }
}

// Last resort for tracked threads that are still alive when control goes back
// to hbloader (the engine normally ends its threads first, TVPStopAllThreads).
// Horizon cannot kill another thread, but it can pause one: it then never runs
// again, and its stack can be unmapped. Kernel calls only: a paused thread may
// hold a libc lock (malloc, a FILE), so nothing here may take one. The registry
// lock stays held, so a thread that wants to untrack itself just waits.
static unsigned PauseTrackedThreads() {
    g_threads_mutex.lock();
    Thread* self = threadGetSelf();
    unsigned paused = 0;
    // detached threads that are still running count as tracked here
    g_detached_mutex.lock();
    for (LibnxPthread* t : g_detached)
        if (std::find(g_threads.begin(), g_threads.end(), &t->thr) == g_threads.end()) g_threads.push_back(&t->thr);
    for (Thread* t : g_threads) {
        if (t == self || t->handle == INVALID_HANDLE) continue;
        // an ended thread only needs its stack unmapped
        if (!ThreadEnded(*t) && R_FAILED(svcSetThreadActivity(t->handle, ThreadActivity_Paused))) continue;
        // The stack is a mirror of heap pages (svcMapMemory); while mapped those
        // pages stay locked, and the next NRO gets the same heap.
        ::MemoryInfo mi;
        u32 pi;
        if (R_SUCCEEDED(svcQueryMemory(&mi, &pi, (u64)t->stack_mirror)) && mi.addr == (u64)t->stack_mirror)
            svcUnmapMemory(t->stack_mirror, t->stack_mem, mi.size);
        ++paused;
    }
    g_threads.clear();
    return paused;
}

void Shutdown() {
    if (!g_inited) return;
    size_t tracked, detached;
    unsigned reaped;
    {
        std::lock_guard<std::mutex> lock(g_detached_mutex);
        reaped = ReapDetachedLocked();
        detached = g_detached.size();
    }
    Log("threads: %u detached thread(s) closed, %zu still running", reaped, detached);
    {
        std::lock_guard<std::mutex> lock(g_threads_mutex);
        tracked = g_threads.size();
    }
    if (tracked) Log("kk4s: shutdown, %zu thread(s) still running: pausing them", tracked);
    else Log("kk4s: shutdown");
    if (g_psm) psmExit();
    CloseLog();
    socketExit();
    romfsExit();
    PauseTrackedThreads();
    g_inited = false;
}

// Nintendo's shared system font (Japanese + Latin + Cyrillic) is in memory;
// FreeType here opens fonts by path, so it is cached on the SD card once.
static std::string CacheSharedFont(PlSharedFontType type, const char* file) {
    std::string path = RootDir() + "/fonts/.cache/" + file;
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && st.st_size > 0) return path;
    PlFontData fd;
    if (R_FAILED(plGetSharedFontByType(&fd, type)) || !fd.address || !fd.size) return "";
    MakeDir(RootDir() + "/fonts/.cache");
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return "";
    bool ok = fwrite(fd.address, 1, fd.size, f) == fd.size;
    fclose(f);
    if (!ok) { remove(path.c_str()); return ""; }
    Log("fonts: cached shared font %s (%u bytes)", file, (unsigned)fd.size);
    return path;
}

std::vector<std::string> SystemFontFiles() {
    std::vector<std::string> out;
    ListFontFiles(RootDir() + "/fonts", false, out);
    ListFontFiles("romfs:/fonts", true, out);
    if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
        std::string p = CacheSharedFont(PlSharedFontType_Standard, "nintendo_standard.ttf");
        if (!p.empty()) out.push_back(p);
        p = CacheSharedFont(PlSharedFontType_ChineseSimplified, "nintendo_chs.ttf");
        if (!p.empty()) out.push_back(p);
        p = CacheSharedFont(PlSharedFontType_KO, "nintendo_ko.ttf");
        if (!p.empty()) out.push_back(p);
        plExit();
    }
    return out;
}

std::string SystemLanguage() {
    std::string code = "en";
    if (R_SUCCEEDED(setInitialize())) {
        u64 lc = 0;
        SetLanguage lang;
        if (R_SUCCEEDED(setGetSystemLanguage(&lc)) && R_SUCCEEDED(setMakeLanguage(lc, &lang))) {
            switch (lang) {
                case SetLanguage_JA: code = "ja"; break;
                case SetLanguage_RU: code = "ru"; break;
                default: code = "en"; break; // the Switch has no Ukrainian UI
            }
        }
        setExit();
    }
    return code;
}

int BatteryPercent(bool* charging) {
    if (!g_psm) g_psm = R_SUCCEEDED(psmInitialize());
    if (!g_psm) return -1;
    u32 p = 0;
    if (R_FAILED(psmGetBatteryChargePercentage(&p))) return -1;
    if (charging) {
        PsmChargerType ct = PsmChargerType_Unconnected;
        psmGetChargerType(&ct);
        *charging = ct != PsmChargerType_Unconnected;
    }
    return (int)p;
}

bool EditText(const std::string& guide, std::string& text) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide.c_str());
    swkbdConfigSetInitialText(&kbd, text.c_str());
    swkbdConfigSetStringLenMax(&kbd, 120);
    char out[512] = {0};
    Result rc = swkbdShow(&kbd, out, sizeof(out));
    swkbdClose(&kbd);
    if (R_FAILED(rc) || !out[0]) return false;
    text = out;
    return true;
}

bool EditTextAvailable() { return true; }

void ExitAfterGame(int code) {
    Settings s;
    s.Load();
    // appletMainLoop() is false when the system asked us to close (HOME > close).
    bool back = GetExitMode() == ExitMode::Launcher || (GetExitMode() == ExitMode::Setting && s.return_to_launcher);
    if (back && appletMainLoop() && envHasNextLoad() && __system_argc > 0 && __system_argv[0]) {
        std::string self = __system_argv[0];
        std::string args = "\"" + self + "\"";
        if (R_SUCCEEDED(envSetNextLoad(self.c_str(), args.c_str()))) Log("kk4s: game quit, back to the launcher");
    }
    Shutdown();
    // _exit: skips libc's stdio cleanup and static destructors, which could wait
    // forever on a lock held by a stopped engine thread or touch engine state.
    // The log is closed and the game has closed its save files.
    _exit(code);
}

bool ReadPad(unsigned& held) {
    static PadState pad;
    static bool inited = false;
    if (!inited) {
        padConfigureInput(8, HidNpadStyleSet_NpadStandard);
        padInitializeAny(&pad);
        inited = true;
    }
    padUpdate(&pad);
    u64 b = padGetButtons(&pad);
    static const struct { u64 nx; unsigned pad; } kMap[] = {
        {HidNpadButton_A, kPadA}, {HidNpadButton_B, kPadB}, {HidNpadButton_X, kPadX}, {HidNpadButton_Y, kPadY},
        {HidNpadButton_L, kPadL}, {HidNpadButton_R, kPadR}, {HidNpadButton_ZL, kPadZL}, {HidNpadButton_ZR, kPadZR},
        {HidNpadButton_Plus, kPadPlus}, {HidNpadButton_Minus, kPadMinus},
        {HidNpadButton_Left | HidNpadButton_StickLLeft, kPadLeft}, {HidNpadButton_Up | HidNpadButton_StickLUp, kPadUp},
        {HidNpadButton_Right | HidNpadButton_StickLRight, kPadRight}, {HidNpadButton_Down | HidNpadButton_StickLDown, kPadDown},
        {HidNpadButton_StickL, kPadLStick}, {HidNpadButton_StickR, kPadRStick},
    };
    held = 0;
    for (const auto& m : kMap)
        if (b & m.nx) held |= m.pad;
    return true;
}

void PlaceWorkerThread(const char* name, bool audio) {
    // Cores the process may use, minus core 0 (main thread) and core 3 (system).
    u64 allowed = 0;
    if (R_FAILED(svcGetInfo(&allowed, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) return;
    u32 mask = (u32)allowed & 0x6u;
    if (!mask) return;
    // Spread ideal cores round-robin; the mask lets the kernel move a thread to
    // the other core when its own is busy.
    static std::atomic<unsigned> next{0};
    int ideal = (mask == 0x6u) ? 1 + (int)(next++ % 2) : (mask & 0x2u ? 1 : 2);
    Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, ideal, mask);
    // Main thread: 0x2C. New threads get 0x3B; the mixer should win over decoders.
    if (audio && R_SUCCEEDED(rc)) svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2B);
    static std::atomic<int> logged{0};
    if (logged++ < 24) Log("threads: %s -> core %d (mask 0x%x)%s", name ? name : "?", ideal, (unsigned)mask,
                           R_SUCCEEDED(rc) ? "" : " failed");
}

void SetGamePerformance(bool boost) {
    if (hosversionBefore(7, 0, 0)) return;
    Result rc = appletSetCpuBoostMode(boost ? ApmCpuBoostMode_FastLoad : ApmCpuBoostMode_Normal);
    Log("perf: CPU boost %s%s", boost ? "on" : "off", R_SUCCEEDED(rc) ? "" : " (failed)");
}

// libnx gives the real argv through __system_argv (hbloader).
int EngineArgc() { return __system_argc > 0 ? __system_argc : 0; }
char** EngineArgv() { return __system_argv; }

void WindowConfig(int& width, int& height, unsigned& sdl_flags) {
    width = 1920;
    height = 1080;
    sdl_flags = 0;
}

// ---------------------------------------------------------------------------
// launcher

std::string SelectGame(SDL_Renderer* renderer) {
    std::vector<std::string> games = ScanGames();
    Log("launcher: %zu game(s) found in %s", games.size(), GamesDir().c_str());
    size_t sel = 0;
    std::string last = ReadLastPlayed();
    for (size_t i = 0; i < games.size(); ++i)
        if (games[i] == last) sel = i;
    std::string chosen = RunLauncher(renderer, games, sel);
    if (chosen.empty()) {
        Log("launcher: no game selected, exiting");
        return "";
    }
    WriteLastPlayed(chosen);
    return SetGameDir(GamesDir() + "/" + chosen);
}

} // namespace kk4s
