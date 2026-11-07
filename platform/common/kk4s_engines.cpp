// kk4s: games of other engines (NScripter, RealLive, Ren'Py) run in their own
// program, which kk4s starts for the folder picked in the launcher. The engines
// stay separate programs (their own licenses, no shared state with Kirikiri):
//   Switch: <RootDir>/engines/<id>/<id>.nro, started as the next NRO by hbloader;
//           it sets kk4s.nro as its own next NRO, so quitting returns to the launcher.
//   PC:     <RootDir>/engines/<id>/<id>.exe (Windows) or <id> (Linux), run and waited for.
// See docs/engines.md.
#include "kk4s_platform.h"
#include "kk4s_internal.h"
#include "kk4s_files.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

namespace kk4s {

using namespace detail;

const char* EngineName(GameEngine e) {
    switch (e) {
        case GameEngine::Kirikiri: return "Kirikiri";
        case GameEngine::NScripter: return "NScripter";
        case GameEngine::RealLive: return "RealLive";
        case GameEngine::RenPy: return "Ren'Py";
        default: return "?";
    }
}

bool IsExternalEngine(GameEngine e) {
    if (IsBuiltInEngine(e)) return false;
    return e == GameEngine::NScripter || e == GameEngine::RealLive || e == GameEngine::RenPy;
}

namespace detail {

// Folder name under <RootDir>/engines/ and the program's base name.
const char* EngineId(GameEngine e) {
    switch (e) {
        case GameEngine::NScripter: return "onscripter";
        case GameEngine::RealLive: return "rlvm";
        case GameEngine::RenPy: return "renpy";
        default: return "";
    }
}

static bool IsRegularFile(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string NoSlash(std::string s) {
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

// The launcher-side name of the game: the folder directly inside GamesDir(),
// even when the game itself sits in a wrapper sub-folder.
std::string GameKey(const std::string& game_dir) {
    std::string dir = NoSlash(game_dir), root = NoSlash(GamesDir()) + "/";
    if (dir.compare(0, root.size(), root) == 0) {
        std::string rest = dir.substr(root.size());
        size_t s1 = rest.find('/');
        std::string first = rest.substr(0, s1);
        // a game of a collection (games/ons/<game>): "ons/<game>", as the launcher lists it
        if (s1 != std::string::npos && IsCollectionFolder(root + first, first)) {
            size_t s2 = rest.find('/', s1 + 1);
            return rest.substr(0, s2);
        }
        return first;
    }
    size_t s = dir.find_last_of("/\\");
    return s == std::string::npos ? dir : dir.substr(s + 1);
}

// Ren'Py on PC: a Ren'Py release ships its own launcher (Game.exe / Game.sh next
// to Game.py), which runs the game as its authors built it.
static std::string RenPyOwnLauncher(const std::string& game_dir) {
#ifdef __SWITCH__
    (void)game_dir;
    return "";
#else
    std::string dir = NoSlash(game_dir);
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    while (struct dirent* e = readdir(d)) {
        std::string n = e->d_name;
        size_t dot = n.rfind('.');
        if (dot == std::string::npos) continue;
        std::string ext = n.substr(dot);
#ifdef _WIN32
        if (strcasecmp(ext.c_str(), ".exe") != 0) continue;
#else
        if (strcasecmp(ext.c_str(), ".sh") != 0) continue;
#endif
        if (IsRegularFile(dir + "/" + n.substr(0, dot) + ".py")) { found = dir + "/" + n; break; }
    }
    closedir(d);
    return found;
#endif
}

} // namespace detail

std::string ExternalEngineProgram(GameEngine e, const std::string& game_dir) {
    if (!IsExternalEngine(e)) return "";
    std::string id = EngineId(e);
    std::string base = RootDir() + "/engines/" + id + "/" + id;
#if defined(__SWITCH__)
    if (IsRegularFile(base + ".nro")) return base + ".nro";
#elif defined(_WIN32)
    if (IsRegularFile(base + ".exe")) return base + ".exe";
#else
    if (IsRegularFile(base)) return base;
    // distribution packages (onscripter, rlvm)
    for (const char* d : {"/usr/bin/", "/usr/games/", "/usr/local/bin/"})
        if (IsRegularFile(std::string(d) + id)) return std::string(d) + id;
#endif
    if (e == GameEngine::RenPy) return RenPyOwnLauncher(game_dir);
    return "";
}

namespace detail {

std::vector<std::string> ExternalEngineArgs(GameEngine e, const std::string& game_dir, const std::string& program,
                                            const std::string& return_to) {
    std::vector<std::string> args;
    std::string dir = NoSlash(game_dir);
    switch (e) {
        case GameEngine::NScripter: {
            // ONScripter adds the '/' to --root itself.
            args = {"--root", dir};
            // NScripter games ship default.ttf; without it ONScripter shows no text.
            bool own_font = false;
            if (DIR* d = opendir(dir.c_str())) {
                while (struct dirent* de = readdir(d))
                    if (strcasecmp(de->d_name, "default.ttf") == 0) { own_font = true; break; }
                closedir(d);
            }
            if (!own_font) {
                std::vector<std::string> fonts = SystemFontFiles();
                if (!fonts.empty()) {
                    args.push_back("--font");
                    args.push_back(fonts[0]);
                }
            }
            if (!return_to.empty()) {
                args.push_back("--kk4s-return");
                args.push_back(return_to);
            }
            break;
        }
        case GameEngine::RealLive: {
            // Saves next to kk4s' own, under the name the launcher lists the
            // game by (saves/rlvm/LB/rlvm), on every platform.
            std::string saves = SavesDir() + "/" + GameKey(game_dir) + "/rlvm";
            files::MakeDirs(saves);
            args.push_back("--kk4s-saves");
            args.push_back(saves);
            if (!return_to.empty()) {
                args.push_back("--kk4s-return");
                args.push_back(return_to);
            }
            args.push_back(dir);
            break;
        }
        case GameEngine::RenPy:
            // The Switch Ren'Py takes no arguments: it reads WriteRenPyHandoff()'s file.
            // The game's own launcher on PC knows its folder.
            (void)program;
            break;
        default: break;
    }
    return args;
}

// The Switch build of Ren'Py (renpy-switch) accepts no arguments, so the game is
// passed in <RootDir>/engines/renpy/kk4s_game.txt, read by kk4s' renpy.py
// (engines/renpy/renpy.py in the repository): key=value lines.
bool WriteRenPyHandoff(const std::string& game_dir, const std::string& return_to) {
    std::string saves = SavesDir() + "/" + GameKey(game_dir) + "/renpy";
    files::MakeDirs(saves);
    std::string path = RootDir() + "/engines/renpy/kk4s_game.txt";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        Log("engines: cannot write %s", path.c_str());
        return false;
    }
    fprintf(f, "game=%s\nsaves=%s\nreturn=%s\n", NoSlash(game_dir).c_str(), saves.c_str(), return_to.c_str());
    fclose(f);
    Log("engines: Ren'Py handoff %s: game %s, saves %s", path.c_str(), NoSlash(game_dir).c_str(), saves.c_str());
    return true;
}

} // namespace detail
} // namespace kk4s
