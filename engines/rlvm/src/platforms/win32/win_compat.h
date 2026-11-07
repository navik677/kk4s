// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// kk4s: small POSIX shims for the Windows build (tools/build-rlvm-win.sh
// passes this header with -include). rlvm was written for glibc, which has
// rand_r(), random() and srandom(); mingw has none of them.
//
// windows.h is deliberately not included here: it is pulled into every
// translation unit by -include, and its macros (GetObject, Rectangle, FLOAT,
// u_long) collide with rlvm's and guichan's own names. PathStr() is
// implemented in win_compat.cc, the only file that includes windows.h.
//
// -----------------------------------------------------------------------

#ifndef SRC_PLATFORMS_WIN32_WIN_COMPAT_H_
#define SRC_PLATFORMS_WIN32_WIN_COMPAT_H_

#if defined(_WIN32) && !defined(__SWITCH__)

#include <cstdlib>
#include <string>

// BSD types that glibc and the Switch's newlib provide through <sys/types.h>
// and mingw does not (xclannad's wavfile.cc uses them).
typedef unsigned long u_long;
typedef unsigned short u_short;
typedef unsigned char u_char;

namespace boost {
namespace filesystem {
class path;
}  // namespace filesystem
}  // namespace boost

// The glibc algorithm is not needed: only the game's random seed matters, and
// any seed-derived sequence is as good as another for a visual novel.
inline int rand_r(unsigned int* seed) {
  *seed = *seed * 1103515245u + 12345u;
  return static_cast<int>((*seed >> 16) & 0x7fff);
}

inline long random() { return std::rand(); }
inline void srandom(unsigned int seed) { std::srand(seed); }

// boost::filesystem::path::string() is a std::wstring on Windows (and this
// Boost has no u8string()), while rlvm passes paths around as UTF-8 strings.
std::string PathStr(const boost::filesystem::path& p);

#endif  // _WIN32

#endif  // SRC_PLATFORMS_WIN32_WIN_COMPAT_H_
