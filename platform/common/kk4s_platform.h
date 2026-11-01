// kk4s platform interface.
// Everything platform-specific that kk4s adds on top of the krkrs engine
// lives behind this small interface, so the engine sources in engine/ stay
// close to upstream. Implementations:
//   platform/switch/kk4s_platform.cpp   (libnx, Nintendo Switch)
//   platform/desktop/kk4s_platform.cpp  (Linux / Windows PC viewer)
#pragma once

#include <string>
#include <vector>

struct SDL_Renderer;

namespace kk4s {

// Data folder layout (see docs/sd-layout.md).
// Switch: sdmc:/switch/kk4s/...   PC: <folder of the exe>/... or $KK4S_ROOT
std::string RootDir();
std::string GamesDir();
std::string LogsDir();
std::string SavesDir();
std::string OverlayDir();
std::string ProfilesDir();
std::string TempDir(); // scratch files of the engine (<root>/tmp, created at start)

// Init services, data folders and the log file. Call first in main().
void Init(int argc, char** argv);

// Close the log file and services. Safe to call more than once.
void Shutdown();

// End the process after a game quit: back to the launcher when the setting
// says so (Switch: hbloader starts kk4s.nro again; PC: re-exec when the game
// was picked in the launcher), otherwise a normal exit. Calls Shutdown().
[[noreturn]] void ExitAfterGame(int code);

// Overrides the "return to launcher" setting for the next ExitAfterGame
// (in-game menu: "exit to launcher" / "quit kk4s").
enum class ExitMode { Setting, Launcher, Quit };
void SetExitMode(ExitMode mode);
ExitMode GetExitMode();

// Launcher: picks a game folder from GamesDir() with the graphical picker
// (platform/common/kk4s_launcher.cpp) drawn into the engine's renderer; on PC
// a folder given on the command line is used directly. Returns the chosen
// folder (with trailing '/'), or "" if the user quit.
std::string SelectGame(SDL_Renderer* renderer);

// Engine arguments to pass on (argv without kk4s' own arguments).
// argv[0] is replaced by "<game folder>/krkr.exe" by the caller.
int EngineArgc();
char** EngineArgv();

// Gamepad buttons by their printed label (Nintendo layout).
enum PadButton : unsigned {
    kPadA = 1u << 0, kPadB = 1u << 1, kPadX = 1u << 2, kPadY = 1u << 3,
    kPadL = 1u << 4, kPadR = 1u << 5, kPadZL = 1u << 6, kPadZR = 1u << 7,
    kPadPlus = 1u << 8, kPadMinus = 1u << 9,
    kPadLeft = 1u << 10, kPadUp = 1u << 11, kPadRight = 1u << 12, kPadDown = 1u << 13,
    kPadLStick = 1u << 14, kPadRStick = 1u << 15, // stick clicks (L3 / R3)
    kPadLast = kPadRStick,
};
// Both sticks pressed together open / close the in-game menu (+ and − stay
// free for the game). The stick clicks do nothing else in games.
const unsigned kPadMenuCombo = kPadLStick | kPadRStick;

// Switch: buttons held right now, read straight from libnx (SDL's controller
// mapping is positional, so its "A" arrives for the Nintendo B button). The
// left stick counts as the D-pad. Returns false on platforms where SDL game
// controller events are used instead (PC).
bool ReadPad(unsigned& held);

// SDL_GameControllerButton -> PadButton (0 if not used). For PC pads.
unsigned PadFromSdlButton(int sdl_button);

// Call at the start of every worker thread (engine threads, audio callback,
// log writer). Switch: libnx puts new threads on core 0 next to the main
// thread, at a lower priority, so they only ran while the main thread waited;
// this moves the calling thread to the other application cores. Elsewhere a
// no-op. audio = true also raises the priority (the mixer must never starve).
void PlaceWorkerThread(const char* name, bool audio = false);

// Threads that do not end by themselves (engine threads, launcher jobs) call
// TrackThread() when they start and UntrackThread() when they end. hbloader
// starts the next NRO (the launcher again, or hbmenu) in the same process, and
// engine threads left running there corrupted its memory (crash in the launcher
// after a game). The engine ends its threads at exit (TVPExitApplication:
// TVPSystemUninit + TVPStopAllThreads); on the Switch Shutdown() pauses any
// tracked thread that is still alive and unmaps its stack, as a last resort.
// Elsewhere a no-op: the OS ends them with the process.
void TrackThread();
void UntrackThread();

// Switch: CPU boost (1785 MHz instead of 1020 MHz) while a game runs, if the
// setting allows it. No-op elsewhere.
void SetGamePerformance(bool boost);

// System UI language as a two-letter code ("uk", "en", "ru", "ja", ...).
std::string SystemLanguage();

// Battery charge 0..100, or -1 if unknown / no battery.
int BatteryPercent(bool* charging);

// System on-screen keyboard (Switch). Returns false if there is none or the
// user cancelled; text is changed only on success.
bool EditText(const std::string& guide, std::string& text);
bool EditTextAvailable(); // true if EditText shows a system keyboard

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

// Font files (native paths) the engine may use as "installed system fonts":
// OS fonts that cover Japanese/Cyrillic/Latin, plus <RootDir>/fonts/.
// Switch: Nintendo's shared system font (cached on SD) + romfs:/fonts/.
std::vector<std::string> SystemFontFiles();

// Full native path of a file directly inside the game folder, matched
// case-insensitively ("xp3filter.tjs" finds "XP3Filter.tjs"). "" if missing.
std::string FindGameFile(const char* name);

// Like FindGameFile, then <data>/profiles/<game folder>/<name>: Kirikiroid2
// helper scripts (xp3filter.tjs, Override2.tjs, AfterInit2.tjs) can live there,
// so a PC game folder runs as it is. Keys stay on the user's SD card.
std::string FindCompatFile(const char* name);

// Logging. Lines go to stderr (nxlink, if connected) and to the log file,
// written by a background thread every 200 ms (fsync at most every 2 s).
void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void LogRaw(const char* text);

// Main-thread time of the current frame by kind (ms), filled by engine hooks
// and printed on "slow frame" log lines, then reset.
struct FrameStats {
    unsigned compose = 0; // layer tree -> frame buffer (software drawing)
    unsigned upload = 0;  // frame buffer -> GPU texture
    unsigned present = 0; // SDL_RenderPresent (waits for vsync)
    unsigned read = 0;    // whole-file reads + decryption of archive files
    unsigned reads = 0;   // how many such files
    unsigned write = 0;   // writing local files (saves): open + write + close
    unsigned writes = 0;  // files opened for writing
    unsigned gc = 0;      // TJS garbage collection / rehash (idle compaction)
};
FrameStats& CurrentFrameStats();

// Memory info from the kernel (bytes).
struct MemoryInfo {
    unsigned long long total;
    unsigned long long used;
    bool applet_mode; // true = launched from album (very little memory)
};
MemoryInfo QueryMemory();

} // namespace kk4s
