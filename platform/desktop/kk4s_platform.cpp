// kk4s platform layer: PC viewer (Linux, Windows).
//
// Usage:
//   kk4s <game folder> [engine options]   run that game
//   kk4s                                  pick from <data folder>/games
// Data folder = $KK4S_ROOT, or the folder of the executable.
#include "kk4s_platform.h"
#include "kk4s_internal.h"
#include "kk4s_fs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace kk4s {

using namespace detail;

static bool g_inited = false;
static std::string g_root;
static std::string g_cli_game;            // game folder given on the command line
static std::vector<char*> g_engine_argv;  // argv without the game folder

static std::string ExeDir(const char* argv0) {
    std::string path;
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH) > 0) path = buf;
#else
    char buf[4096] = {0};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) path.assign(buf, n);
#endif
    if (path.empty() && argv0) path = argv0;
    for (char& c : path)
        if (c == '\\') c = '/';
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

static bool IsDir(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool IsFile(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string RootDir() { return g_root; }

bool ReadPad(unsigned& held) { held = 0; return false; } // SDL controller events on PC

std::string SystemLanguage() {
#ifdef _WIN32
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_UKRAINIAN: return "uk";
        case LANG_RUSSIAN: return "ru";
        case LANG_JAPANESE: return "ja";
        default: return "en";
    }
#else
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* v = getenv(var);
        if (v && v[0] && v[1] && strcmp(v, "C") != 0 && strncmp(v, "POSIX", 5) != 0)
            return std::string(v, 2);
    }
    return "en";
#endif
}

int BatteryPercent(bool* charging) {
    int pct = -1;
    SDL_PowerState st = SDL_GetPowerInfo(nullptr, &pct);
    if (charging) *charging = st == SDL_POWERSTATE_CHARGING || st == SDL_POWERSTATE_CHARGED;
    if (st == SDL_POWERSTATE_NO_BATTERY || st == SDL_POWERSTATE_UNKNOWN) return -1;
    return pct;
}

bool EditText(const std::string&, std::string&) { return false; } // PC: typed in place
bool EditTextAvailable() { return false; }

MemoryInfo QueryMemory() { return MemoryInfo{0, 0, false}; }

void Init(int argc, char** argv) {
    if (g_inited) return;
    g_inited = true;

    const char* env = getenv("KK4S_ROOT");
    g_root = (env && *env) ? env : ExeDir(argc > 0 ? argv[0] : nullptr);

    // First non-option argument that is a folder = game folder.
    for (int i = 0; i < argc; ++i) {
        if (i > 0 && g_cli_game.empty() && argv[i][0] != '-' && IsDir(argv[i])) {
            // Absolute path: the engine turns relative paths into broken storage names.
            if (char* abs = realpath(argv[i], nullptr)) {
                g_cli_game = abs;
                free(abs);
            } else {
                g_cli_game = argv[i];
            }
            continue;
        }
        g_engine_argv.push_back(argv[i]);
    }
    g_engine_argv.push_back(nullptr);

    InitDataDirsAndLog();
#ifdef _WIN32
    LogBanner("Windows");
#else
    LogBanner("Linux");
#endif
}

void Shutdown() {
    if (!g_inited) return;
    Log("kk4s: shutdown");
    CloseLog();
    g_inited = false;
}

std::vector<std::string> SystemFontFiles() {
    std::vector<std::string> out;
    ListFontFiles(RootDir() + "/fonts", true, out);
#ifdef _WIN32
    // The fonts Windows games ask for by name (MS Gothic, Meiryo, ...).
    const char* windir = getenv("WINDIR");
    std::string fonts = std::string(windir ? windir : "C:/Windows") + "/Fonts/";
    for (char& c : fonts)
        if (c == '\\') c = '/';
    static const char* const kNames[] = {
        "msgothic.ttc", "msmincho.ttc", "meiryo.ttc", "YuGothR.ttc", "YuGothM.ttc",
        "yumin.ttf", "msyh.ttc", "simsun.ttc", "malgun.ttf", "arial.ttf", "times.ttf",
        "segoeui.ttf", "tahoma.ttf",
    };
    for (const char* n : kNames) {
        std::string p = fonts + n;
        if (IsFile(p)) out.push_back(p);
    }
#else
    // Common Linux locations of CJK-capable fonts (Noto CJK, IPA, VL Gothic...).
    static const char* const kDirs[] = {
        "/usr/share/fonts/opentype/noto", "/usr/share/fonts/noto-cjk", "/usr/share/fonts/google-noto-cjk",
        "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf", "/usr/share/fonts/opentype/ipafont-gothic",
        "/usr/share/fonts/truetype/vlgothic", "/usr/share/fonts/truetype/dejavu",
        // Fedora
        "/usr/share/fonts/google-noto-sans-cjk-vf-fonts", "/usr/share/fonts/google-noto-sans-cjk-fonts",
        "/usr/share/fonts/google-droid-sans-fonts", "/usr/share/fonts/dejavu-sans-fonts",
    };
    for (const char* d : kDirs) {
        if (IsFile(d)) out.push_back(d);
        else ListFontFiles(d, false, out);
    }
#endif
    return out;
}

int EngineArgc() { return (int)g_engine_argv.size() - 1; }
char** EngineArgv() { return g_engine_argv.data(); }

void WindowConfig(int& width, int& height, unsigned& sdl_flags) {
    width = 1280;
    height = 720;
    sdl_flags = SDL_WINDOW_RESIZABLE;
}

std::string SelectGame(SDL_Renderer* renderer) {
    if (!g_cli_game.empty()) {
        return SetGameDir(g_cli_game);
    }
    std::vector<std::string> games = ScanGames();
    Log("launcher: %zu game(s) found in %s", games.size(), GamesDir().c_str());
    size_t sel = 0;
    std::string last = ReadLastPlayed();
    for (size_t i = 0; i < games.size(); ++i)
        if (games[i] == last) sel = i;
    std::string chosen = RunLauncher(renderer, games, sel);
    if (chosen.empty()) return "";
    WriteLastPlayed(chosen);
    return SetGameDir(GamesDir() + "/" + chosen);
}

} // namespace kk4s
