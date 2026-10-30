// kk4s platform layer: Nintendo Switch (libnx).
#include "kk4s_platform.h"
#include "kk4s_internal.h"

#include <switch.h>

#include <cstdio>
#include <sys/stat.h>
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
    };
    held = 0;
    for (const auto& m : kMap)
        if (b & m.nx) held |= m.pad;
    return true;
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
