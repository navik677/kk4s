// kk4s settings and play history, stored in <data>/settings.ini. Read by the
// launcher and by the engine side (save folder, gamepad mapping).
#pragma once

#include <map>
#include <string>

namespace kk4s {

// What a gamepad button does in a game (keys / mouse buttons KAG understands).
enum class PadAction { None, Enter, RightClick, Ctrl, Space, PageUp, PageDown, WheelUp, WheelDown, Shift, Esc, Tab, Overlay, Count };
// Overlay = open the kk4s in-game menu. WheelUp / WheelDown = one mouse wheel
// notch (the backlog in KAG and most Kirikiri games), repeated while held.

const char* PadActionId(PadAction a);              // "enter", "rclick", ...
PadAction PadActionFromId(const std::string& id);  // None if unknown
PadAction DefaultPadAction(unsigned pad_button);   // kPadA -> Enter, ...

struct Settings {
    std::string language = "auto"; // auto, uk, en, ru, ja
    bool grid = false;             // grid layout instead of the carousel
    bool sort_recent = false;      // recently played first instead of by title
    bool cover_background = true;  // blurred cover behind the launcher
    bool hide_explicit = true;     // skip covers VNDB marks as sexual
    bool separate_saves = true;    // saves in <data>/saves/<game>/ instead of the game folder
    bool return_to_launcher = true;// back to the launcher when a game quits
    bool autostart = false;        // start the last game after a short countdown
    bool cpu_boost = true;         // Switch: CPU at 1785 MHz while a game runs
    bool keep_aspect = true;       // letterbox the game picture instead of stretching it
    bool zoom_fill = false;        // with keep_aspect: fill the whole screen, cropping the overflow (4:3 on 16:9)

    // Screen mode for games whose picture is not the screen's shape (4:3 on 16:9):
    // 0 original proportions with black bars, 1 stretched over the whole screen, 2 zoomed to fill (crop).
    enum { kScreenBars = 0, kScreenStretch = 1, kScreenZoom = 2, kScreenModes = 3 };
    int ScreenMode() const { return !keep_aspect ? kScreenStretch : (zoom_fill ? kScreenZoom : kScreenBars); }
    void SetScreenMode(int m) {
        m = ((m % kScreenModes) + kScreenModes) % kScreenModes;
        keep_aspect = m != kScreenStretch;
        zoom_fill = m == kScreenZoom;
    }
    bool soft_mode = false;        // missing members of native classes -> log + void once a plugin was ignored
    int volume = 100;              // game sound volume, 0..100 (WaveSoundBuffer.globalVolume)

    std::map<unsigned, PadAction> pad; // by kPad* button (A..Minus); missing = default

    struct Play {
        long long last = 0; // unix time of the last launch
        int count = 0;
    };
    std::map<std::string, Play> played; // by game folder name

    void Load();
    void Save() const;
    void RecordLaunch(const std::string& folder);
    PadAction PadFor(unsigned pad_button) const;
};

// Settings shared by the engine side (pad mapping, volume) and the in-game
// menu; loaded on first use.
Settings& LiveSettings();

// Folder the game's saves live in: <data>/saves/<folder>/ with separate
// saves, otherwise <game>/savedata/. Ends with '/'.
std::string SaveFolderFor(const Settings& s, const std::string& game_folder);

} // namespace kk4s
