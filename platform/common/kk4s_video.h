// kk4s: a video drawn over the game picture (VideoOverlay in overlay mode).
// The player (engine/source/core/movie/kk4s_videoplayer.cpp) hands frames and
// the target rectangle (game coordinates) over from its thread; the presenter
// in sdl2/stubs.cpp draws the newest frame on top of the game every frame.
#pragma once

#include <cstdint>

namespace kk4s {

// One video at a time: the last caller ("who") owns the screen overlay.
void VideoScreenFrame(void* who, const uint8_t* rgba, int width, int height);
void VideoScreenRect(void* who, int left, int top, int right, int bottom, bool visible);
void VideoScreenRemove(void* who);

} // namespace kk4s
