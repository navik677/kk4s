// kk4s launcher: the game picker, drawn with SDL2 into the engine's window.
//
// Home: a carousel (or grid) of cover cards over a blurred copy of the
// selected cover, title and play history below, button hints at the bottom.
// Panels: settings (language, layout, sort, ...) and the VNDB cover search.
// Works with gamepad (libnx on Switch, SDL game controllers on PC), touch,
// keyboard and mouse. Text is translated (kk4s_i18n).
//
// Covers, first found wins (see docs/sd-layout.md):
//   <data>/covers/<game folder name>.*            (the cover search saves here)
//   <game>/cover.* | folder.* | poster.*         (png, jpg, jpeg, webp)
//   otherwise a generated card with the game's icon (*.ico or the .exe icon).
// Title: <game>/title.txt, <data>/covers/<folder>.txt (UTF-8, first line), else the folder name.
#include "kk4s_i18n.h"
#include "kk4s_image.h"
#include "kk4s_internal.h"
#include "kk4s_net.h"
#include "kk4s_platform.h"
#include "kk4s_settings.h"
#include "kk4s_ui.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <memory>
#include <mutex>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <vector>

#ifndef KK4S_VERSION
#define KK4S_VERSION "dev"
#endif
#ifndef KK4S_GIT_HASH
#define KK4S_GIT_HASH "unknown"
#endif

namespace kk4s {
namespace detail {
namespace {

using namespace ui;
using img::Image;
using i18n::T;

// palette
const SDL_Color kBase = Rgba(11, 13, 20);
const SDL_Color kPanel = Rgba(20, 24, 36, 250);
const SDL_Color kSurface = Rgba(255, 255, 255, 18);
const SDL_Color kSurfaceHi = Rgba(255, 255, 255, 34);
const SDL_Color kAccent = Rgba(124, 156, 255);
const SDL_Color kText = Rgba(244, 246, 251);
const SDL_Color kText2 = Rgba(163, 171, 194);
const SDL_Color kText3 = Rgba(110, 118, 144);
const SDL_Color kWarn = Rgba(255, 160, 140);

float Clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
float Ease(float t) { t = Clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
float Approach(float cur, float target, float dt, float speed = 12) { return cur + (target - cur) * Clamp01(dt * speed); }

// ---------------------------------------------------------------------------
// games

struct Game {
    std::string folder; // name inside GamesDir()
    std::string title;
    SDL_Texture* cover = nullptr; // full cover art
    SDL_Texture* blur = nullptr;  // tiny blurred cover for the background
    SDL_Texture* icon = nullptr;  // icon for generated cards
    int cover_w = 0, cover_h = 0;
    SDL_Color tint{60, 70, 110, 255};
};

std::vector<std::string> ListFiles(const std::string& dir, bool (*pred)(const std::string&)) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string name = e->d_name;
        if (pred(img::Lower(name))) out.push_back(name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool IsImageExt(const std::string& ext) { return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp"; }
bool IsImageFile(const std::string& n) { return IsImageExt(img::Ext(n)); }

std::string BaseLower(const std::string& f) { return img::Lower(f.substr(0, f.rfind('.'))); }

// <data>/covers/<folder>.* first (where the cover search saves), then the game folder.
std::string FindCover(const std::string& dir, const std::string& folder) {
    std::string covers = RootDir() + "/covers";
    for (const std::string& f : ListFiles(covers, IsImageFile))
        if (BaseLower(f) == img::Lower(folder)) return covers + "/" + f;
    for (const char* base : {"cover", "folder", "poster"})
        for (const std::string& f : ListFiles(dir, IsImageFile))
            if (BaseLower(f) == base) return dir + "/" + f;
    return "";
}

std::string FindIcon(const std::string& dir, const std::string& folder) {
    std::vector<std::string> icos = ListFiles(dir, [](const std::string& n) { return img::Ext(n) == "ico"; });
    if (!icos.empty()) return dir + "/" + icos[0];
    // The game exe: same name as the folder, else the biggest one that is not an uninstaller.
    std::string best;
    long best_size = -1;
    for (const std::string& f : ListFiles(dir, [](const std::string& n) { return img::Ext(n) == "exe"; })) {
        std::string low = img::Lower(f);
        if (low.compare(0, 5, "unins") == 0) continue;
        if (low == img::Lower(folder) + ".exe") return dir + "/" + f;
        struct stat st;
        if (stat((dir + "/" + f).c_str(), &st) == 0 && st.st_size > best_size) { best_size = st.st_size; best = f; }
    }
    return best.empty() ? "" : dir + "/" + best;
}

std::string ReadFirstLine(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    char buf[256] = {0};
    if (!fgets(buf, sizeof(buf), f)) buf[0] = 0;
    fclose(f);
    char* s = buf;
    if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
    s[strcspn(s, "\r\n")] = 0;
    return s;
}

std::string ReadTitle(const std::string& dir, const std::string& folder) {
    std::string t = ReadFirstLine(dir + "/title.txt");
    if (t.empty()) t = ReadFirstLine(RootDir() + "/covers/" + folder + ".txt");
    return t.empty() ? folder : t;
}

SDL_Color TintFor(const std::string& name) {
    uint32_t h = 2166136261u;
    for (char c : name) h = (h ^ (unsigned char)c) * 16777619u;
    static const SDL_Color kTints[] = {{88, 76, 170, 255}, {44, 110, 160, 255}, {150, 60, 100, 255},
                                       {40, 130, 118, 255}, {160, 100, 50, 255}, {100, 64, 150, 255}};
    return kTints[h % (sizeof(kTints) / sizeof(kTints[0]))];
}

void FreeGame(Game& g) {
    for (SDL_Texture** t : {&g.cover, &g.blur, &g.icon})
        if (*t) { SDL_DestroyTexture(*t); *t = nullptr; }
}

void LoadGame(SDL_Renderer* r, Game& g) {
    FreeGame(g);
    std::string dir = GamesDir() + "/" + g.folder;
    g.title = ReadTitle(dir, g.folder);
    g.tint = TintFor(g.folder);
    std::string cover = FindCover(dir, g.folder);
    if (!cover.empty()) {
        Image im = img::Shrink(img::DecodeFile(cover), 600, 840);
        g.cover = MakeTexture(r, im);
        g.cover_w = im.w;
        g.cover_h = im.h;
        if (im.ok()) {
            Image small = img::Shrink(im, 48, 68);
            img::BoxBlur(small, 2, 3);
            g.blur = MakeTexture(r, small);
        }
        Log("launcher: %s: cover %s%s", g.folder.c_str(), cover.c_str(), g.cover ? "" : " (cannot decode)");
    }
    if (!g.cover) {
        std::string icon = FindIcon(dir, g.folder);
        if (!icon.empty()) {
            g.icon = MakeTexture(r, img::Shrink(img::DecodeFile(icon), 256, 256));
            if (g.icon) Log("launcher: %s: icon from %s", g.folder.c_str(), icon.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// background jobs (network work on detached threads; the UI polls)

template <class F>
void RunDetached(const char* name, F fn) {
    // Own stack size: TLS + JSON + JPEG decoding need more than the default on Switch.
    auto* heap = new F(std::move(fn));
    SDL_Thread* t = SDL_CreateThreadWithStackSize(
        [](void* p) -> int {
            F* f = static_cast<F*>(p);
            (*f)();
            delete f;
            return 0;
        },
        name, 1u << 20, heap);
    if (t) SDL_DetachThread(t);
    else { (*heap)(); delete heap; }
}

std::string Fetch(const std::string& url, std::string& err) {
    std::string data;
    if (!net::Request(url, nullptr, data, err)) return "";
    return data;
}

struct SearchJob {
    std::mutex m;
    bool done = false, ok = false;
    std::string err, used_query;
    std::vector<net::CoverCandidate> found;
    std::vector<Image> thumbs;
};

// exact = search this text as typed; otherwise clean it up and retry shorter.
std::shared_ptr<SearchJob> StartSearch(const std::string& text, bool exact, bool hide_explicit) {
    auto job = std::make_shared<SearchJob>();
    RunDetached("kk4s-cover-search", [job, text, exact, hide_explicit] {
        std::vector<net::CoverCandidate> found;
        std::string err, used = text;
        bool ok = exact ? net::SearchVndbCovers(text, hide_explicit, found, err)
                        : net::SearchCoversForTitle(text, hide_explicit, found, used, err);
        std::vector<Image> thumbs;
        for (const net::CoverCandidate& c : found) {
            std::string e, data = Fetch(c.thumb_url, e);
            thumbs.push_back(img::Shrink(img::DecodeImageData((const uint8_t*)data.data(), data.size()), 300, 420));
        }
        std::lock_guard<std::mutex> lock(job->m);
        job->found = std::move(found);
        job->thumbs = std::move(thumbs);
        job->err = err;
        job->used_query = used;
        job->ok = ok;
        job->done = true;
    });
    return job;
}

struct DownloadJob {
    std::mutex m;
    bool done = false, ok = false;
    std::string err, data;
};

std::shared_ptr<DownloadJob> StartDownload(const std::string& url) {
    auto job = std::make_shared<DownloadJob>();
    RunDetached("kk4s-cover-download", [job, url] {
        std::string err, data = Fetch(url, err);
        bool ok = !data.empty() && img::DecodeImageData((const uint8_t*)data.data(), data.size()).ok();
        if (!data.empty() && !ok) err = "not an image";
        std::lock_guard<std::mutex> lock(job->m);
        job->data = std::move(data);
        job->err = err;
        job->ok = ok;
        job->done = true;
    });
    return job;
}

// "Download all covers": one confident VNDB match per game without a cover.
struct BatchJob {
    struct Item { std::string folder, title, data; };
    std::mutex m;
    int total = 0, processed = 0, saved = 0;
    bool done = false;
    std::vector<Item> ready; // downloaded, waiting for the UI thread to save
};

std::shared_ptr<BatchJob> StartBatch(const std::vector<std::pair<std::string, std::string>>& todo, bool hide_explicit) {
    auto job = std::make_shared<BatchJob>();
    job->total = (int)todo.size();
    RunDetached("kk4s-cover-batch", [job, todo, hide_explicit] {
        for (const auto& t : todo) {
            std::vector<net::CoverCandidate> found;
            std::string used, err;
            BatchJob::Item item{t.first, "", ""};
            if (net::SearchCoversForTitle(t.second, hide_explicit, found, used, err) && !found.empty() &&
                net::IsConfidentMatch(used, found[0])) {
                std::string data = Fetch(found[0].image_url, err);
                if (!data.empty() && img::DecodeImageData((const uint8_t*)data.data(), data.size()).ok()) {
                    item.title = found[0].title;
                    item.data = std::move(data);
                }
            }
            std::lock_guard<std::mutex> lock(job->m);
            job->processed++;
            if (!item.data.empty()) job->ready.push_back(std::move(item));
        }
        std::lock_guard<std::mutex> lock(job->m);
        job->done = true;
    });
    return job;
}

bool WriteFile(const std::string& path, const std::string& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (fclose(f) == 0) && ok;
    return ok;
}

// Store a cover as <data>/covers/<folder>.<ext> (+ .txt with the title).
bool SaveCover(const std::string& folder, const std::string& data, const std::string& title) {
    std::string dir = RootDir() + "/covers";
    MakeDir(dir);
    for (const char* ext : {"png", "jpg", "jpeg", "webp"}) remove((dir + "/" + folder + "." + ext).c_str());
    if (!WriteFile(dir + "/" + folder + "." + img::ImageExt(data), data)) return false;
    if (!title.empty()) WriteFile(dir + "/" + folder + ".txt", title + "\n");
    return true;
}

// ---------------------------------------------------------------------------
// UI pieces

enum class Action {
    None, Left, Right, Up, Down, PageLeft, PageRight, Ok, Back, Quit, Search, Settings, EditQuery
};

struct HitBox {
    float x, y, w, h;
    Action action;
    int row; // settings row, or -1
};

struct Hint {
    const char* key;
    std::string label;
    Action action;
};

// Pill: (key) label.
float HintWidth(const View& v, Fonts& f, const Hint& h) { return 8 + 26 + 8 + TextWidth(v, f, h.label, 18) + 14; }

void DrawHints(const View& v, Fonts& f, const std::vector<Hint>& hints, float right, float y, float alpha,
               std::vector<HitBox>& boxes) {
    float total = 0;
    for (const Hint& h : hints) total += HintWidth(v, f, h) + 10;
    float x = right - total + 10;
    for (const Hint& h : hints) {
        float w = HintWidth(v, f, h);
        RoundRect(v, x, y, w, 38, 19, WithAlpha(kSurface, alpha));
        Circle(v, x + 8 + 13, y + 19, 13, WithAlpha(kText, alpha));
        DrawText(v, f, h.key, x + 8 + 13, y + 7, 16, WithAlpha(kBase, alpha), kCenter, 0, true);
        DrawText(v, f, h.label, x + 8 + 26 + 8, y + 7, 18, WithAlpha(kText, alpha));
        boxes.push_back({x, y, w, 38, h.action, -1});
        x += w + 10;
    }
}

// Generated card for a game without cover art.
void DrawGeneratedCard(const View& v, Fonts& f, const Game& g, float x, float y, float w, float h, float rad) {
    SDL_Color top = g.tint;
    SDL_Color bottom = Rgba(g.tint.r / 4, g.tint.g / 4, g.tint.b / 4);
    RoundRect(v, x, y, w, h, rad, bottom);
    RoundRect(v, x, y, w, h * 0.5f, rad, top);
    Gradient(v, x, y + rad, w, h * 0.5f, top, bottom);
    float icon = w * 0.42f;
    if (g.icon) {
        SDL_FRect rc = v.Map(x + w / 2 - icon / 2, y + h * 0.2f, icon, icon);
        SDL_SetTextureAlphaMod(g.icon, 255);
        SDL_RenderCopyF(v.r, g.icon, nullptr, &rc);
    } else {
        DrawText(v, f, FirstCharacter(g.title), x + w / 2, y + h * 0.16f, w * 0.40f, WithAlpha(kText, 0.9f), kCenter, 0,
                 true);
    }
    // title, wrapped onto two lines at a space
    std::string t1 = g.title, t2;
    float size = w * 0.082f;
    if (TextWidth(v, f, t1, size, true) > w - 24) {
        size_t sp = t1.rfind(' ', t1.size() / 2 + 6);
        if (sp != std::string::npos && sp > 0) { t2 = t1.substr(sp + 1); t1 = t1.substr(0, sp); }
    }
    float ty = y + h * 0.70f;
    DrawText(v, f, t1, x + w / 2, ty, size, kText, kCenter, w - 24, true);
    if (!t2.empty()) DrawText(v, f, t2, x + w / 2, ty + size * 1.3f, size, kText, kCenter, w - 24, true);
}

void DrawCard(const View& v, Fonts& f, const Shadow& shadow, const Game& g, float cx, float cy, float w, float h,
              float focus, float glow) {
    float x = cx - w / 2, y = cy - h / 2, rad = 16;
    shadow.Draw(v, x, y + 10, w, h, 26, Rgba(0, 0, 0, 150));
    if (focus > 0.01f) shadow.Draw(v, x, y, w, h, 34, WithAlpha(kAccent, 0.35f * focus * (0.7f + 0.3f * glow)));
    if (g.cover) {
        SDL_Rect src = CoverCrop(g.cover_w, g.cover_h, w, h);
        RoundImage(v, g.cover, &src, x, y, w, h, rad, Rgba(255, 255, 255));
    } else {
        DrawGeneratedCard(v, f, g, x, y, w, h, rad);
    }
    if (focus < 0.99f) RoundRect(v, x, y, w, h, rad, Rgba(8, 10, 16, (Uint8)(110 * (1 - focus))));
    if (focus > 0.01f) RoundFrame(v, x - 5, y - 5, w + 10, h + 10, rad + 5, 3, WithAlpha(kAccent, focus));
}

float ChipWidth(const View& v, Fonts& f, const std::string& text, bool dot) {
    return TextWidth(v, f, text, 16) + 28 + (dot ? 14 : 0);
}

void Chip(const View& v, Fonts& f, float& x, float y, const std::string& text, bool dot) {
    float w = ChipWidth(v, f, text, dot);
    RoundRect(v, x, y, w, 30, 15, kSurface);
    float tx = x + 14;
    if (dot) { Circle(v, tx + 4, y + 15, 4, kAccent); tx += 14; }
    DrawText(v, f, text, tx, y + 5, 16, kText2);
    x += w + 8;
}

std::string FormatDate(long long t) {
    if (t <= 0) return "";
    time_t tt = (time_t)t, now = time(nullptr);
    struct tm a = *localtime(&tt);
    struct tm b = *localtime(&now);
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) return T(i18n::kToday);
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday - 1) return T(i18n::kYesterday);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", a.tm_mday, a.tm_mon + 1, a.tm_year + 1900);
    return buf;
}

// settings rows
enum Row { kRowLanguage, kRowLayout, kRowSort, kRowBackground, kRowExplicit, kRowCovers, kRowCount };
const char* const kLangCodes[] = {"auto", "uk", "en", "ru", "ja"};

struct Input {
    float drag_x0 = 0, drag_y0 = 0, drag_x = 0, drag_y = 0, drag_pos = 0;
    bool dragging = false, moved = false;
    Action held = Action::None;
    Uint32 next_repeat = 0;
    unsigned pad_prev = 0;
};

} // namespace

// ---------------------------------------------------------------------------

std::string RunLauncher(SDL_Renderer* r, const std::vector<std::string>& folders, size_t initial) {
    Settings settings;
    settings.Load();
    i18n::SetLanguage(settings.language);
    Fonts fonts;
    if (!fonts.Init()) Log("launcher: no usable font, text will be missing");
    Shadow shadow;
    shadow.Init(r);

    std::vector<Game> games(folders.size());
    for (size_t i = 0; i < folders.size(); ++i) {
        games[i].folder = folders[i];
        LoadGame(r, games[i]);
    }
    std::string last = ReadLastPlayed();
    size_t sel = std::min(initial, games.empty() ? (size_t)0 : games.size() - 1);

    auto resort = [&] {
        std::string keep = games.empty() ? "" : games[sel].folder;
        std::stable_sort(games.begin(), games.end(), [&](const Game& a, const Game& b) {
            if (settings.sort_recent) {
                auto ia = settings.played.find(a.folder), ib = settings.played.find(b.folder);
                long long ta = ia == settings.played.end() ? 0 : ia->second.last;
                long long tb = ib == settings.played.end() ? 0 : ib->second.last;
                if (ta != tb) return ta > tb;
            }
            return strcasecmp(a.title.c_str(), b.title.c_str()) < 0;
        });
        for (size_t i = 0; i < games.size(); ++i)
            if (games[i].folder == keep) sel = i;
    };
    resort();

    // carousel / grid geometry
    const float card_w = 196, card_h = 276, step = card_w + 44, center_y = 292;
    const int cols = 6;
    const float gw = 164, gh = 230, gstep_x = 194, gstep_y = 300, grid_top = 112;
    const float grid_x0 = 640 - (cols * gstep_x - (gstep_x - gw)) / 2;

    float pos = (float)sel, grid_scroll = 0;
    float bg_fade = 1;
    int bg_prev = -1, bg_cur = (int)sel;
    Input in;
    std::string chosen;
    bool running = true, leaving = false;
    Uint32 start = SDL_GetTicks(), prev = start, leave_at = 0;
    std::vector<HitBox> boxes;

    std::string toast;
    Uint32 toast_until = 0;
    auto show_toast = [&](const std::string& s, Uint32 ms = 2600) { toast = s; toast_until = SDL_GetTicks() + ms; };

    int battery = -1;
    bool charging = false;
    Uint32 battery_at = 0;

    // settings panel
    bool settings_open = false;
    float settings_anim = 0;
    int row = 0;
    std::shared_ptr<BatchJob> batch;

    // cover search panel
    bool searching = false;
    float search_anim = 0;
    std::shared_ptr<SearchJob> search;
    std::shared_ptr<DownloadJob> download;
    std::vector<net::CoverCandidate> found;
    std::vector<SDL_Texture*> thumbs;
    std::vector<SDL_Point> thumb_size;
    size_t pick = 0;
    float pick_pos = 0;
    std::string query, search_msg;
    bool editing = false; // PC: typing the query in place
    std::string edit_text;

    auto clear_results = [&] {
        for (SDL_Texture* t : thumbs) if (t) SDL_DestroyTexture(t);
        thumbs.clear();
        thumb_size.clear();
        found.clear();
    };
    auto run_search = [&](const std::string& text, bool exact) {
        clear_results();
        pick = 0;
        pick_pos = 0;
        search_msg.clear();
        query = exact ? text : net::CleanQuery(text);
        Log("launcher: cover search '%s'%s", text.c_str(), exact ? " (as typed)" : "");
        search = StartSearch(text, exact, settings.hide_explicit);
    };
    auto close_search = [&] {
        searching = false;
        editing = false;
        SDL_StopTextInput();
        search.reset();
        download.reset();
        clear_results();
    };
    auto change_setting = [&](int which, int dir) {
        switch (which) {
            case kRowLanguage: {
                int n = (int)(sizeof(kLangCodes) / sizeof(kLangCodes[0])), cur = 0;
                for (int i = 0; i < n; ++i)
                    if (settings.language == kLangCodes[i]) cur = i;
                settings.language = kLangCodes[(cur + dir + n) % n];
                i18n::SetLanguage(settings.language);
                break;
            }
            case kRowLayout: settings.grid = !settings.grid; break;
            case kRowSort: settings.sort_recent = !settings.sort_recent; resort(); pos = (float)sel; break;
            case kRowBackground: settings.cover_background = !settings.cover_background; break;
            case kRowExplicit: settings.hide_explicit = !settings.hide_explicit; break;
            case kRowCovers: {
                if (batch) break;
                std::vector<std::pair<std::string, std::string>> todo;
                for (const Game& g : games)
                    if (!g.cover) todo.push_back({g.folder, g.title});
                if (todo.empty()) { show_toast(i18n::F(i18n::kCoversDone, "0")); return; }
                Log("launcher: downloading covers for %zu game(s)", todo.size());
                batch = StartBatch(todo, settings.hide_explicit);
                show_toast(i18n::F(i18n::kCoversProgress, "0/" + std::to_string(todo.size())), 60000);
                return;
            }
        }
        settings.Save();
    };

    auto move = [&](int d) {
        if (searching) {
            if (!found.empty() && !download) pick = (size_t)std::max(0, std::min((int)found.size() - 1, (int)pick + d));
            return;
        }
        if (games.empty()) return;
        sel = (size_t)std::max(0, std::min((int)games.size() - 1, (int)sel + d));
    };
    auto start_edit = [&] {
        std::string text = query;
        if (EditText(T(i18n::kEditQueryGuide), text)) {
            if (!text.empty()) run_search(text, true);
        } else if (!EditTextAvailable()) {
            // no system keyboard (PC): type in place
            editing = true;
            edit_text = query;
            SDL_StartTextInput();
        }
    };
    auto act = [&](Action a) {
        if (leaving) return;
        if (settings_open) {
            switch (a) {
                case Action::Up: row = (row + kRowCount - 1) % kRowCount; break;
                case Action::Down: row = (row + 1) % kRowCount; break;
                case Action::Left: change_setting(row, -1); break;
                case Action::Right: case Action::Ok: change_setting(row, 1); break;
                case Action::Back: case Action::Settings: case Action::Quit: settings_open = false; break;
                default: break;
            }
            return;
        }
        if (searching) {
            switch (a) {
                case Action::Left: case Action::Up: move(-1); break;
                case Action::Right: case Action::Down: move(1); break;
                case Action::Ok:
                    if (!found.empty() && !download && search && search->done) {
                        download = StartDownload(found[pick].image_url);
                        search_msg.clear();
                    }
                    break;
                case Action::EditQuery: case Action::Search: if (!download) start_edit(); break;
                case Action::Back: case Action::Quit: close_search(); break;
                default: break;
            }
            return;
        }
        int line = settings.grid ? cols : 1;
        switch (a) {
            case Action::Left: move(-1); break;
            case Action::Right: move(1); break;
            case Action::Up: move(-line); break;
            case Action::Down: move(line); break;
            case Action::PageLeft: move(settings.grid ? -cols * 2 : -5); break;
            case Action::PageRight: move(settings.grid ? cols * 2 : 5); break;
            case Action::Ok:
                if (!games.empty()) {
                    chosen = games[sel].folder;
                    leaving = true;
                    leave_at = SDL_GetTicks();
                }
                break;
            case Action::Search:
                if (!games.empty()) {
                    searching = true;
                    run_search(games[sel].title, false);
                }
                break;
            case Action::Settings: settings_open = true; break;
            case Action::Quit: running = false; break;
            default: break;
        }
    };
    auto pad = [&](unsigned bit, bool down) {
        static int logged = 0;
        if (down && logged < 20) { ++logged; Log("launcher: pad 0x%x", bit); }
        Action a = Action::None;
        switch (bit) {
            case kPadLeft: a = Action::Left; break;
            case kPadRight: a = Action::Right; break;
            case kPadUp: a = Action::Up; break;
            case kPadDown: a = Action::Down; break;
            case kPadL: a = Action::PageLeft; break;
            case kPadR: a = Action::PageRight; break;
            case kPadA: a = Action::Ok; break;
            case kPadB: a = Action::Back; break;
            case kPadX: a = searching ? Action::EditQuery : Action::Settings; break;
            case kPadY: a = Action::Search; break;
            case kPadPlus: a = Action::Quit; break;
        }
        bool dir = a == Action::Left || a == Action::Right || a == Action::Up || a == Action::Down;
        if (!down) {
            if (dir && in.held == a) in.held = Action::None;
            return;
        }
        act(a);
        if (dir) { in.held = a; in.next_repeat = SDL_GetTicks() + 380; }
    };

    while (running) {
        int ow = 1280, oh = 720;
        SDL_GetRendererOutputSize(r, &ow, &oh);
        View v;
        v.r = r;
        v.s = std::min(ow / 1280.f, oh / 720.f);
        v.ox = (ow - 1280 * v.s) / 2;
        v.oy = (oh - 720 * v.s) / 2;

        // Switch buttons straight from libnx (by label); PC uses SDL events below.
        unsigned held;
        if (ReadPad(held)) {
            unsigned changed = held ^ in.pad_prev;
            in.pad_prev = held;
            for (unsigned bit = 1; changed && bit <= kPadDown; bit <<= 1)
                if (changed & bit) pad(bit, (held & bit) != 0);
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (editing) {
                if (e.type == SDL_TEXTINPUT) { edit_text += e.text.text; continue; }
                if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode k = e.key.keysym.sym;
                    if (k == SDLK_BACKSPACE && !edit_text.empty()) {
                        do edit_text.pop_back();
                        while (!edit_text.empty() && ((unsigned char)edit_text.back() & 0xC0) == 0x80);
                    } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                        editing = false;
                        SDL_StopTextInput();
                        if (!edit_text.empty()) run_search(edit_text, true);
                    } else if (k == SDLK_ESCAPE) {
                        editing = false;
                        SDL_StopTextInput();
                    }
                    continue;
                }
            }
            switch (e.type) {
                case SDL_QUIT: running = false; chosen.clear(); break;
                case SDL_CONTROLLERDEVICEADDED:
                    if (SDL_GameControllerOpen(e.cdevice.which))
                        Log("input: controller '%s'", SDL_GameControllerNameForIndex(e.cdevice.which));
                    break;
#ifndef __SWITCH__
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP:
                    pad(PadFromSdlButton(e.cbutton.button), e.type == SDL_CONTROLLERBUTTONDOWN);
                    break;
#endif
                case SDL_KEYDOWN:
                    switch (e.key.keysym.sym) {
                        case SDLK_LEFT: act(Action::Left); break;
                        case SDLK_RIGHT: act(Action::Right); break;
                        case SDLK_UP: act(Action::Up); break;
                        case SDLK_DOWN: act(Action::Down); break;
                        case SDLK_PAGEUP: act(Action::PageLeft); break;
                        case SDLK_PAGEDOWN: act(Action::PageRight); break;
                        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: act(Action::Ok); break;
                        case SDLK_BACKSPACE: act(Action::Back); break;
                        case SDLK_ESCAPE: act(settings_open || searching ? Action::Back : Action::Quit); break;
                        case SDLK_y: case SDLK_F3: act(Action::Search); break;
                        case SDLK_x: case SDLK_F2: act(searching ? Action::EditQuery : Action::Settings); break;
                    }
                    break;
                case SDL_MOUSEWHEEL: {
                    bool vertical = settings.grid && !searching && !settings_open;
                    if (e.wheel.y > 0) act(vertical || settings_open ? Action::Up : Action::Left);
                    else if (e.wheel.y < 0) act(vertical || settings_open ? Action::Down : Action::Right);
                    break;
                }
                case SDL_FINGERDOWN: case SDL_MOUSEBUTTONDOWN: {
                    if (e.type == SDL_MOUSEBUTTONDOWN && (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT)) break;
                    float x = e.type == SDL_FINGERDOWN ? v.LayoutX(e.tfinger.x * ow) : v.LayoutX((float)e.button.x);
                    float y = e.type == SDL_FINGERDOWN ? v.LayoutY(e.tfinger.y * oh) : v.LayoutY((float)e.button.y);
                    in.dragging = true;
                    in.moved = false;
                    in.drag_x0 = in.drag_x = x;
                    in.drag_y0 = in.drag_y = y;
                    in.drag_pos = searching ? pick_pos : settings.grid ? grid_scroll : pos;
                    break;
                }
                case SDL_FINGERMOTION: case SDL_MOUSEMOTION: {
                    if (!in.dragging) break;
                    if (e.type == SDL_MOUSEMOTION && e.motion.which == SDL_TOUCH_MOUSEID) break;
                    in.drag_x = e.type == SDL_FINGERMOTION ? v.LayoutX(e.tfinger.x * ow) : v.LayoutX((float)e.motion.x);
                    in.drag_y = e.type == SDL_FINGERMOTION ? v.LayoutY(e.tfinger.y * oh) : v.LayoutY((float)e.motion.y);
                    if (std::fabs(in.drag_x - in.drag_x0) > 12 || std::fabs(in.drag_y - in.drag_y0) > 12) in.moved = true;
                    break;
                }
                case SDL_FINGERUP: case SDL_MOUSEBUTTONUP: {
                    if (!in.dragging) break;
                    if (e.type == SDL_MOUSEBUTTONUP && e.button.which == SDL_TOUCH_MOUSEID) break;
                    in.dragging = false;
                    float x = in.drag_x, y = in.drag_y;
                    if (!in.moved) {
                        // buttons first (hints, gear, settings rows); last drawn is on top
                        bool hit = false;
                        for (auto it = boxes.rbegin(); it != boxes.rend() && !hit; ++it) {
                            if (x < it->x || x >= it->x + it->w || y < it->y || y >= it->y + it->h) continue;
                            hit = true;
                            if (it->row >= 0) {
                                if (row == it->row || it->row == kRowCovers || it->row == kRowBackground ||
                                    it->row == kRowExplicit)
                                    change_setting(it->row, 1);
                                row = it->row;
                            } else {
                                act(it->action);
                            }
                        }
                        if (hit) break;
                    }
                    if (settings_open) {
                        if (!in.moved && x < 1280 - 560) settings_open = false;
                        break;
                    }
                    if (searching) {
                        const float tstep = 196;
                        if (!in.moved && (x < 120 || x > 1160 || y < 80 || y > 640)) { close_search(); break; }
                        if (found.empty() || download) break;
                        if (in.moved) {
                            float p = in.drag_pos - (x - in.drag_x0) / tstep;
                            pick = (size_t)std::max(0, std::min((int)found.size() - 1, (int)std::lround(p)));
                        } else if (std::fabs(y - 380) < 130) {
                            int h = (int)std::lround(pick_pos + (x - 640) / tstep);
                            if (h >= 0 && h < (int)found.size()) {
                                if ((size_t)h == pick) act(Action::Ok);
                                else pick = (size_t)h;
                            }
                        }
                        break;
                    }
                    if (games.empty()) break;
                    if (settings.grid) {
                        if (in.moved) break; // that was a scroll
                        int c = (int)std::floor((x - grid_x0 + (gstep_x - gw) / 2) / gstep_x);
                        int rr = (int)std::floor((y - grid_top + grid_scroll + (gstep_y - gh) / 2) / gstep_y);
                        int i = rr * cols + c;
                        if (c >= 0 && c < cols && rr >= 0 && i < (int)games.size()) {
                            if ((size_t)i == sel) act(Action::Ok);
                            else sel = (size_t)i;
                        }
                        break;
                    }
                    if (in.moved) {
                        float p = in.drag_pos - (x - in.drag_x0) / step;
                        sel = (size_t)std::max(0, std::min((int)games.size() - 1, (int)std::lround(p)));
                        pos = std::max(0.f, std::min((float)games.size() - 1, p));
                    } else if (std::fabs(y - center_y) < card_h * 0.65f) {
                        int h = (int)std::lround(pos + (x - 640) / step);
                        if (h >= 0 && h < (int)games.size()) {
                            if ((size_t)h == sel) act(Action::Ok);
                            else sel = (size_t)h;
                        }
                    }
                    break;
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = std::min(0.1f, (now - prev) / 1000.f);
        prev = now;
        if (in.held != Action::None && now >= in.next_repeat) { act(in.held); in.next_repeat = now + 110; }
        if (leaving && now - leave_at > 260) running = false;

        // animation state
        settings_anim = Approach(settings_anim, settings_open ? 1.f : 0.f, dt, 14);
        search_anim = Approach(search_anim, searching ? 1.f : 0.f, dt, 14);
        bool drag_home = in.dragging && in.moved && !searching && !settings_open;
        if (drag_home && !settings.grid)
            pos = std::max(-0.4f, std::min((float)games.size() - 0.6f, in.drag_pos - (in.drag_x - in.drag_x0) / step));
        else
            pos = Approach(pos, (float)sel, dt);
        int sel_row = (int)sel / cols, rows = ((int)games.size() + cols - 1) / cols;
        const float grid_view = 640 - grid_top;
        float max_scroll = std::max(0.f, rows * gstep_y - grid_view);
        if (drag_home && settings.grid) {
            grid_scroll = std::max(0.f, std::min(max_scroll, in.drag_pos - (in.drag_y - in.drag_y0)));
        } else if (settings.grid) {
            float top = sel_row * gstep_y, target = grid_scroll;
            if (top < grid_scroll) target = top;
            if (top + gh + 40 > grid_scroll + grid_view) target = top + gh + 40 - grid_view;
            grid_scroll = Approach(grid_scroll, std::max(0.f, std::min(max_scroll, target)), dt);
        }
        if (searching && in.dragging && in.moved && !found.empty())
            pick_pos = std::max(-0.4f, std::min((float)found.size() - 0.6f, in.drag_pos - (in.drag_x - in.drag_x0) / 196));
        else
            pick_pos = Approach(pick_pos, (float)pick, dt);
        if ((int)sel != bg_cur) { bg_prev = bg_cur; bg_cur = (int)sel; bg_fade = 0; }
        bg_fade = Clamp01(bg_fade + dt * 3.5f);
        float glow = 0.5f + 0.5f * std::sin(now / 300.f);
        if (now >= battery_at) { battery = BatteryPercent(&charging); battery_at = now + 30000; }

        // background jobs
        if (searching && search && found.empty() && search_msg.empty()) {
            std::lock_guard<std::mutex> lock(search->m);
            if (search->done) {
                found = search->found;
                query = search->used_query;
                for (const Image& im : search->thumbs) {
                    thumbs.push_back(MakeTexture(r, im));
                    thumb_size.push_back({im.w, im.h});
                }
                if (!search->ok) search_msg = i18n::F(i18n::kNoConnection, search->err);
                else if (found.empty()) search_msg = T(i18n::kNothingFound);
                Log("launcher: cover search '%s': %zu result(s)%s%s", query.c_str(), found.size(),
                    search->ok ? "" : ", error: ", search->err.c_str());
            }
        }
        if (searching && download) {
            bool done, ok;
            std::string data, err;
            {
                std::lock_guard<std::mutex> lock(download->m);
                done = download->done;
                ok = download->ok;
                if (done) { data.swap(download->data); err = download->err; }
            }
            if (done) {
                download.reset();
                Game& g = games[sel];
                if (ok && SaveCover(g.folder, data, found[pick].title)) {
                    Log("launcher: %s: downloaded cover %s", g.folder.c_str(), found[pick].image_url.c_str());
                    LoadGame(r, g);
                    close_search();
                    bg_fade = 0;
                    bg_prev = -1;
                    show_toast(T(i18n::kCoverSaved));
                } else {
                    search_msg = i18n::F(i18n::kDownloadFailed, ok ? T(i18n::kCannotWriteSd) : err);
                }
            }
        }
        if (batch) {
            std::vector<BatchJob::Item> ready;
            int processed, total;
            bool done;
            {
                std::lock_guard<std::mutex> lock(batch->m);
                ready.swap(batch->ready);
                processed = batch->processed;
                total = batch->total;
                done = batch->done;
            }
            for (BatchJob::Item& it : ready) {
                if (!SaveCover(it.folder, it.data, it.title)) continue;
                batch->saved++;
                for (Game& g : games)
                    if (g.folder == it.folder) LoadGame(r, g);
                Log("launcher: %s: cover saved (download all)", it.folder.c_str());
            }
            if (done) {
                show_toast(i18n::F(i18n::kCoversDone, std::to_string(batch->saved)));
                batch.reset();
                resort();
                bg_fade = 0;
                bg_prev = -1;
            } else {
                show_toast(i18n::F(i18n::kCoversProgress, std::to_string(processed) + "/" + std::to_string(total)), 60000);
            }
        }

        boxes.clear();

        // ---- background
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        FillRect(v, 0, 0, 1280, 720, kBase);
        auto draw_bg = [&](int i, float alpha) {
            if (i < 0 || i >= (int)games.size() || alpha <= 0) return;
            const Game& g = games[i];
            if (settings.cover_background && g.blur) {
                int bw, bh;
                SDL_QueryTexture(g.blur, nullptr, nullptr, &bw, &bh);
                SDL_Rect src = CoverCrop(bw, bh, 1280, 720);
                SDL_FRect rc = v.Map(-40, -40, 1360, 800);
                SDL_SetTextureColorMod(g.blur, 150, 150, 160);
                SDL_SetTextureAlphaMod(g.blur, (Uint8)(255 * alpha));
                SDL_RenderCopyF(r, g.blur, &src, &rc);
            } else {
                Gradient(v, 0, 0, 1280, 720, WithAlpha(Rgba(g.tint.r, g.tint.g, g.tint.b, 120), alpha), Rgba(0, 0, 0, 0));
            }
        };
        if (!games.empty()) {
            if (bg_fade < 1) draw_bg(bg_prev, 1);
            draw_bg(bg_cur, Ease(bg_fade));
        }
        Gradient(v, 0, 0, 1280, 180, Rgba(8, 10, 16, 200), Rgba(8, 10, 16, 0));
        Gradient(v, 0, 360, 1280, 360, Rgba(8, 10, 16, 0), Rgba(8, 10, 16, 235));
        HGradient(v, 0, 0, 220, 720, Rgba(8, 10, 16, 110), Rgba(8, 10, 16, 0));
        HGradient(v, 1060, 0, 220, 720, Rgba(8, 10, 16, 0), Rgba(8, 10, 16, 110));

        // ---- top bar
        RoundRect(v, 48, 30, 34, 34, 10, kAccent);
        DrawText(v, fonts, "k", 65, 32, 24, kBase, kCenter, 0, true);
        float lx = 94 + DrawText(v, fonts, "kk4s", 94, 32, 24, kText, kLeft, 0, true);
        DrawText(v, fonts, T(i18n::kAppSubtitle), lx + 12, 38, 17, kText2);
        float rx = 1232;
        RoundRect(v, rx - 40, 27, 40, 40, 20, settings_open ? kSurfaceHi : kSurface);
        IconGear(v, rx - 20, 47, 24, kText);
        boxes.push_back({rx - 44, 22, 48, 50, Action::Settings, -1});
        rx -= 56;
        if (battery >= 0) {
            char b[16];
            snprintf(b, sizeof(b), "%d%%", battery);
            float bw = TextWidth(v, fonts, b, 18);
            DrawText(v, fonts, b, rx, 37, 18, kText2, kRight);
            IconBattery(v, rx - bw - 38, 39, 28, 15, battery, charging, kText2);
            rx -= bw + 52;
        }
        {
            time_t t = time(nullptr);
            struct tm* lt = localtime(&t);
            char clock[16];
            snprintf(clock, sizeof(clock), "%02d:%02d", lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0);
            DrawText(v, fonts, clock, rx, 34, 22, kText, kRight, 0, true);
        }

        // ---- games
        if (games.empty()) {
            RoundRect(v, 340, 220, 600, 240, 24, kSurface);
            IconSearch(v, 640, 280, 54, kText3);
            DrawText(v, fonts, T(i18n::kNoGames), 640, 322, 32, kText, kCenter, 0, true);
            DrawText(v, fonts, T(i18n::kNoGamesHint), 640, 372, 18, kText2, kCenter, 560);
            DrawText(v, fonts, GamesDir() + "/<Game>/", 640, 402, 18, kAccent, kCenter, 560);
        } else if (!settings.grid) {
            std::vector<int> order;
            for (int i = 0; i < (int)games.size(); ++i)
                if (std::fabs(i - pos) < 6) order.push_back(i);
            std::sort(order.begin(), order.end(), [&](int a, int b) { return std::fabs(a - pos) > std::fabs(b - pos); });
            for (int i : order) {
                float focus = std::max(0.f, 1 - std::fabs(i - pos));
                float k = 1 + 0.2f * focus;
                DrawCard(v, fonts, shadow, games[i], 640 + (i - pos) * step, center_y, card_w * k, card_h * k, focus, glow);
            }
            const Game& g = games[sel];
            DrawText(v, fonts, g.title, 640, 486, 34, kText, kCenter, 1080, true);
            // chips: last played, launches, folder
            std::vector<std::string> chips;
            auto it = settings.played.find(g.folder);
            bool has_play = it != settings.played.end() && it->second.last > 0;
            chips.push_back(has_play ? i18n::F(i18n::kLastPlayed, FormatDate(it->second.last)) : T(i18n::kNeverPlayed));
            if (has_play) chips.push_back(i18n::Launches(it->second.count));
            if (g.title != g.folder) chips.push_back(g.folder);
            bool dot = g.folder == last;
            float total = 0;
            for (size_t c = 0; c < chips.size(); ++c) total += ChipWidth(v, fonts, chips[c], c == 0 && dot) + 8;
            float cx = 640 - (total - 8) / 2;
            for (size_t c = 0; c < chips.size(); ++c) Chip(v, fonts, cx, 540, chips[c], c == 0 && dot);
        } else {
            SDL_FRect clipf = v.Map(0, 92, 1280, 548);
            SDL_Rect clip{(int)clipf.x, (int)clipf.y, (int)clipf.w, (int)clipf.h};
            SDL_RenderSetClipRect(r, &clip);
            for (int i = 0; i < (int)games.size(); ++i) {
                float x = grid_x0 + (i % cols) * gstep_x, y = grid_top + (i / cols) * gstep_y - grid_scroll;
                if (y > 660 || y + gh + 40 < 80) continue;
                bool focused = (size_t)i == sel;
                float k = focused ? 1.06f : 1;
                DrawCard(v, fonts, shadow, games[i], x + gw / 2, y + gh / 2, gw * k, gh * k, focused ? 1.f : 0.25f, glow);
                DrawText(v, fonts, games[i].title, x + gw / 2, y + gh + 14, 16, focused ? kText : kText2, kCenter, gw + 20,
                         focused);
            }
            SDL_RenderSetClipRect(r, nullptr);
        }

        // ---- bottom bar
        if (!searching) {
            std::vector<Hint> hints;
            if (!games.empty()) {
                hints.push_back({"Y", T(i18n::kCover), Action::Search});
                hints.push_back({"A", T(i18n::kPlay), Action::Ok});
            }
            hints.push_back({"X", T(i18n::kSettings), Action::Settings});
            hints.push_back({"+", T(i18n::kQuit), Action::Quit});
            DrawHints(v, fonts, hints, 1232, 650, 1, boxes);
            if (!games.empty()) DrawText(v, fonts, i18n::Games((int)games.size()), 48, 658, 18, kText3);
        }

        // ---- settings sheet (slides in from the right)
        if (settings_anim > 0.005f) {
            float a = Ease(settings_anim);
            FillRect(v, 0, 0, 1280, 720, Rgba(4, 6, 10, (Uint8)(150 * a)));
            boxes.clear(); // the home screen behind is not clickable
            const float pw = 560, px = 1280 - pw * a;
            shadow.Draw(v, px, 0, pw + 40, 720, 40, Rgba(0, 0, 0, (Uint8)(160 * a)));
            FillRect(v, px, 0, pw, 720, kPanel);
            IconGear(v, px + 56, 62, 28, kAccent);
            DrawText(v, fonts, T(i18n::kSettings), px + 84, 44, 30, kText, kLeft, 0, true);
            float ry = 116;
            for (int i = 0; i < kRowCount; ++i) {
                bool focused = i == row;
                float rh = i == kRowCovers ? 76 : 62;
                if (focused) {
                    RoundRect(v, px + 24, ry, pw - 48, rh, 14, kSurfaceHi);
                    RoundRect(v, px + 24, ry + 14, 4, rh - 28, 2, kAccent);
                }
                std::string label, value;
                int toggle = -1; // -1 = text value, 0/1 = switch
                switch (i) {
                    case kRowLanguage:
                        label = T(i18n::kLanguage);
                        value = settings.language == "auto"
                                    ? std::string(T(i18n::kLanguageAuto)) + " · " + i18n::LanguageName(i18n::Language())
                                    : i18n::LanguageName(settings.language);
                        break;
                    case kRowLayout: label = T(i18n::kView); value = T(settings.grid ? i18n::kViewGrid : i18n::kViewCarousel); break;
                    case kRowSort: label = T(i18n::kSort); value = T(settings.sort_recent ? i18n::kSortRecent : i18n::kSortName); break;
                    case kRowBackground: label = T(i18n::kCoverBackground); toggle = settings.cover_background; break;
                    case kRowExplicit: label = T(i18n::kHideExplicit); toggle = settings.hide_explicit; break;
                    case kRowCovers: label = T(i18n::kDownloadAllCovers); break;
                }
                float ty = ry + (rh - 24) / 2 - (i == kRowCovers ? 9 : 0);
                DrawText(v, fonts, label, px + 48, ty, 20, kText, kLeft, 300);
                if (i == kRowCovers) {
                    DrawText(v, fonts, T(i18n::kDownloadAllHint), px + 48, ty + 28, 15, kText3, kLeft, 380);
                    if (batch) {
                        std::string dots(1 + (now / 400) % 3, '.');
                        DrawText(v, fonts, dots, px + pw - 52, ty + 6, 22, kAccent, kRight);
                    } else {
                        IconSearch(v, px + pw - 64, ry + rh / 2, 26, kText2);
                    }
                } else if (toggle >= 0) {
                    float sx = px + pw - 48 - 54, sy = ry + rh / 2 - 15;
                    RoundRect(v, sx, sy, 54, 30, 15, toggle ? kAccent : kSurfaceHi);
                    Circle(v, toggle ? sx + 54 - 15 : sx + 15, sy + 15, 11, kText);
                } else {
                    float vw = TextWidth(v, fonts, value, 18);
                    float vx = px + pw - 48;
                    DrawText(v, fonts, value, vx - (focused ? 22 : 0), ty + 1, 18, focused ? kText : kText2, kRight);
                    if (focused) {
                        DrawText(v, fonts, "<", vx - vw - 44, ty + 1, 18, kAccent, kRight);
                        DrawText(v, fonts, ">", vx, ty + 1, 18, kAccent, kRight);
                    }
                }
                if (settings_open) boxes.push_back({px + 24, ry, pw - 48, rh, Action::None, i});
                ry += rh + 8;
            }
            // about
            float ay = 560;
            FillRect(v, px + 40, ay - 16, pw - 80, 1, kSurfaceHi);
            DrawText(v, fonts, T(i18n::kAboutText), px + 48, ay, 15, kText2, kLeft, pw - 96);
            DrawText(v, fonts, std::string(T(i18n::kVersion)) + ": " KK4S_VERSION " (" KK4S_GIT_HASH ")", px + 48, ay + 24,
                     15, kText3, kLeft, pw - 96);
            DrawText(v, fonts, std::string(T(i18n::kSource)) + ": vndb.org", px + 48, ay + 46, 15, kText3, kLeft, pw - 96);
            std::vector<Hint> hints = {{"A", T(i18n::kChange), Action::Ok}, {"B", T(i18n::kBack), Action::Back}};
            std::vector<HitBox> panel_boxes;
            DrawHints(v, fonts, hints, px + pw - 32, 650, a, panel_boxes);
            if (settings_open) boxes.insert(boxes.end(), panel_boxes.begin(), panel_boxes.end());
        }

        // ---- cover search (centered modal)
        if (search_anim > 0.005f && !games.empty()) {
            float a = Ease(search_anim);
            FillRect(v, 0, 0, 1280, 720, Rgba(4, 6, 10, (Uint8)(170 * a)));
            boxes.clear();
            float k = 0.96f + 0.04f * a;
            float pw = 1040 * k, ph = 560 * k, px = 640 - pw / 2, py = 360 - ph / 2;
            shadow.Draw(v, px, py + 12, pw, ph, 40, Rgba(0, 0, 0, (Uint8)(180 * a)));
            RoundRect(v, px, py, pw, ph, 26, WithAlpha(kPanel, a));
            IconSearch(v, px + 52, py + 50, 30, WithAlpha(kAccent, a));
            DrawText(v, fonts, T(i18n::kCoverFromInternet), px + 80, py + 32, 28, WithAlpha(kText, a), kLeft, 600, true);
            DrawText(v, fonts, "vndb.org", px + pw - 40, py + 40, 16, WithAlpha(kText3, a), kRight);
            // query field
            float fx = px + 40, fy = py + 84, fw = pw - 80;
            RoundRect(v, fx, fy, fw, 44, 22, WithAlpha(editing ? kSurfaceHi : kSurface, a));
            if (editing) RoundFrame(v, fx, fy, fw, 44, 22, 2, WithAlpha(kAccent, a));
            std::string shown = editing ? edit_text + ((now / 500) % 2 ? "|" : " ") : query;
            DrawText(v, fonts, shown, fx + 22, fy + 10, 19, WithAlpha(kText, a), kLeft, fw - 230);
            if (!editing) {
                std::vector<HitBox> fb;
                DrawHints(v, fonts, {Hint{"X", T(i18n::kEditQuery), Action::EditQuery}}, fx + fw - 4, fy + 3, a, fb);
                if (searching) boxes.insert(boxes.end(), fb.begin(), fb.end());
            }
            bool waiting = search && found.empty() && search_msg.empty();
            if (waiting) {
                for (int d = 0; d < 3; ++d) {
                    float wave = 0.5f + 0.5f * std::sin(now / 180.f - d * 0.9f);
                    Circle(v, 616 + d * 24, py + ph * 0.5f + 10, 6, WithAlpha(kAccent, a * (0.3f + 0.7f * wave)));
                }
                DrawText(v, fonts, T(i18n::kSearching), 640, py + ph * 0.5f + 34, 20, WithAlpha(kText2, a), kCenter);
            } else if (!found.empty()) {
                const float tw = 166, th = 236, tstep = 196, ty = py + 300 * k;
                SDL_FRect clipf = v.Map(px + 2, py, pw - 4, ph);
                SDL_Rect clip{(int)clipf.x, (int)clipf.y, (int)clipf.w, (int)clipf.h};
                SDL_RenderSetClipRect(r, &clip);
                for (int i = 0; i < (int)found.size(); ++i) {
                    float cx = 640 + (i - pick_pos) * tstep;
                    if (cx < px - tw || cx > px + pw + tw) continue;
                    float focus = std::max(0.f, 1 - std::fabs(i - pick_pos));
                    float s2 = 1 + 0.12f * focus, w = tw * s2, h = th * s2, x = cx - w / 2, y = ty - h / 2;
                    shadow.Draw(v, x, y + 8, w, h, 20, Rgba(0, 0, 0, (Uint8)(130 * a)));
                    if (thumbs[i]) {
                        SDL_Rect src = CoverCrop(thumb_size[i].x, thumb_size[i].y, w, h);
                        RoundImage(v, thumbs[i], &src, x, y, w, h, 14, Rgba(255, 255, 255, (Uint8)(255 * a)));
                    } else {
                        RoundRect(v, x, y, w, h, 14, WithAlpha(kSurfaceHi, a));
                    }
                    if (focus < 0.99f) RoundRect(v, x, y, w, h, 14, Rgba(8, 10, 16, (Uint8)(110 * (1 - focus) * a)));
                    if ((size_t)i == pick) RoundFrame(v, x - 5, y - 5, w + 10, h + 10, 19, 3, WithAlpha(kAccent, a));
                }
                SDL_RenderSetClipRect(r, nullptr);
                const net::CoverCandidate& c = found[pick];
                DrawText(v, fonts, c.title, 640, py + ph - 128, 22, WithAlpha(kText, a), kCenter, pw - 80, true);
                std::string info = c.alt_title;
                if (c.released.size() >= 4 && c.released != "TBA")
                    info += (info.empty() ? "" : "  ·  ") + c.released.substr(0, 4);
                info += (info.empty() ? "" : "  ·  ") + std::to_string(pick + 1) + "/" + std::to_string(found.size());
                DrawText(v, fonts, info, 640, py + ph - 96, 16, WithAlpha(kText2, a), kCenter, pw - 80);
                if (download) {
                    std::string dots(1 + (now / 400) % 3, '.');
                    DrawText(v, fonts, std::string(T(i18n::kDownloading)) + dots, px + 40, py + ph - 52, 18,
                             WithAlpha(kAccent, a));
                }
            }
            if (!search_msg.empty())
                DrawText(v, fonts, search_msg, 640, found.empty() ? py + ph * 0.5f : py + ph - 70, 19, WithAlpha(kWarn, a),
                         kCenter, pw - 80);
            std::vector<Hint> hints;
            if (!found.empty()) hints.push_back({"A", T(i18n::kSelect), Action::Ok});
            hints.push_back({"B", T(i18n::kBack), Action::Back});
            std::vector<HitBox> pb;
            DrawHints(v, fonts, hints, px + pw - 32, py + ph - 58, a, pb);
            if (searching) boxes.insert(boxes.end(), pb.begin(), pb.end());
        }

        // ---- toast
        if (!toast.empty() && now < toast_until + 300) {
            float ta = now < toast_until ? 1 : 1 - (now - toast_until) / 300.f;
            float tw = TextWidth(v, fonts, toast, 18) + 70;
            float tx = 640 - tw / 2, ty = 588;
            shadow.Draw(v, tx, ty + 6, tw, 44, 20, Rgba(0, 0, 0, (Uint8)(120 * ta)));
            RoundRect(v, tx, ty, tw, 44, 22, Rgba(34, 40, 58, (Uint8)(245 * ta)));
            IconCheck(v, tx + 26, ty + 22, 20, WithAlpha(kAccent, ta));
            DrawText(v, fonts, toast, tx + 46, ty + 11, 18, WithAlpha(kText, ta));
        }

        // ---- fade in at start, fade out when a game starts
        float fade = 1 - Ease((now - start) / 350.f);
        if (leaving) fade = Clamp01((now - leave_at) / 250.f);
        if (fade > 0) FillRect(v, 0, 0, 1280, 720, Rgba(0, 0, 0, (Uint8)(255 * fade)));

        // Debugging aids (PC; screen capture does not work everywhere, e.g. KDE Wayland):
        //   KK4S_LAUNCHER_SHOT=<file.bmp>  save a frame and quit
        //   KK4S_LAUNCHER_SEARCH=1 | pick  open the cover search first (pick: take the first result)
        //   KK4S_LAUNCHER_SETTINGS=1       open the settings first
        static int frames = 0;
        ++frames;
        const char* dbg_search = getenv("KK4S_LAUNCHER_SEARCH");
        if (frames == 2 && dbg_search) act(Action::Search);
        if (frames == 2 && getenv("KK4S_LAUNCHER_SETTINGS")) act(Action::Settings);
        if (frames == 600 && dbg_search && strcmp(dbg_search, "pick") == 0) act(Action::Ok);
        if (const char* shot = getenv("KK4S_LAUNCHER_SHOT")) {
            if (frames == (dbg_search ? 900 : 60)) {
                SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
                if (surf && SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0)
                    SDL_SaveBMP(surf, shot);
                if (surf) SDL_FreeSurface(surf);
                running = false;
                chosen.clear();
            }
        }
        SDL_RenderPresent(r);
    }

    SDL_StopTextInput();
    clear_results();
    for (Game& g : games) FreeGame(g);
    if (!chosen.empty()) {
        settings.RecordLaunch(chosen);
        settings.Save();
    }
    // leave the renderer clean for the engine
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_RenderPresent(r);
    return chosen;
}

} // namespace detail
} // namespace kk4s
