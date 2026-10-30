// kk4s platform layer for Nintendo Switch (libnx).
// Everything Switch-specific that kk4s adds on top of the krkrs engine lives
// behind this small interface, so the engine sources in engine/ stay close to
// upstream.
#pragma once

#include <string>

namespace kk4s {

// SD card layout (see docs/sd-layout.md)
extern const char* const kRootDir;     // sdmc:/switch/kk4s
extern const char* const kGamesDir;    // sdmc:/switch/kk4s/games
extern const char* const kLogsDir;     // sdmc:/switch/kk4s/logs
extern const char* const kSavesDir;    // sdmc:/switch/kk4s/saves
extern const char* const kOverlayDir;  // sdmc:/switch/kk4s/overlay
extern const char* const kProfilesDir; // sdmc:/switch/kk4s/profiles

// Init romfs, sockets/nxlink, SD folders and the log file. Call first in main().
void Init();

// Close the log file and services. Safe to call more than once.
void Shutdown();

// Text console launcher: lists game folders in kGamesDir and lets the user
// pick one. Returns the chosen folder (with trailing '/'), or "" if the user
// quit. Must be called before SDL creates its window.
std::string SelectGame();

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
