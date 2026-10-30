// Helpers shared by the platform implementations (not used by the engine).
#pragma once

#include <string>
#include <vector>

namespace kk4s {
namespace detail {

// Create the data folders under RootDir() and open the log file.
void InitDataDirsAndLog();
void CloseLog();

// Write the startup banner (version, git hash, memory).
void LogBanner(const char* platform_name);

// Game folders (names, sorted) inside GamesDir() that contain startup.tjs or *.xp3.
std::vector<std::string> ScanGames();
bool IsGameFolder(const std::string& dir);

std::string ReadLastPlayed();
void WriteLastPlayed(const std::string& name);

// Store the selected folder (adds trailing '/'), returns it.
std::string SetGameDir(const std::string& dir);

// Append font files (.ttf .ttc .otf) found in dir to out (optionally recursive).
void ListFontFiles(const std::string& dir, bool recursive, std::vector<std::string>& out);

void MakeDir(const std::string& path);

} // namespace detail
} // namespace kk4s
