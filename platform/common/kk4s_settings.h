// kk4s settings and play history, stored in <data>/settings.ini. Read by the
// launcher and by the engine side (save folder, gamepad mapping).
#pragma once

#include <map>
#include <string>

namespace kk4s {

// What a gamepad button does in a game (keys / mouse buttons KAG understands).
enum class PadAction { None, Enter, RightClick, Ctrl, Space, PageUp, PageDown, Shift, Esc, Tab, Count };

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

// Folder the game's saves live in: <data>/saves/<folder>/ with separate
// saves, otherwise <game>/savedata/. Ends with '/'.
std::string SaveFolderFor(const Settings& s, const std::string& game_folder);

} // namespace kk4s
