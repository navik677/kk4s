// kk4s compatibility layer: things the original Windows krkr.exe gets from
// DLLs/TPMs or from third-party runtimes (Kirikiroid2), implemented in the
// engine so game folders run unmodified. Called from a few hook points in
// engine/ (marked "kk4s:").
#pragma once

#include "tjsCommHead.h"

namespace kk4s {
namespace compat {

// Right before startup.tjs runs: register extra TJS APIs and run
// xp3filter.tjs from the game folder (Kirikiroid2 convention).
void BeforeStartup();

// After startup.tjs returned.
void AfterStartup();

// After TVPExecuteStorage() finished a script (shortname = file name only).
// Used to run Override2.tjs / AfterInit2.tjs at the right moment.
void AfterExecuteStorage(const ttstr& shortname);

// Plugins.link(): kk4s native replacement for a Windows plugin DLL.
// Returns false if kk4s has none (compat/kk4s_plugins.cpp).
bool LinkBuiltinPlugin(const ttstr& name);

// Plugins.link() for a DLL kk4s has no built-in replacement for.
void ReportMissingPlugin(const ttstr& name);

// Write an engine log line (TVPAddLog) to the kk4s log file as UTF-8.
void LogTJS(const ttstr& line);

// Native path -> TVP storage name ("file://./c/..." style).
ttstr NativeToStorage(const std::string& native_path);

} // namespace compat
} // namespace kk4s
