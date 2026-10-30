#include "kk4s_platform.h"

#include <switch.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifndef KK4S_VERSION
#define KK4S_VERSION "dev"
#endif
#ifndef KK4S_GIT_HASH
#define KK4S_GIT_HASH "unknown"
#endif

namespace kk4s {

const char* const kRootDir     = "sdmc:/switch/kk4s";
const char* const kGamesDir    = "sdmc:/switch/kk4s/games";
const char* const kLogsDir     = "sdmc:/switch/kk4s/logs";
const char* const kSavesDir    = "sdmc:/switch/kk4s/saves";
const char* const kOverlayDir  = "sdmc:/switch/kk4s/overlay";
const char* const kProfilesDir = "sdmc:/switch/kk4s/profiles";

static const char* const kLastPlayedFile = "sdmc:/switch/kk4s/last.txt";

static FILE* g_log = nullptr;
static bool g_inited = false;
static bool g_nxlink = false;
static std::string g_game_dir;

// ---------------------------------------------------------------------------
// logging

void LogRaw(const char* text) {
    if (!text) return;
    fputs(text, stderr);
    if (g_log) {
        fputs(text, g_log);
        fflush(g_log);
        fsync(fileno(g_log));
    }
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

static void MakeDir(const char* path) {
    mkdir(path, 0777); // fails harmlessly if it exists
}

static void OpenLogFile() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char path[256];
    snprintf(path, sizeof(path), "%s/kk4s-%04d%02d%02d-%02d%02d%02d.log", kLogsDir,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    g_log = fopen(path, "w");
}

MemoryInfo QueryMemory() {
    MemoryInfo info{0, 0, false};
    u64 v = 0;
    if (R_SUCCEEDED(svcGetInfo(&v, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0))) info.total = v;
    if (R_SUCCEEDED(svcGetInfo(&v, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0))) info.used = v;
    AppletType at = appletGetAppletType();
    info.applet_mode = (at != AppletType_Application && at != AppletType_SystemApplication);
    return info;
}

void Init() {
    if (g_inited) return;
    g_inited = true;

    romfsInit();
    socketInitializeDefault();
    // Only redirect stdio over the network when launched with `nxlink -s`.
    g_nxlink = nxlinkStdio() >= 0;

    MakeDir("sdmc:/switch");
    MakeDir(kRootDir);
    MakeDir(kGamesDir);
    MakeDir(kLogsDir);
    MakeDir(kSavesDir);
    MakeDir(kOverlayDir);
    MakeDir(kProfilesDir);
    OpenLogFile();

    Log("kk4s %s (git %s, built %s %s)", KK4S_VERSION, KK4S_GIT_HASH, __DATE__, __TIME__);
    Log("nxlink: %s", g_nxlink ? "connected" : "no");
    MemoryInfo mem = QueryMemory();
    Log("memory: total %llu MiB, used %llu MiB, mode: %s", mem.total >> 20, mem.used >> 20,
        mem.applet_mode ? "APPLET (album launch, low memory!)" : "title takeover");
}

void Shutdown() {
    if (!g_inited) return;
    Log("kk4s: shutdown");
    if (g_log) {
        fclose(g_log);
        g_log = nullptr;
    }
    socketExit();
    romfsExit();
    g_inited = false;
}

const std::string& GameDir() { return g_game_dir; }

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

// ---------------------------------------------------------------------------
// launcher

static bool IsGameFolder(const std::string& dir) {
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

static std::vector<std::string> ScanGames() {
    std::vector<std::string> games;
    DIR* d = opendir(kGamesDir);
    if (!d) return games;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string full = std::string(kGamesDir) + "/" + e->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (IsGameFolder(full)) games.push_back(e->d_name);
    }
    closedir(d);
    std::sort(games.begin(), games.end(),
              [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
    return games;
}

static std::string ReadLastPlayed() {
    FILE* f = fopen(kLastPlayedFile, "r");
    if (!f) return "";
    char buf[512] = {0};
    if (!fgets(buf, sizeof(buf), f)) buf[0] = 0;
    fclose(f);
    buf[strcspn(buf, "\r\n")] = 0;
    return buf;
}

static void WriteLastPlayed(const std::string& name) {
    FILE* f = fopen(kLastPlayedFile, "w");
    if (!f) return;
    fputs(name.c_str(), f);
    fclose(f);
}

static void Draw(const std::vector<std::string>& games, size_t sel, size_t top, size_t rows) {
    consoleClear();
    printf("\x1b[1;1H\x1b[36mkk4s\x1b[0m - Kirikiri for Switch (%s)\n", KK4S_VERSION);
    printf("Games: %s/\n", kGamesDir);
    printf("------------------------------------------------------------\n");
    if (games.empty()) {
        printf("\nNo games found.\n\n");
        printf("Copy a Kirikiri game folder (with data.xp3 or startup.tjs)\n");
        printf("to %s/<GameName>/\n\n", kGamesDir);
        printf("Press + to exit.\n");
        return;
    }
    for (size_t i = top; i < games.size() && i < top + rows; ++i) {
        if (i == sel) printf("\x1b[30;47m> %s\x1b[0m\n", games[i].c_str());
        else printf("  %s\n", games[i].c_str());
    }
    printf("\x1b[44;1H------------------------------------------------------------\n");
    printf("A: play   Up/Down: select   +: exit");
}

std::string SelectGame() {
    std::vector<std::string> games = ScanGames();
    Log("launcher: %zu game(s) found in %s", games.size(), kGamesDir);

    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    size_t sel = 0;
    std::string last = ReadLastPlayed();
    for (size_t i = 0; i < games.size(); ++i)
        if (games[i] == last) sel = i;

    const size_t rows = 38;
    size_t top = 0;
    std::string chosen;
    bool redraw = true;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;
        if (!games.empty()) {
            if (down & HidNpadButton_AnyDown) { sel = (sel + 1) % games.size(); redraw = true; }
            if (down & HidNpadButton_AnyUp) { sel = (sel + games.size() - 1) % games.size(); redraw = true; }
            if (down & HidNpadButton_A) { chosen = games[sel]; break; }
        }
        if (redraw) {
            if (sel < top) top = sel;
            if (sel >= top + rows) top = sel - rows + 1;
            Draw(games, sel, top, rows);
            redraw = false;
        }
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);

    if (chosen.empty()) {
        Log("launcher: no game selected, exiting");
        return "";
    }
    WriteLastPlayed(chosen);
    g_game_dir = std::string(kGamesDir) + "/" + chosen + "/";
    Log("launcher: selected %s", g_game_dir.c_str());
    return g_game_dir;
}

} // namespace kk4s
