// kk4s: ONScripter (NScripter games) built into kk4s, see docs/engines.md.
// After the launcher the process runs either Kirikiri or ONScripter. ONScripter
// ends with exit(); an atexit handler then does what a Kirikiri game's end
// does (ExitAfterGame: back to the launcher, or quit).
#include "kk4s_platform.h"
#include "kk4s_internal.h"
#include "kk4s_settings.h"
#include "kk4s_files.h"
#include "kk4s_overlay.h"

#include <cstdint>
#ifdef KK4S_HAS_ONSCRIPTER
#define KK4S_EMBEDDED 1
#include "kk4s_ons.h"
#endif

#include <SDL.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace kk4s {

bool IsBuiltInEngine(GameEngine e) {
#ifdef KK4S_HAS_ONSCRIPTER
    if (e == GameEngine::NScripter) return true;
#endif
#ifdef KK4S_HAS_RLVM
    if (e == GameEngine::RealLive) return true;
#endif
    (void)e;
    return false;
}

#ifdef KK4S_HAS_ONSCRIPTER
static void AfterOnscripter() {
    Log("onscripter: game ended");
    ExitAfterGame(0); // does not return: _exit after the launcher hand-off
}

// ---------------------------------------------------------------------------
// Input and the kk4s menu inside ONScripter.
//
// Switch: ONScripter-NX's input thread calls kk4sOnsInputTick() every 10 ms. It
// reads the pad (the only reader while ONScripter runs) and turns the kk4s pad
// mapping into the keys ONScripter knows. L3 + R3 (or a button mapped to the
// menu) asks for the kk4s menu; the main thread runs it in kk4sOnsFilterEvent,
// over the last frame. While the menu is open the thread passes pad changes to
// it as SDL user events, so the menu only ever runs on the main thread.

static Uint32 g_ev = (Uint32)-1;           // SDL user event type of kk4s
enum : Sint32 { kEvWake = 1, kEvPad = 2 };  // event.user.code
static SDL_atomic_t g_menu_wanted;          // the thread asked for the menu
static SDL_atomic_t g_menu_open;            // the menu loop runs (pad goes to it)

static void PushUser(Sint32 code, unsigned bit = 0, bool down = false) {
    if (g_ev == (Uint32)-1) return;
    SDL_Event e;
    SDL_zero(e);
    e.type = g_ev;
    e.user.code = code;
    e.user.data1 = (void*)(uintptr_t)bit;
    e.user.data2 = (void*)(uintptr_t)(down ? 1 : 0);
    SDL_PushEvent(&e);
}

static void PushKey(SDL_Keycode sym, SDL_Scancode sc, bool down) {
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.keysym.sym = sym;
    e.key.keysym.scancode = sc;
    SDL_PushEvent(&e);
}

static void PushWheel(int y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_MOUSEWHEEL;
    e.wheel.y = y;
    SDL_PushEvent(&e);
}

// A kk4s pad action as ONScripter keys. Right click is Esc there (the game's
// right-click menu); the wheel scrolls the text history.
static void PadAction_(PadAction a, bool down) {
    switch (a) {
        case PadAction::Enter: PushKey(SDLK_RETURN, SDL_SCANCODE_RETURN, down); break;
        case PadAction::RightClick:
        case PadAction::Esc: PushKey(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, down); break;
        case PadAction::Ctrl: PushKey(SDLK_LCTRL, SDL_SCANCODE_LCTRL, down); break;
        case PadAction::Space: PushKey(SDLK_SPACE, SDL_SCANCODE_SPACE, down); break;
        case PadAction::PageUp: PushKey(SDLK_PAGEUP, SDL_SCANCODE_PAGEUP, down); break;
        case PadAction::PageDown: PushKey(SDLK_PAGEDOWN, SDL_SCANCODE_PAGEDOWN, down); break;
        case PadAction::WheelUp: if (down) PushWheel(1); break;
        case PadAction::WheelDown: if (down) PushWheel(-1); break;
        case PadAction::Shift: PushKey(SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, down); break;
        case PadAction::Tab: PushKey(SDLK_TAB, SDL_SCANCODE_TAB, down); break;
        case PadAction::Overlay:
            if (down) {
                SDL_AtomicSet(&g_menu_wanted, 1);
                PushUser(kEvWake);
            }
            break;
        default: break;
    }
}

// D-pad / left stick: up / left = previous button or choice, down / right = next
// (ONScripter's h / l), repeated while held.
static void PadDirection(unsigned bit) {
    if (bit == kPadUp || bit == kPadLeft) {
        PushKey(SDLK_h, SDL_SCANCODE_H, true);
        PushKey(SDLK_h, SDL_SCANCODE_H, false);
    } else {
        PushKey(SDLK_l, SDL_SCANCODE_L, true);
        PushKey(SDLK_l, SDL_SCANCODE_L, false);
    }
}

#endif // KK4S_HAS_ONSCRIPTER

} // namespace kk4s

#ifdef KK4S_HAS_ONSCRIPTER
void kk4sOnsInputTick() {
    using namespace kk4s;
    static unsigned prev = 0, rep_bit = 0;
    static Uint32 rep_at = 0, wake_at = 0;
    unsigned held = 0;
    if (!ReadPad(held)) return;
    unsigned down = held & ~prev, up = prev & ~held;
    prev = held;
    Uint32 now = SDL_GetTicks();
    if (SDL_AtomicGet(&g_menu_open)) { // the menu takes the pad
        for (unsigned bit = 1; bit && bit <= kPadLast; bit <<= 1) {
            if (down & bit) PushUser(kEvPad, bit, true);
            if (up & bit) PushUser(kEvPad, bit, false);
        }
        rep_bit = 0;
        return;
    }
    if (SDL_AtomicGet(&g_menu_wanted)) { // keep waking the main loop until it opens the menu
        if (now >= wake_at) { PushUser(kEvWake); wake_at = now + 100; }
        return;
    }
    if ((held & kPadMenuCombo) == kPadMenuCombo && (down & kPadMenuCombo)) {
        SDL_AtomicSet(&g_menu_wanted, 1);
        PushUser(kEvWake);
        wake_at = now + 100;
        return;
    }
    const unsigned dirs = kPadUp | kPadDown | kPadLeft | kPadRight;
    for (unsigned bit = 1; bit && bit <= kPadLast; bit <<= 1) {
        if (bit & kPadMenuCombo) continue; // the stick clicks only open the menu
        if (bit & dirs) {
            if (down & bit) { PadDirection(bit); rep_bit = bit; rep_at = now + 350; }
            continue;
        }
        if (down & bit) PadAction_(LiveSettings().PadFor(bit), true);
        if (up & bit) PadAction_(LiveSettings().PadFor(bit), false);
    }
    if (rep_bit && (held & rep_bit) && now >= rep_at) { PadDirection(rep_bit); rep_at = now + 110; }
    if (rep_bit && !(held & rep_bit)) rep_bit = 0;
}

// The kk4s menu over the last frame. ONScripter draws into the window surface,
// so the menu is drawn by a software renderer into its own surface, which is
// then copied to the window.
static void RunMenu(SDL_Window* window, SDL_Surface* screen) {
    using namespace kk4s;
    static SDL_Surface* canvas = nullptr;
    static SDL_Renderer* sr = nullptr; // kept: the menu caches textures of its renderer
    SDL_AtomicSet(&g_menu_wanted, 0);
    if (!window || !screen) return;
    if (!canvas) {
        canvas = SDL_CreateRGBSurfaceWithFormat(0, screen->w, screen->h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (canvas) sr = SDL_CreateSoftwareRenderer(canvas);
        if (!sr) {
            Log("onscripter: no software renderer for the kk4s menu: %s", SDL_GetError());
            return;
        }
    }
    SDL_Surface* frame = SDL_ConvertSurfaceFormat(screen, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_Texture* frame_tex = frame ? SDL_CreateTextureFromSurface(sr, frame) : nullptr;
    std::string title = GameDir();
    while (!title.empty() && title.back() == '/') title.pop_back();
    title = title.substr(title.find_last_of('/') + 1);
    overlay::SetBasic(true);
    overlay::Open(title, false);
    SDL_AtomicSet(&g_menu_open, 1);
    Log("onscripter: kk4s menu open");
    bool quit = false;
    // KK4S_ONS_MENU_SHOT=<file.bmp> (tests): save the 30th menu frame and leave the game
    const char* shot = getenv("KK4S_ONS_MENU_SHOT");
    int frames = 0;
    while (overlay::Visible() && !quit) {
        if (shot && *shot && ++frames == 30) {
            SDL_SaveBMP(canvas, shot);
            Log("onscripter: menu frame saved to %s", shot);
            SetExitMode(ExitMode::Quit);
            quit = true;
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { quit = true; SetExitMode(ExitMode::Quit); break; }
            if (e.type == g_ev) {
                if (e.user.code == kEvPad) overlay::Pad((unsigned)(uintptr_t)e.user.data1, e.user.data2 != nullptr);
                continue;
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F1) { overlay::Close(); continue; }
            overlay::Event(e, canvas->w, canvas->h);
        }
        using overlay::Request;
        for (Request q = overlay::TakeRequest(); q != Request::None; q = overlay::TakeRequest()) {
            if (q == Request::ToLauncher) { SetExitMode(ExitMode::Launcher); quit = true; }
            else if (q == Request::Quit) { SetExitMode(ExitMode::Quit); quit = true; }
        }
        SDL_SetRenderDrawColor(sr, 0, 0, 0, 255);
        SDL_RenderClear(sr);
        if (frame_tex) SDL_RenderCopy(sr, frame_tex, nullptr, nullptr);
        overlay::Draw(sr);
        SDL_RenderPresent(sr);
        SDL_BlitSurface(canvas, nullptr, screen, nullptr);
        SDL_UpdateWindowSurface(window);
        SDL_Delay(16);
    }
    if (overlay::IsOpen()) overlay::Close();
    SDL_AtomicSet(&g_menu_open, 0);
    // the game's frame back as it was
    if (frame) SDL_BlitSurface(frame, nullptr, screen, nullptr);
    SDL_UpdateWindowSurface(window);
    if (frame_tex) SDL_DestroyTexture(frame_tex);
    if (frame) SDL_FreeSurface(frame);
    Log("onscripter: kk4s menu closed%s", quit ? ", leaving the game" : "");
    if (quit) {
        SDL_Event q;
        SDL_zero(q);
        q.type = SDL_QUIT; // ONScripter saves and exits; AfterOnscripter() does the rest
        SDL_PushEvent(&q);
    }
}

bool kk4sOnsFilterEvent(SDL_Event* event, SDL_Window* window, SDL_Surface* screen) {
    bool ours = event->type == kk4s::g_ev;
    // F1 opens the menu on PC, as in Kirikiri games
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F1) ours = true, SDL_AtomicSet(&kk4s::g_menu_wanted, 1);
    if (SDL_AtomicGet(&kk4s::g_menu_wanted)) RunMenu(window, screen);
    return ours;
}
#endif // KK4S_HAS_ONSCRIPTER

namespace kk4s {

void RunBuiltInEngine(GameEngine e, const std::string& game_dir, SDL_Window* window, SDL_Renderer* renderer) {
#ifdef KK4S_HAS_RLVM
    if (e == GameEngine::RealLive) {
        RunBuiltInRealLive(game_dir, window, renderer);  // does not return
        return;
    }
#endif
#ifdef KK4S_HAS_ONSCRIPTER
    if (e != GameEngine::NScripter) return;
    std::string saves = SavesDir() + "/" + detail::GameKey(game_dir) + "/";
    files::MakeDirs(saves);
    std::vector<std::string> args{"onscripter"};
    for (const std::string& a : detail::ExternalEngineArgs(e, game_dir, "", "")) args.push_back(a);
    args.push_back("--kk4s-saves");
    args.push_back(saves);
    std::string line;
    for (const std::string& a : args) line += " \"" + a + "\"";
    Log("onscripter: built-in ONScripter-NX,%s", line.c_str());

    // ONScripter opens its own window: the launcher's goes away first.
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SetGamePerformance(LiveSettings().cpu_boost);
    atexit(AfterOnscripter);
    // ONScripter uses SDL_USEREVENT .. +6 without registering them: take a type past those
    {
        Uint32 base = SDL_RegisterEvents(16);
        g_ev = base == (Uint32)-1 ? (Uint32)-1 : base + 15;
    }
    if (const char* shot = getenv("KK4S_ONS_MENU_SHOT")) {
        if (*shot)
            SDL_AddTimer(1500, [](Uint32, void*) -> Uint32 { // as if L3 + R3 were pressed
                SDL_AtomicSet(&g_menu_wanted, 1);
                PushUser(kEvWake);
                return 0;
            }, nullptr);
    }
    SDL_AtomicSet(&g_menu_wanted, 0);
    SDL_AtomicSet(&g_menu_open, 0);

    static std::vector<std::string> keep; // argv strings live until exit
    keep = args;
    std::vector<char*> argv;
    for (std::string& a : keep) argv.push_back(&a[0]);
    argv.push_back(nullptr);
    onscripter_main((int)keep.size(), argv.data());
    exit(0); // onscripter_main ends with exit() itself
#else
    (void)e;
    (void)game_dir;
    (void)window;
    (void)renderer;
#endif
}

} // namespace kk4s
