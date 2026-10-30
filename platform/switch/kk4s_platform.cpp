// kk4s platform layer: Nintendo Switch (libnx).
#include "kk4s_platform.h"
#include "kk4s_internal.h"

#include <switch.h>

#include <cstdio>
#include <SDL.h>

#ifndef KK4S_VERSION
#define KK4S_VERSION "dev"
#endif

// Return to hbmenu on exit (keep from upstream krkrs).
extern "C" { u32 __nx_applet_exit_mode = 1; }
extern int __system_argc;
extern char** __system_argv;

namespace kk4s {

using namespace detail;

static bool g_inited = false;
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

void Shutdown() {
    if (!g_inited) return;
    Log("kk4s: shutdown");
    CloseLog();
    socketExit();
    romfsExit();
    g_inited = false;
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
// text console launcher

static void Draw(const std::vector<std::string>& games, size_t sel, size_t top, size_t rows) {
    consoleClear();
    printf("\x1b[1;1H\x1b[36mkk4s\x1b[0m - Kirikiri for Switch (%s)\n", KK4S_VERSION);
    printf("Games: %s/\n", GamesDir().c_str());
    printf("------------------------------------------------------------\n");
    if (games.empty()) {
        printf("\nNo games found.\n\n");
        printf("Copy a Kirikiri game folder (with data.xp3 or startup.tjs)\n");
        printf("to %s/<GameName>/\n\n", GamesDir().c_str());
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
    Log("launcher: %zu game(s) found in %s", games.size(), GamesDir().c_str());

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
    return SetGameDir(GamesDir() + "/" + chosen);
}

} // namespace kk4s
