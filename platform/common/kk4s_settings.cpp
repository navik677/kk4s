// kk4s settings (see kk4s_settings.h). Plain "key=value" lines; play history
// as "played.<folder>=<unix time> <count>", gamepad as "pad.<button>=<action>".
#include "kk4s_settings.h"
#include "kk4s_platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace kk4s {

namespace {

const char* const kActionIds[] = {"none", "enter", "rclick", "ctrl", "space", "pageup", "pagedown", "shift", "esc", "tab"};

const struct { unsigned bit; const char* name; } kButtons[] = {
    {kPadA, "a"}, {kPadB, "b"}, {kPadX, "x"}, {kPadY, "y"}, {kPadL, "l"},
    {kPadR, "r"}, {kPadZL, "zl"}, {kPadZR, "zr"}, {kPadPlus, "plus"}, {kPadMinus, "minus"},
};

std::string SettingsPath() { return RootDir() + "/settings.ini"; }

} // namespace

const char* PadActionId(PadAction a) {
    int i = (int)a;
    return i >= 0 && i < (int)PadAction::Count ? kActionIds[i] : "none";
}

PadAction PadActionFromId(const std::string& id) {
    for (int i = 0; i < (int)PadAction::Count; ++i)
        if (id == kActionIds[i]) return (PadAction)i;
    return PadAction::None;
}

PadAction DefaultPadAction(unsigned b) {
    switch (b) {
        case kPadA: return PadAction::Enter;
        case kPadB: return PadAction::RightClick;
        case kPadX: case kPadZR: return PadAction::Ctrl;
        case kPadY: return PadAction::Space;
        case kPadL: return PadAction::PageUp;
        case kPadR: return PadAction::PageDown;
        case kPadZL: return PadAction::Shift;
        case kPadPlus: return PadAction::Esc;
        case kPadMinus: return PadAction::Tab;
    }
    return PadAction::None;
}

PadAction Settings::PadFor(unsigned b) const {
    auto it = pad.find(b);
    return it == pad.end() ? DefaultPadAction(b) : it->second;
}

void Settings::Load() {
    FILE* f = fopen(SettingsPath().c_str(), "rb");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        std::string s = line;
        size_t eq = s.rfind('=');
        if (eq == std::string::npos || s[0] == '#') continue;
        std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if (k == "language") language = v;
        else if (k == "layout") grid = v == "grid";
        else if (k == "sort") sort_recent = v == "recent";
        else if (k == "cover_background") cover_background = v != "0";
        else if (k == "hide_explicit") hide_explicit = v != "0";
        else if (k == "separate_saves") separate_saves = v != "0";
        else if (k == "return_to_launcher") return_to_launcher = v != "0";
        else if (k == "autostart") autostart = v != "0";
        else if (k.compare(0, 4, "pad.") == 0) {
            for (const auto& b : kButtons)
                if (k.substr(4) == b.name) pad[b.bit] = PadActionFromId(v);
        } else if (k.compare(0, 7, "played.") == 0) {
            Play p;
            p.last = atoll(v.c_str());
            size_t sp = v.find(' ');
            p.count = sp == std::string::npos ? 1 : atoi(v.c_str() + sp + 1);
            played[k.substr(7)] = p;
        }
    }
    fclose(f);
}

void Settings::Save() const {
    std::string tmp = SettingsPath() + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    fprintf(f, "# kk4s settings\n");
    fprintf(f, "language=%s\n", language.c_str());
    fprintf(f, "layout=%s\n", grid ? "grid" : "carousel");
    fprintf(f, "sort=%s\n", sort_recent ? "recent" : "title");
    fprintf(f, "cover_background=%d\n", cover_background ? 1 : 0);
    fprintf(f, "hide_explicit=%d\n", hide_explicit ? 1 : 0);
    fprintf(f, "separate_saves=%d\n", separate_saves ? 1 : 0);
    fprintf(f, "return_to_launcher=%d\n", return_to_launcher ? 1 : 0);
    fprintf(f, "autostart=%d\n", autostart ? 1 : 0);
    for (const auto& b : kButtons)
        if (pad.count(b.bit)) fprintf(f, "pad.%s=%s\n", b.name, PadActionId(pad.at(b.bit)));
    for (const auto& kv : played)
        fprintf(f, "played.%s=%lld %d\n", kv.first.c_str(), kv.second.last, kv.second.count);
    bool ok = fclose(f) == 0;
    // rename over an existing file fails on some file systems: remove first then
    if (ok && rename(tmp.c_str(), SettingsPath().c_str()) != 0) {
        remove(SettingsPath().c_str());
        rename(tmp.c_str(), SettingsPath().c_str());
    }
}

void Settings::RecordLaunch(const std::string& folder) {
    Play& p = played[folder];
    p.last = (long long)time(nullptr);
    p.count++;
}

std::string SaveFolderFor(const Settings& s, const std::string& game_folder) {
    if (s.separate_saves) return SavesDir() + "/" + game_folder + "/";
    return GamesDir() + "/" + game_folder + "/savedata/";
}

} // namespace kk4s
