// kk4s compatibility layer, part 3: replacements for common layer-effect
// plugins (filter.dll, drawer.dll, slideopen.dll, firespark.dll).
//
// The original DLLs are Windows-only. Their functions are re-created here:
// exactly where we know the behaviour, approximately where we do not
// (an effect that looks a bit different is much better than a game that
// stops). Approximations are listed in docs/plugins.md.
//
// Most shims are TJS (they only call standard Layer methods); pixel loops
// that TJS would be too slow for are native helpers named kk4s_fx_*.
#include "kk4s_compat.h"

#include "ScriptMgnIntf.h"
#include "tjsObject.h"
#include "kk4s_platform.h"

#include <cstdint>
#include <cstdlib>
#include <random>

namespace kk4s {
namespace compat {

// ---------------------------------------------------------------------------
// native pixel helpers

struct LayerPixels {
    uint8_t* bits = nullptr;
    tjs_int64 pitch = 0;
    tjs_int w = 0, h = 0;
};

static bool GetLayerPixels(tTJSVariant& layer, LayerPixels& out) {
    if (layer.Type() != tvtObject) return false;
    iTJSDispatch2* obj = layer.AsObjectNoAddRef();
    if (!obj) return false;
    tTJSVariant v;
    if (TJS_FAILED(obj->PropGet(0, TJS_W("imageWidth"), nullptr, &v, obj))) return false;
    out.w = (tjs_int)v.AsInteger();
    if (TJS_FAILED(obj->PropGet(0, TJS_W("imageHeight"), nullptr, &v, obj))) return false;
    out.h = (tjs_int)v.AsInteger();
    if (TJS_FAILED(obj->PropGet(0, TJS_W("mainImageBufferForWrite"), nullptr, &v, obj))) return false;
    out.bits = (uint8_t*)(intptr_t)v.AsInteger();
    if (TJS_FAILED(obj->PropGet(0, TJS_W("mainImageBufferPitch"), nullptr, &v, obj))) return false;
    out.pitch = v.AsInteger();
    return out.bits && out.w > 0 && out.h > 0;
}

static void UpdateLayer(tTJSVariant& layer, tjs_int x, tjs_int y, tjs_int w, tjs_int h) {
    iTJSDispatch2* obj = layer.AsObjectNoAddRef();
    tTJSVariant a[4] = {x, y, w, h};
    tTJSVariant* p[4] = {&a[0], &a[1], &a[2], &a[3]};
    obj->FuncCall(0, TJS_W("update"), nullptr, nullptr, 4, p, obj);
}

static inline void PutPixel(LayerPixels& px, tjs_int x, tjs_int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= px.w || y >= px.h) return;
    *(uint32_t*)(px.bits + px.pitch * y + x * 4) = c;
}

class tFxFunction : public tTJSDispatch {
public:
    using Fn = tjs_error (*)(tTJSVariant* result, tjs_int n, tTJSVariant** p);
    explicit tFxFunction(Fn f) : F(f) {}
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
                                       tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                       iTJSDispatch2* objthis) override {
        if (membername) return TJS_E_MEMBERNOTFOUND;
        if (result) result->Clear();
        try {
            return F(result, numparams, param);
        } catch (...) {
            return TJS_S_OK; // effects must never take the game down
        }
    }
private:
    Fn F;
};

// kk4s_fx_noise(layer, monochrome): fill with random noise (TV static)
static tjs_error FxNoise(tTJSVariant*, tjs_int n, tTJSVariant** p) {
    if (n < 1) return TJS_E_BADPARAMCOUNT;
    LayerPixels px;
    if (!GetLayerPixels(*p[0], px)) return TJS_S_OK;
    bool mono = n >= 2 && p[1]->Type() != tvtVoid && p[1]->AsInteger() != 0;
    static std::minstd_rand rng(12345);
    for (tjs_int y = 0; y < px.h; ++y) {
        uint32_t* row = (uint32_t*)(px.bits + px.pitch * y);
        for (tjs_int x = 0; x < px.w; ++x) {
            uint32_t r = rng();
            if (mono) {
                uint32_t g = r & 0xff;
                row[x] = 0xff000000u | (g << 16) | (g << 8) | g;
            } else {
                row[x] = 0xff000000u | (r & 0xffffff);
            }
        }
    }
    UpdateLayer(*p[0], 0, 0, px.w, px.h);
    return TJS_S_OK;
}

// kk4s_fx_line(layer, x0, y0, x1, y1, color): Bresenham line
static tjs_error FxLine(tTJSVariant*, tjs_int n, tTJSVariant** p) {
    if (n < 6) return TJS_E_BADPARAMCOUNT;
    LayerPixels px;
    if (!GetLayerPixels(*p[0], px)) return TJS_S_OK;
    tjs_int x0 = (tjs_int)p[1]->AsInteger(), y0 = (tjs_int)p[2]->AsInteger();
    tjs_int x1 = (tjs_int)p[3]->AsInteger(), y1 = (tjs_int)p[4]->AsInteger();
    uint32_t c = (uint32_t)p[5]->AsInteger();
    if ((c >> 24) == 0) c |= 0xff000000u; // 0xRRGGBB means opaque
    tjs_int minx = x0 < x1 ? x0 : x1, miny = y0 < y1 ? y0 : y1;
    tjs_int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    tjs_int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    tjs_int err = dx + dy;
    for (tjs_int guard = 0; guard < 100000; ++guard) {
        PutPixel(px, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        tjs_int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    UpdateLayer(*p[0], minx, miny, dx + 1, -dy + 1);
    return TJS_S_OK;
}

static void DefineNative(const tjs_char* name, tFxFunction::Fn fn) {
    iTJSDispatch2* g = TVPGetScriptEngine()->GetGlobalNoAddRef();
    iTJSDispatch2* f = new tFxFunction(fn);
    tTJSVariant v(f, nullptr);
    f->Release();
    g->PropSet(TJS_MEMBERENSURE, name, nullptr, &v, g);
}

static void EnsureNativeHelpers() {
    static bool done = false;
    if (done) return;
    done = true;
    DefineNative(TJS_W("kk4s_fx_noise"), FxNoise);
    DefineNative(TJS_W("kk4s_fx_line"), FxLine);
}

// Run a TJS shim. Each shim defines its functions only if they do not exist.
static void RunShim(const char* tag, const tjs_char* script) {
    EnsureNativeHelpers();
    try {
        TVPExecuteScript(ttstr(script));
    } catch (const eTJS& e) {
        kk4s::Log("compat: shim %s failed: %s", tag, e.GetMessage().AsNarrowStdString().c_str());
    }
}

// ---------------------------------------------------------------------------
// filter.dll: Stretch, Blur, Smudge, Contrast, Noise, Lens, Haze

static const tjs_char* kFilterShim = TJS_W(R"TJS(
global.kk4s_opt = function(e, name, def) { return (e !== void && e[name] !== void) ? e[name] : def; };

if (typeof global.Stretch == "undefined")
global.Stretch = function(e) {
	var s = e.src, d = e.dest;
	if (s === void || d === void) return;
	var sl = kk4s_opt(e, "sleft", 0), st = kk4s_opt(e, "stop", 0);
	var sw = kk4s_opt(e, "swidth", s.imageWidth), sh = kk4s_opt(e, "sheight", s.imageHeight);
	var dl = kk4s_opt(e, "dleft", 0), dt = kk4s_opt(e, "dtop", 0);
	var dw = kk4s_opt(e, "dwidth", d.imageWidth), dh = kk4s_opt(e, "dheight", d.imageHeight);
	var opa = kk4s_opt(e, "opa", kk4s_opt(e, "opacity", 255));
	d.operateStretch(dl, dt, dw, dh, s, sl, st, sw, sh, omAuto, opa, stFastLinear);
};

if (typeof global.Blur == "undefined")
global.Blur = function(e) {
	var l = kk4s_opt(e, "layer", void);
	if (l === void) return;
	var lv = int(kk4s_opt(e, "level", 1));
	if (lv > 0) l.doBoxBlur(lv, lv);
};

if (typeof global.Smudge == "undefined")
global.Smudge = function(e) {	// approximation: blur
	var l = kk4s_opt(e, "layer", void);
	if (l === void) return;
	var lv = int(kk4s_opt(e, "level", 1));
	if (lv > 0) l.doBoxBlur(lv, lv);
};

if (typeof global.Contrast == "undefined")
global.Contrast = function(e) {	// approximation: gamma
	var l = kk4s_opt(e, "layer", void);
	if (l === void) return;
	var lv = +kk4s_opt(e, "level", 1.0);
	if (lv <= 0) lv = 1.0;
	l.adjustGamma(lv, 0, 255, lv, 0, 255, lv, 0, 255);
};

if (typeof global.Noise == "undefined")
global.Noise = function(e) {
	var l = kk4s_opt(e, "layer", void);
	if (l !== void) kk4s_fx_noise(l, kk4s_opt(e, "monocro", false));
};

// Lens: not implemented yet (picture stays undistorted)
if (typeof global.InitLens == "undefined") global.InitLens = function() {};
if (typeof global.Lens == "undefined") global.Lens = function() {};
if (typeof global.ReleaseLens == "undefined") global.ReleaseLens = function() {};

// Haze (heat shimmer): approximation without the wave, just copies src
if (typeof global.initHaze == "undefined") global.initHaze = function() {};
if (typeof global.endHaze == "undefined") global.endHaze = function() {};
if (typeof global.doHaze == "undefined")
global.doHaze = function(e) {
	var s = kk4s_opt(e, "src", void), d = kk4s_opt(e, "dest", void);
	if (s === void || d === void) return;
	d.copyRect(0, 0, s, 0, 0, s.imageWidth, s.imageHeight);
};
)TJS");

// ---------------------------------------------------------------------------
// drawer.dll: drawLine / drawAALine / drawAATriangle

static const tjs_char* kDrawerShim = TJS_W(R"TJS(
if (typeof global.drawLine == "undefined")
global.drawLine = function(layer, x0, y0, x1, y1, color) {
	kk4s_fx_line(layer, int(x0), int(y0), int(x1), int(y1), color);
};
if (typeof global.drawAALine == "undefined")
global.drawAALine = function(layer, x0, y0, x1, y1, color) {	// no anti-aliasing yet
	kk4s_fx_line(layer, int(x0), int(y0), int(x1), int(y1), color);
};
if (typeof global.drawAATriangle == "undefined")
global.drawAATriangle = function(layer, x0, y0, x1, y1, x2, y2, color) {	// outline only
	kk4s_fx_line(layer, int(x0), int(y0), int(x1), int(y1), color);
	kk4s_fx_line(layer, int(x1), int(y1), int(x2), int(y2), color);
	kk4s_fx_line(layer, int(x2), int(y2), int(x0), int(y0), color);
};
)TJS");

// ---------------------------------------------------------------------------
// slideopen.dll: initSlideOpen(obj) / drawSlideOpen(per) / finishSlideOpen()
// obj has .dest, .src, .type, .open. Approximation: cross-fade from the
// picture at init time to src, by "openness" per (0.0 .. 1.0).

static const tjs_char* kSlideOpenShim = TJS_W(R"TJS(
if (typeof global.initSlideOpen == "undefined") {
global.kk4s_slideopen = %[];
global.initSlideOpen = function(obj) {
	var so = global.kk4s_slideopen;
	so.obj = obj;
	var d = obj.dest !== void ? obj.dest : kag.fore.base;
	so.dest = d;
	if (so.snap === void) so.snap = new global.Layer(kag, kag.primaryLayer);
	so.snap.setImageSize(d.imageWidth, d.imageHeight);
	so.snap.copyRect(0, 0, d, 0, 0, d.imageWidth, d.imageHeight);
};
global.drawSlideOpen = function(per) {
	var so = global.kk4s_slideopen;
	if (so.obj === void) return;
	var d = so.dest, s = so.obj.src;
	if (per < 0) per = 0; if (per > 1) per = 1;
	d.copyRect(0, 0, so.snap, 0, 0, so.snap.imageWidth, so.snap.imageHeight);
	if (s !== void) d.operateRect(0, 0, s, 0, 0, s.imageWidth, s.imageHeight, omAuto, int(255 * per));
};
global.finishSlideOpen = function() {
	var so = global.kk4s_slideopen;
	so.obj = void;
	if (so.snap !== void) so.snap.setImageSize(1, 1);
};
}
)TJS");

// ---------------------------------------------------------------------------
// firespark.dll: particle sparks. Not drawn yet; the game logic keeps going.

static const tjs_char* kFireSparkShim = TJS_W(R"TJS(
if (typeof global.initFireSpark == "undefined") global.initFireSpark = function() {};
if (typeof global.drawFireSpark == "undefined") global.drawFireSpark = function() {};
if (typeof global.changeFireSpark == "undefined") global.changeFireSpark = function() {};
if (typeof global.finishFireSpark == "undefined") global.finishFireSpark = function() {};
)TJS");

void LinkFilterDll() { RunShim("filter.dll", kFilterShim); }
void LinkDrawerDll() { RunShim("drawer.dll", kDrawerShim); }
void LinkSlideOpenDll() { RunShim("slideopen.dll", kSlideOpenShim); }
void LinkFireSparkDll() { RunShim("firespark.dll", kFireSparkShim); }

} // namespace compat
} // namespace kk4s
