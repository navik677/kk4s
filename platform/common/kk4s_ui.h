// kk4s launcher drawing helpers: text (FreeType), rounded shapes, shadows,
// simple vector icons. Layout is in 1280x720 units, scaled to the output.
#pragma once

#include "kk4s_image.h"

#include <SDL.h>
#include <map>
#include <string>
#include <vector>

typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace kk4s {
namespace ui {

SDL_Texture* MakeTexture(SDL_Renderer* r, const img::Image& image);

struct View {
    SDL_Renderer* r = nullptr;
    float s = 1, ox = 0, oy = 0; // scale and offset of the 1280x720 layout
    SDL_FRect Map(float x, float y, float w, float h) const { return {ox + x * s, oy + y * s, w * s, h * s}; }
    float LayoutX(float px) const { return (px - ox) / s; }
    float LayoutY(float py) const { return (py - oy) / s; }
};

inline SDL_Color Rgba(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) { return SDL_Color{r, g, b, a}; }
inline SDL_Color WithAlpha(SDL_Color c, float a) { c.a = (Uint8)(c.a * (a < 0 ? 0 : a > 1 ? 1 : a)); return c; }

// ---------------------------------------------------------------------------
// text

struct Text {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};

class Fonts {
public:
    Fonts() = default;
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;
    ~Fonts();
    // Picks faces from kk4s::SystemFontFiles(): a static font with proportional
    // Cyrillic first, then any Cyrillic font, then a CJK font for Japanese.
    bool Init();
    // White text in a cached texture (tint with color mod). Cut with "…" past max_w pixels.
    const Text& Get(SDL_Renderer* r, const std::string& utf8, int px, int max_w, bool bold);
    void ClearCache();

private:
    FT_Face FaceFor(uint32_t cp);
    int Advance(uint32_t cp, int px, bool bold);
    void Render(SDL_Renderer* r, std::vector<uint32_t> cps, int px, int max_w, bool bold, Text& out);

    FT_Library lib_ = nullptr;
    std::vector<FT_Face> faces_;
    std::map<std::string, Text> cache_;
};

std::vector<uint32_t> DecodeUtf8(const std::string& s);
std::string FirstCharacter(const std::string& s);

enum Align { kLeft, kCenter, kRight };
// Draws text with its top at y; returns its width in layout units.
float DrawText(const View& v, Fonts& f, const std::string& s, float x, float y, float size, SDL_Color c,
               Align align = kLeft, float max_w = 0, bool bold = false);
float TextWidth(const View& v, Fonts& f, const std::string& s, float size, bool bold = false);

// ---------------------------------------------------------------------------
// shapes

void FillRect(const View& v, float x, float y, float w, float h, SDL_Color c);
void Gradient(const View& v, float x, float y, float w, float h, SDL_Color top, SDL_Color bottom);
void HGradient(const View& v, float x, float y, float w, float h, SDL_Color left, SDL_Color right);
void RoundRect(const View& v, float x, float y, float w, float h, float radius, SDL_Color c);
// Rounded outline (ring of width t).
void RoundFrame(const View& v, float x, float y, float w, float h, float radius, float t, SDL_Color c);
// Texture clipped to a rounded rect; src = part of the texture in pixels (nullptr = all).
void RoundImage(const View& v, SDL_Texture* tex, const SDL_Rect* src, float x, float y, float w, float h,
                float radius, SDL_Color mod);
void Circle(const View& v, float cx, float cy, float radius, SDL_Color c);
void Ring(const View& v, float cx, float cy, float radius, float t, SDL_Color c);

// Soft drop shadow texture (white, blurred rounded rect) drawn tinted.
class Shadow {
public:
    void Init(SDL_Renderer* r);
    ~Shadow();
    void Draw(const View& v, float x, float y, float w, float h, float spread, SDL_Color c) const;

private:
    SDL_Texture* tex_ = nullptr;
};

// Crop rect that makes an image of iw x ih fill a box of aspect w/h.
SDL_Rect CoverCrop(int iw, int ih, float w, float h);

// ---------------------------------------------------------------------------
// icons (line art, size = box edge in layout units)

void IconGear(const View& v, float cx, float cy, float size, SDL_Color c);
void IconSearch(const View& v, float cx, float cy, float size, SDL_Color c);
void IconBattery(const View& v, float x, float y, float w, float h, int percent, bool charging, SDL_Color c);
void IconCheck(const View& v, float cx, float cy, float size, SDL_Color c);

} // namespace ui
} // namespace kk4s
