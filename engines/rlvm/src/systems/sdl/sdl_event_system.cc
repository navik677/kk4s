// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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

#include "systems/sdl/sdl_event_system.h"
#include "systems/sdl/sdl_graphics_system.h"
#include "systems/sdl/sdl_system.h"

#include <SDL2/SDL.h>
#ifdef __ANDROID__  // kk4s: the JNI hook below is Android only
#include <jni.h>
#endif
#include <atomic>

#include <algorithm>
#include <cstdlib>
#include <functional>

#include "machine/rlmachine.h"
#include "systems/base/event_listener.h"
#include "systems/base/graphics_system.h"
#include "systems/sdl/sdl_system.h"

using std::bind;
using std::placeholders::_1;

// Set from Java via JNI during two-finger gestures. When true,
// synthetic touch-to-mouse button events (SDL_TOUCH_MOUSEID) are
// dropped so they don't conflict with the RIGHT CLICK fired by the
// Java layer. Cleared after the gesture completes.
static std::atomic<bool> g_two_finger_active{false};

#ifdef __ANDROID__  // kk4s
extern "C" JNIEXPORT void JNICALL
Java_org_libsdl_app_SDLActivity_nativeSetTwoFingerActive(JNIEnv* env,
                                                          jclass cls,
                                                          jboolean active) {
  g_two_finger_active.store(active, std::memory_order_relaxed);
}
#endif

SDLEventSystem::SDLEventSystem(SDLSystem& sys, Gameexe& gexe)
    : EventSystem(gexe),
      shift_pressed_(false),
      ctrl_pressed_(false),
      mouse_inside_window_(true),
      mouse_pos_(),
      button1_state_(0),
      button2_state_(0),
      last_get_currsor_time_(0),
      last_mouse_move_time_(0),
      system_(sys),
      raw_handler_(NULL) {
#ifdef __SWITCH__
  // kk4s: the Joy-Cons / Pro Controller act as mouse and keys (see
  // TranslatePadEvent()). Touch already arrives as SDL mouse events.
  SDL_InitSubSystem(SDL_INIT_JOYSTICK);
  for (int i = 0; i < SDL_NumJoysticks() && i < 2; ++i)
    SDL_JoystickOpen(i);
  pad_x_ = pad_y_ = -1.0f;
  last_pad_ticks_ = SDL_GetTicks();
#endif
}

void SDLEventSystem::ExecuteEventSystem(RLMachine& machine) {
  SDL_Event event;
#ifdef __SWITCH__
  MovePadPointer();  // kk4s
#endif
  while (SDL_PollEvent(&event)) {
#ifdef __SWITCH__
    TranslatePadEvent(event);  // kk4s
#endif
    switch (event.type) {
      case SDL_KEYDOWN: {
        if (raw_handler_)
          raw_handler_->pushInput(event);
        else
          HandleKeyDown(machine, event);
        break;
      }
      case SDL_KEYUP: {
        if (raw_handler_)
          raw_handler_->pushInput(event);
        else
          HandleKeyUp(machine, event);
        break;
      }
      case SDL_MOUSEMOTION: {
        if (raw_handler_)
          raw_handler_->pushInput(event);
        HandleMouseMotion(machine, event);
        break;
      }
      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP: {
        // During a two-finger gesture, filter out synthetic touch-to-mouse
        // button events (SDL_TOUCH_MOUSEID).  The Java layer fires RIGHT
        // CLICK via onNativeMouse (mouseID 0); SDL's synthetic LEFT CLICK
        // from finger 0 must not reach the game.
        if (g_two_finger_active.load(std::memory_order_relaxed) &&
            event.button.which == SDL_TOUCH_MOUSEID)
          break;
        if (raw_handler_)
          raw_handler_->pushInput(event);
        else
          HandleMouseButtonEvent(machine, event);
        break;
      }
      case SDL_QUIT:
        machine.Halt();
        break;
      case SDL_WINDOWEVENT:
        if (raw_handler_)
          raw_handler_->pushInput(event);
        HandleActiveEvent(machine, event);
        // Handle window resize events to update viewport
        if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
            event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
          // Get the graphics system and calculate the new viewport
          SDLGraphicsSystem* graphics = getSDLGraphics(system_);
          if (graphics) {
            graphics->CalculateViewport(event.window.data1, event.window.data2);
          }
        }
        break;
    }
  }
}

bool SDLEventSystem::CtrlPressed() const {
  return system_.force_fast_forward() || ctrl_pressed_;
}

Point SDLEventSystem::GetCursorPos() {
  PreventCursorPosSpinning();
  return mouse_pos_;
}

void SDLEventSystem::GetCursorPos(Point& position, int& button1, int& button2) {
  PreventCursorPosSpinning();
  position = mouse_pos_;
  button1 = button1_state_;
  button2 = button2_state_;
}

void SDLEventSystem::FlushMouseClicks() {
  button1_state_ = 0;
  button2_state_ = 0;
}

unsigned int SDLEventSystem::TimeOfLastMouseMove() {
  return last_mouse_move_time_;
}

unsigned int SDLEventSystem::GetTicks() const { return SDL_GetTicks(); }

void SDLEventSystem::Wait(unsigned int milliseconds) const {
  SDL_Delay(milliseconds);
}

bool SDLEventSystem::ShiftPressed() const { return shift_pressed_; }

void SDLEventSystem::InjectMouseMovement(RLMachine& machine, const Point& loc) {
  mouse_pos_ = loc;
  BroadcastEvent(machine, bind(&EventListener::MouseMotion, _1, mouse_pos_));
}

void SDLEventSystem::InjectMouseDown(RLMachine& machine) {
  button1_state_ = 1;
  button2_state_ = 0;

  DispatchEvent(machine, bind(&EventListener::MouseButtonStateChanged, _1,
                              MOUSE_LEFT, 1));
}

void SDLEventSystem::InjectMouseUp(RLMachine& machine) {
  button1_state_ = 2;
  button2_state_ = 0;

  DispatchEvent(machine, bind(&EventListener::MouseButtonStateChanged, _1,
                              MOUSE_LEFT, 1));
}

void SDLEventSystem::PreventCursorPosSpinning() {
  unsigned int newTime = GetTicks();

  if ((system_.graphics().screen_update_mode() !=
       GraphicsSystem::SCREENUPDATEMODE_MANUAL) &&
      (newTime - last_get_currsor_time_) < 20) {
    // Prevent spinning on input. When we're not in manual mode, we don't get
    // convenient refresh() calls to insert pauses at. Instead, we need to sort
    // of intuit about what's going on and the easiest way to slow down is to
    // track when the bytecode keeps spamming us for the cursor.
    system_.set_force_wait(true);
  }

  last_get_currsor_time_ = newTime;
}

void SDLEventSystem::HandleKeyDown(RLMachine& machine, SDL_Event& event) {
  switch (event.key.keysym.sym) {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: {
      shift_pressed_ = true;
      break;
    }
    case SDLK_LCTRL:
    case SDLK_RCTRL: {
      ctrl_pressed_ = true;
      break;
    }
    case SDLK_RETURN:
    case SDLK_f: {
      if ((event.key.keysym.mod & KMOD_ALT) ||
          (event.key.keysym.mod & KMOD_GUI)) {
        machine.system().graphics().ToggleFullscreen();

        // Stop processing because we don't want to Dispatch this event, which
        // might advance the text.
        return;
      }

      break;
    }
    default:
      break;
  }

  KeyCode code = KeyCode(event.key.keysym.sym);
  DispatchEvent(machine, bind(&EventListener::KeyStateChanged, _1, code, true));
}

void SDLEventSystem::HandleKeyUp(RLMachine& machine, SDL_Event& event) {
  switch (event.key.keysym.sym) {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: {
      shift_pressed_ = false;
      break;
    }
    case SDLK_LCTRL:
    case SDLK_RCTRL: {
      ctrl_pressed_ = false;
      break;
    }
    case SDLK_F1: {
      machine.system().ShowSystemInfo(machine);
      break;
    }
    case SDLK_F12: {
      machine.system().DumpRenderTree(machine);
      break;
    }
    default:
      break;
  }

  KeyCode code = KeyCode(event.key.keysym.sym);
  DispatchEvent(machine,
                bind(&EventListener::KeyStateChanged, _1, code, false));
}

void SDLEventSystem::HandleMouseMotion(RLMachine& machine, SDL_Event& event) {
  if (mouse_inside_window_) {
    // Transform window coordinates to game coordinates
    Point window_coords(event.motion.x, event.motion.y);
    SDLGraphicsSystem* sdl_graphics =
        static_cast<SDLGraphicsSystem*>(&system_.graphics());
    mouse_pos_ =
        sdl_graphics->WindowToGameCoords(window_coords.x(), window_coords.y());
    last_mouse_move_time_ = GetTicks();

    // Handle this somehow.
    BroadcastEvent(machine, bind(&EventListener::MouseMotion, _1, mouse_pos_));
  }
}

void SDLEventSystem::HandleMouseButtonEvent(RLMachine& machine,
                                            SDL_Event& event) {
  if (mouse_inside_window_) {
    // Transform window coordinates to game coordinates
    Point window_coords(event.button.x, event.button.y);
    SDLGraphicsSystem* sdl_graphics =
        static_cast<SDLGraphicsSystem*>(&system_.graphics());
    mouse_pos_ =
        sdl_graphics->WindowToGameCoords(window_coords.x(), window_coords.y());
    last_mouse_move_time_ = GetTicks();

    bool pressed = event.type == SDL_MOUSEBUTTONDOWN;
    int press_code = pressed ? 1 : 2;

    if (event.button.button == SDL_BUTTON_LEFT)
      button1_state_ = press_code;
    else if (event.button.button == SDL_BUTTON_RIGHT)
      button2_state_ = press_code;

    MouseButton button = MOUSE_NONE;
    switch (event.button.button) {
      case SDL_BUTTON_LEFT:
        button = MOUSE_LEFT;
        break;
      case SDL_BUTTON_RIGHT:
        button = MOUSE_RIGHT;
        break;
      case SDL_BUTTON_MIDDLE:
        button = MOUSE_MIDDLE;
        break;
      // case SDL_BUTTON_WHEELUP: TODO(xyz)
      //   button = MOUSE_WHEELUP;
      //   break;
      // case SDL_BUTTON_WHEELDOWN:
      //   button = MOUSE_WHEELDOWN;
      //   break;
      default:
        break;
    }

    DispatchEvent(machine, bind(&EventListener::MouseButtonStateChanged, _1,
                                button, pressed));
  }
}

void SDLEventSystem::HandleActiveEvent(RLMachine& machine, SDL_Event& event) {
  if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
    // Assume the mouse is inside the window. Actually checking the mouse
    // state doesn't work in the case where we mouse click on another window
    // that's partially covered by rlvm's window and then alt-tab back.
    mouse_inside_window_ = true;
  } else if (event.window.event == SDL_WINDOWEVENT_ENTER) {
    mouse_inside_window_ = true;
  } else if (event.window.event == SDL_WINDOWEVENT_LEAVE) {
    mouse_inside_window_ = false;
  }

  // Force a mouse refresh:
  machine.system().graphics().MarkScreenAsDirty(GUT_MOUSE_MOTION);

  if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
    machine.system().graphics().ForceRefresh();
  }
}

#ifdef __SWITCH__
// -----------------------------------------------------------------------
// kk4s: Switch controller
// -----------------------------------------------------------------------
//
// libnx SDL2 joystick buttons: 0 A, 1 B, 2 X, 3 Y, 4 L-stick, 5 R-stick, 6 L,
// 7 R, 8 ZL, 9 ZR, 10 +, 11 -, 12 left, 13 up, 14 right, 15 down.
// RealLive games are driven by the mouse, so the left stick moves a pointer
// and A clicks where it is (this advances text and picks choices); B is the
// right click (the game's menu, or cancel). Touch works as the mouse anyway.

namespace {
enum {
  PAD_A = 0, PAD_B = 1, PAD_X = 2, PAD_Y = 3, PAD_L = 6, PAD_R = 7,
  PAD_ZL = 8, PAD_ZR = 9, PAD_PLUS = 10, PAD_MINUS = 11,
  PAD_LEFT = 12, PAD_UP = 13, PAD_RIGHT = 14, PAD_DOWN = 15
};
const int kStickDeadZone = 8000;
const float kPointerSpeed = 900.0f;  // window pixels per second at full tilt

SDL_Keycode PadKey(int button) {
  switch (button) {
    case PAD_LEFT: return SDLK_LEFT;
    case PAD_UP: return SDLK_UP;
    case PAD_RIGHT: return SDLK_RIGHT;
    case PAD_DOWN: return SDLK_DOWN;
    case PAD_R:
    case PAD_ZR: return SDLK_LCTRL;  // hold to skip text
    case PAD_X: return SDLK_SPACE;
    default: return SDLK_UNKNOWN;
  }
}
}  // namespace

void SDLEventSystem::PadWindowSize(int& w, int& h) {
  w = 1280;
  h = 720;
  if (SDL_Window* win = SDL_GL_GetCurrentWindow())
    SDL_GetWindowSize(win, &w, &h);
}

void SDLEventSystem::MovePadPointer() {
  unsigned int now = SDL_GetTicks();
  float dt = (now - last_pad_ticks_) / 1000.0f;
  last_pad_ticks_ = now;
  int w, h;
  PadWindowSize(w, h);
  if (pad_x_ < 0) {
    pad_x_ = w / 2.0f;
    pad_y_ = h / 2.0f;
  }
  SDL_Joystick* pad = SDL_JoystickFromInstanceID(0);
  if (!pad && SDL_NumJoysticks() > 0)
    pad = SDL_JoystickOpen(0);
  if (!pad)
    return;
  int ax = SDL_JoystickGetAxis(pad, 0), ay = SDL_JoystickGetAxis(pad, 1);
  if (std::abs(ax) < kStickDeadZone) ax = 0;
  if (std::abs(ay) < kStickDeadZone) ay = 0;
  if (!ax && !ay)
    return;
  float nx = pad_x_ + ax / 32768.0f * kPointerSpeed * dt;
  float ny = pad_y_ + ay / 32768.0f * kPointerSpeed * dt;
  nx = std::max(0.0f, std::min(nx, float(w - 1)));
  ny = std::max(0.0f, std::min(ny, float(h - 1)));
  SDL_Event motion;
  SDL_zero(motion);
  motion.type = SDL_MOUSEMOTION;
  motion.motion.timestamp = now;
  motion.motion.x = int(nx);
  motion.motion.y = int(ny);
  motion.motion.xrel = int(nx) - int(pad_x_);
  motion.motion.yrel = int(ny) - int(pad_y_);
  pad_x_ = nx;
  pad_y_ = ny;
  SDL_PushEvent(&motion);
}

void SDLEventSystem::TranslatePadEvent(SDL_Event& event) {
  if (event.type == SDL_MOUSEMOTION && event.motion.which != 0) {
    // Touch: keep the pad pointer where the finger was.
    pad_x_ = event.motion.x;
    pad_y_ = event.motion.y;
    return;
  }
  if (event.type != SDL_JOYBUTTONDOWN && event.type != SDL_JOYBUTTONUP)
    return;
  bool down = event.type == SDL_JOYBUTTONDOWN;
  int button = event.jbutton.button;
  Uint32 ticks = event.jbutton.timestamp;

  if (button == PAD_PLUS) {
    if (down) {
      SDL_zero(event);
      event.type = SDL_QUIT;  // halts the machine; global memory is saved
      event.quit.timestamp = ticks;
    }
    return;
  }
  if (button == PAD_A || button == PAD_B) {
    if (pad_x_ < 0) {
      int w, h;
      PadWindowSize(w, h);
      pad_x_ = w / 2.0f;
      pad_y_ = h / 2.0f;
    }
    SDL_zero(event);
    event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    event.button.timestamp = ticks;
    event.button.button = button == PAD_A ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
    event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.button.clicks = 1;
    event.button.x = int(pad_x_);
    event.button.y = int(pad_y_);
    return;
  }
  SDL_Keycode key = PadKey(button);
  if (key != SDLK_UNKNOWN) {
    SDL_zero(event);
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.timestamp = ticks;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = key;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
  }
}
#endif  // __SWITCH__
