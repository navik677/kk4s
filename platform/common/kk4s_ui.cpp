// kk4s launcher drawing helpers (see kk4s_ui.h).
#include "kk4s_ui.h"
#include "kk4s_platform.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace kk4s {
namespace ui {

SDL_Texture* MakeTexture(SDL_Renderer* r, const img::Image& image) {
    if (!image.ok()) return nullptr;
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, image.w, image.h);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, image.rgba.data(), image.w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    return t;
}

// ---------------------------------------------------------------------------
// text

std::vector<uint32_t> DecodeUtf8(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        uint32_t cp = len == 1 ? c : c & (0xff >> (len + 1));
        for (int k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3f);
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string FirstCharacter(const std::string& s) {
    if (s.empty()) return s;
    size_t len = 1;
    while (len < s.size() && ((unsigned char)s[len] & 0xC0) == 0x80) ++len;
    return s.substr(0, len);
}

Fonts::~Fonts() {
    ClearCache();
    for (FT_Face f : faces_) FT_Done_Face(f);
    if (lib_) FT_Done_FreeType(lib_);
}

void Fonts::ClearCache() {
    for (auto& kv : cache_)
        if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    cache_.clear();
}

bool Fonts::Init() {
    if (FT_Init_FreeType(&lib_)) return false;
    FT_Face proportional = nullptr, cyrillic = nullptr, cjk = nullptr;
    for (const std::string& path : SystemFontFiles()) {
        FT_Face f;
        if (FT_New_Face(lib_, path.c_str(), 0, &f)) continue;
        bool used = false;
        bool has_cyr = FT_Get_Char_Index(f, 0x0416) != 0, has_cjk = FT_Get_Char_Index(f, 0x3042) != 0;
        // Static font with proportional Cyrillic (variable fonts load a thin weight).
        if (!proportional && has_cyr && !FT_HAS_MULTIPLE_MASTERS(f) && !FT_Load_Char(f, 0x0430, FT_LOAD_NO_SCALE) &&
            f->glyph->metrics.horiAdvance < f->units_per_EM * 3 / 4) {
            proportional = f;
            used = true;
        } else if (!cyrillic && has_cyr) {
            cyrillic = f;
            used = true;
        }
        if (!cjk && has_cjk && !used) {
            cjk = f;
            used = true;
        }
        if (!used) FT_Done_Face(f);
    }
    for (FT_Face f : {proportional, cyrillic, cjk})
        if (f) faces_.push_back(f);
    return !faces_.empty();
}

const Text& Fonts::Get(SDL_Renderer* r, const std::string& utf8, int px, int max_w, bool bold) {
    char key[40];
    snprintf(key, sizeof(key), "|%d|%d|%d", px, max_w, bold ? 1 : 0);
    std::string k = utf8 + key;
    auto it = cache_.find(k);
    if (it != cache_.end()) return it->second;
    if (cache_.size() > 600) ClearCache(); // clock / progress strings change every frame
    Text& t = cache_[k];
    Render(r, DecodeUtf8(utf8), px, max_w, bold, t);
    return t;
}

FT_Face Fonts::FaceFor(uint32_t cp) {
    for (FT_Face f : faces_)
        if (FT_Get_Char_Index(f, cp)) return f;
    return faces_[0];
}

static bool LoadGlyph(FT_Face f, uint32_t cp, int px, bool bold, bool render) {
    FT_Set_Pixel_Sizes(f, 0, px);
    if (FT_Load_Char(f, cp, FT_LOAD_DEFAULT)) return false;
    if (bold && f->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_Pos s = std::max<FT_Pos>(64, px * 64 / 22); // ~1/22 em heavier
        FT_Outline_EmboldenXY(&f->glyph->outline, s, s / 2);
        f->glyph->advance.x += s;
    }
    if (render && FT_Render_Glyph(f->glyph, FT_RENDER_MODE_NORMAL)) return false;
    return true;
}

int Fonts::Advance(uint32_t cp, int px, bool bold) {
    FT_Face f = FaceFor(cp);
    if (!LoadGlyph(f, cp, px, bold, false)) return 0;
    return (int)(f->glyph->advance.x >> 6);
}

void Fonts::Render(SDL_Renderer* r, std::vector<uint32_t> cps, int px, int max_w, bool bold, Text& out) {
    if (faces_.empty() || cps.empty() || px <= 0) return;
    int width = 0;
    for (uint32_t cp : cps) width += Advance(cp, px, bold);
    if (max_w > 0 && width > max_w) {
        int ell = Advance(0x2026, px, bold);
        while (!cps.empty() && width + ell > max_w) { width -= Advance(cps.back(), px, bold); cps.pop_back(); }
        while (!cps.empty() && cps.back() == ' ') { width -= Advance(' ', px, bold); cps.pop_back(); }
        cps.push_back(0x2026);
        width += ell;
    }
    FT_Set_Pixel_Sizes(faces_[0], 0, px);
    int ascent = (int)(faces_[0]->size->metrics.ascender >> 6);
    int height = (int)((faces_[0]->size->metrics.ascender - faces_[0]->size->metrics.descender) >> 6) + 2;
    width += 2;
    if (width <= 2 || height <= 0) return;
    std::vector<uint32_t> pix((size_t)width * height, 0x00ffffffu);
    int pen = 0;
    for (uint32_t cp : cps) {
        FT_Face f = FaceFor(cp);
        if (!LoadGlyph(f, cp, px, bold, true)) continue;
        FT_GlyphSlot g = f->glyph;
        for (unsigned y = 0; y < g->bitmap.rows; ++y)
            for (unsigned x = 0; x < g->bitmap.width; ++x) {
                int dx = pen + g->bitmap_left + (int)x, dy = ascent - g->bitmap_top + (int)y;
                if (dx < 0 || dy < 0 || dx >= width || dy >= height) continue;
                uint32_t a = g->bitmap.buffer[y * g->bitmap.pitch + x];
                uint32_t& p = pix[(size_t)dy * width + dx];
                if (a > (p >> 24)) p = (a << 24) | 0x00ffffffu;
            }
        pen += (int)(g->advance.x >> 6);
    }
    out.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
    if (!out.tex) return;
    SDL_UpdateTexture(out.tex, nullptr, pix.data(), width * 4);
    SDL_SetTextureBlendMode(out.tex, SDL_BLENDMODE_BLEND);
    out.w = width;
    out.h = height;
}

float DrawText(const View& v, Fonts& f, const std::string& s, float x, float y, float size, SDL_Color c, Align align,
               float max_w, bool bold, float scale) {
    if (s.empty()) return 0;
    const Text& t = f.Get(v.r, s, (int)std::lround(size * v.s), (int)(max_w * v.s), bold);
    if (!t.tex) return 0;
    float w = t.w / v.s * scale, h = t.h / v.s * scale;
    if (align == kCenter) x -= w / 2;
    if (align == kRight) x -= w;
    SDL_SetTextureColorMod(t.tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t.tex, c.a);
    SDL_FRect rc = v.Map(x, y, w, h);
    SDL_RenderCopyF(v.r, t.tex, nullptr, &rc);
    return w;
}

float TextWidth(const View& v, Fonts& f, const std::string& s, float size, bool bold) {
    if (s.empty()) return 0;
    const Text& t = f.Get(v.r, s, (int)std::lround(size * v.s), 0, bold);
    return t.w / v.s;
}

// ---------------------------------------------------------------------------
// shapes

static SDL_Vertex Vert(float x, float y, SDL_Color c, float u = 0, float w = 0) {
    SDL_Vertex vx;
    vx.position = {x, y};
    vx.color = c;
    vx.tex_coord = {u, w};
    return vx;
}

void FillRect(const View& v, float x, float y, float w, float h, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(v.r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(v.r, c.r, c.g, c.b, c.a);
    SDL_FRect rc = v.Map(x, y, w, h);
    SDL_RenderFillRectF(v.r, &rc);
}

void Gradient(const View& v, float x, float y, float w, float h, SDL_Color top, SDL_Color bottom) {
    SDL_FRect rc = v.Map(x, y, w, h);
    SDL_Vertex vs[4] = {Vert(rc.x, rc.y, top), Vert(rc.x + rc.w, rc.y, top), Vert(rc.x + rc.w, rc.y + rc.h, bottom),
                        Vert(rc.x, rc.y + rc.h, bottom)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(v.r, nullptr, vs, 4, idx, 6);
}

void HGradient(const View& v, float x, float y, float w, float h, SDL_Color left, SDL_Color right) {
    SDL_FRect rc = v.Map(x, y, w, h);
    SDL_Vertex vs[4] = {Vert(rc.x, rc.y, left), Vert(rc.x + rc.w, rc.y, right), Vert(rc.x + rc.w, rc.y + rc.h, right),
                        Vert(rc.x, rc.y + rc.h, left)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(v.r, nullptr, vs, 4, idx, 6);
}

// Perimeter of a rounded rect in output pixels, clockwise from the top-left arc.
static int ArcSegments(float rad) { return rad < 6 ? 2 : rad < 20 ? 5 : 8; }

static std::vector<SDL_FPoint> RoundPath(const SDL_FRect& rc, float rad, int seg = -1) {
    rad = std::max(0.f, std::min(rad, std::min(rc.w, rc.h) / 2));
    if (seg < 0) seg = ArcSegments(rad);
    std::vector<SDL_FPoint> pts;
    const float cx[4] = {rc.x + rad, rc.x + rc.w - rad, rc.x + rc.w - rad, rc.x + rad};
    const float cy[4] = {rc.y + rad, rc.y + rad, rc.y + rc.h - rad, rc.y + rc.h - rad};
    for (int c = 0; c < 4; ++c) {
        float a0 = (float)M_PI * (1.0f + 0.5f * c);
        for (int i = 0; i <= seg; ++i) {
            float a = a0 + (float)M_PI * 0.5f * i / seg;
            pts.push_back({cx[c] + std::cos(a) * rad, cy[c] + std::sin(a) * rad});
        }
    }
    return pts;
}

static void FanFill(const View& v, SDL_Texture* tex, const SDL_FRect& rc, const std::vector<SDL_FPoint>& pts,
                    SDL_Color c, const SDL_FRect& uv) {
    std::vector<SDL_Vertex> vs;
    std::vector<int> idx;
    auto uv_of = [&](float px, float py, float& u, float& w) {
        u = uv.x + (px - rc.x) / rc.w * uv.w;
        w = uv.y + (py - rc.y) / rc.h * uv.h;
    };
    float u, w;
    uv_of(rc.x + rc.w / 2, rc.y + rc.h / 2, u, w);
    vs.push_back(Vert(rc.x + rc.w / 2, rc.y + rc.h / 2, c, u, w));
    for (const SDL_FPoint& p : pts) {
        uv_of(p.x, p.y, u, w);
        vs.push_back(Vert(p.x, p.y, c, u, w));
    }
    int n = (int)pts.size();
    for (int i = 0; i < n; ++i) {
        idx.push_back(0);
        idx.push_back(1 + i);
        idx.push_back(1 + (i + 1) % n);
    }
    SDL_RenderGeometry(v.r, tex, vs.data(), (int)vs.size(), idx.data(), (int)idx.size());
}

void RoundRect(const View& v, float x, float y, float w, float h, float radius, SDL_Color c) {
    if (w <= 0 || h <= 0) return;
    SDL_FRect rc = v.Map(x, y, w, h);
    FanFill(v, nullptr, rc, RoundPath(rc, radius * v.s), c, SDL_FRect{0, 0, 1, 1});
}

void RoundFrame(const View& v, float x, float y, float w, float h, float radius, float t, SDL_Color c) {
    SDL_FRect outer = v.Map(x, y, w, h), inner = v.Map(x + t, y + t, w - 2 * t, h - 2 * t);
    int seg = ArcSegments(radius * v.s); // same point count on both edges
    std::vector<SDL_FPoint> po = RoundPath(outer, radius * v.s, seg),
                            pi = RoundPath(inner, std::max(0.f, radius - t) * v.s, seg);
    std::vector<SDL_Vertex> vs;
    std::vector<int> idx;
    int n = (int)po.size();
    for (int i = 0; i < n; ++i) {
        vs.push_back(Vert(po[i].x, po[i].y, c));
        vs.push_back(Vert(pi[i].x, pi[i].y, c));
    }
    for (int i = 0; i < n; ++i) {
        int a = 2 * i, b = 2 * ((i + 1) % n);
        idx.insert(idx.end(), {a, a + 1, b, b, a + 1, b + 1});
    }
    SDL_RenderGeometry(v.r, nullptr, vs.data(), (int)vs.size(), idx.data(), (int)idx.size());
}

void RoundImage(const View& v, SDL_Texture* tex, const SDL_Rect* src, float x, float y, float w, float h, float radius,
                SDL_Color mod) {
    if (!tex || w <= 0 || h <= 0) return;
    int tw = 1, th = 1;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    SDL_FRect uv{0, 0, 1, 1};
    if (src) uv = {(float)src->x / tw, (float)src->y / th, (float)src->w / tw, (float)src->h / th};
    SDL_FRect rc = v.Map(x, y, w, h);
    FanFill(v, tex, rc, RoundPath(rc, radius * v.s), mod, uv);
}

void Circle(const View& v, float cx, float cy, float radius, SDL_Color c) {
    RoundRect(v, cx - radius, cy - radius, radius * 2, radius * 2, radius, c);
}

void Ring(const View& v, float cx, float cy, float radius, float t, SDL_Color c) {
    RoundFrame(v, cx - radius, cy - radius, radius * 2, radius * 2, radius, t, c);
}

void Shadow::Init(SDL_Renderer* r) {
    // White rounded rect with a soft falloff; drawn stretched with a margin.
    const int n = 128, margin = 32;
    img::Image im;
    im.w = im.h = n;
    im.rgba.resize(n * n * 4);
    const float half = n / 2.f - margin, rad = 16;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float dx = std::max(0.f, std::fabs(x + 0.5f - n / 2.f) - (half - rad));
            float dy = std::max(0.f, std::fabs(y + 0.5f - n / 2.f) - (half - rad));
            float d = std::sqrt(dx * dx + dy * dy) - rad; // signed distance to the rect
            float a = std::max(0.f, std::min(1.f, 1 - (d + 4) / (margin - 2)));
            a = a * a * (3 - 2 * a);
            uint8_t* p = &im.rgba[(y * n + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = (uint8_t)(a * 255);
        }
    tex_ = MakeTexture(r, im);
}

Shadow::~Shadow() {
    if (tex_) SDL_DestroyTexture(tex_);
}

void Shadow::Draw(const View& v, float x, float y, float w, float h, float spread, SDL_Color c) const {
    if (!tex_) return;
    // the texture's rect sits 32 px inside a 128 px image: scale the margin with it
    float mx = spread, my = spread;
    SDL_FRect rc = v.Map(x - mx, y - my, w + 2 * mx, h + 2 * my);
    SDL_SetTextureColorMod(tex_, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(tex_, c.a);
    SDL_RenderCopyF(v.r, tex_, nullptr, &rc);
}

SDL_Rect CoverCrop(int iw, int ih, float w, float h) {
    SDL_Rect src{0, 0, iw, ih};
    float ar = (float)iw / ih, box = w / h;
    if (ar > box) {
        src.w = (int)(ih * box);
        src.x = (iw - src.w) / 2;
    } else {
        src.h = (int)(iw / box);
        src.y = (ih - src.h) / 2;
    }
    return src;
}

// ---------------------------------------------------------------------------
// icons

void IconGear(const View& v, float cx, float cy, float size, SDL_Color c) {
    float r = size * 0.30f;
    for (int i = 0; i < 8; ++i) {
        float a = (float)M_PI * i / 4;
        float tx = cx + std::cos(a) * r * 1.25f, ty = cy + std::sin(a) * r * 1.25f;
        Circle(v, tx, ty, size * 0.09f, c);
    }
    Ring(v, cx, cy, r, size * 0.13f, c);
    Ring(v, cx, cy, r * 1.05f, size * 0.06f, c);
}

void IconSearch(const View& v, float cx, float cy, float size, SDL_Color c) {
    float r = size * 0.27f, t = size * 0.09f;
    float ox = cx - size * 0.08f, oy = cy - size * 0.08f;
    Ring(v, ox, oy, r, t, c);
    // handle as a rotated quad
    float a = (float)M_PI / 4, x0 = ox + std::cos(a) * (r - t / 2), y0 = oy + std::sin(a) * (r - t / 2);
    float x1 = x0 + std::cos(a) * size * 0.25f, y1 = y0 + std::sin(a) * size * 0.25f;
    float nx = -std::sin(a) * t * 0.6f, ny = std::cos(a) * t * 0.6f;
    SDL_FRect p0 = v.Map(x0 + nx, y0 + ny, 0, 0), p1 = v.Map(x1 + nx, y1 + ny, 0, 0), p2 = v.Map(x1 - nx, y1 - ny, 0, 0),
              p3 = v.Map(x0 - nx, y0 - ny, 0, 0);
    SDL_Vertex vs[4] = {Vert(p0.x, p0.y, c), Vert(p1.x, p1.y, c), Vert(p2.x, p2.y, c), Vert(p3.x, p3.y, c)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(v.r, nullptr, vs, 4, idx, 6);
}

void IconBattery(const View& v, float x, float y, float w, float h, int percent, bool charging, SDL_Color c) {
    RoundFrame(v, x, y, w, h, 3, 1.6f, c);
    FillRect(v, x + w, y + h * 0.3f, 2.2f, h * 0.4f, c);
    float fill = (w - 5) * std::max(0, std::min(100, percent)) / 100.f;
    SDL_Color fc = percent <= 15 && !charging ? Rgba(255, 90, 80) : charging ? Rgba(110, 220, 130) : c;
    RoundRect(v, x + 2.5f, y + 2.5f, fill, h - 5, 1.5f, fc);
}

void IconCheck(const View& v, float cx, float cy, float size, SDL_Color c) {
    float t = size * 0.14f;
    auto seg = [&](float x0, float y0, float x1, float y1) {
        float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        float nx = -dy / len * t / 2, ny = dx / len * t / 2;
        SDL_FRect p0 = v.Map(x0 + nx, y0 + ny, 0, 0), p1 = v.Map(x1 + nx, y1 + ny, 0, 0),
                  p2 = v.Map(x1 - nx, y1 - ny, 0, 0), p3 = v.Map(x0 - nx, y0 - ny, 0, 0);
        SDL_Vertex vs[4] = {Vert(p0.x, p0.y, c), Vert(p1.x, p1.y, c), Vert(p2.x, p2.y, c), Vert(p3.x, p3.y, c)};
        int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(v.r, nullptr, vs, 4, idx, 6);
    };
    seg(cx - size * 0.32f, cy, cx - size * 0.08f, cy + size * 0.24f);
    seg(cx - size * 0.12f, cy + size * 0.24f, cx + size * 0.34f, cy - size * 0.26f);
}

} // namespace ui
} // namespace kk4s
