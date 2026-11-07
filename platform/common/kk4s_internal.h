// Helpers shared by the platform implementations (not used by the engine).
#pragma once

#include <string>
#include <vector>

#include "kk4s_platform.h"

struct SDL_Renderer;

namespace kk4s {
namespace detail {

// Create the data folders under RootDir() and open the log file.
void InitDataDirsAndLog();
void CloseLog();

// Write the startup banner (version, git hash, memory).
void LogBanner(const char* platform_name);

// Game folders (names, sorted) inside GamesDir() that contain startup.tjs or *.xp3.
// A folder of several games (games/ons/<game>/) is a collection: each of its
// games is listed as "<collection>/<game>".
std::vector<std::string> ScanGames();
bool IsGameFolder(const std::string& dir);
std::vector<std::string> CollectionGames(const std::string& dir);
// games/<name> is a collection (ScanGames lists "<name>/<game>"), not a game.
bool IsCollectionFolder(const std::string& full, const std::string& name);

// Offset of an XP3 archive inside a Windows executable, -1 if none. Looks after
// the PE image first; full_scan also searches the whole file as Kirikiri does.
long long FindXP3InExecutable(const std::string& path, bool full_scan);

// "" for a plain KiriKiri folder, else a short reason why it cannot start as
// one (Steam/PackinOne repack markers). Shown in the launcher and the log.
std::string DetectGameIssue(const std::string& dir);

// External engines (kk4s_engines.cpp).
const char* EngineId(GameEngine e);           // "onscripter", "rlvm", "renpy"
std::string GameKey(const std::string& game_dir); // folder name directly in GamesDir()
// Arguments after argv[0]; return_to = program to come back to (Switch), or "".
std::vector<std::string> ExternalEngineArgs(GameEngine e, const std::string& game_dir, const std::string& program,
                                            const std::string& return_to);
bool WriteRenPyHandoff(const std::string& game_dir, const std::string& return_to);

std::string ReadLastPlayed();
void WriteLastPlayed(const std::string& name);

// Store the selected folder (adds trailing '/'), returns it.
std::string SetGameDir(const std::string& dir);

// Append font files (.ttf .ttc .otf) found in dir to out (optionally recursive).
void ListFontFiles(const std::string& dir, bool recursive, std::vector<std::string>& out);

void MakeDir(const std::string& path);

// Graphical game picker (kk4s_launcher.cpp). folders = names in GamesDir(),
// sel = initially selected. Returns the chosen name, or "" if the user quit.
std::string RunLauncher(SDL_Renderer* renderer, const std::vector<std::string>& folders, size_t sel);

} // namespace detail
} // namespace kk4s
