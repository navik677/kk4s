// kk4s compatibility layer: things the original Windows krkr.exe gets from
// DLLs/TPMs or from third-party runtimes (Kirikiroid2), implemented in the
// engine so game folders run unmodified. Called from a few hook points in
// engine/ (marked "kk4s:").
#pragma once

#include "tjsCommHead.h"

#include <string>

namespace kk4s {
namespace compat {

// Right before startup.tjs runs: register extra TJS APIs and run
// xp3filter.tjs from the game folder (Kirikiroid2 convention).
void BeforeStartup();

// After startup.tjs returned.
void AfterStartup();

// kk4s: for hashed-name (PackinOne) repacks the real startup.tjs is an
// obfuscated bytecode stub. Return a TJS source shim (KiriKiri Z globals then
// system/Initialize.tjs) when the game has that script, NULL otherwise.
const tjs_char* StartupScriptShim();

// After TVPExecuteStorage() finished a script (shortname = file name only).
// Used to run Override2.tjs / AfterInit2.tjs at the right moment.
void AfterExecuteStorage(const ttstr& shortname);

// Plugins.link(): kk4s native replacement for a Windows plugin DLL.
// Returns false if kk4s has none (compat/kk4s_plugins.cpp).
bool LinkBuiltinPlugin(const ttstr& name);

// Plugins.link() for a DLL kk4s has no built-in replacement for.
void ReportMissingPlugin(const ttstr& name);

// Register game-folder fonts and OS fonts as "installed" fonts. Idempotent.
void RegisterFonts();

// A font name was not found and 'chosen' is used instead (logged once).
void ReportFontFallback(const ttstr& requested, const ttstr& chosen);

// 'font' draws Cyrillic full-width; its Cyrillic comes from 'chosen'
// (empty: no suitable font found). Logged once per font.
void ReportCyrillicSubstitute(const ttstr& font, const ttstr& chosen);

// Folder for the game's saves (native path, ends with '/') when separate
// saves are on: <data>/saves/<game>/. Existing saves in `original` are copied
// there the first time. "" = keep the engine's own data path.
std::string RedirectDataPath(const std::string& original);

// Native path of a local file access: paths in the game's own save folders
// (<game>/savedata/ and the engine's original data path) are mapped into the
// RedirectDataPath() folder, so KAG games that build "<exe>/savedata/..."
// themselves never write into the game folder. Other paths are returned as-is.
std::string RedirectSavePath(const std::string& native);

// True on the thread that runs the engine and TJS.
bool IsMainThread();

// True while Storages.setXP3ArchiveExtractionFilter() has a TJS function set.
// Such a filter must only run on the main thread (TJS is not thread-safe).
bool ScriptFilterActive();

// Write an engine log line (TVPAddLog) to the kk4s log file as UTF-8.
void LogTJS(const ttstr& line);

// Native path -> TVP storage name ("file://./c/..." style).
ttstr NativeToStorage(const std::string& native_path);

} // namespace compat
} // namespace kk4s
