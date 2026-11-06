// kk4s: ONScripter (NScripter games) built into kk4s, see docs/engines.md.
// After the launcher the process runs either Kirikiri or ONScripter. ONScripter
// ends with exit(); an atexit handler then does what a Kirikiri game's end
// does (ExitAfterGame: back to the launcher, or quit).
#include "kk4s_platform.h"
#include "kk4s_internal.h"
#include "kk4s_settings.h"

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
    return e == GameEngine::NScripter;
#else
    (void)e;
    return false;
#endif
}

#ifdef KK4S_HAS_ONSCRIPTER
static void AfterOnscripter() {
    Log("onscripter: game ended");
    ExitAfterGame(0); // does not return: _exit after the launcher hand-off
}
#endif

void RunBuiltInEngine(GameEngine e, const std::string& game_dir, SDL_Window* window, SDL_Renderer* renderer) {
#ifdef KK4S_HAS_ONSCRIPTER
    if (e != GameEngine::NScripter) return;
    std::string saves = SavesDir() + "/" + detail::GameKey(game_dir) + "/";
    detail::MakeDir(saves);
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
