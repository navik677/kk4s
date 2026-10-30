// kk4s platform layer: PC viewer (Linux, Windows).
//
// Usage:
//   kk4s <game folder> [engine options]   run that game
//   kk4s                                  pick from <data folder>/games
// Data folder = $KK4S_ROOT, or the folder of the executable.
#include "kk4s_platform.h"
#include "kk4s_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
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

std::string RootDir() { return g_root; }

MemoryInfo QueryMemory() { return MemoryInfo{0, 0, false}; }

void Init(int argc, char** argv) {
    if (g_inited) return;
    g_inited = true;

    const char* env = getenv("KK4S_ROOT");
    g_root = (env && *env) ? env : ExeDir(argc > 0 ? argv[0] : nullptr);

    // First non-option argument that is a folder = game folder.
    for (int i = 0; i < argc; ++i) {
        if (i > 0 && g_cli_game.empty() && argv[i][0] != '-' && IsDir(argv[i])) {
            g_cli_game = argv[i];
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

int EngineArgc() { return (int)g_engine_argv.size() - 1; }
char** EngineArgv() { return g_engine_argv.data(); }

void WindowConfig(int& width, int& height, unsigned& sdl_flags) {
    width = 1280;
    height = 720;
    sdl_flags = SDL_WINDOW_RESIZABLE;
}

static void ShowMessage(const char* title, const std::string& text) {
    fprintf(stderr, "%s\n%s\n", title, text.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, text.c_str(), nullptr);
}

std::string SelectGame() {
    if (!g_cli_game.empty()) {
        return SetGameDir(g_cli_game);
    }

    std::vector<std::string> games = ScanGames();
    Log("launcher: %zu game(s) found in %s", games.size(), GamesDir().c_str());
    if (games.empty()) {
        ShowMessage("kk4s",
                    "No games found.\n\n"
                    "Put a Kirikiri game folder (with data.xp3 or startup.tjs) into\n" +
                        GamesDir() + "/<GameName>/\n\n"
                        "or run:  kk4s <path to game folder>");
        return "";
    }

    size_t sel = 0;
    if (games.size() > 1) {
        std::string last = ReadLastPlayed();
        printf("kk4s - games in %s:\n", GamesDir().c_str());
        for (size_t i = 0; i < games.size(); ++i)
            printf("  %zu) %s%s\n", i + 1, games[i].c_str(), games[i] == last ? "  (last)" : "");
        printf("Number to play (Enter = 1, q = quit): ");
        fflush(stdout);
        char line[64] = {0};
        if (fgets(line, sizeof(line), stdin)) {
            if (line[0] == 'q' || line[0] == 'Q') return "";
            int n = atoi(line);
            if (n >= 1 && (size_t)n <= games.size()) sel = (size_t)n - 1;
        }
    }
    WriteLastPlayed(games[sel]);
    return SetGameDir(GamesDir() + "/" + games[sel]);
}

} // namespace kk4s
