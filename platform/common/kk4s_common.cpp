// Platform-independent part of the kk4s layer: logging, data folders,
// game folder scanning and project dir resolution.
#include "kk4s_platform.h"
#include "kk4s_internal.h"
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#endif

#ifndef KK4S_VERSION
#define KK4S_VERSION "dev"
#endif
#ifndef KK4S_GIT_HASH
#define KK4S_GIT_HASH "unknown"
#endif

namespace kk4s {

std::string GamesDir()    { return RootDir() + "/games"; }
std::string LogsDir()     { return RootDir() + "/logs"; }
std::string SavesDir()    { return RootDir() + "/saves"; }
std::string OverlayDir()  { return RootDir() + "/overlay"; }
std::string ProfilesDir() { return RootDir() + "/profiles"; }

static FILE* g_log = nullptr;
static std::string g_game_dir;

// ---------------------------------------------------------------------------
// logging

// The log is written by a background thread in batches (every 200 ms, fsync
// at most every 2 s). Writing each line straight to the Switch SD card cost
// several ms per line, and the engine logs every scenario tag: that froze
// games for seconds. On a crash the last ~200 ms of lines can be missing
// (Atmosphere's crash report has the backtrace).
static std::mutex g_log_mutex;
static std::condition_variable g_log_cv;
static std::string g_log_pending;
static bool g_log_stop = false;
static std::thread* g_log_thread = nullptr; // never destroyed: a joinable std::thread at exit would terminate()

static void WriteLogBatch(const std::string& batch, bool sync) {
    if (!g_log || batch.empty()) return;
    fwrite(batch.data(), 1, batch.size(), g_log);
    fflush(g_log);
    if (sync) {
#ifdef _WIN32
        _commit(_fileno(g_log));
#else
        fsync(fileno(g_log));
#endif
    }
}

static void LogWriterThread() {
    PlaceWorkerThread("log");
    time_t last_sync = time(nullptr);
    std::unique_lock<std::mutex> lock(g_log_mutex);
    while (true) {
        g_log_cv.wait_for(lock, std::chrono::milliseconds(200),
                          [] { return g_log_stop || g_log_pending.size() > (64u << 10); });
        std::string batch;
        batch.swap(g_log_pending);
        bool stop = g_log_stop;
        lock.unlock();
        time_t now = time(nullptr);
        bool sync = stop || now - last_sync >= 2;
        if (sync) last_sync = now;
        WriteLogBatch(batch, sync);
        lock.lock();
        if (stop && g_log_pending.empty()) return;
    }
}

void LogRaw(const char* text) {
    if (!text) return;
    fputs(text, stderr);
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log) return;
    g_log_pending += text;
    if (g_log_pending.size() > (64u << 10)) g_log_cv.notify_one();
}

void Log(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    size_t len = strlen(buf);
    if (len == 0 || buf[len - 1] != '\n') {
        buf[len] = '\n';
        buf[len + 1] = 0;
    }
    LogRaw(buf);
}

const std::string& GameDir() { return g_game_dir; }

static ExitMode g_exit_mode = ExitMode::Setting;
void SetExitMode(ExitMode mode) { g_exit_mode = mode; }
ExitMode GetExitMode() { return g_exit_mode; }

unsigned PadFromSdlButton(int b) {
    switch (b) {
        case SDL_CONTROLLER_BUTTON_A: return kPadA;
        case SDL_CONTROLLER_BUTTON_B: return kPadB;
        case SDL_CONTROLLER_BUTTON_X: return kPadX;
        case SDL_CONTROLLER_BUTTON_Y: return kPadY;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kPadL;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kPadR;
        case SDL_CONTROLLER_BUTTON_START: return kPadPlus;
        case SDL_CONTROLLER_BUTTON_BACK: return kPadMinus;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kPadLeft;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return kPadUp;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kPadRight;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kPadDown;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: return kPadLStick;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return kPadRStick;
    }
    return 0;
}

FrameStats& CurrentFrameStats() {
    static FrameStats stats;
    return stats;
}

namespace detail {

void MakeDir(const std::string& path) {
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0777); // fails harmlessly if it exists
#endif
}

void InitDataDirsAndLog() {
    MakeDir(RootDir());
    MakeDir(GamesDir());
    MakeDir(LogsDir());
    MakeDir(SavesDir());
    MakeDir(OverlayDir());
    MakeDir(ProfilesDir());
    MakeDir(RootDir() + "/fonts");

    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char name[64];
    snprintf(name, sizeof(name), "/kk4s-%04d%02d%02d-%02d%02d%02d.log",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    g_log = fopen((LogsDir() + name).c_str(), "w");
    if (g_log) {
        g_log_stop = false;
        g_log_thread = new std::thread(LogWriterThread);
        atexit(CloseLog); // flush on any exit() path, not only Shutdown()
    }
}

void CloseLog() {
    if (g_log_thread) {
        {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            g_log_stop = true;
        }
        g_log_cv.notify_one();
        g_log_thread->join();
        delete g_log_thread;
        g_log_thread = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log) {
        WriteLogBatch(g_log_pending, true);
        g_log_pending.clear();
        fclose(g_log);
        g_log = nullptr;
    }
}

void LogBanner(const char* platform_name) {
    Log("kk4s %s (git %s, built %s %s) on %s", KK4S_VERSION, KK4S_GIT_HASH, __DATE__, __TIME__, platform_name);
    Log("data folder: %s", RootDir().c_str());
    MemoryInfo mem = QueryMemory();
    if (mem.total)
        Log("memory: total %llu MiB, used %llu MiB, mode: %s", mem.total >> 20, mem.used >> 20,
            mem.applet_mode ? "APPLET (album launch, low memory!)" : "full");
}

bool IsGameFolder(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return false;
    bool ok = false;
    while (struct dirent* e = readdir(d)) {
        const char* n = e->d_name;
        size_t len = strlen(n);
        if (strcasecmp(n, "startup.tjs") == 0 ||
            (len > 4 && strcasecmp(n + len - 4, ".xp3") == 0)) {
            ok = true;
            break;
        }
    }
    closedir(d);
    return ok;
}

std::vector<std::string> ScanGames() {
    std::vector<std::string> games;
    std::string root = GamesDir();
    DIR* d = opendir(root.c_str());
    if (!d) return games;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string full = root + "/" + e->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (IsGameFolder(full)) games.push_back(e->d_name);
    }
    closedir(d);
    std::sort(games.begin(), games.end(),
              [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
    return games;
}

std::string ReadLastPlayed() {
    FILE* f = fopen((RootDir() + "/last.txt").c_str(), "r");
    if (!f) return "";
    char buf[512] = {0};
    if (!fgets(buf, sizeof(buf), f)) buf[0] = 0;
    fclose(f);
    buf[strcspn(buf, "\r\n")] = 0;
    return buf;
}

void WriteLastPlayed(const std::string& name) {
    FILE* f = fopen((RootDir() + "/last.txt").c_str(), "w");
    if (!f) return;
    fputs(name.c_str(), f);
    fclose(f);
}

void ListFontFiles(const std::string& dir, bool recursive, std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string full = dir + "/" + e->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (recursive) ListFontFiles(full, true, out);
            continue;
        }
        size_t len = strlen(e->d_name);
        if (len > 4) {
            const char* ext = e->d_name + len - 4;
            if (!strcasecmp(ext, ".ttf") || !strcasecmp(ext, ".ttc") || !strcasecmp(ext, ".otf"))
                out.push_back(full);
        }
    }
    closedir(d);
}

std::string SetGameDir(const std::string& dir) {
    g_game_dir = dir;
    for (char& c : g_game_dir)
        if (c == '\\') c = '/';
    if (g_game_dir.empty() || g_game_dir.back() != '/') g_game_dir += '/';
    Log("game folder: %s", g_game_dir.c_str());
    return g_game_dir;
}

} // namespace detail

// ---------------------------------------------------------------------------
// project dir

// Find a directory entry by case-insensitive name. Returns its real name or "".
static std::string FindEntry(const std::string& dir, const char* name, bool want_dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    while (struct dirent* e = readdir(d)) {
        if (strcasecmp(e->d_name, name) != 0) continue;
        struct stat st;
        std::string full = dir + e->d_name;
        if (stat(full.c_str(), &st) != 0) continue;
        if (want_dir != S_ISDIR(st.st_mode)) continue;
        found = e->d_name;
        break;
    }
    closedir(d);
    return found;
}

std::string FindGameFile(const char* name) {
    if (g_game_dir.empty()) return "";
    std::string n = FindEntry(g_game_dir, name, false);
    return n.empty() ? std::string() : g_game_dir + n;
}

std::string FindCompatFile(const char* name) {
    std::string path = FindGameFile(name);
    if (!path.empty() || g_game_dir.empty()) return path;
    // <data>/profiles/<game folder>/: helper files kept outside the game folder
    std::string folder = g_game_dir;
    while (!folder.empty() && folder.back() == '/') folder.pop_back();
    folder = folder.substr(folder.find_last_of('/') + 1);
    std::string profiles = ProfilesDir() + "/";
    std::string dir = FindEntry(profiles, folder.c_str(), true);
    if (dir.empty()) return "";
    dir = profiles + dir + "/";
    std::string n = FindEntry(dir, name, false);
    return n.empty() ? std::string() : dir + n;
}

std::string ResolveProjectDir(const std::string& game_dir) {
    std::string result;
    std::string n;
    if (!FindEntry(game_dir, "startup.tjs", false).empty()) {
        result = game_dir;
    } else if (!(n = FindEntry(game_dir, "data.xp3", false)).empty()) {
        result = game_dir + n + ">";
    } else if (!(n = FindEntry(game_dir, "data", true)).empty()) {
        result = game_dir + n + "/";
    } else {
        result = game_dir;
    }
    Log("project dir: %s", result.c_str());
    return result;
}

} // namespace kk4s
