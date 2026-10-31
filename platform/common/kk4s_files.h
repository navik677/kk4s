// Small file helpers (POSIX) for save management: stats, recursive copy, move.
#pragma once

#include <string>
#include <vector>

namespace kk4s {
namespace files {

bool IsDirectory(const std::string& path);

struct DirStats {
    int files = 0;
    unsigned long long bytes = 0;
    long long newest = 0; // unix time of the newest file
};
DirStats Stats(const std::string& dir); // recursive

// Recursive copy into `to` (created). Existing files are overwritten.
bool CopyDir(const std::string& from, const std::string& to);
// rename(), or copy + remove when that fails (e.g. across folders on FAT).
bool MoveDir(const std::string& from, const std::string& to);
bool RemoveDir(const std::string& dir); // recursive
bool MakeDirs(const std::string& path);  // like mkdir -p

// Sub-folder names of dir, sorted (newest last for timestamp names).
std::vector<std::string> SubDirs(const std::string& dir);

std::string Timestamp(); // "20261031-021500"

} // namespace files
} // namespace kk4s
