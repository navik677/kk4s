// kk4s in-game menu (see kk4s_overlay.h).
#include "kk4s_overlay.h"
#include "kk4s_i18n.h"
#include "kk4s_platform.h"
#include "kk4s_settings.h"
#include "kk4s_ui.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace kk4s {
namespace overlay {

namespace {

using namespace ui;
using i18n::T;

const SDL_Color kPanel = Rgba(20, 24, 36, 248);
const SDL_Color kSurfaceHi = Rgba(255, 255, 255, 34);
const SDL_Color kAccent = Rgba(124, 156, 255);
const SDL_Color kText = Rgba(244, 246, 251);
const SDL_Color kText2 = Rgba(163, 171, 194);
const SDL_Color kText3 = Rgba(110, 118, 144);
const SDL_Color kWarn = Rgba(255, 160, 140);

enum RowId { kResume, kQuickSave, kQuickLoad, kVolume, kControls, kToLauncher, kQuit, kPadFirst = 100, kReset = 200 };

const struct { unsigned bit; const char* name; } kPads[] = {
    {kPadA, "A"}, {kPadB, "B"}, {kPadX, "X"}, {kPadY, "Y"}, {kPadL, "L"},
    {kPadR, "R"}, {kPadZL, "ZL"}, {kPadZR, "ZR"}, {kPadPlus, "+"}, {kPadMinus, "−"},
};
const int kPadCount = (int)(sizeof(kPads) / sizeof(kPads[0]));

struct Row {
    int id;
    std::string label, hint, value;
    bool choice = false, danger = false, disabled = false, submenu = false;
};

struct State {
    bool open = false, controls = false, quick_exists = false;
    float anim = 0;
    int row = 0;
    int confirm = -1;
    Uint32 confirm_until = 0, toast_until = 0, prev = 0, repeat_at = 0;
    int held_dir = 0;
    std::string title, toast;
    Request request = Request::None;
    std::unique_ptr<Fonts> fonts;
    std::unique_ptr<Shadow> shadow;
    struct Box { float x, y, w, h; int row; };
    std::vector<Box> boxes;
} g;

const char* ActionName(PadAction a) {
    static const i18n::Str names[] = {i18n::kActNone, i18n::kActEnter, i18n::kActRightClick, i18n::kActCtrl,
                                      i18n::kActSpace, i18n::kActPageUp, i18n::kActPageDown, i18n::kActShift,
                                      i18n::kActEsc, i18n::kActTab, i18n::kActOverlay};
    int i = (int)a;
    return i >= 0 && i < (int)PadAction::Count ? T(names[i]) : "";
}

std::vector<Row> Rows() {
    std::vector<Row> rows;
    Settings& s = LiveSettings();
    if (g.controls) {
        for (int i = 0; i < kPadCount; ++i) {
            Row r{kPadFirst + i, kPads[i].name};
            r.value = ActionName(s.PadFor(kPads[i].bit));
            r.choice = true;
            rows.push_back(r);
        }
        rows.push_back(Row{kReset, T(i18n::kResetControls)});
        return rows;
    }
    rows.push_back(Row{kResume, T(i18n::kResume)});
    rows.push_back(Row{kQuickSave, T(i18n::kQuickSave), T(i18n::kQuickSaveHint)});
    Row load{kQuickLoad, T(i18n::kQuickLoad)};
    load.disabled = !g.quick_exists;
    if (!g.quick_exists) load.hint = T(i18n::kNoQuickSave);
    rows.push_back(load);
    Row vol{kVolume, T(i18n::kVolume)};
    vol.value = std::to_string(s.volume) + "%";
    vol.choice = true;
    rows.push_back(vol);
    Row ctl{kControls, T(i18n::kControls), T(i18n::kControlsHint)};
    ctl.submenu = true;
    rows.push_back(ctl);
    Row launcher{kToLauncher, T(i18n::kExitToLauncher)};
    launcher.danger = true;
    rows.push_back(launcher);
    Row quit{kQuit, T(i18n::kExitApp)};
    quit.danger = true;
    rows.push_back(quit);
    return rows;
}

void Apply(const Row& r, int dir) {
    if (r.disabled) return;
    if (r.danger) {
        if (g.confirm != r.id || SDL_GetTicks() > g.confirm_until) {
            g.confirm = r.id;
            g.confirm_until = SDL_GetTicks() + 3000;
            Toast(T(i18n::kPressAgain));
            return;
        }
        g.confirm = -1;
    }
    Settings& s = LiveSettings();
    int d = dir == 0 ? 1 : dir;
    switch (r.id) {
        case kResume: Close(); break;
        case kQuickSave: g.request = Request::QuickSave; break;
        case kQuickLoad: g.request = Request::QuickLoad; break;
        case kVolume:
            if (dir == 0) break;
            s.volume = std::max(0, std::min(100, s.volume + d * 10));
            s.Save();
            g.request = Request::Volume;
            break;
        case kControls: g.controls = true; g.row = 0; break;
        case kToLauncher: g.request = Request::ToLauncher; break;
        case kQuit: g.request = Request::Quit; break;
        case kReset: s.pad.clear(); s.Save(); break;
        default:
            if (r.id >= kPadFirst && r.id < kPadFirst + kPadCount) {
                unsigned bit = kPads[r.id - kPadFirst].bit;
                int n = (int)PadAction::Count;
                s.pad[bit] = (PadAction)(((int)s.PadFor(bit) + d + n) % n);
                s.Save();
            }
    }
}

void Back() {
    if (g.controls) {
        g.controls = false;
        g.row = kControls;
    } else {
        Close();
    }
}

void Move(int dir) {
    std::vector<Row> rows = Rows();
    int n = (int)rows.size();
    if (n) g.row = (g.row + dir + n) % n;
}

void Side(int dir) {
    std::vector<Row> rows = Rows();
    if (g.row < (int)rows.size() && rows[g.row].choice) Apply(rows[g.row], dir);
}

void Activate() {
    std::vector<Row> rows = Rows();
    if (g.row < (int)rows.size()) Apply(rows[g.row], 0);
}

} // namespace

bool IsOpen() { return g.open; }
bool Visible() { return g.open || g.anim > 0.01f; }

void Open(const std::string& game_title, bool quick_save_exists) {
    g.open = true;
    g.controls = false;
    g.row = 0;
    g.confirm = -1;
    g.held_dir = 0;
    g.title = game_title;
    g.quick_exists = quick_save_exists;
    i18n::SetLanguage(LiveSettings().language);
    Log("overlay: opened");
}

void Close() {
    g.open = false;
    g.held_dir = 0;
}

void SetQuickSaveExists(bool exists) { g.quick_exists = exists; }

void Toast(const std::string& text) {
    g.toast = text;
    g.toast_until = SDL_GetTicks() + 2200;
}

Request TakeRequest() {
    Request r = g.request;
    g.request = Request::None;
    return r;
}

void Pad(unsigned bit, bool down) {
    if (!g.open) return;
    if (!down) {
        if (bit == kPadUp || bit == kPadDown) g.held_dir = 0;
        return;
    }
    switch (bit) {
        case kPadUp: Move(-1); g.held_dir = -1; g.repeat_at = SDL_GetTicks() + 380; break;
        case kPadDown: Move(1); g.held_dir = 1; g.repeat_at = SDL_GetTicks() + 380; break;
        case kPadLeft: Side(-1); break;
        case kPadRight: Side(1); break;
        case kPadA: Activate(); break;
        case kPadB: Back(); break;
        default:
            // the button that opens the menu also closes it
            if (LiveSettings().PadFor(bit) == PadAction::Overlay) Close();
            break;
    }
}

bool Event(const SDL_Event& e, int ow, int oh) {
    if (!g.open) return false;
    switch (e.type) {
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
                case SDLK_UP: Move(-1); break;
                case SDLK_DOWN: Move(1); break;
                case SDLK_LEFT: Side(-1); break;
                case SDLK_RIGHT: Side(1); break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: Activate(); break;
                case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_F1: Back(); break;
            }
            return true;
        case SDL_KEYUP: case SDL_TEXTINPUT: case SDL_MOUSEMOTION: case SDL_FINGERMOTION: case SDL_FINGERDOWN:
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEWHEEL:
            return true;
        case SDL_FINGERUP: case SDL_MOUSEBUTTONUP: {
            float s = std::min(ow / 1280.f, oh / 720.f), ox = (ow - 1280 * s) / 2, oy = (oh - 720 * s) / 2;
            float px = e.type == SDL_FINGERUP ? e.tfinger.x * ow : (float)e.button.x;
            float py = e.type == SDL_FINGERUP ? e.tfinger.y * oh : (float)e.button.y;
            float x = (px - ox) / s, y = (py - oy) / s;
            for (const State::Box& b : g.boxes) {
                if (x < b.x || x >= b.x + b.w || y < b.y || y >= b.y + b.h) continue;
                std::vector<Row> rows = Rows();
                if (b.row == -1) { Back(); return true; }
                if (b.row >= (int)rows.size()) return true;
                bool again = g.row == b.row;
                g.row = b.row;
                if (!rows[b.row].choice || again) Apply(rows[b.row], rows[b.row].choice ? 1 : 0);
                return true;
            }
            if (x < 330 || x > 950) Back(); // tap outside the panel
            return true;
        }
    }
    return false;
}

void Draw(SDL_Renderer* r) {
    Uint32 now = SDL_GetTicks();
    float dt = g.prev ? std::min(0.1f, (now - g.prev) / 1000.f) : 0.016f;
    g.prev = now;
    g.anim += ((g.open ? 1.f : 0.f) - g.anim) * std::min(1.f, dt * 14);
    if (g.held_dir && g.open && now >= g.repeat_at) { Move(g.held_dir); g.repeat_at = now + 110; }
    if (g.confirm >= 0 && now > g.confirm_until) g.confirm = -1;
    if (g.anim <= 0.01f && !g.open) return;
    if (!g.fonts) {
        g.fonts.reset(new Fonts());
        g.fonts->Init();
        g.shadow.reset(new Shadow());
        g.shadow->Init(r);
    }
    Fonts& f = *g.fonts;
    int ow = 1280, oh = 720;
    SDL_GetRendererOutputSize(r, &ow, &oh);
    View v;
    v.r = r;
    v.s = std::min(ow / 1280.f, oh / 720.f);
    v.ox = (ow - 1280 * v.s) / 2;
    v.oy = (oh - 720 * v.s) / 2;
    float a = 1 - (1 - g.anim) * (1 - g.anim) * (1 - g.anim);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderSetClipRect(r, nullptr);
    SDL_SetRenderDrawColor(r, 4, 6, 10, (Uint8)(170 * a));
    SDL_RenderFillRect(r, nullptr); // dim the whole screen, also outside 16:9

    std::vector<Row> rows = Rows();
    if (g.row >= (int)rows.size()) g.row = 0;
    float row_gap = 8, total = 0;
    for (const Row& rw : rows) total += (rw.hint.empty() ? 54.f : 68.f) + row_gap;
    const float pw = 600, header = 112, footer = 70;
    float ph = std::min(680.f, header + total + footer);
    float k = 0.96f + 0.04f * a;
    float px = 640 - pw * k / 2, py = 360 - ph * k / 2;
    g.shadow->Draw(v, px, py + 12, pw * k, ph * k, 40, Rgba(0, 0, 0, (Uint8)(180 * a)));
    RoundRect(v, px, py, pw * k, ph * k, 26, WithAlpha(kPanel, a));
    float x0 = px + 32 * k, w0 = pw * k - 64 * k;
    DrawText(v, f, g.controls ? T(i18n::kControls) : T(i18n::kMenu), x0, py + 26, 28, WithAlpha(kText, a), kLeft, w0,
             true);
    DrawText(v, f, g.controls ? T(i18n::kControlsHint) : g.title, x0, py + 66, 17, WithAlpha(kText2, a), kLeft, w0);

    g.boxes.clear();
    float y = py + header;
    for (int i = 0; i < (int)rows.size(); ++i) {
        const Row& rw = rows[i];
        float rh = rw.hint.empty() ? 54.f : 68.f;
        bool focused = i == g.row, confirming = g.confirm == rw.id;
        if (focused) {
            RoundRect(v, x0 - 12, y, w0 + 24, rh, 14, confirming ? Rgba(255, 120, 100, (Uint8)(40 * a)) : WithAlpha(kSurfaceHi, a));
            RoundRect(v, x0 - 12, y + 12, 4, rh - 24, 2, WithAlpha(confirming ? kWarn : kAccent, a));
        }
        SDL_Color lc = rw.disabled ? kText3 : rw.danger && focused ? kWarn : kText;
        float ty = y + (rh - 24) / 2 - (rw.hint.empty() ? 0 : 8);
        DrawText(v, f, rw.label, x0 + 8, ty, 20, WithAlpha(lc, a), kLeft, rw.choice ? 260 : w0 - 40);
        if (!rw.hint.empty()) DrawText(v, f, rw.hint, x0 + 8, ty + 27, 14, WithAlpha(kText3, a), kLeft, w0 - 40);
        if (rw.choice) {
            float vx = x0 + w0 - 4;
            float vw = std::min(TextWidth(v, f, rw.value, 18), 240.f);
            DrawText(v, f, rw.value, vx - (focused ? 22 : 0), ty + 1, 18, WithAlpha(focused ? kText : kText2, a), kRight, 240);
            if (focused) {
                DrawText(v, f, "<", vx - vw - 44, ty + 1, 18, WithAlpha(kAccent, a), kRight);
                DrawText(v, f, ">", vx, ty + 1, 18, WithAlpha(kAccent, a), kRight);
            }
            if (rw.id == kVolume) { // level bar under the value
                float bx = vx - 22 - 120, by = ty + 26;
                RoundRect(v, bx, by, 120, 4, 2, WithAlpha(kSurfaceHi, a));
                RoundRect(v, bx, by, 120 * LiveSettings().volume / 100.f, 4, 2, WithAlpha(kAccent, a));
            }
        } else if (rw.submenu) {
            DrawText(v, f, ">", x0 + w0 - 4, y + rh / 2 - 12, 20, WithAlpha(kText2, a), kRight);
        }
        g.boxes.push_back({x0 - 12, y, w0 + 24, rh, i});
        y += rh + row_gap;
    }
    // footer hints
    float fy = py + ph * k - 56;
    struct H { const char* key; const char* label; };
    H hints[2] = {{"A", T(i18n::kSelect)}, {"B", T(i18n::kBack)}};
    float hx = px + pw * k - 32;
    for (int i = 1; i >= 0; --i) {
        float w = 8 + 26 + 8 + TextWidth(v, f, hints[i].label, 18) + 14;
        hx -= w;
        RoundRect(v, hx, fy, w, 38, 19, WithAlpha(Rgba(255, 255, 255, 18), a));
        Circle(v, hx + 21, fy + 19, 13, WithAlpha(kText, a));
        DrawText(v, f, hints[i].key, hx + 21, fy + 7, 16, WithAlpha(Rgba(11, 13, 20), a), kCenter, 0, true);
        DrawText(v, f, hints[i].label, hx + 42, fy + 7, 18, WithAlpha(kText, a));
        if (i == 1) g.boxes.push_back({hx, fy, w, 38, -1});
        hx -= 10;
    }
    // toast
    if (!g.toast.empty() && now < g.toast_until + 300) {
        float ta = (now < g.toast_until ? 1 : 1 - (now - g.toast_until) / 300.f) * a;
        float tw = TextWidth(v, f, g.toast, 18) + 70, tx = 640 - tw / 2, tyy = py + ph * k + 18;
        if (tyy > 660) tyy = 660;
        RoundRect(v, tx, tyy, tw, 44, 22, Rgba(34, 40, 58, (Uint8)(245 * ta)));
        IconCheck(v, tx + 26, tyy + 22, 20, WithAlpha(kAccent, ta));
        DrawText(v, f, g.toast, tx + 46, tyy + 11, 18, WithAlpha(kText, ta));
    }
}

} // namespace overlay
} // namespace kk4s
