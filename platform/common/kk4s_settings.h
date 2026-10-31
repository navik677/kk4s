// kk4s launcher settings and play history, stored in <data>/settings.ini.
#pragma once

#include <map>
#include <string>

namespace kk4s {

struct Settings {
    std::string language = "auto"; // auto, uk, en, ru, ja
    bool grid = false;             // grid layout instead of the carousel
    bool sort_recent = false;      // recently played first instead of by title
    bool cover_background = true;  // blurred cover behind the launcher
    bool hide_explicit = true;     // skip covers VNDB marks as sexual

    struct Play {
        long long last = 0; // unix time of the last launch
        int count = 0;
    };
    std::map<std::string, Play> played; // by game folder name

    void Load();
    void Save() const;
    void RecordLaunch(const std::string& folder);
};

} // namespace kk4s
