// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// kk4s: Windows front end for the standalone rlvm program (rlvm.exe). The
// Switch front end (platforms/switch/rlvm_switch.cc) is the same program
// without the libnx calls; kk4s starts this one from the launcher as
//
//   rlvm.exe [--font <file>] [--kk4s-saves <dir>] [options] <game dir>
//
// --kk4s-return is accepted and ignored (there is no NRO to hand over to).
// Saves default to <kk4s root>/saves/<game>/rlvm, where the kk4s root is
// KK4S_ROOT if set, else the folder above engines/rlvm/ (the layout the
// launcher uses: <root>/engines/rlvm/rlvm.exe) or the exe's own folder.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
//
// -----------------------------------------------------------------------

#include <windows.h>

// A console program with its own main(): SDL must not rename it to SDL_main.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <sys/stat.h>

#include <boost/filesystem.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>

#include "machine/rlvm_instance.h"
#include "systems/base/system.h"
#include "utilities/file.h"

namespace fs = boost::filesystem;

// Debug help for this port: a failed allocation in rlvm's own code is either a
// misread size or a texture, so report the size before giving up (bad_alloc on
// its own says nothing about which block was asked for).
void* operator new(std::size_t n) {
  if (n > (std::size_t)256 * 1024 * 1024) {
    std::fprintf(stderr, "rlvm: absurd allocation of %zu bytes\n", n);
    std::abort();
  }
  void* p = std::malloc(n ? n : 1);
  if (!p) {
    std::fprintf(stderr, "rlvm: out of memory for %zu bytes\n", n);
    std::abort();
  }
  return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

class WinRLVMInstance : public RLVMInstance {
 public:
  bool failed() const { return failed_; }

 protected:
  void ReportFatalError(const std::string& message_text,
                        const std::string& informative_text) override {
    failed_ = true;
    std::cerr << "rlvm: " << message_text << ": " << informative_text
              << std::endl;
    // For debugging: KK4S_RLVM_ABORT=1 turns the dialog into a crash so gdb
    // stops at the faulty frame instead of waiting on a hidden message box.
    if (std::getenv("KK4S_RLVM_ABORT"))
      std::abort();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             ("rlvm: " + message_text).c_str(),
                             informative_text.c_str(), nullptr);
  }

  // No dialog toolkit here: answer "no" (keep the data as it is).
  bool AskUserPrompt(const std::string& message_text,
                     const std::string& informative_text,
                     const std::string& true_button,
                     const std::string& false_button) override {
    std::cerr << "rlvm: " << message_text << " (" << informative_text
              << "): answering '" << false_button << "'" << std::endl;
    return false;
  }

 private:
  bool failed_ = false;
};

std::string NoSlash(std::string s) {
  while (s.size() > 1 && (s.back() == '/' || s.back() == '\\'))
    s.pop_back();
  return s;
}

std::string ExeDir() {
  char buf[MAX_PATH];
  DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string path(buf, n);
  size_t s = path.find_last_of("/\\");
  return s == std::string::npos ? std::string(".") : path.substr(0, s);
}

std::string BaseName(const std::string& dir) {
  std::string d = NoSlash(dir);
  size_t s = d.find_last_of("/\\");
  return s == std::string::npos ? d : d.substr(s + 1);
}

// Same rule as kk4s::GameKey(): the folder directly under the games folder,
// else the last path component.
std::string GameKey(const std::string& game_dir) { return BaseName(game_dir); }

bool IsFile(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

// <root>/engines/rlvm/rlvm.exe -> <root>; a copy elsewhere -> its own folder.
std::string DefaultRoot() {
  if (const char* env = std::getenv("KK4S_ROOT"))
    if (*env) return NoSlash(env);
  std::string dir = ExeDir();
  if (BaseName(dir) == "rlvm") {
    fs::path parent = fs::path(dir).parent_path();
    if (BaseName(PathStr(parent)) == "engines") return PathStr(parent.parent_path());
  }
  return dir;
}

}  // namespace

int main(int argc, char* argv[]) {
  std::cout << "rlvm (" << GetRlvmVersionString() << ") for kk4s (Windows)"
            << std::endl;

  WinRLVMInstance instance;
  std::string game, font, saves;
  const std::string root = DefaultRoot();

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto value = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "rlvm: " << name << " needs a value" << std::endl;
        return "";
      }
      return argv[++i];
    };
    if (a == "--kk4s-return") {
      value("--kk4s-return");  // no NRO hand-off on Windows
    } else if (a == "--kk4s-saves") {
      saves = value("--kk4s-saves");
    } else if (a == "--font") {
      font = value("--font");
    } else if (a == "--start-seen") {
      instance.set_seen_start(std::atoi(value("--start-seen").c_str()));
    } else if (a == "--load-save") {
      instance.set_load_save(std::atoi(value("--load-save").c_str()));
    } else if (a == "--memory") {
      instance.set_memory();
    } else if (a == "--undefined-opcodes") {
      instance.set_undefined_opcodes();
    } else if (a == "--count-undefined") {
      instance.set_count_undefined();
    } else if (a == "--trace") {
      instance.set_tracing();
    } else if (a.compare(0, 2, "--") == 0) {
      std::cerr << "rlvm: unknown option " << a << std::endl;
    } else {
      game = a;
    }
  }

  if (game.empty()) {
    std::cerr << "rlvm: no game: pass a folder with Gameexe.ini and Seen.txt"
              << std::endl;
    return 1;
  }

  fs::path gamerootPath(NoSlash(game));
  if (!fs::is_directory(gamerootPath)) {
    std::cerr << "rlvm: game folder not found: " << game << std::endl;
    return 1;
  }
  // Unpacked games often sit in a wrapper folder (games/rlvm/clannad): when the
  // folder has no Gameexe.ini but exactly one sub-folder does, that is the game.
  if (CorrectPathCase(gamerootPath / "Gameexe.ini").empty()) {
    boost::system::error_code ec;
    fs::directory_iterator it(gamerootPath, ec), end;
    std::string inner;
    int hits = 0;
    for (; !ec && it != end; it.increment(ec)) {
      if (!fs::is_directory(it->path(), ec))
        continue;
      if (!CorrectPathCase(it->path() / "Gameexe.ini").empty()) {
        inner = PathStr(it->path());
        ++hits;
      }
    }
    if (hits == 1) {
      gamerootPath = fs::path(inner);
      std::cout << "game folder: wrapper, the game is in " << inner << std::endl;
    }
  }

  // Some games hide data in a lower subdirectory (as the desktop rlvm does).
  if (CorrectPathCase(gamerootPath / "Gameexe.ini").empty()) {
    if (!CorrectPathCase(gamerootPath / "KINETICDATA" / "Gameexe.ini").empty())
      gamerootPath /= "KINETICDATA";
    else if (!CorrectPathCase(gamerootPath / "REALLIVEDATA" / "Gameexe.ini")
                  .empty())
      gamerootPath /= "REALLIVEDATA";
  }
  std::cout << "game " << PathStr(gamerootPath) << std::endl;

  if (saves.empty())
    saves = root + "/saves/" + GameKey(game) + "/rlvm";
  instance.set_save_dir(saves);
  std::cout << "saves " << saves << std::endl;

  if (font.empty() && CorrectPathCase(gamerootPath / "msgothic.ttc").empty()) {
    // kk4s: the Steam editions of Key's games ship the font of the translation
    // instead of msgothic.ttc (CLANNAD HD: dat/migmix-1m-regular.ttf).
    font = PathStr(CorrectPathCase(gamerootPath / "dat" / "migmix-1m-regular.ttf"));
    if (font.empty()) {
      const char* system_font = "C:/Windows/Fonts/msgothic.ttc";
      if (IsFile(system_font)) font = system_font;
    }
  }
  if (!font.empty()) {
    instance.set_custom_font(font);
    std::cout << "font " << font << std::endl;
  }

  instance.Run(gamerootPath);
  std::cout << "rlvm exits" << std::endl;
  return instance.failed() ? 1 : 0;
}
