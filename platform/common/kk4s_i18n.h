// kk4s launcher translations (Ukrainian, English, Russian, Japanese).
#pragma once

#include <string>

namespace kk4s {
namespace i18n {

enum Str {
    kAppSubtitle,
    kPlay, kCover, kSettings, kQuit, kBack, kSelect, kChoose, kEditQuery, kSearch, kChange, kOpen,
    kNoGames, kNoGamesHint,
    kLastPlayed, kNeverPlayed, kToday, kYesterday,
    kCoverFromInternet, kSearchFor, kSearching, kNothingFound, kNoConnection, kDownloadFailed,
    kDownloading, kCoverSaved, kCannotWriteSd, kEditQueryGuide,
    kLanguage, kLanguageAuto, kView, kViewCarousel, kViewGrid, kSort, kSortName, kSortRecent,
    kCoverBackground, kOn, kOff, kHideExplicit, kDownloadAllCovers, kDownloadAllHint,
    kCoversProgress, kCoversDone, kAbout, kVersion, kDataFolder, kSource, kAboutText,
    kSeparateSaves, kSeparateSavesHint, kReturnToLauncher, kAutostart, kAutostartIn, kCancel, kControls, kControlsHint, kResetControls, kActNone, kActEnter, kActRightClick, kActCtrl, kActSpace, kActPageUp, kActPageDown, kActShift, kActEsc, kActTab, kGameOptions, kOptions, kSaves, kNoSaves, kBackupSaves, kRestoreSaves, kLatestBackup, kNoBackups, kDeleteSaves, kDeleteSavesHint, kDeleteCover, kPressAgain, kBackupDone, kRestoreDone, kDeleteDone, kCoverRemoved, kFailed, kSavedAt,
    kMenu, kResume, kQuickSave, kQuickSaveHint, kQuickLoad, kVolume, kExitToLauncher, kExitApp, kSaved, kLoaded, kCannotSaveNow, kNoQuickSave, kNotSupported, kActOverlay, kOpenMenuHint,
    kCount
};

// "auto", "uk", "en", "ru", "ja". "auto" follows the system language (Switch
// settings / $LANG), falling back to English.
void SetLanguage(const std::string& code);
const std::string& Language();          // resolved code ("uk", ...)
const char* T(Str s);                    // translated text (UTF-8)
std::string F(Str s, const std::string& arg); // T(s) with "%s" replaced by arg

// "%d" forms for counted words: games, launches. n is inserted.
std::string Games(int n);
std::string Launches(int n);
std::string Files(int n);

// Name of a language in itself ("Українська", "English", ...), "" for unknown.
const char* LanguageName(const std::string& code);

} // namespace i18n
} // namespace kk4s
