// kk4s compatibility layer, part 5: the wamsoft layerEx* plugins that add pixel
// methods to Layer: layerExBtoA.dll, layerExImage.dll, layerExRaster.dll.
//
// PackinOne.dll is these plugins (and others) linked into one DLL, so linking
// PackinOne installs all three. The contracts follow the plugins' published
// sources and manuals (github.com/wamsoft/layerExBtoA, layerExImage,
// layerExRaster): same argument lists, same clip-rect rules, same pixel maths.
// Where the original has an obvious slip (an update() that is never called) kk4s
// does the useful thing; where a quirk changes results games may depend on
// (fillToProvince's match-all and returned width) the original is kept and the
// spot is commented.
//
// Differences on purpose:
//  - a layer without an image is logged once and the call does nothing; the DLLs
//    throw "dest must be Layer." (an effect must not take the game down);
//  - the random source is a fixed-seed generator instead of the CRT rand();
//  - channels are addressed in krkrs' RGBA byte order (kR/kG/kB below), and
//    colour arguments are converted the way the engine stores them.
//
// Methods are native class methods attached to the Layer class (the pattern the
// engine's own perspective.dll uses), so `this` is the layer, also for TJS
// subclasses such as KAGLayer. Each method logs its first call with the argument
// count, so a game log shows which contract it actually used.
#include "kk4s_compat.h"

#include "GraphicsLoaderIntf.h"
#include "LayerIntf.h"
#include "MsgIntf.h"
#include "ScriptMgnIntf.h"
#include "UtilStreams.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "tjsNative.h"
#include "kk4s_platform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace kk4s {
namespace compat {

namespace {

const tjs_int kLtAddAlpha = 12; // drawable.h ltAddAlpha

// krkrs keeps layer pixels as RGBA bytes (uint32 0xAABBGGRR; the SDL texture is
// ABGR8888), while the Windows DLLs address BGRA. Channel offsets inside a pixel:
const int kR = 0, kG = 1, kB = 2, kA = 3;
// TJS colour 0xAARRGGBB <-> stored pixel (same swap as the engine's TVP_REVRGB).
inline uint32_t RevRGB(uint32_t v) { return (v & 0xff00ff00u) | ((v >> 16) & 0xffu) | ((v & 0xffu) << 16); }

bool ParamExists(tjs_int n, tTJSVariant** p, tjs_int i) {
    return n > i && p[i]->Type() != tvtVoid;
}

bool GetInt(iTJSDispatch2* o, const tjs_char* name, tjs_int64& out) {
    tTJSVariant v;
    if (TJS_FAILED(o->PropGet(0, name, nullptr, &v, o))) return false;
    out = v.AsInteger();
    return true;
}

bool IsLayer(iTJSDispatch2* o) {
    return o && TJS_SUCCEEDED(o->IsInstanceOf(0, nullptr, nullptr, TJS_W("Layer"), o));
}

// Image of a layer: whole image plus its clip rect.
struct Img {
    uint8_t* bits = nullptr;
    tjs_int64 pitch = 0;
    tjs_int w = 0, h = 0;
    tjs_int cl = 0, ct = 0, cw = 0, ch = 0;
    uint8_t* row(tjs_int y) const { return bits + pitch * y; }
    uint32_t* px(tjs_int x, tjs_int y) const { return (uint32_t*)(bits + pitch * y) + x; }
};

// write = true asks for mainImageBufferForWrite (makes the buffer unique).
bool GetImg(iTJSDispatch2* o, bool write, Img& out) {
    if (!IsLayer(o)) return false;
    tjs_int64 v;
    if (!GetInt(o, TJS_W("hasImage"), v) || v == 0) return false;
    if (!GetInt(o, TJS_W("imageWidth"), v)) return false;
    out.w = (tjs_int)v;
    if (!GetInt(o, TJS_W("imageHeight"), v)) return false;
    out.h = (tjs_int)v;
    if (!GetInt(o, TJS_W("mainImageBufferPitch"), v)) return false;
    out.pitch = v;
    if (!GetInt(o, write ? TJS_W("mainImageBufferForWrite") : TJS_W("mainImageBuffer"), v)) return false;
    out.bits = (uint8_t*)(intptr_t)v;
    if (!GetInt(o, TJS_W("clipLeft"), v)) return false;
    out.cl = (tjs_int)v;
    if (!GetInt(o, TJS_W("clipTop"), v)) return false;
    out.ct = (tjs_int)v;
    if (!GetInt(o, TJS_W("clipWidth"), v)) return false;
    out.cw = (tjs_int)v;
    if (!GetInt(o, TJS_W("clipHeight"), v)) return false;
    out.ch = (tjs_int)v;
    return out.bits && out.w > 0 && out.h > 0 && out.pitch != 0;
}

struct Province {
    uint8_t* bits = nullptr;
    tjs_int64 pitch = 0;
};

bool GetProvince(iTJSDispatch2* o, bool write, Province& out) {
    tjs_int64 v;
    if (!GetInt(o, write ? TJS_W("provinceImageBufferForWrite") : TJS_W("provinceImageBuffer"), v)) return false;
    out.bits = (uint8_t*)(intptr_t)v;
    if (!GetInt(o, TJS_W("provinceImageBufferPitch"), v)) return false;
    out.pitch = v;
    return out.bits && out.pitch != 0;
}

void Update(iTJSDispatch2* o, tjs_int x, tjs_int y, tjs_int w, tjs_int h) {
    if (w <= 0 || h <= 0) return;
    tTJSVariant a[4] = {(tjs_int64)x, (tjs_int64)y, (tjs_int64)w, (tjs_int64)h};
    tTJSVariant* p[4] = {&a[0], &a[1], &a[2], &a[3]};
    o->FuncCall(0, TJS_W("update"), nullptr, nullptr, 4, p, o);
}

void NoImage(const char* method) {
    static std::vector<std::string> seen;
    if (std::find(seen.begin(), seen.end(), method) != seen.end()) return;
    seen.push_back(method);
    kk4s::Log("compat: Layer.%s on a layer without an image (or not a Layer), ignored", method);
}

std::minstd_rand& Rng() {
    static std::minstd_rand rng(0x6b6b3473u);
    return rng;
}

inline uint8_t Clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

// ---------------------------------------------------------------------------
// layerExBtoA

// The right (bottom) half's blue channel becomes the left (top) half's alpha.
// Whole image, the clip rect is ignored (as in the DLL).
tjs_error CopyRightBlueToLeftAlpha(tTJSVariant*, tjs_int, tTJSVariant**, iTJSDispatch2* lay) {
    Img d;
    if (!GetImg(lay, true, d)) { NoImage("copyRightBlueToLeftAlpha"); return TJS_S_OK; }
    tjs_int hw = d.w / 2;
    for (tjs_int y = 0; y < d.h; ++y) {
        uint8_t* r = d.row(y);
        for (tjs_int x = 0; x < hw; ++x) r[x * 4 + kA] = r[(x + hw) * 4 + kB];
    }
    Update(lay, 0, 0, hw, d.h);
    return TJS_S_OK;
}

tjs_error CopyBottomBlueToTopAlpha(tTJSVariant*, tjs_int, tTJSVariant**, iTJSDispatch2* lay) {
    Img d;
    if (!GetImg(lay, true, d)) { NoImage("copyBottomBlueToTopAlpha"); return TJS_S_OK; }
    tjs_int hh = d.h / 2;
    for (tjs_int y = 0; y < hh; ++y) {
        uint8_t* dst = d.row(y);
        const uint8_t* src = d.row(y + hh);
        for (tjs_int x = 0; x < d.w; ++x) dst[x * 4 + kA] = src[x * 4 + kB];
    }
    Update(lay, 0, 0, d.w, hh);
    return TJS_S_OK;
}

// Alpha := 255 inside the clip rect. Takes no arguments (extra ones are ignored).
tjs_error FillAlpha(tTJSVariant*, tjs_int, tTJSVariant**, iTJSDispatch2* lay) {
    Img d;
    if (!GetImg(lay, true, d)) { NoImage("fillAlpha"); return TJS_S_OK; }
    for (tjs_int y = 0; y < d.ch; ++y) {
        uint8_t* r = d.row(d.ct + y) + d.cl * 4;
        for (tjs_int x = 0; x < d.cw; ++x) r[x * 4 + 3] = 0xff;
    }
    Update(lay, d.cl, d.ct, d.cw, d.ch);
    return TJS_S_OK;
}

// copyAlphaToProvince(threshold = void, matched = 1, otherwise = 0), clip rect.
tjs_error CopyAlphaToProvince(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    tjs_int threshold = -1, matched = 1, otherwise = 0;
    if (ParamExists(n, p, 0)) threshold = (tjs_int)p[0]->AsInteger();
    if (ParamExists(n, p, 1)) matched = (tjs_int)p[1]->AsInteger();
    if (ParamExists(n, p, 2)) otherwise = (tjs_int)p[2]->AsInteger();

    Img s;
    Province pv;
    if (!GetImg(lay, false, s) || !GetProvince(lay, true, pv)) { NoImage("copyAlphaToProvince"); return TJS_S_OK; }

    int mode = 0; // 0 copy, 1 both, 2 all = otherwise, 3 only matched, 4 only otherwise
    if (threshold >= 0 && threshold < 256) {
        bool m = matched >= 0 && matched < 256, o = otherwise >= 0 && otherwise < 256;
        if (!m && !o) return TJS_S_OK;
        mode = (m && o) ? 1 : m ? 3 : 4;
    } else if (threshold >= 256) {
        if (otherwise < 0 || otherwise >= 256) return TJS_S_OK;
        mode = 2;
    }
    uint8_t th = (uint8_t)threshold, on = (uint8_t)matched, off = (uint8_t)otherwise;
    for (tjs_int y = 0; y < s.ch; ++y) {
        const uint8_t* q = s.row(s.ct + y) + s.cl * 4 + 3;
        uint8_t* d = pv.bits + pv.pitch * (s.ct + y) + s.cl;
        for (tjs_int x = 0; x < s.cw; ++x, q += 4, ++d) {
            switch (mode) {
            case 0: *d = *q; break;
            case 1: *d = *q >= th ? on : off; break;
            case 2: *d = off; break;
            case 3: if (*q >= th) *d = on; break;
            case 4: if (*q < th) *d = off; break;
            }
        }
    }
    Update(lay, s.cl, s.ct, s.cw, s.ch);
    return TJS_S_OK;
}

// Clip a copy rect against source and destination sizes (layerExBtoA CalcClipArea).
// Returns false when nothing is left.
bool ClipRect(tjs_int& dx, tjs_int& dy, tjs_int dw, tjs_int dh,
              tjs_int& sx, tjs_int& sy, tjs_int sw, tjs_int sh, tjs_int& w, tjs_int& h) {
    if (sx + w <= 0 || sy + h <= 0 || sx >= sw || sy >= sh) return false;
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    tjs_int cut;
    if ((cut = sx + w - sw) > 0) w -= cut;
    if ((cut = sy + h - sh) > 0) h -= cut;
    if (dx + w <= 0 || dy + h <= 0 || dx >= dw || dy >= dh) return false;
    if (dx < 0) { w += dx; sx -= dx; dx = 0; }
    if (dy < 0) { h += dy; sy -= dy; dy = 0; }
    if ((cut = dx + w - dw) > 0) w -= cut;
    if ((cut = dy + h - dh) > 0) h -= cut;
    return w > 0 && h > 0;
}

// Scale all four channels by a (premultiplied layers, ltAddAlpha).
inline uint32_t ScaleAll(uint32_t px, uint32_t a) {
    if (a == 255) return px;
    if (a == 0) return 0;
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t c = ((px >> sh) & 0xff) * a;
        out |= ((c + (c >> 7)) >> 8) << sh;
    }
    return out;
}

inline uint8_t MulAlpha(uint8_t a, uint8_t b) {
    uint32_t n = (uint32_t)a * b;
    return (uint8_t)((n + (n >> 7)) >> 8);
}

// clipAlphaRect(dleft, dtop, src, sleft, stop, swidth, sheight, clear = void):
// inside the destination clip rect, multiply this layer's alpha by src's alpha
// over the copied rect; if `clear` is 0..255, alpha outside that rect (still
// inside the clip rect) becomes `clear`. On ltAddAlpha layers the whole
// premultiplied pixel is scaled instead of the alpha byte.
tjs_error ClipAlphaRect(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 7) return TJS_E_BADPARAMCOUNT;
    tjs_int dx = (tjs_int)p[0]->AsInteger(), dy = (tjs_int)p[1]->AsInteger();
    iTJSDispatch2* src = p[2]->AsObjectNoAddRef();
    tjs_int sx = (tjs_int)p[3]->AsInteger(), sy = (tjs_int)p[4]->AsInteger();
    tjs_int w = (tjs_int)p[5]->AsInteger(), h = (tjs_int)p[6]->AsInteger();
    bool clr = false;
    uint8_t clrval = 0;
    if (ParamExists(n, p, 7)) {
        tjs_int64 c = p[7]->AsInteger();
        clr = c >= 0 && c < 256;
        clrval = (uint8_t)(c & 255);
    }
    if (w <= 0 || h <= 0) return TJS_E_INVALIDPARAM;

    // Destination first: if src is this same layer, its read pointer must be the
    // buffer that the write request may have just made unique.
    Img d, s;
    if (!GetImg(lay, true, d) || !GetImg(src, false, s)) { NoImage("clipAlphaRect"); return TJS_S_OK; }
    tjs_int64 type = 0;
    GetInt(lay, TJS_W("type"), type);
    bool addalpha = type == kLtAddAlpha;

    // Work in clip-rect coordinates.
    const tjs_int dl = d.cl, dt = d.ct, diw = d.cw, dih = d.ch;
    dx -= dl;
    dy -= dt;
    bool any = ClipRect(dx, dy, diw, dih, sx, sy, s.w, s.h, w, h);
    auto dpx = [&](tjs_int x, tjs_int y) { return d.px(dl + x, dt + y); };
    auto clear_px = [&](tjs_int x, tjs_int y) {
        uint32_t* q = dpx(x, y);
        if (addalpha) *q = ScaleAll(*q, clrval);
        else ((uint8_t*)q)[kA] = clrval;
    };
    // ltAddAlpha with clear == 255 keeps the outside as is (the DLL skips it).
    bool clear_outside = clr && !(addalpha && clrval == 255);

    if (!any) {
        if (clr) { // the DLL writes the alpha byte here, also for ltAddAlpha
            for (tjs_int y = 0; y < dih; ++y)
                for (tjs_int x = 0; x < diw; ++x) ((uint8_t*)dpx(x, y))[kA] = clrval;
            Update(lay, dl, dt, diw, dih);
        }
        return TJS_S_OK;
    }

    if (clear_outside) {
        for (tjs_int y = 0; y < dih; ++y) {
            bool row_in = y >= dy && y < dy + h;
            for (tjs_int x = 0; x < diw; ++x)
                if (!row_in || x < dx || x >= dx + w) clear_px(x, y);
        }
    }
    for (tjs_int y = 0; y < h; ++y) {
        uint32_t* q = dpx(dx, dy + y);
        const uint8_t* a = (const uint8_t*)s.px(sx, sy + y) + 3;
        for (tjs_int x = 0; x < w; ++x, ++q, a += 4) {
            if (addalpha) *q = ScaleAll(*q, *a);
            else ((uint8_t*)q)[kA] = MulAlpha(((uint8_t*)q)[kA], *a);
        }
    }
    if (clr) Update(lay, dl, dt, diw, dih);
    else Update(lay, dl + dx, dt + dy, w, h);
    return TJS_S_OK;
}

// overwrapRect(dleft, dtop, src, sleft, stop, swidth, sheight, threshold = 1):
// copy src pixels whose alpha >= threshold. (The DLL forgets the update() call;
// kk4s updates the copied rect so the change shows.)
tjs_error OverwrapRect(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 7) return TJS_E_BADPARAMCOUNT;
    tjs_int dx = (tjs_int)p[0]->AsInteger(), dy = (tjs_int)p[1]->AsInteger();
    iTJSDispatch2* src = p[2]->AsObjectNoAddRef();
    tjs_int sx = (tjs_int)p[3]->AsInteger(), sy = (tjs_int)p[4]->AsInteger();
    tjs_int w = (tjs_int)p[5]->AsInteger(), h = (tjs_int)p[6]->AsInteger();
    uint8_t threshold = 1;
    if (ParamExists(n, p, 7)) {
        tjs_int64 t = p[7]->AsInteger();
        if (t >= 0 && t < 256) threshold = (uint8_t)t;
    }
    if (w <= 0 || h <= 0) return TJS_E_INVALIDPARAM;
    Img d, s;
    if (!GetImg(lay, true, d) || !GetImg(src, false, s)) { NoImage("overwrapRect"); return TJS_S_OK; }
    dx -= d.cl;
    dy -= d.ct;
    if (!ClipRect(dx, dy, d.cw, d.ch, sx, sy, s.w, s.h, w, h)) return TJS_S_OK;
    for (tjs_int y = 0; y < h; ++y) {
        uint32_t* q = d.px(d.cl + dx, d.ct + dy + y);
        const uint32_t* r = s.px(sx, sy + y);
        for (tjs_int x = 0; x < w; ++x)
            if ((r[x] >> 24) >= threshold) q[x] = r[x];
    }
    Update(lay, d.cl + dx, d.ct + dy, w, h);
    return TJS_S_OK;
}

// fillByProvince(index, color): pixels whose province value is index get color
// (0xAARRGGBB), clip rect.
tjs_error FillByProvince(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 2) return TJS_E_BADPARAMCOUNT;
    uint8_t index = (uint8_t)p[0]->AsInteger();
    uint32_t color = RevRGB((uint32_t)p[1]->AsInteger());
    Img d;
    Province pv;
    if (!GetImg(lay, true, d) || !GetProvince(lay, false, pv)) { NoImage("fillByProvince"); return TJS_S_OK; }
    for (tjs_int y = 0; y < d.ch; ++y) {
        const uint8_t* q = pv.bits + pv.pitch * (d.ct + y) + d.cl;
        uint32_t* r = d.px(d.cl, d.ct + y);
        for (tjs_int x = 0; x < d.cw; ++x)
            if (q[x] == index) r[x] = color;
    }
    Update(lay, d.cl, d.ct, d.cw, d.ch);
    return TJS_S_OK;
}

// fillToProvince(color, index, threshold = 64): pixels of color (0xRRGGBB) with
// alpha >= threshold set the province to index; returns [x, y, w, h] of the
// matches or void. Kept as in the DLL: a negative color does not match
// everything (its "all match" test is on an unsigned value, so it never fires),
// and the returned width/height add clipLeft/clipTop once too often.
tjs_error FillToProvince(tTJSVariant* result, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 2) return TJS_E_BADPARAMCOUNT;
    uint32_t color = (uint32_t)p[0]->AsInteger() & 0xffffffu;
    uint8_t index = (uint8_t)p[1]->AsInteger();
    tjs_int threshold = ParamExists(n, p, 2) ? (tjs_int)p[2]->AsInteger() : 64;
    Img s;
    Province pv;
    if (!GetImg(lay, false, s) || !GetProvince(lay, true, pv)) { NoImage("fillToProvince"); return TJS_S_OK; }
    tjs_int minx = -1, miny = -1, maxx = -1, maxy = -1;
    for (tjs_int y = 0; y < s.ch; ++y) {
        const uint32_t* q = s.px(s.cl, s.ct + y);
        uint8_t* d = pv.bits + pv.pitch * (s.ct + y) + s.cl;
        for (tjs_int x = 0; x < s.cw; ++x) {
            if ((RevRGB(q[x]) & 0xffffffu) == color && (tjs_int)(q[x] >> 24) >= threshold) {
                d[x] = index;
                if (minx < 0 || x < minx) minx = x;
                if (miny < 0 || y < miny) miny = y;
                if (x > maxx) maxx = x;
                if (y > maxy) maxy = y;
            }
        }
    }
    if (result && minx >= 0) {
        iTJSDispatch2* arr = TJSCreateArrayObject();
        tTJSVariant v;
        v = (tjs_int64)(minx + s.cl);              arr->PropSetByNum(TJS_MEMBERENSURE, 0, &v, arr);
        v = (tjs_int64)(miny + s.ct);              arr->PropSetByNum(TJS_MEMBERENSURE, 1, &v, arr);
        v = (tjs_int64)(maxx + s.cl - minx + 1);   arr->PropSetByNum(TJS_MEMBERENSURE, 2, &v, arr);
        v = (tjs_int64)(maxy + s.ct - miny + 1);   arr->PropSetByNum(TJS_MEMBERENSURE, 3, &v, arr);
        *result = tTJSVariant(arr, arr);
        arr->Release();
    }
    Update(lay, s.cl, s.ct, s.cw, s.ch);
    return TJS_S_OK;
}

// ---------------------------------------------------------------------------
// layerExImage: colour effects on the clip rect (CxImage-derived maths).

template <class F>
void ForEachClipPixel(const Img& d, F f) {
    for (tjs_int y = 0; y < d.ch; ++y) {
        uint8_t* r = d.row(d.ct + y) + d.cl * 4;
        for (tjs_int x = 0; x < d.cw; ++x) f(r + x * 4); // index with kR/kG/kB/kA
    }
}

// light(brightness -255..255, contrast -100..100)
tjs_error Light(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 2) return TJS_E_BADPARAMCOUNT;
    int brightness = (int)p[0]->AsInteger() + 128;
    float c = (100 + (int)p[1]->AsInteger()) / 100.0f;
    Img d;
    if (!GetImg(lay, true, d)) { NoImage("light"); return TJS_S_OK; }
    uint8_t lut[256];
    for (int i = 0; i < 256; ++i) lut[i] = Clamp8((int)((i - 128) * c + brightness));
    ForEachClipPixel(d, [&](uint8_t* q) { q[kR] = lut[q[kR]]; q[kG] = lut[q[kG]]; q[kB] = lut[q[kB]]; });
    Update(lay, d.cl, d.ct, d.cw, d.ch);
    return TJS_S_OK;
}

float HueToRgb(float n1, float n2, float hue) {
    if (hue > 360) hue -= 360;
    else if (hue < 0) hue += 360;
    if (hue < 60) return n1 + (n2 - n1) * hue / 60.0f;
    if (hue < 180) return n2;
    if (hue < 240) return n1 + (n2 - n1) * (240 - hue) / 60;
    return n1;
}

// colorize(hue 0..255, sat 0..255, blend 0..1): keep lightness, set hue and
// saturation (CxImage HSL, 0..255 scale), blended with the original colour.
tjs_error Colorize(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 3) return TJS_E_BADPARAMCOUNT;
    uint8_t hue = (uint8_t)p[0]->AsInteger(), sat = (uint8_t)p[1]->AsInteger();
    double blend = p[2]->AsReal();
    if (blend < 0) blend = 0;
    if (blend > 1) blend = 1;
    int a0 = (int)(256 * blend), a1 = 256 - a0;
    bool full = blend > 0.999f;
    Img d;
    if (!GetImg(lay, true, d)) { NoImage("colorize"); return TJS_S_OK; }
    float h = hue * 360.0f / 255.0f, s = sat / 255.0f;
    ForEachClipPixel(d, [&](uint8_t* q) {
        int b = q[kB], g = q[kG], r = q[kR];
        int cmax = std::max(std::max(r, g), b), cmin = std::min(std::min(r, g), b);
        uint8_t L = (uint8_t)((((cmax + cmin) * 255) + 255) / (2 * 255));
        float l = L / 255.0f;
        float m2 = l <= 0.5f ? l * (1 + s) : l + s - l * s;
        float m1 = 2 * l - m2;
        uint8_t nr, ng, nb;
        if (sat == 0) {
            nr = ng = nb = (uint8_t)(l * 255.0f);
        } else {
            nr = (uint8_t)(HueToRgb(m1, m2, h + 120) * 255.0f);
            ng = (uint8_t)(HueToRgb(m1, m2, h) * 255.0f);
            nb = (uint8_t)(HueToRgb(m1, m2, h - 120) * 255.0f);
        }
        if (full) {
            q[kB] = nb; q[kG] = ng; q[kR] = nr;
        } else {
            q[kB] = (uint8_t)((nb * a0 + b * a1) >> 8);
            q[kG] = (uint8_t)((ng * a0 + g * a1) >> 8);
            q[kR] = (uint8_t)((nr * a0 + r * a1) >> 8);
        }
    });
    Update(lay, d.cl, d.ct, d.cw, d.ch);
    return TJS_S_OK;
}

int Hue2Rgb(double n1, double n2, double hue) {
    if (hue < 0) hue += 1.0;
    else if (hue > 1.0) hue -= 1.0;
    double c;
    if (hue < 1.0 / 6.0) c = n1 + (n2 - n1) * hue * 6.0;
    else if (hue < 1.0 / 2.0) c = n2;
    else if (hue < 2.0 / 3.0) c = n1 + (n2 - n1) * (2.0 / 3.0 - hue) * 6.0;
    else c = n1;
    return (int)(c * 255.0);
}

// modulate(hue -180..180 degrees, saturation -100..100 %, luminance -100..100 %)
tjs_error Modulate(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 3) return TJS_E_BADPARAMCOUNT;
    double dh = (int)p[0]->AsInteger() / 360.0, ds = (int)p[1]->AsInteger() / 100.0,
           dl = (int)p[2]->AsInteger() / 100.0;
    Img d;
    if (!GetImg(lay, true, d)) { NoImage("modulate"); return TJS_S_OK; }
    ForEachClipPixel(d, [&](uint8_t* q) {
        double r = q[kR] / 255.0, g = q[kG] / 255.0, b = q[kB] / 255.0;
        double cmax = std::max(std::max(r, g), b), cmin = std::min(std::min(r, g), b);
        double delta = cmax - cmin, add = cmax + cmin;
        double lum = add / 2.0, hue = 0, sat = 0;
        if (delta != 0) {
            sat = lum < 0.5 ? delta / add : delta / (2.0 - add);
            if (r == cmax) hue = (g - b) / delta;
            else if (g == cmax) hue = 2.0 + (b - r) / delta;
            else hue = 4.0 + (r - g) / delta;
            hue /= 6.0;
        }
        hue += dh;
        while (hue < 0) hue += 1.0;
        while (hue > 1.0) hue -= 1.0;
        sat += ds > 0 ? (1.0 - sat) * ds : sat * ds;
        lum += dl > 0 ? (1.0 - lum) * dl : lum * dl;
        int nr, ng, nb;
        if (sat == 0.0) {
            nr = ng = nb = (int)(lum * 255.0);
        } else {
            double m2 = lum <= 0.5 ? lum * (1 + sat) : lum + sat - lum * sat;
            double m1 = 2.0 * lum - m2;
            nr = Hue2Rgb(m1, m2, hue + 1.0 / 3.0);
            ng = Hue2Rgb(m1, m2, hue);
            nb = Hue2Rgb(m1, m2, hue - 1.0 / 3.0);
        }
        q[kB] = Clamp8(nb); q[kG] = Clamp8(ng); q[kR] = Clamp8(nr);
    });
    Update(lay, d.cl, d.ct, d.cw, d.ch);
    return TJS_S_OK;
}

// noise(level 0..255): add +-level/2 random noise per colour channel.
tjs_error Noise(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 1) return TJS_E_BADPARAMCOUNT;
    int level = (int)p[0]->AsInteger();
    Img d;
    if (!GetImg(lay, true, d)) { NoImage("noise"); return TJS_S_OK; }
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    auto& rng = Rng();
    ForEachClipPixel(d, [&](uint8_t* q) {
        for (int c : {kB, kG, kR}) q[c] = Clamp8(q[c] + (int)((u(rng) - 0.5f) * level));
    });
    Update(lay, d.cl, d.ct, d.cw, d.ch);
    return TJS_S_OK;
}

// generateWhiteNoise(): grey white noise over the colour, alpha kept.
tjs_error GenerateWhiteNoise(tTJSVariant*, tjs_int, tTJSVariant**, iTJSDispatch2* lay) {
    Img d;
    if (!GetImg(lay, true, d)) { NoImage("generateWhiteNoise"); return TJS_S_OK; }
    auto& rng = Rng();
    ForEachClipPixel(d, [&](uint8_t* q) {
        uint8_t v = (uint8_t)(rng() >> 7);
        q[kR] = q[kG] = q[kB] = v;
    });
    Update(lay, d.cl, d.ct, d.cw, d.ch);
    return TJS_S_OK;
}

// Kernel of the CxImage/GIMP gaussian blur: standard deviation |r/2| + 0.25,
// support 2 * sd, each tap the mean of 50 samples of the gaussian over the
// pixel; normalised to 1.
std::vector<float> GaussKernel(float radius) {
    float sd = std::fabs(0.5f * radius) + 0.25f;
    float r = sd * 2;
    int len = (int)(2 * std::ceil(r - 0.5f) + 1);
    if (len <= 0) len = 1;
    std::vector<float> k(len);
    int mid = len / 2;
    for (int i = mid + 1; i < len; ++i) {
        float base = i - (float)mid - 0.5f, sum = 0;
        for (int j = 1; j <= 50; ++j) {
            float x = base + 0.02f * j;
            if (x <= r) sum += std::exp(-x * x / (2 * sd * sd));
        }
        k[i] = sum / 50;
    }
    for (int i = 0; i < mid; ++i) k[i] = k[len - 1 - i];
    float sum = 0;
    for (int j = 0; j <= 50; ++j) {
        float x = 0.5f + 0.02f * j;
        sum += std::exp(-x * x / (2 * sd * sd));
    }
    k[mid] = sum / 51;
    float total = 0;
    for (float v : k) total += v;
    for (float& v : k) v /= total;
    return k;
}

// Blur `count` pixels (4 bytes each; `in_stride` / `out_stride` bytes apart).
// Taps outside the line are dropped and the rest renormalised (as CxImage does).
void BlurLine(const std::vector<float>& k, const uint8_t* in, tjs_int64 in_stride,
              uint8_t* out, tjs_int64 out_stride, tjs_int count) {
    int len = (int)k.size(), mid = len / 2;
    for (tjs_int i = 0; i < count; ++i) {
        float acc[4] = {0, 0, 0, 0}, wsum = 0;
        for (int t = 0; t < len; ++t) {
            tjs_int j = i + t - mid;
            if (j < 0 || j >= count) continue;
            const uint8_t* q = in + in_stride * j;
            for (int c = 0; c < 4; ++c) acc[c] += k[t] * q[c];
            wsum += k[t];
        }
        uint8_t* o = out + out_stride * i;
        for (int c = 0; c < 4; ++c) o[c] = (uint8_t)(0.5f + acc[c] / wsum);
    }
}

// gaussianBlur(radius = 1.0): all four channels, clip rect.
tjs_error GaussianBlur(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    float radius = ParamExists(n, p, 0) ? (float)p[0]->AsReal() : 1.0f;
    Img d;
    if (!GetImg(lay, true, d)) { NoImage("gaussianBlur"); return TJS_S_OK; }
    if (d.cw <= 0 || d.ch <= 0) return TJS_S_OK;
    std::vector<float> k = GaussKernel(radius);
    tjs_int64 tp = (tjs_int64)d.cw * 4;
    std::vector<uint8_t> tmp((size_t)(tp * d.ch));
    for (tjs_int y = 0; y < d.ch; ++y)
        BlurLine(k, d.row(d.ct + y) + d.cl * 4, 4, tmp.data() + tp * y, 4, d.cw);
    std::vector<uint8_t> col((size_t)d.ch * 4);
    for (tjs_int x = 0; x < d.cw; ++x) {
        BlurLine(k, tmp.data() + x * 4, tp, col.data(), 4, d.ch);
        for (tjs_int y = 0; y < d.ch; ++y)
            std::copy_n(&col[(size_t)y * 4], 4, d.row(d.ct + y) + (d.cl + x) * 4);
    }
    Update(lay, d.cl, d.ct, d.cw, d.ch);
    return TJS_S_OK;
}

// ---------------------------------------------------------------------------
// layerExRaster

// copyRaster(src, maxh, lines, cycle, time): copy src (same image size) into the
// clip rect with every line shifted horizontally by sin(phase) * maxh, `lines`
// lines per wave period, the wave moving with time / cycle. Pixels a shift
// uncovers keep their previous value (as in the DLL).
tjs_error CopyRaster(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 5) return TJS_E_BADPARAMCOUNT;
    iTJSDispatch2* src = p[0]->AsObjectNoAddRef();
    tjs_int maxh = (tjs_int)p[1]->AsInteger();
    tjs_int lines = (tjs_int)p[2]->AsInteger();
    tjs_int cycle = (tjs_int)p[3]->AsInteger();
    tjs_int64 time = p[4]->AsInteger();
    Img d, s;
    if (!GetImg(lay, true, d) || !GetImg(src, false, s)) { NoImage("copyRaster"); return TJS_S_OK; }
    if (d.w != s.w || d.h != s.h) return TJS_S_OK; // the DLL also does nothing
    if (lines == 0) lines = 1;                       // guards against a division by zero
    if (cycle == 0) cycle = 1;
    const double kPi = 3.14159265358979323846;
    double omega = 2 * kPi / lines;
    double rad = -omega * (double)time / cycle * (d.h / 2) + omega * d.ct;
    for (tjs_int y = 0; y < d.ch; ++y, rad += omega) {
        tjs_int off = (tjs_int)(std::sin(rad) * maxh);
        const uint32_t* from = s.px(d.cl, d.ct + y);
        uint32_t* to = d.px(d.cl, d.ct + y);
        tjs_int w = d.cw - (off >= 0 ? off : -off);
        if (w <= 0) continue;
        if (off >= 0) std::copy_n(from, w, to + off);
        else std::copy_n(from - off, w, to);
    }
    Update(lay, d.cl, d.ct, d.cw, d.ch);
    return TJS_S_OK;
}

// ---------------------------------------------------------------------------
// layerExSave: image saving and the analysis helpers that ship with it
// (wamsoft/layerExSave). Saving goes through the engine's own PNG / TLG
// writers (they know krkrs' RGBA order and write through the storage layer,
// so the save-folder redirect applies).

tTVPBaseTexture* MainImageOf(iTJSDispatch2* lay) {
    tTJSNI_Layer* ni = nullptr;
    if (!lay || TJS_FAILED(lay->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                                      (iTJSNativeInstance**)&ni)) || !ni)
        return nullptr;
    return ni->GetMainImage();
}

iTJSDispatch2* DictParam(tjs_int n, tTJSVariant** p, tjs_int i) {
    return ParamExists(n, p, i) && p[i]->Type() == tvtObject ? p[i]->AsObjectNoAddRef() : nullptr;
}

tjs_error SaveAs(const tjs_char* mode, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 1) return TJS_E_BADPARAMCOUNT;
    tTVPBaseTexture* img = MainImageOf(lay);
    if (!img) TVPThrowExceptionMessage(TJS_W("Invalid layer image."));
    TVPSaveImage(*p[0], mode, img, DictParam(n, p, 1));
    return TJS_S_OK;
}

// saveLayerImagePng(filename, tags = void). The engine writer ignores the tags and
// comp_lv (PNG text chunks are not written); the image itself is exact.
tjs_error SaveLayerImagePng(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    return SaveAs(TJS_W("png"), n, p, lay);
}

// saveLayerImageTlg5(filename, tags = void): tags go into the TLG "tags" chunk.
tjs_error SaveLayerImageTlg5(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    return SaveAs(TJS_W("tlg5"), n, p, lay);
}

// saveLayerImagePngOctet(level_or_tags = 1) -> octet with the PNG file
tjs_error SaveLayerImagePngOctet(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    tTVPBaseTexture* img = MainImageOf(lay);
    if (!img) TVPThrowExceptionMessage(TJS_W("Invalid layer image."));
    tTVPMemoryStream mem;
    TVPSaveAsPNG(nullptr, &mem, img, TJS_W("png"), DictParam(n, p, 0));
    if (r) *r = tTJSVariant((const tjs_uint8*)mem.GetInternalBuffer(), (tjs_uint)mem.GetSize());
    return TJS_S_OK;
}

void RectDict(tTJSVariant* r, tjs_int x, tjs_int y, tjs_int w, tjs_int h) {
    if (!r) return;
    iTJSDispatch2* d = TJSCreateDictionaryObject();
    tTJSVariant v;
    v = (tjs_int64)x; d->PropSet(TJS_MEMBERENSURE, TJS_W("x"), nullptr, &v, d);
    v = (tjs_int64)y; d->PropSet(TJS_MEMBERENSURE, TJS_W("y"), nullptr, &v, d);
    v = (tjs_int64)w; d->PropSet(TJS_MEMBERENSURE, TJS_W("w"), nullptr, &v, d);
    v = (tjs_int64)h; d->PropSet(TJS_MEMBERENSURE, TJS_W("h"), nullptr, &v, d);
    *r = tTJSVariant(d, d);
    d->Release();
}

// Bounding box of the pixels for which keep(pixel) is true; void if none.
template <class Keep>
void BoundingBox(const Img& s, tTJSVariant* r, Keep keep) {
    tjs_int x1 = s.w, y1 = s.h, x2 = -1, y2 = -1;
    for (tjs_int y = 0; y < s.h; ++y) {
        const uint32_t* row = s.px(0, y);
        for (tjs_int x = 0; x < s.w; ++x)
            if (keep(row[x], x, y)) {
                if (x < x1) x1 = x;
                if (x > x2) x2 = x;
                if (y < y1) y1 = y;
                if (y > y2) y2 = y;
            }
    }
    if (r) r->Clear();
    if (x2 >= 0) RectDict(r, x1, y1, x2 - x1 + 1, y2 - y1 + 1);
}

// getCropRect(): box of the pixels with alpha != 0 -> %[x, y, w, h] or void
tjs_error GetCropRect(tTJSVariant* r, tjs_int, tTJSVariant**, iTJSDispatch2* lay) {
    Img s;
    if (!GetImg(lay, false, s)) TVPThrowExceptionMessage(TJS_W("Invalid layer image."));
    BoundingBox(s, r, [](uint32_t c, tjs_int, tjs_int) { return (c >> 24) != 0; });
    return TJS_S_OK;
}

// getCropRectZero(): box of the pixels that are not 0x00000000
tjs_error GetCropRectZero(tTJSVariant* r, tjs_int, tTJSVariant**, iTJSDispatch2* lay) {
    Img s;
    if (!GetImg(lay, false, s)) TVPThrowExceptionMessage(TJS_W("Invalid layer image."));
    BoundingBox(s, r, [](uint32_t c, tjs_int, tjs_int) { return c != 0; });
    return TJS_S_OK;
}

// Same colour as layerExSave compares: equal alpha, and equal colour unless transparent.
inline bool SameColor(uint32_t a, uint32_t b) {
    return (a >> 24) == (b >> 24) && ((a >> 24) == 0 || (a & 0xffffffu) == (b & 0xffffffu));
}

bool SameSizePair(iTJSDispatch2* lay, bool write, tjs_int n, tTJSVariant** p, Img& t, Img& b) {
    if (n < 1) return false;
    if (!GetImg(lay, write, t) || !GetImg(p[0]->AsObjectNoAddRef(), false, b))
        TVPThrowExceptionMessage(TJS_W("Invalid layer image."));
    if (t.w != b.w || t.h != b.h) TVPThrowExceptionMessage(TJS_W("Different layer size."));
    return true;
}

// getDiffRect(base): box of the pixels that differ from base -> dict or void
tjs_error GetDiffRect(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    Img t, b;
    if (!SameSizePair(lay, false, n, p, t, b)) return TJS_E_BADPARAMCOUNT;
    BoundingBox(t, r, [&](uint32_t c, tjs_int x, tjs_int y) { return !SameColor(c, *b.px(x, y)); });
    return TJS_S_OK;
}

// getDiffPixel(base, samecol = void, diffcol = void) -> number of differing pixels,
// optionally painting same / different pixels (0xAARRGGBB)
tjs_error GetDiffPixel(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    Img t, b;
    if (!SameSizePair(lay, true, n, p, t, b)) return TJS_E_BADPARAMCOUNT;
    bool sfill = ParamExists(n, p, 1), dfill = ParamExists(n, p, 2);
    uint32_t scol = sfill ? RevRGB((uint32_t)p[1]->AsInteger()) : 0;
    uint32_t dcol = dfill ? RevRGB((uint32_t)p[2]->AsInteger()) : 0;
    tjs_int64 count = 0;
    for (tjs_int y = 0; y < t.h; ++y) {
        uint32_t* q = t.px(0, y);
        const uint32_t* o = b.px(0, y);
        for (tjs_int x = 0; x < t.w; ++x) {
            if (SameColor(q[x], o[x])) { if (sfill) q[x] = scol; }
            else { if (dfill) q[x] = dcol; ++count; }
        }
    }
    if (sfill || dfill) Update(lay, 0, 0, t.w, t.h);
    if (r) *r = count;
    return TJS_S_OK;
}

// oozeColor(level, threshold = 1, fillColor = 0): spread the colour of opaque pixels
// into transparent neighbours `level` times (keeps shrunk images free of dark fringes).
tjs_error OozeColor(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 1) return TJS_E_BADPARAMCOUNT;
    int level = (int)p[0]->AsInteger();
    if (level <= 0) TVPThrowExceptionMessage(TJS_W("Invalid level count."));
    int threshold = n > 1 ? (int)(uint8_t)p[1]->AsInteger() : 1;
    if (threshold < 1) threshold = 1;
    uint32_t fill = n > 2 ? (uint32_t)p[2]->AsInteger() : 0;
    uint8_t fr = (uint8_t)(fill >> 16), fg = (uint8_t)(fill >> 8), fb = (uint8_t)fill;
    Img d;
    if (!GetImg(lay, true, d)) TVPThrowExceptionMessage(TJS_W("Invalid layer image."));
    const tjs_int ow = d.w + 2;
    std::vector<signed char> map((size_t)ow * (d.h + 2), 0); // -1 done, 1 filled this pass
    auto at = [&](tjs_int x, tjs_int y) -> signed char& { return map[(size_t)(y + 1) * ow + x + 1]; };
    for (tjs_int y = 0; y < d.h; ++y)
        for (tjs_int x = 0; x < d.w; ++x) {
            uint8_t* q = (uint8_t*)d.px(x, y);
            if (q[kA] >= threshold) at(x, y) = -1;
            else { q[kR] = fr; q[kG] = fg; q[kB] = fb; }
        }
    for (int i = 0; i < level; ++i) {
        for (tjs_int y = 0; y < d.h; ++y)
            for (tjs_int x = 0; x < d.w; ++x) {
                if (at(x, y)) continue;
                unsigned cr = 0, cg = 0, cb = 0, cnt = 0;
                auto add = [&](tjs_int xx, tjs_int yy) {
                    if (at(xx, yy) >= 0) return;
                    const uint8_t* c = (const uint8_t*)d.px(xx, yy);
                    cr += c[kR]; cg += c[kG]; cb += c[kB]; ++cnt;
                };
                add(x, y - 1); add(x, y + 1); add(x - 1, y); add(x + 1, y);
                if (!cnt) continue;
                uint8_t* q = (uint8_t*)d.px(x, y);
                q[kR] = (uint8_t)(cr / cnt); q[kG] = (uint8_t)(cg / cnt); q[kB] = (uint8_t)(cb / cnt);
                at(x, y) = 1;
            }
        for (signed char& m : map) if (m > 0) m = -1;
    }
    Update(lay, 0, 0, d.w, d.h);
    return TJS_S_OK;
}

// copyBlueToAlpha(src): src's blue channel becomes this layer's alpha (common size)
tjs_error CopyBlueToAlpha(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 1) return TJS_E_BADPARAMCOUNT;
    Img d, s;
    if (!GetImg(lay, true, d) || !GetImg(p[0]->AsObjectNoAddRef(), false, s))
        TVPThrowExceptionMessage(TJS_W("src must be Layer."));
    tjs_int w = std::min(d.w, s.w), h = std::min(d.h, s.h);
    for (tjs_int y = 0; y < h; ++y) {
        uint8_t* q = (uint8_t*)d.px(0, y);
        const uint8_t* b = (const uint8_t*)s.px(0, y);
        for (tjs_int x = 0; x < w; ++x) q[x * 4 + kA] = b[x * 4 + kB];
    }
    Update(lay, 0, 0, w, h);
    return TJS_S_OK;
}

// isBlank(x, y, w, h): true if the rect has no non-zero byte where the DLL looks
// (the first byte of each pixel on Windows, which is blue)
tjs_error IsBlank(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 4) return TJS_E_BADPARAMCOUNT;
    Img s;
    if (!GetImg(lay, false, s)) TVPThrowExceptionMessage(TJS_W("src must be Layer."));
    tjs_int l = (tjs_int)p[0]->AsInteger(), t = (tjs_int)p[1]->AsInteger();
    tjs_int w = (tjs_int)p[2]->AsInteger(), h = (tjs_int)p[3]->AsInteger();
    if (l < 0) { w += l; l = 0; }
    if (t < 0) { h += t; t = 0; }
    if (l + w > s.w) w = s.w - l;
    if (t + h > s.h) h = s.h - t;
    bool blank = true;
    for (tjs_int y = t; blank && y < t + h; ++y)
        for (tjs_int x = l; x < l + w; ++x)
            if (((const uint8_t*)s.px(x, y))[kB]) { blank = false; break; }
    if (r) *r = (tjs_int)blank;
    return TJS_S_OK;
}

// clearAlpha(threshold = 0, fillColor = 0): pixels with alpha <= threshold become
// fillColor (0xRRGGBB) with alpha 0
tjs_error ClearAlpha(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    int threshold = n > 0 ? (int)p[0]->AsInteger() : 0;
    uint32_t fill = RevRGB((uint32_t)(n > 1 ? p[1]->AsInteger() : 0) & 0xffffffu);
    Img d;
    if (!GetImg(lay, true, d)) TVPThrowExceptionMessage(TJS_W("dest must be Layer."));
    for (tjs_int y = 0; y < d.h; ++y) {
        uint32_t* q = d.px(0, y);
        for (tjs_int x = 0; x < d.w; ++x)
            if ((int)(q[x] >> 24) <= threshold) q[x] = fill;
    }
    Update(lay, 0, 0, d.w, d.h);
    return TJS_S_OK;
}

// getAverageColor(x, y, w, h). Kept as the DLL computes it: it averages the bytes
// of each Windows pixel (B, G, R, A) and packs them as (B << 24 | G << 16 | R << 8 | A).
tjs_error GetAverageColor(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 4) return TJS_E_BADPARAMCOUNT;
    Img s;
    if (!GetImg(lay, false, s)) TVPThrowExceptionMessage(TJS_W("src must be Layer."));
    tjs_int l = (tjs_int)p[0]->AsInteger(), t = (tjs_int)p[1]->AsInteger();
    tjs_int w = (tjs_int)p[2]->AsInteger(), h = (tjs_int)p[3]->AsInteger();
    if (l < 0) { w += l; l = 0; }
    if (t < 0) { h += t; t = 0; }
    if (l + w > s.w) w = s.w - l;
    if (t + h > s.h) h = s.h - t;
    if (w <= 0 || h <= 0) TVPThrowExceptionMessage(TJS_W("invalid layer range"));
    double sum[4] = {0, 0, 0, 0}; // Windows byte order: B, G, R, A
    for (tjs_int y = t; y < t + h; ++y)
        for (tjs_int x = l; x < l + w; ++x) {
            const uint8_t* q = (const uint8_t*)s.px(x, y);
            sum[0] += q[kB]; sum[1] += q[kG]; sum[2] += q[kR]; sum[3] += q[kA];
        }
    double size = (double)w * h;
    uint32_t c = 0;
    for (int i = 0; i < 4; ++i) c |= ((uint32_t)(sum[i] / size) & 0xffu) << (24 - 8 * i);
    if (r) *r = (tjs_int64)(tjs_int)c;
    return TJS_S_OK;
}

// ---------------------------------------------------------------------------
// shrinkCopy.dll / layerExAreaAverage.dll: area-average resampling
//
// Every destination pixel is the average of the source area it covers, with
// fractional weights at the edges (separable). Straight (non-premultiplied)
// averages, as in the DLLs, which is why their manuals recommend oozeColor.

struct Span { tjs_int first; std::vector<float> w; };  // source pixels and weights of one dest pixel

// Weights of the source pixels under destination pixel [d, d+1) of a mapping
// that sends [dpos, dpos+dlen) onto [spos, spos+slen).
Span CoverSpan(double d, double dpos, double dlen, tjs_int spos, tjs_int slen) {
    double a = d < dpos ? dpos : d, b = d + 1 < dpos + dlen ? d + 1 : dpos + dlen;
    double z = slen / dlen;
    double s0 = spos + (a - dpos) * z, s1 = spos + (b - dpos) * z;
    Span sp;
    if (s1 <= s0) { sp.first = (tjs_int)s0; sp.w.push_back(1.0f); return sp; }
    sp.first = (tjs_int)std::floor(s0);
    for (tjs_int i = sp.first; i < s1; ++i) {
        double lo = i > s0 ? i : s0, hi = i + 1 < s1 ? i + 1 : s1;
        sp.w.push_back((float)(hi - lo));
    }
    return sp;
}

void AreaAverage(const Img& d, double dx, double dy, double dw, double dh,
                 const Img& s, tjs_int sx, tjs_int sy, tjs_int sw, tjs_int sh, bool opaque) {
    tjs_int x0 = (tjs_int)std::floor(dx), y0 = (tjs_int)std::floor(dy);
    tjs_int x1 = (tjs_int)std::ceil(dx + dw), y1 = (tjs_int)std::ceil(dy + dh);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > d.w) x1 = d.w;
    if (y1 > d.h) y1 = d.h;
    std::vector<Span> cols, rows;
    for (tjs_int x = x0; x < x1; ++x) cols.push_back(CoverSpan(x, dx, dw, sx, sw));
    for (tjs_int y = y0; y < y1; ++y) rows.push_back(CoverSpan(y, dy, dh, sy, sh));
    for (tjs_int y = y0; y < y1; ++y) {
        const Span& ry = rows[y - y0];
        uint32_t* out = d.px(0, y);
        for (tjs_int x = x0; x < x1; ++x) {
            const Span& cx = cols[x - x0];
            float acc[4] = {0, 0, 0, 0}, wsum = 0;
            for (size_t j = 0; j < ry.w.size(); ++j) {
                tjs_int syy = ry.first + (tjs_int)j;
                if (syy < 0 || syy >= s.h) continue;
                const uint8_t* srow = s.row(syy);
                for (size_t i = 0; i < cx.w.size(); ++i) {
                    tjs_int sxx = cx.first + (tjs_int)i;
                    if (sxx < 0 || sxx >= s.w) continue;
                    float wgt = ry.w[j] * cx.w[i];
                    const uint8_t* p = srow + sxx * 4;
                    for (int c = 0; c < 4; ++c) acc[c] += wgt * p[c];
                    wsum += wgt;
                }
            }
            if (wsum <= 0) continue;
            uint8_t* q = (uint8_t*)&out[x];
            for (int c = 0; c < 4; ++c) q[c] = (uint8_t)(acc[c] / wsum + 0.5f);
            if (opaque) q[kA] = 255;
        }
    }
}

// shrinkCopy(dleft, dtop, dwidth, dheight, src, sleft, stop, swidth, sheight):
// fractional destination rect, integer source rect; enlarging is an error.
tjs_error ShrinkCopy(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 9) return TJS_E_BADPARAMCOUNT;
    double dx = p[0]->AsReal(), dy = p[1]->AsReal(), dw = p[2]->AsReal(), dh = p[3]->AsReal();
    tjs_int sx = (tjs_int)p[5]->AsInteger(), sy = (tjs_int)p[6]->AsInteger();
    tjs_int sw = (tjs_int)p[7]->AsInteger(), sh = (tjs_int)p[8]->AsInteger();
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || sw < (tjs_int)dw || sh < (tjs_int)dh) return TJS_E_INVALIDPARAM;
    Img d, s;
    if (!GetImg(lay, true, d) || !GetImg(p[4]->AsObjectNoAddRef(), false, s)) return TJS_E_INVALIDPARAM;
    AreaAverage(d, dx, dy, dw, dh, s, sx, sy, sw, sh, false);
    Update(lay, (tjs_int)std::floor(dx), (tjs_int)std::floor(dy), (tjs_int)std::ceil(dw) + 1, (tjs_int)std::ceil(dh) + 1);
    return TJS_S_OK;
}

// shrinkCopyFast(src, shrink_x, shrink_y = shrink_x): this layer becomes
// ceil(src / factor) and each pixel the average of its block, alpha ignored
// (the result is opaque).
tjs_error ShrinkCopyFast(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 2) return TJS_E_BADPARAMCOUNT;
    tjs_int fx = (tjs_int)p[1]->AsInteger();
    tjs_int fy = ParamExists(n, p, 2) ? (tjs_int)p[2]->AsInteger() : 0;
    if (fy <= 0) fy = fx;
    if (fx < 1 || fy < 1) return TJS_E_INVALIDPARAM;
    iTJSDispatch2* srcobj = p[0]->AsObjectNoAddRef();
    Img s;
    if (!GetImg(srcobj, false, s)) return TJS_E_INVALIDPARAM;
    tjs_int w = (s.w + fx - 1) / fx, h = (s.h + fy - 1) / fy;
    tTJSVariant a[2] = {(tjs_int64)w, (tjs_int64)h};
    tTJSVariant* args[2] = {&a[0], &a[1]};
    lay->FuncCall(0, TJS_W("setImageSize"), nullptr, nullptr, 2, args, lay);
    Img d;
    if (!GetImg(lay, true, d) || !GetImg(srcobj, false, s)) return TJS_E_INVALIDPARAM;
    AreaAverage(d, 0, 0, (double)s.w / fx, (double)s.h / fy, s, 0, 0, s.w, s.h, true);
    Update(lay, 0, 0, d.w, d.h);
    return TJS_S_OK;
}

// stretchCopyAA(dleft, dtop, dwidth, dheight, src, sleft, stop, swidth, sheight)
tjs_error StretchCopyAA(tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2* lay) {
    if (n < 9) return TJS_E_BADPARAMCOUNT;
    double dx = p[0]->AsReal(), dy = p[1]->AsReal(), dw = p[2]->AsReal(), dh = p[3]->AsReal();
    tjs_int sx = (tjs_int)p[5]->AsInteger(), sy = (tjs_int)p[6]->AsInteger();
    tjs_int sw = (tjs_int)p[7]->AsInteger(), sh = (tjs_int)p[8]->AsInteger();
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return TJS_S_OK;
    Img d, s;
    if (!GetImg(lay, true, d) || !GetImg(p[4]->AsObjectNoAddRef(), false, s)) return TJS_E_INVALIDPARAM;
    AreaAverage(d, dx, dy, dw, dh, s, sx, sy, sw, sh, false);
    Update(lay, (tjs_int)std::floor(dx), (tjs_int)std::floor(dy), (tjs_int)std::ceil(dw) + 1, (tjs_int)std::ceil(dh) + 1);
    return TJS_S_OK;
}

// ---------------------------------------------------------------------------
// registration

struct Method {
    const tjs_char* name;
    const char* name8;
    tTJSNativeClassMethodCallback fn;
};

// One-time "first call" log per method, then the real work.
template <tjs_error (*F)(tTJSVariant*, tjs_int, tTJSVariant**, iTJSDispatch2*)>
tjs_error TJS_INTF_METHOD Logged(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* o);

#define KK4S_LX(fn, name) {TJS_W(name), name, &Logged<fn>}

const Method kBtoA[] = {
    KK4S_LX(CopyRightBlueToLeftAlpha, "copyRightBlueToLeftAlpha"),
    KK4S_LX(CopyBottomBlueToTopAlpha, "copyBottomBlueToTopAlpha"),
    KK4S_LX(FillAlpha, "fillAlpha"),
    KK4S_LX(CopyAlphaToProvince, "copyAlphaToProvince"),
    KK4S_LX(ClipAlphaRect, "clipAlphaRect"),
    KK4S_LX(OverwrapRect, "overwrapRect"),
    KK4S_LX(FillByProvince, "fillByProvince"),
    KK4S_LX(FillToProvince, "fillToProvince"),
};
const Method kImage[] = {
    KK4S_LX(Light, "light"),
    KK4S_LX(Colorize, "colorize"),
    KK4S_LX(Modulate, "modulate"),
    KK4S_LX(Noise, "noise"),
    KK4S_LX(GenerateWhiteNoise, "generateWhiteNoise"),
    KK4S_LX(GaussianBlur, "gaussianBlur"),
};
const Method kRaster[] = {
    KK4S_LX(CopyRaster, "copyRaster"),
};
const Method kShrink[] = {
    KK4S_LX(ShrinkCopy, "shrinkCopy"),
    KK4S_LX(ShrinkCopyFast, "shrinkCopyFast"),
};
const Method kAreaAverage[] = {
    KK4S_LX(StretchCopyAA, "stretchCopyAA"),
};
const Method kSave[] = {
    KK4S_LX(SaveLayerImagePng, "saveLayerImagePng"),
    KK4S_LX(SaveLayerImageTlg5, "saveLayerImageTlg5"),
    KK4S_LX(SaveLayerImagePngOctet, "saveLayerImagePngOctet"),
    KK4S_LX(GetCropRect, "getCropRect"),
    KK4S_LX(GetCropRectZero, "getCropRectZero"),
    KK4S_LX(GetDiffRect, "getDiffRect"),
    KK4S_LX(GetDiffPixel, "getDiffPixel"),
    KK4S_LX(OozeColor, "oozeColor"),
    KK4S_LX(CopyBlueToAlpha, "copyBlueToAlpha"),
    KK4S_LX(IsBlank, "isBlank"),
    KK4S_LX(ClearAlpha, "clearAlpha"),
    KK4S_LX(GetAverageColor, "getAverageColor"),
};

#undef KK4S_LX

template <tjs_error (*F)(tTJSVariant*, tjs_int, tTJSVariant**, iTJSDispatch2*)>
tjs_error TJS_INTF_METHOD Logged(tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* o) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        const char* name = "?";
        for (const Method& m : kBtoA) if (m.fn == &Logged<F>) name = m.name8;
        for (const Method& m : kImage) if (m.fn == &Logged<F>) name = m.name8;
        for (const Method& m : kRaster) if (m.fn == &Logged<F>) name = m.name8;
        for (const Method& m : kSave) if (m.fn == &Logged<F>) name = m.name8;
        for (const Method& m : kShrink) if (m.fn == &Logged<F>) name = m.name8;
        for (const Method& m : kAreaAverage) if (m.fn == &Logged<F>) name = m.name8;
        kk4s::Log("compat: Layer.%s first call, %d argument(s)", name, (int)n);
    }
    return F(r, n, p, o);
}

template <size_t N>
void Attach(const char* dll, const Method (&methods)[N]) {
    iTJSDispatch2* g = TVPGetScriptEngine()->GetGlobalNoAddRef();
    tTJSVariant cls;
    if (TJS_FAILED(g->PropGet(0, TJS_W("Layer"), nullptr, &cls, g)) || cls.Type() != tvtObject) return;
    iTJSDispatch2* layer = cls.AsObjectNoAddRef();
    int added = 0;
    for (const Method& m : methods) {
        tTJSVariant existing;
        if (TJS_SUCCEEDED(layer->PropGet(0, m.name, nullptr, &existing, layer)) && existing.Type() == tvtObject) {
            kk4s::Log("compat: %s: Layer.%s already exists, kept", dll, m.name8);
            continue;
        }
        iTJSDispatch2* f = TJSCreateNativeClassMethod(m.fn);
        tTJSVariant v(f);
        f->Release();
        layer->PropSet(TJS_MEMBERENSURE, m.name, nullptr, &v, layer);
        ++added;
    }
    kk4s::Log("compat: %s: %d Layer method(s) added", dll, added);
}

} // namespace

void LinkLayerExBtoADll() { Attach("layerExBtoA.dll", kBtoA); }
void LinkLayerExImageDll() { Attach("layerExImage.dll", kImage); }
void LinkLayerExRasterDll() { Attach("layerExRaster.dll", kRaster); }
void LinkShrinkCopyDll() { Attach("shrinkCopy.dll", kShrink); }
void LinkLayerExAreaAverageDll() { Attach("layerExAreaAverage.dll", kAreaAverage); }

// Window.startSaveLayerImage(layer, filename, tags) -> handler. The DLL copies the
// layer (assignImages), saves the copy on a thread (.png -> PNG, else TLG5) and
// then calls window.onSaveLayerImageProgress(handler, percent, layer, filename) and
// onSaveLayerImageDone(handler, canceled, layer, filename) with that copy, which
// games usually invalidate there. kk4s saves the copy at once and delivers the two
// events on the next frame; cancel/stop before then behave like the DLL's.
static const tjs_char* kWindowSaveShim = TJS_W(R"TJS(
if (typeof Window.startSaveLayerImage == "undefined") {
global.kk4s_saveimage = %[ next: 0, jobs: [], hooked: false ];
global.kk4s_saveimage_tick = function() {
	var s = global.kk4s_saveimage, jobs = s.jobs;
	s.jobs = [];
	System.removeContinuousHandler(global.kk4s_saveimage_tick);
	s.hooked = false;
	for (var i = 0; i < jobs.count; i++) {
		var j = jobs[i];
		if (j.stopped || !isvalid j.window) continue;
		if (!j.canceled && typeof j.window.onSaveLayerImageProgress != "undefined")
			j.window.onSaveLayerImageProgress(j.handler, 100, j.layer, j.filename);
		if (typeof j.window.onSaveLayerImageDone != "undefined")
			j.window.onSaveLayerImageDone(j.handler, j.canceled ? 1 : 0, j.layer, j.filename);
	}
};
global.kk4s_saveimage_find = function(h) {
	var jobs = global.kk4s_saveimage.jobs;
	for (var i = 0; i < jobs.count; i++) if (jobs[i].handler == h) return jobs[i];
	return void;
};
Window.startSaveLayerImage = function(layer, filename, tags) {
	var copy = new Layer(this, this.primaryLayer);
	copy.name = "saveLayer:" + filename;
	copy.assignImages(layer);
	if (Storages.extractStorageExt(filename).toLowerCase() == ".png") copy.saveLayerImagePng(filename, tags);
	else copy.saveLayerImageTlg5(filename, tags);
	var s = global.kk4s_saveimage;
	var h = s.next++;
	s.jobs.add(%[ window: this, handler: h, layer: copy, filename: filename, canceled: false, stopped: false ]);
	if (!s.hooked) { System.addContinuousHandler(global.kk4s_saveimage_tick); s.hooked = true; }
	return h;
};
Window.cancelSaveLayerImage = function(h) { var j = kk4s_saveimage_find(h); if (j !== void) j.canceled = true; };
Window.stopSaveLayerImage = function(h) { var j = kk4s_saveimage_find(h); if (j !== void) j.stopped = true; };
}
)TJS");

void LinkLayerExSaveDll() {
    Attach("layerExSave.dll", kSave);
    try {
        TVPExecuteScript(ttstr(kWindowSaveShim));
    } catch (const eTJS& e) {
        kk4s::Log("compat: layerExSave.dll Window shim failed: %s", e.GetMessage().AsNarrowStdString().c_str());
    }
}



// PackinOne.dll bundles the layerEx plugins (layerExSave among them, its source
// notes the static-link name clash) into one DLL.
void LinkPackinOneDll() {
    LinkLayerExBtoADll();
    LinkLayerExImageDll();
    LinkLayerExRasterDll();
    LinkLayerExSaveDll();
}

} // namespace compat
} // namespace kk4s
