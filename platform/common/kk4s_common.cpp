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
#include <vector>
#include <cstdarg>
#include <cstdint>
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
std::string TempDir()     { return RootDir() + "/tmp"; }

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
    MakeDir(TempDir());
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

// ---------------------------------------------------------------------------
// XP3 archives appended to a Windows executable
//
// Kirikiri's releaser can append the game's archive to krkr.exe ("self-combined"
// games, also data.exe). The engine finds it by scanning the exe for the XP3 mark
// on 16-byte boundaries (XP3Archive.cpp). The archive normally starts right after
// the PE image (the "overlay"), so read the PE section table and look there first.

// 64-bit seek (MinGW's long is 32 bits).
static int Seek64(FILE* f, long long pos) {
#ifdef _WIN32
    return _fseeki64(f, pos, SEEK_SET);
#else
    return fseeko(f, (off_t)pos, SEEK_SET);
#endif
}

static uint32_t Le32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// The mark is assembled at run time, as the engine does, so that this binary's own
// data never contains it whole.
static void XP3Mark(unsigned char out[11]) {
    static const unsigned char a[8] = {'X', 'P', '3', '\r', '\n', ' ', '\n', 0x1a};
    static const unsigned char b[3] = {0x8b, 0x67, 0x01};
    memcpy(out, a, 8);
    memcpy(out + 8, b, 3);
}

// Search [from, from + limit) for the mark on 16-byte boundaries; -1 if absent.
static long long ScanForXP3(FILE* f, long long from, long long limit) {
    unsigned char mark[11];
    XP3Mark(mark);
    from = (from + 15) & ~15LL;
    if (from < 16) from = 16;
    std::vector<unsigned char> buf(256 * 1024 + 16);
    long long pos = from, end = limit < 0 ? -1 : from + limit;
    while (end < 0 || pos < end) {
        if (Seek64(f, pos) != 0) return -1;
        size_t want = buf.size();
        size_t got = fread(buf.data(), 1, want, f);
        if (got < 11) return -1;
        for (size_t i = 0; i + 11 <= got; i += 16) {
            if (end >= 0 && pos + (long long)i >= end) return -1;
            if (memcmp(buf.data() + i, mark, 11) == 0) return pos + (long long)i;
        }
        if (got < want) return -1;
        pos += (long long)(got - 16); // keep 16-byte alignment, overlap one paragraph
    }
    return -1;
}

long long FindXP3InExecutable(const std::string& path, bool full_scan) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return -1;
    long long found = -1;
    unsigned char dos[64];
    if (fread(dos, 1, sizeof(dos), f) == sizeof(dos) && dos[0] == 'M' && dos[1] == 'Z') {
        long long overlay = 0;
        uint32_t lfanew = Le32(dos + 0x3c);
        unsigned char pe[24];
        if (lfanew < (1u << 24) && Seek64(f, lfanew) == 0 && fread(pe, 1, 24, f) == 24 &&
            memcmp(pe, "PE\0\0", 4) == 0) {
            unsigned sections = pe[6] | (pe[7] << 8), optional = pe[20] | (pe[21] << 8);
            if (Seek64(f, (long long)lfanew + 24 + optional) == 0) {
                for (unsigned i = 0; i < sections && i < 96; ++i) {
                    unsigned char sec[40];
                    if (fread(sec, 1, sizeof(sec), f) != sizeof(sec)) break;
                    long long end = (long long)Le32(sec + 20) + Le32(sec + 16); // PointerToRawData + SizeOfRawData
                    if (end > overlay) overlay = end;
                }
            }
        }
        // Right after the image (a digital signature or padding may come first).
        if (overlay > 0) found = ScanForXP3(f, overlay, 4 << 20);
        // Kirikiri's own rule: anywhere in the file.
        if (found < 0 && full_scan) found = ScanForXP3(f, 16, -1);
    }
    fclose(f);
    return found;
}

// The executable in dir that carries the game's archive: data.exe first (Kirikiri
// checks it before its own exe), then the largest *.exe with an XP3 inside. "" if none.
static std::string FindSelfCombinedExe(const std::string& dir, bool full_scan) {
    std::string base = dir;
    if (!base.empty() && base.back() != '/') base += '/';
    DIR* d = opendir(base.c_str());
    if (!d) return "";
    std::vector<std::pair<long long, std::string>> exes; // size, name
    std::string data_exe;
    while (struct dirent* e = readdir(d)) {
        const char* n = e->d_name;
        size_t len = strlen(n);
        if (len < 5 || strcasecmp(n + len - 4, ".exe") != 0) continue;
        struct stat st;
        if (stat((base + n).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (strcasecmp(n, "data.exe") == 0) data_exe = n;
        else exes.emplace_back((long long)st.st_size, n);
    }
    closedir(d);
    if (!data_exe.empty() && FindXP3InExecutable(base + data_exe, full_scan) >= 0) return data_exe;
    std::sort(exes.begin(), exes.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& e : exes)
        if (FindXP3InExecutable(base + e.second, full_scan) >= 0) return e.second;
    return "";
}

// Case-insensitive file lookup inside a subfolder: dir/<sub>/<name>.
static bool SubfolderHas(const std::string& dir, const char* sub, const char* name) {
    std::string base = dir;
    if (!base.empty() && base.back() != '/') base += '/';
    DIR* d = opendir(base.c_str());
    if (!d) return false;
    std::string folder;
    while (struct dirent* e = readdir(d)) {
        if (strcasecmp(e->d_name, sub) != 0) continue;
        struct stat st;
        if (stat((base + e->d_name).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { folder = e->d_name; break; }
    }
    closedir(d);
    if (folder.empty()) return false;
    DIR* s = opendir((base + folder).c_str());
    if (!s) return false;
    bool found = false;
    while (struct dirent* e = readdir(s))
        if (strcasecmp(e->d_name, name) == 0) { found = true; break; }
    closedir(s);
    return found;
}

// A folder is a game when Kirikiri would find a project in it: startup.tjs or an
// XP3 at the top, startup.tjs in data/ or content-data/, or an archive inside an exe.
static bool IsGameFolderFlat(const std::string& dir) {
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
    if (ok) return true;
    if (SubfolderHas(dir, "data", "startup.tjs") || SubfolderHas(dir, "content-data", "startup.tjs")) return true;
    return !FindSelfCombinedExe(dir, false).empty();
}

// Archives often unpack into a wrapper: games/<Name>/<Name>/data.xp3. When `dir` is not a game itself
// but exactly one of its sub-folders (up to two levels down) is, that sub-folder is the game.
// Returns it with no trailing slash, or "" when `dir` has no unambiguous game inside.
static std::string WrapperGameDir(const std::string& dir, int depth = 0) {
    if (depth >= 2) return "";
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    int hits = 0;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string sub = dir;
        if (!sub.empty() && sub.back() != '/' && sub.back() != '\\') sub += '/';
        sub += e->d_name;
        struct stat st;
        if (stat(sub.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        std::string game;
        if (IsGameFolderFlat(sub)) game = sub;
        else game = WrapperGameDir(sub, depth + 1);
        if (!game.empty()) {
            found = game;
            hits++;
        }
    }
    closedir(d);
    return hits == 1 ? found : std::string();
}

bool IsGameFolder(const std::string& dir) {
    return IsGameFolderFlat(dir) || !WrapperGameDir(dir).empty();
}

// True if <dir>/<name> exists (case-insensitive); want_dir also requires a
// directory. Cheap: only reads the directory entry names.
static bool HasEntry(const std::string& dir, const char* name, bool want_dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return false;
    bool found = false;
    while (struct dirent* e = readdir(d)) {
        if (strcasecmp(e->d_name, name) != 0) continue;
        if (want_dir) {
            struct stat st;
            if (stat((dir + "/" + e->d_name).c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        }
        found = true;
        break;
    }
    closedir(d);
    return found;
}

// A short marker when a folder cannot run as a plain KiriKiri game, "" if
// nothing stands out. This never changes how the game is started; it only
// explains the failure in the log and in the launcher.
//
// Steam emulator repacks and "protected" XP3 packers (PackinOne) replace the
// real startup.tjs with a decoy and serve the game from a native Windows
// protection DLL. A vanilla engine gets the decoy, so the game quits before
// KAG starts. Such folders need a DRM-free (GOG/JAST/Steam) release; see
// docs/HANDOFF.md. Detection is by well-known wrapper files, not game names.
std::string DetectGameIssue(const std::string& game_dir) {
    bool steam = HasEntry(game_dir, "steam_api.dll", false) ||
                 HasEntry(game_dir, "steam_api64.dll", false);
    bool emu = HasEntry(game_dir, "ds.ini", false) ||
               HasEntry(game_dir, "steam_emu.ini", false) ||
               HasEntry(game_dir, "SteamEmu", true) ||
               HasEntry(game_dir, "PackinOne.dll", false) ||
               HasEntry(game_dir + "/plugin", "PackinOne.dll", false);
    bool index = false;
    if (DIR* d = opendir(game_dir.c_str())) {
        while (struct dirent* e = readdir(d)) {
            const char* n = e->d_name;
            size_t len = strlen(n);
            if (len > 10 && strcasecmp(n + len - 10, ".xp3.index") == 0) {
                index = true;
                break;
            }
        }
        closedir(d);
    }
    if (steam && (emu || index)) return "Steam/PackinOne repack";
    if (index) return "XP3 with external .index (protected repack)";
    return "";
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
        if (IsGameFolder(full)) {
            games.push_back(e->d_name);
            std::string issue = DetectGameIssue(full);
            if (!issue.empty())
                Log("launcher: %s: %s (a plain Kirikiri game has no native protection; "
                    "a clean DRM-free release is needed, see docs/HANDOFF.md)", e->d_name, issue.c_str());
        }
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
    if (!IsGameFolderFlat(g_game_dir)) {
        std::string inner = WrapperGameDir(g_game_dir);
        if (!inner.empty()) {
            Log("game folder: %s is a wrapper, the game is in %s", g_game_dir.c_str(), inner.c_str());
            g_game_dir = inner;
        }
    }
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

// Kirikiri's search order (engine SysInitImpl.cpp): content-data/, data.xp3,
// data.exe, an archive appended to the exe, data/, the exe's folder. kk4s checks
// a loose startup.tjs first (Kirikiroid2-prepared folders); FSN's Russian patch
// depends on data.xp3 coming before anything that follows it here.
std::string ResolveProjectDir(const std::string& game_dir) {
    std::string result;
    std::string n;
    if (!FindEntry(game_dir, "startup.tjs", false).empty()) {
        result = game_dir;
    } else if (!(n = FindEntry(game_dir, "content-data", true)).empty()) {
        result = game_dir + n + "/";
    } else if (!(n = FindEntry(game_dir, "data.xp3", false)).empty()) {
        result = game_dir + n + ">";
    } else if (!(n = detail::FindSelfCombinedExe(game_dir, true)).empty()) {
        result = game_dir + n + ">";
        Log("compat: the game's archive is inside %s", n.c_str());
    } else if (!(n = FindEntry(game_dir, "data", true)).empty()) {
        result = game_dir + n + "/";
    } else {
        result = game_dir;
    }
    Log("project dir: %s", result.c_str());
    return result;
}

} // namespace kk4s
