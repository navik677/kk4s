// kk4s compatibility layer, part 4: emoteplayer.dll / motionplayer.dll.
//
// KAGZ titles (NekoPara and others) call `new Motion.ResourceManager(kag, cacheSize)`
// while their window is created, so the `Motion` namespace has to exist before the
// game's scripts run. The real DLLs are Windows-only; the pieces below are:
//
//  - Motion.ResourceManager: a real resource cache. It resolves the storage, reads
//    and parses it (compat/psb reads the PSB container), accounts the bytes and
//    evicts least-recently-used entries over the limit. `load` returns the same
//    object for an already cached path (the DLL does that too, and the game's own
//    MotionResourceManager adds a reference count on top).
//  - Motion.Player / Motion.EmotePlayer: the documented playback state machine
//    (play/stop/skip/progress/time/playing and the option members the scripts set).
//  - E-mote *rendering* is not implemented: emote.xp3 holds 79 .psb files, and
//    drawing them needs the E-mote runtime (mesh deformation, physics, timelines).
//    Player.draw() therefore draws nothing and logs once per played motion. This is
//    documented in docs/plugins.md; games that only construct the manager for
//    future use (NekoPara's own scenario has no motion tags) are unaffected.
//
// Constants: the scripts only pass them back to the plugin (maskMode, flags), so
// kk4s defines its own stable values. They are not interchangeable with the DLL's
// numbers across a process boundary, which never happens here.
#include "kk4s_compat.h"

#include "StorageIntf.h"
#include "ScriptMgnIntf.h"
#include "tjsObject.h"
#include "kk4s_platform.h"

#include "psb/psb.h"

#include <cstdio>
#include <string>
#include <vector>

namespace kk4s {
namespace compat {

// ---------------------------------------------------------------------------
// native helpers

// kk4s_motion_probe(storage) -> "format\tversion\tsize" or "-\t<error>"
// format is "psb" (parsed ok), "raw" (readable, not a PSB container) or "-".
static tjs_error FxMotionProbe(tTJSVariant* result, tjs_int n, tTJSVariant** p) {
    std::string out = "-\tmissing parameter";
    if (n >= 1 && p[0]->Type() != tvtVoid) {
        ttstr name = *p[0];
        ttstr norm = TVPNormalizeStorageName(name);
        tTJSBinaryStream* s = TVPCreateStream(norm, TJS_BS_READ);
        if (!s) {
            out = "-\tnot found";
        } else {
            std::vector<uint8_t> data;
            try {
                size_t size = (size_t)s->GetSize();
                data.resize(size);
                if (size) s->ReadBuffer(data.data(), (tjs_uint)size);
            } catch (...) {
                data.clear();
            }
            delete s;
            if (data.empty()) {
                out = "-\tempty";
            } else {
                psb::Reader reader;
                if (reader.Open(data.data(), data.size())) {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, "psb\t%u\t%zu", (unsigned)reader.version(), data.size());
                    out = buf;
                } else {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, "raw\t0\t%zu", data.size());
                    out = buf;
                }
            }
        }
    }
    if (result) *result = ttstr(out.c_str());
    return TJS_S_OK;
}

class tMotionFunction : public tTJSDispatch {
public:
    using Fn = tjs_error (*)(tTJSVariant*, tjs_int, tTJSVariant**);
    explicit tMotionFunction(Fn f) : F(f) {}
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char* membername, tjs_uint32*,
                                       tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                       iTJSDispatch2*) override {
        if (membername) return TJS_E_MEMBERNOTFOUND;
        if (result) result->Clear();
        try {
            return F(result, numparams, param);
        } catch (...) {
            return TJS_S_OK;
        }
    }
private:
    Fn F;
};

static void DefineNative(const tjs_char* name, tMotionFunction::Fn fn) {
    iTJSDispatch2* g = TVPGetScriptEngine()->GetGlobalNoAddRef();
    iTJSDispatch2* f = new tMotionFunction(fn);
    tTJSVariant v(f, nullptr);
    f->Release();
    g->PropSet(TJS_MEMBERENSURE, name, nullptr, &v, g);
}

// ---------------------------------------------------------------------------
// TJS side

static const tjs_char* kMotionShim = TJS_W(R"TJS(
// dm() belongs to the game's KAG layer and may not exist yet when the plugin is
// linked (the manager is created during window init), so logging is best-effort.
function kk4s_motion_log(msg) { try { if (typeof dm == "Function") dm(msg); } catch (e) {} }

// Resource cache. The DLL caches by storage name with a byte budget; entries are
// released least-recently-used first. The probe reads and parses the file, so a
// broken or missing resource is reported here instead of at playback time.
class kk4sMotionResourceManager {
	var _cache = %[];
	var _order = [];
	var _limit = 20 * 1024 * 1024;
	var _used = 0;
	var kag = void;

	function kk4sMotionResourceManager(kag, limit) {
		this.kag = kag;
		if (limit !== void && +limit > 0) _limit = +limit;
	}

	property cacheSize { getter { return _used; } }
	property cacheLimit { getter { return _limit; } setter(v) { _limit = +v; _evict(); } }
	property count { getter { return _order.count; } }

	function load(path) {
		var key = "" + path;
		if (_cache[key] !== void) {
			_touch(key);
			return _cache[key].resource;
		}
		var info = kk4s_motion_probe(key);
		var parts = info.split("\t");
		var resource = %[storage:key, format:parts[0], version:int(parts[1]), size:int(parts[2]),
			playing:false, renderable:false];
		if (parts[0] == "-") {
			kk4s_motion_log("emoteplayer: cannot load motion resource " + key + ": " + parts[1]);
		} else if (parts[0] != "psb") {
			kk4s_motion_log("emoteplayer: " + key + " is not a PSB container; playback not possible");
		}
		_cache[key] = %[resource:resource, size:int(parts[2])];
		_order.add(key);
		_used += int(parts[2]);
		_evict(key);
		return resource;
	}

	function unload(path) {
		var key = "" + path;
		var e = _cache[key];
		if (e === void) return;
		delete _cache[key];
		_used -= e.size;
		var at = _order.indexOf(key);
		if (at >= 0) _order.erase(at);
	}

	function clearCache() {
		_cache = %[];
		_order = [];
		_used = 0;
	}

	function _touch(key) {
		var at = _order.indexOf(key);
		if (at >= 0) _order.erase(at);
		_order.add(key);
	}

	function _evict(keep) {
		while (_used > _limit && _order.count > 1) {
			var victim = void;
			for (var i = 0; i < _order.count; i++) if (_order[i] != keep) { victim = _order[i]; break; }
			if (victim === void) break;
			unload(victim);
		}
	}
}

// Playback state machine. The DLL interprets a motion resource and draws deformed
// meshes; kk4s keeps the state the scripts observe (playing/time/storage/options)
// and leaves draw() empty - see the file header.
class kk4sMotionPlayer {
	var _manager = void;
	var _storage = void;
	var _playing = false;
	var _time = 0;
	var _tick = void;
	var _warned = %[];
	var maskMode = 0;
	var completionType = 0;
	var x = 0, y = 0, zoom = 1.0, opacity = 255;
	var onAction = void;
	var onStop = void;

	function kk4sMotionPlayer(manager) { _manager = manager; }

	property playing { getter { return _playing; } }
	property time { getter { return _time; } setter(v) { _time = +v; } }
	property storage { getter { return _storage; } }

	function play(motion, flags) {
		if (motion === void) return false;
		if (_manager !== void && typeof motion == "String") {
			// motion is either a resource path or a label inside the loaded resource;
			// only paths can be resolved without the E-mote timeline reader.
			if (_storage !== "" + motion && ("" + motion).indexOf(".") >= 0) {
				_manager.load("" + motion);
				_storage = "" + motion;
			}
		}
		if (_warned["" + motion] === void) {
			_warned["" + motion] = true;
			kk4s_motion_log("emoteplayer: playback of " + motion + " is not rendered (E-mote runtime absent)");
		}
		_playing = true;
		_time = 0;
		_tick = (typeof System == "Object" && typeof System.getTickCount == "Function") ? System.getTickCount() : 0;
		return true;
	}

	function stop() {
		if (!_playing) return;
		_playing = false;
		_tick = void;
		if (onStop !== void) { try { onStop(this); } catch (e) {} }
	}

	function skip() { stop(); }

	function progress(interval) {
		if (!_playing) return;
		_time += interval;
	}

	function draw(target) { return false; }

	function reset() { _time = 0; _playing = false; _tick = void; }

	function finalize() {
		if (_manager !== void && _storage !== void) _manager.unload(_storage);
		_manager = void; _storage = void;
	}
}

// E-mote player: same state machine plus the character/physics/timeline API the
// scripts call. Timelines come from the PSB resource, which needs the E-mote
// runtime; the lists are empty, so scripts see "no timelines" instead of an error.
class kk4sMotionEmotePlayer extends kk4sMotionPlayer {
	var chara = void;
	var _color = void;

	function kk4sMotionEmotePlayer(manager) { super.kk4sMotionPlayer(manager); }
	function initPhysics(metadata) { return true; }
	function setColor(c) { _color = c; }
	function serialize() { return %[chara:chara, time:time, playing:playing]; }
	function unserialize(src) {
		if (src === void) return false;
		chara = src.chara;
		return true;
	}
	function getMainTimelineLabelList() { return []; }
	function getDiffTimelineLabelList() { return []; }
	function getTimelineLabelList() { return []; }
}

// Binder adaptor used when a motion layer is separated from an AffineLayer.
class kk4sMotionSeparateLayerAdaptor {
	var _layer = void;
	function kk4sMotionSeparateLayerAdaptor(layer) { _layer = layer; }
	function update() { return true; }
	function finalize() { _layer = void; }
}

if (typeof global.Motion != "Object") global.Motion = %[];
if (global.Motion.ResourceManager === void) global.Motion.ResourceManager = kk4sMotionResourceManager;
if (global.Motion.Player === void) global.Motion.Player = kk4sMotionPlayer;
if (global.Motion.EmotePlayer === void) global.Motion.EmotePlayer = kk4sMotionEmotePlayer;
if (global.Motion.SeparateLayerAdaptor === void)
	global.Motion.SeparateLayerAdaptor = kk4sMotionSeparateLayerAdaptor;

// Values are kk4s-internal (the scripts only hand them back to these classes).
if (global.Motion.MaskModeAlpha === void) global.Motion.MaskModeAlpha = 0;
if (global.Motion.MaskModeAdditive === void) global.Motion.MaskModeAdditive = 1;
if (global.Motion.MaskModeSubtract === void) global.Motion.MaskModeSubtract = 2;
if (global.Motion.PlayFlagForce === void) global.Motion.PlayFlagForce = 1;
if (global.Motion.PlayFlagNoLoop === void) global.Motion.PlayFlagNoLoop = 2;
if (global.Motion.ShapeTypePoint === void) global.Motion.ShapeTypePoint = 0;
if (global.Motion.ShapeTypeCircle === void) global.Motion.ShapeTypeCircle = 1;
if (global.Motion.ShapeTypeRect === void) global.Motion.ShapeTypeRect = 2;
if (global.Motion.ShapeTypeQuad === void) global.Motion.ShapeTypeQuad = 3;
)TJS");

void LinkEmotePlayerDll() {
    static bool natives = false;
    if (!natives) {
        natives = true;
        DefineNative(TJS_W("kk4s_motion_probe"), FxMotionProbe);
    }
    try {
        TVPExecuteScript(ttstr(kMotionShim));
        kk4s::Log("compat: Motion.* registered (resource cache and playback state; "
                  "E-mote rendering not implemented)");
    } catch (const eTJS& e) {
        kk4s::Log("compat: emoteplayer shim failed: %s", e.GetMessage().AsNarrowStdString().c_str());
    }
}

} // namespace compat
} // namespace kk4s
