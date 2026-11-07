// kk4s: rlvm (RealLive games) built into kk4s, see docs/engines.md.
// Like ONScripter, rlvm runs in this process after the launcher: it opens its
// own window, and when the game ends kk4s does what it does after a Kirikiri
// game (ExitAfterGame: back to the launcher, or quit).
#include "kk4s_platform.h"
#include "kk4s_internal.h"
#include "kk4s_settings.h"

#ifdef KK4S_HAS_RLVM

#include <SDL.h>

#include <string>
#include <vector>

// rlvm's Switch front end (engines/rlvm/src/platforms/switch/rlvm_switch.cc).
// With -DKK4S_EMBEDDED it is the same program the separate rlvm.nro is, minus
// libnx's main(): no log redirection, no next-NRO hand-off (kk4s does both).
int rlvm_main(int argc, char* argv[]);

namespace kk4s {

void RunBuiltInRealLive(const std::string& game_dir, SDL_Window* window, SDL_Renderer* renderer) {
    std::string saves = SavesDir() + "/" + detail::GameKey(game_dir) + "/rlvm";
    detail::MakeDir(saves);
    std::vector<std::string> args{"rlvm", "--kk4s-saves", saves};
    // The game folder (with its trailing '/'), as for the separate program.
    for (const std::string& a : detail::ExternalEngineArgs(GameEngine::RealLive, game_dir, "", "")) args.push_back(a);
    std::string line;
    for (const std::string& a : args) line += " \"" + a + "\"";
    Log("rlvm: built-in rlvm,%s", line.c_str());

    // rlvm opens its own window: the launcher's goes away first.
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SetGamePerformance(LiveSettings().cpu_boost);

    static std::vector<std::string> keep;  // argv strings live until exit
    keep = args;
    std::vector<char*> argv;
    for (std::string& a : keep) argv.push_back(&a[0]);
    argv.push_back(nullptr);

    int code = rlvm_main(static_cast<int>(keep.size()), argv.data());
    Log("rlvm: game ended (rlvm returned %d)", code);
    ExitAfterGame(0);  // does not return
}

}  // namespace kk4s

#endif  // KK4S_HAS_RLVM
