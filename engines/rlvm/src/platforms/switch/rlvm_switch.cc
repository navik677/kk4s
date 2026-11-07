// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// kk4s: Nintendo Switch front end for rlvm (replaces platforms/gtk/rlvm.cc).
//
// Started by the kk4s launcher (docs/engines.md) as
//
//   rlvm.nro [--kk4s-return <kk4s.nro>] [--font <file>] [options] <game dir>
//
// --kk4s-return  hbloader starts that NRO when rlvm exits (any exit path),
//                so the player comes back to the kk4s launcher.
// --font         TrueType font; default: msgothic.ttc in the game folder, else
//                the Switch's own Japanese font (cached as a .ttf under
//                sdmc:/switch/kk4s/fonts/.cache/).
// --kk4s-saves   save folder; default sdmc:/switch/kk4s/saves/<game folder>/rlvm
//
// The debug options of the desktop rlvm (--start-seen, --load-save, --memory,
// --undefined-opcodes, --count-undefined, --trace) are accepted as well.
// Output goes to sdmc:/switch/kk4s/logs/rlvm.log.
//
// Built with -DKK4S_EMBEDDED (rlvm linked into kk4s.nro, like ONScripter-NX),
// the entry point is rlvm_main() and stdout/stderr are left to the host.
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

#include <switch.h>

#include <SDL2/SDL.h>
#include <sys/stat.h>

#include <boost/filesystem.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "machine/rlvm_instance.h"
#include "systems/base/system.h"
#include "utilities/file.h"

namespace fs = boost::filesystem;

namespace {

const char kRoot[] = "sdmc:/switch/kk4s";

class SwitchRLVMInstance : public RLVMInstance {
 public:
  bool failed() const { return failed_; }

 protected:
  void ReportFatalError(const std::string& message_text,
                        const std::string& informative_text) override {
    failed_ = true;
    std::cerr << "rlvm: " << message_text << ": " << informative_text
              << std::endl;
    ShowError(message_text, informative_text);
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

 public:
  // The system error applet; SDL is shut down by then (SDLSystem's destructor
  // runs before Run() reports the error).
  static void ShowError(const std::string& title, const std::string& detail) {
#ifdef KK4S_EMBEDDED
    // kk4s owns this process and its log: no system error applet over the
    // launcher, whose window rlvm has already replaced.
    std::cerr << "rlvm: " << title << ": " << detail << std::endl;
    return;
#else
    ErrorApplicationConfig c;
    std::string shortText = "rlvm: " + title;
    if (R_SUCCEEDED(errorApplicationCreate(&c, shortText.c_str(),
                                           detail.c_str())))
      errorApplicationShow(&c);
#endif
  }

 private:
  bool failed_ = false;
};

std::string NoSlash(std::string s) {
  while (s.size() > 1 && (s.back() == '/' || s.back() == '\\'))
    s.pop_back();
  return s;
}

// Same rule as kk4s::GameKey() (platform/common/kk4s_engines.cpp): the folder
// directly under sdmc:/switch/kk4s/games, else the last path component.
std::string GameKey(const std::string& game_dir) {
  std::string dir = NoSlash(game_dir);
  std::string root = std::string(kRoot) + "/games/";
  if (dir.compare(0, root.size(), root) == 0) {
    std::string rest = dir.substr(root.size());
    return rest.substr(0, rest.find('/'));
  }
  size_t s = dir.find_last_of("/\\");
  return s == std::string::npos ? dir : dir.substr(s + 1);
}

bool IsFile(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

// The Switch's shared Japanese font, written once as a .ttf (kk4s does the
// same in platform/switch/kk4s_platform.cpp, so the file may already be there).
std::string SharedFontFile() {
  std::string dir = std::string(kRoot) + "/fonts/.cache";
  std::string path = dir + "/nintendo_standard.ttf";
  if (IsFile(path))
    return path;
  std::string out;
  if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
    PlFontData fd;
    if (R_SUCCEEDED(plGetSharedFontByType(&fd, PlSharedFontType_Standard)) &&
        fd.address && fd.size) {
      boost::system::error_code ec;
      fs::create_directories(dir, ec);
      if (FILE* f = fopen(path.c_str(), "wb")) {
        bool ok = fwrite(fd.address, 1, fd.size, f) == fd.size;
        fclose(f);
        if (ok)
          out = path;
        else
          remove(path.c_str());
      }
    }
    plExit();
  }
  return out;
}

#ifndef KK4S_EMBEDDED
void OpenLog() {
  std::string dir = std::string(kRoot) + "/logs";
  mkdir(kRoot, 0777);
  mkdir(dir.c_str(), 0777);
  std::string path = dir + "/rlvm.log";
  if (freopen(path.c_str(), "w", stdout))
    setvbuf(stdout, nullptr, _IOLBF, 0);
  if (freopen(path.c_str(), "a", stderr))
    setvbuf(stderr, nullptr, _IONBF, 0);
}
#endif

}  // namespace

#ifdef KK4S_EMBEDDED
int rlvm_main(int argc, char* argv[]) {
#else
int main(int argc, char* argv[]) {
  OpenLog();
#endif
  std::cout << "rlvm (" << GetRlvmVersionString() << ") for kk4s" << std::endl;

  SwitchRLVMInstance instance;
  std::string game, font, saves;

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
#ifdef KK4S_EMBEDDED
      value("--kk4s-return");  // no NRO hand-off: kk4s exits to the launcher
#else
      std::string nro = value("--kk4s-return");
      // hbloader runs this NRO after rlvm exits, however it exits.
      if (!nro.empty() && envHasNextLoad()) {
        std::string args = "\"" + nro + "\"";
        envSetNextLoad(nro.c_str(), args.c_str());
        std::cout << "return to " << nro << std::endl;
      }
#endif
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
    SwitchRLVMInstance::ShowError(
        "no game",
        "Start a RealLive game (a folder with Gameexe.ini and Seen.txt) from "
        "the kk4s launcher, or pass its folder as the argument.");
    return 1;
  }

  fs::path gamerootPath(NoSlash(game));
  if (!fs::is_directory(gamerootPath)) {
    SwitchRLVMInstance::ShowError("game folder not found", game);
    return 1;
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
    saves = std::string(kRoot) + "/saves/" + GameKey(game) + "/rlvm";
  instance.set_save_dir(saves);
  std::cout << "saves " << saves << std::endl;

  if (font.empty() && CorrectPathCase(gamerootPath / "msgothic.ttc").empty()) {
    // kk4s: the Steam editions of Key's games ship the font of the translation
    // instead of msgothic.ttc (CLANNAD HD: dat/migmix-1m-regular.ttf).
    font = PathStr(CorrectPathCase(gamerootPath / "dat" / "migmix-1m-regular.ttf"));
    if (font.empty())
      font = SharedFontFile();
  }
  if (!font.empty()) {
    instance.set_custom_font(font);
    std::cout << "font " << font << std::endl;
  }

  instance.Run(gamerootPath);
  std::cout << "rlvm exits" << std::endl;
  return instance.failed() ? 1 : 0;
}
