// kk4s platform interface.
// Everything platform-specific that kk4s adds on top of the krkrs engine
// lives behind this small interface, so the engine sources in engine/ stay
// close to upstream. Implementations:
//   platform/switch/kk4s_platform.cpp   (libnx, Nintendo Switch)
//   platform/desktop/kk4s_platform.cpp  (Linux / Windows PC viewer)
#pragma once

#include <string>

namespace kk4s {

// Data folder layout (see docs/sd-layout.md).
// Switch: sdmc:/switch/kk4s/...   PC: <folder of the exe>/... or $KK4S_ROOT
std::string RootDir();
std::string GamesDir();
std::string LogsDir();
std::string SavesDir();
std::string OverlayDir();
std::string ProfilesDir();

// Init services, data folders and the log file. Call first in main().
void Init(int argc, char** argv);

// Close the log file and services. Safe to call more than once.
void Shutdown();

// Launcher: picks a game folder from GamesDir() (Switch: text console menu;
// PC: path from the command line, or a list in the terminal). Returns the
// chosen folder (with trailing '/'), or "" if the user quit. Must be called
// before SDL creates its window.
std::string SelectGame();

// Engine arguments to pass on (argv without kk4s' own arguments).
// argv[0] is replaced by "<game folder>/krkr.exe" by the caller.
int EngineArgc();
char** EngineArgv();

// Initial window size and SDL window flags for this platform.
void WindowConfig(int& width, int& height, unsigned& sdl_flags);

// The folder chosen by SelectGame() ("" if none).
const std::string& GameDir();

// Decide the Kirikiri project directory for a game folder, the same way the
// Windows krkr.exe does next to the exe:
//   startup.tjs in the folder -> "<dir>/"
//   data.xp3                  -> "<dir>/data.xp3>"   (archive delimiter)
//   data/ folder              -> "<dir>/data/"
//   otherwise                 -> "<dir>/"
// File names are matched case-insensitively. game_dir must end with '/'.
std::string ResolveProjectDir(const std::string& game_dir);

// Logging. Every line goes to the log file (fflush + fsync, so the last line
// before a crash is on disk) and to stderr (nxlink, if connected).
void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void LogRaw(const char* text);

// Memory info from the kernel (bytes).
struct MemoryInfo {
    unsigned long long total;
    unsigned long long used;
    bool applet_mode; // true = launched from album (very little memory)
};
MemoryInfo QueryMemory();

} // namespace kk4s
