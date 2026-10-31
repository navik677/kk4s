// kk4s in-game menu: drawn over the running game (+ / − by default).
// Resume, quick save / load, volume, controls, exit to the launcher, quit.
// The engine side (sdl2/stubs.cpp) feeds input, draws it before presenting
// and carries out the requests (TJS calls, exit).
#pragma once

#include <SDL.h>
#include <string>

namespace kk4s {
namespace overlay {

enum class Request { None, QuickSave, QuickLoad, Volume, ToLauncher, Quit };

bool IsOpen();   // takes input
bool Visible();  // open, or still fading out (needs drawing)
void Open(const std::string& game_title, bool quick_save_exists);
void Close();

// Input while open.
void Pad(unsigned pad_button, bool down);
// Keyboard / mouse / touch; returns true if the event belongs to the menu.
bool Event(const SDL_Event& e, int out_w, int out_h);

// Draw over the current frame (call before SDL_RenderPresent).
void Draw(SDL_Renderer* r);

// Next request for the engine (None if there is none).
Request TakeRequest();
void SetQuickSaveExists(bool exists);
void Toast(const std::string& text);

} // namespace overlay
} // namespace kk4s
