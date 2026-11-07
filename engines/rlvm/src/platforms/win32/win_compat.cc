// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// kk4s: the Windows half of win_compat.h. This is the only file that includes
// windows.h; see the comment there for why.
//
// -----------------------------------------------------------------------

#include "platforms/win32/win_compat.h"

#if defined(_WIN32) && !defined(__SWITCH__)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>

#include <boost/filesystem.hpp>

std::string PathStr(const boost::filesystem::path& p) {
  const std::wstring w = p.wstring();
  if (w.empty())
    return std::string();
  const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                      nullptr, 0, nullptr, nullptr);
  if (n <= 0)
    return std::string();
  std::string out((size_t)n, '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], n,
                        nullptr, nullptr);
  return out;
}

#endif  // _WIN32
