// kk4s launcher settings (see kk4s_settings.h). Plain "key=value" lines;
// play history as "played.<folder>=<unix time> <count>".
#include "kk4s_settings.h"
#include "kk4s_platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace kk4s {

static std::string SettingsPath() { return RootDir() + "/settings.ini"; }

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
        else if (k.compare(0, 7, "played.") == 0) {
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
    fprintf(f, "# kk4s launcher settings\n");
    fprintf(f, "language=%s\n", language.c_str());
    fprintf(f, "layout=%s\n", grid ? "grid" : "carousel");
    fprintf(f, "sort=%s\n", sort_recent ? "recent" : "title");
    fprintf(f, "cover_background=%d\n", cover_background ? 1 : 0);
    fprintf(f, "hide_explicit=%d\n", hide_explicit ? 1 : 0);
    for (const auto& kv : played)
        fprintf(f, "played.%s=%lld %d\n", kv.first.c_str(), kv.second.last, kv.second.count);
    bool ok = fclose(f) == 0;
    // replace atomically where possible (rename over an existing file fails on some FS)
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

} // namespace kk4s
