// kk4s: PathStr(), a boost::filesystem::path as a UTF-8 std::string.
//
// On Windows path::string() is in the ANSI code page, so the Windows build
// converts from wstring (platforms/win32/win_compat.cc). Everywhere else
// (the Switch, Linux) path::string() already is that string.

#ifndef SRC_UTILITIES_PATH_STR_H_
#define SRC_UTILITIES_PATH_STR_H_

#include <boost/filesystem/path.hpp>

#include <string>

#if defined(_WIN32) && !defined(__SWITCH__)
std::string PathStr(const boost::filesystem::path& p);
#else
inline std::string PathStr(const boost::filesystem::path& p) {
  return p.string();
}
#endif

#endif  // SRC_UTILITIES_PATH_STR_H_
