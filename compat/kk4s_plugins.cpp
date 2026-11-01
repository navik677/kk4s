// kk4s compatibility layer, part 2: native replacements for Windows plugins.
//
// Plugins.link("name.dll") cannot load a Windows DLL on Switch/Linux. The
// engine first tries its own internal plugins (engine/source/plugins), then
// this table. Each entry registers what the DLL would have added to TJS.
// Unknown DLLs are ignored with a log line (most games survive that, and the
// log shows what to implement next).
//
// Add a plugin: write a Link function and add it to kPlugins below.
#include "kk4s_compat.h"

extern bool TVPKAGParserExMode; // engine/source/core/utils/KAGParser.cpp

#include "CharacterSet.h"
#include "StorageIntf.h"
#include "TextStream.h"
#include "ScriptMgnIntf.h"
#include "tjsObject.h"
#include "tjsArray.h"
#include "kk4s_platform.h"
#include "kk4s_fs.h"

#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace kk4s {
namespace compat {

// ---------------------------------------------------------------------------
// helpers

using NativeFn = std::function<tjs_error(tTJSVariant* result, tjs_int numparams, tTJSVariant** param)>;

// A TJS-callable function object backed by a C++ lambda.
class tLambdaFunction : public tTJSDispatch {
    NativeFn Fn;
public:
    explicit tLambdaFunction(NativeFn fn) : Fn(std::move(fn)) {}
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
                                       tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                       iTJSDispatch2* objthis) override {
        if (membername) return TJS_E_MEMBERNOTFOUND;
        if (result) result->Clear();
        return Fn(result, numparams, param);
    }
};

// Define obj.name = function (only if obj has no such member yet).
static void DefineFunction(iTJSDispatch2* obj, const tjs_char* name, NativeFn fn) {
    tTJSVariant existing;
    if (TJS_SUCCEEDED(obj->PropGet(0, name, nullptr, &existing, obj)) && existing.Type() != tvtVoid)
        return; // never override what the game or engine already defines
    iTJSDispatch2* f = new tLambdaFunction(std::move(fn));
    tTJSVariant v(f, nullptr);
    f->Release();
    obj->PropSet(TJS_MEMBERENSURE, name, nullptr, &v, obj);
}

static iTJSDispatch2* Global() { return TVPGetScriptEngine()->GetGlobalNoAddRef(); }

static std::string Utf8(const ttstr& s) {
    std::string out;
    if (!TVPUtf16ToUtf8(out, s.AsStdString())) out = s.AsNarrowStdString();
    return out;
}

// TVP storage name -> native path ("" if it is inside an archive).
static std::string NativePath(const ttstr& storage) {
    ttstr local = TVPGetLocallyAccessibleName(TVPNormalizeStorageName(storage));
    return local.IsEmpty() ? std::string() : Utf8(local);
}

static bool IsRelativeName(const ttstr& n) {
    const tjs_char* p = n.c_str();
    if (!*p || *p == TJS_W('/') || *p == TJS_W('\\')) return false;
    if (TJS_strchr(p, TJS_W(':')) || TJS_strchr(p, TVPArchiveDelimiter)) return false;
    return true;
}

// Resolve a file name the way plugins written for krkr.exe expect:
// relative names are looked up next to the exe (= the game folder) first,
// then through the normal storage search (archives, auto paths).
static ttstr ResolveStorage(const ttstr& name) {
    if (IsRelativeName(name)) {
        ttstr beside = NativeToStorage(kk4s::GameDir()) + name;
        if (TVPIsExistentStorageNoSearch(beside)) return beside;
    }
    if (TVPIsExistentStorageNoSearch(name)) return TVPNormalizeStorageName(name);
    ttstr placed = TVPGetPlacedPath(name);
    return placed;
}

// ---------------------------------------------------------------------------
// util.dll - file helpers used for save data (FSN and other TYPE-MOON games)

static void LinkUtilDll() {
    iTJSDispatch2* g = Global();

    // GetFileSize(storage) -> size in bytes, or -1 if missing
    DefineFunction(g, TJS_W("GetFileSize"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        tjs_int64 size = -1;
        ttstr name = *p[0];
        if (TVPIsExistentStorageNoSearch(name)) {
            tTJSBinaryStream* s = TVPCreateStream(TVPNormalizeStorageName(name), TJS_BS_READ);
            if (s) { size = (tjs_int64)s->GetSize(); delete s; }
        }
        if (r) *r = size;
        return TJS_S_OK;
    });

    // DeleteFile(storage) -> true on success
    DefineFunction(g, TJS_W("DeleteFile"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        std::string path = NativePath(*p[0]);
        bool ok = !path.empty() && remove(path.c_str()) == 0;
        TVPClearStorageCaches();
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });

    // MoveFile(from, to) -> true on success
    DefineFunction(g, TJS_W("MoveFile"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 2) return TJS_E_BADPARAMCOUNT;
        std::string from = NativePath(*p[0]), to = NativePath(*p[1]);
        bool ok = !from.empty() && !to.empty() && rename(from.c_str(), to.c_str()) == 0;
        TVPClearStorageCaches();
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });
}

// Run a TJS shim; errors are logged, never fatal.
static void RunTjsShim(const char* tag, const tjs_char* script) {
    try {
        TVPExecuteScript(ttstr(script));
    } catch (const eTJS& e) {
        kk4s::Log("compat: %s shim failed: %s", tag, Utf8(e.GetMessage()).c_str());
    } catch (...) {
        kk4s::Log("compat: %s shim failed", tag);
    }
}

// The rest of util.dll as Kirikiroid2 packs fill it in (patch.tjs): IME and
// clipboard do not exist here, fonts come from the engine's list. FSN's load
// menu called enabledIME(false) when closing and got stuck without it.
static const tjs_char* kUtilShim = TJS_W(R"TJS(
if (typeof global.enabledIME == "undefined") global.enabledIME = function() {};
if (typeof global.SetClipboardText == "undefined") global.SetClipboardText = function() {};
if (typeof global.IsActiveWindow == "undefined") global.IsActiveWindow = function() { return true; };
if (typeof global.GetFileTime == "undefined") global.GetFileTime = function() { return 0; };
if (typeof global.enumFont == "undefined") global.enumFont = function(win, fonts) {
    var layer = win.primaryLayer !== void ? win.primaryLayer : win.fore.base;
    var list = layer.font.getList(0);
    for (var i = 0; i < list.count; i++) fonts.add(list[i]);
};
if (typeof global.GetDatetime == "undefined") global.GetDatetime = function() {
    var d = new Date();
    return %[ year: d.getYear(), mon: d.getMonth() + 1, day: d.getDate(), wday: d.getDay(),
              hour: d.getHours(), minute: d.getMinutes(), second: d.getSeconds() ];
};
if (typeof global.CopyFile == "undefined") global.CopyFile = function(from, to) { return Storages.copyFile(from, to, false); };
if (typeof global.CreateDirectory == "undefined") global.CreateDirectory = function(dir) { return Storages.createDirectory(dir); };
if (typeof global.ExistDirectory == "undefined") global.ExistDirectory = function(dir) { return Storages.isExistentDirectory(dir); };
if (typeof global.SelectDirectory == "undefined") global.SelectDirectory = function(title, dir) { return ""; };
)TJS");

// ---------------------------------------------------------------------------
// fstat.dll - Storages file and folder helpers (many games use them for saves)

static iTJSDispatch2* StoragesObject() {
    tTJSVariant v;
    iTJSDispatch2* g = Global();
    if (TJS_FAILED(g->PropGet(0, TJS_W("Storages"), nullptr, &v, g)) || v.Type() != tvtObject) return nullptr;
    return v.AsObjectNoAddRef();
}

// Native path of a folder storage name, without the trailing '/'.
static std::string NativeDir(const ttstr& storage) {
    std::string path = NativePath(storage);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

static bool CopyStorage(const ttstr& from, const ttstr& to, bool fail_if_exists) {
    std::string dest = NativePath(to);
    if (dest.empty()) return false;
    struct stat st;
    if (fail_if_exists && stat(dest.c_str(), &st) == 0) return false;
    std::string src_native = NativePath(from);
    if (!src_native.empty()) { // same file: nothing to do, and "wb" would truncate it
        char a[PATH_MAX], b[PATH_MAX];
        if (realpath(src_native.c_str(), a) && realpath(dest.c_str(), b) && strcmp(a, b) == 0) return true;
    }
    // read the source first (it may be inside an archive), then write the copy
    std::string data;
    {
        tTJSBinaryStream* in = TVPCreateStream(TVPNormalizeStorageName(from), TJS_BS_READ);
        try {
            char buf[65536];
            tjs_uint got;
            while ((got = in->Read(buf, sizeof(buf))) > 0) data.append(buf, got);
        } catch (...) {
            delete in;
            throw;
        }
        delete in;
    }
    std::string tmp = dest + ".kk4s-tmp";
    FILE* out = fopen(tmp.c_str(), "wb");
    if (!out) return false;
    bool ok = fwrite(data.data(), 1, data.size(), out) == data.size();
    if (fclose(out) != 0) ok = false;
    if (ok) {
        remove(dest.c_str()); // rename over an existing file fails on some file systems
        ok = rename(tmp.c_str(), dest.c_str()) == 0;
    }
    if (!ok) remove(tmp.c_str());
    TVPClearStorageCaches();
    return ok;
}

static void LinkFstatDll() {
    iTJSDispatch2* st = StoragesObject();
    if (!st) return;
    DefineFunction(st, TJS_W("deleteFile"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        std::string path = NativePath(*p[0]);
        bool ok = !path.empty() && remove(path.c_str()) == 0;
        TVPClearStorageCaches();
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });
    DefineFunction(st, TJS_W("moveFile"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 2) return TJS_E_BADPARAMCOUNT;
        std::string from = NativePath(*p[0]), to = NativePath(*p[1]);
        bool ok = !from.empty() && !to.empty() && rename(from.c_str(), to.c_str()) == 0;
        TVPClearStorageCaches();
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });
    NativeFn copy = [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 2) return TJS_E_BADPARAMCOUNT;
        bool fail = n >= 3 && p[2]->Type() != tvtVoid && (bool)*p[2];
        bool ok = false;
        try { ok = CopyStorage(*p[0], *p[1], fail); } catch (...) { ok = false; }
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    };
    DefineFunction(st, TJS_W("copyFile"), copy);
    DefineFunction(st, TJS_W("exportFile"), copy);
    DefineFunction(st, TJS_W("createDirectory"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        std::string path = NativeDir(*p[0]);
        bool ok = false;
        if (!path.empty()) { // like mkdir -p
            for (size_t i = 1; i <= path.size(); ++i)
                if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0777);
            struct stat sb;
            ok = stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
        }
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });
    DefineFunction(st, TJS_W("removeDirectory"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        std::string path = NativeDir(*p[0]);
        bool ok = !path.empty() && rmdir(path.c_str()) == 0;
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });
    DefineFunction(st, TJS_W("isExistentDirectory"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        std::string path = NativeDir(*p[0]);
        struct stat sb;
        bool ok = !path.empty() && stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode);
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });
    // kk4sStat(name) -> [size, mtime in ms] or void; Storages.fstat is built on it below
    DefineFunction(st, TJS_W("kk4sStat"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        ttstr name = *p[0];
        tjs_int64 size = -1, mtime = 0;
        std::string path = NativePath(name);
        struct stat sb;
        if (!path.empty() && stat(path.c_str(), &sb) == 0) {
            size = S_ISDIR(sb.st_mode) ? 0 : (tjs_int64)sb.st_size;
            mtime = (tjs_int64)sb.st_mtime * 1000;
        } else if (TVPIsExistentStorageNoSearch(name)) { // inside an archive
            tTJSBinaryStream* s = TVPCreateStream(TVPNormalizeStorageName(name), TJS_BS_READ);
            if (s) { size = (tjs_int64)s->GetSize(); delete s; }
        }
        if (!r) return TJS_S_OK;
        if (size < 0) { r->Clear(); return TJS_S_OK; }
        iTJSDispatch2* arr = TJSCreateArrayObject();
        tTJSVariant a(size), b(mtime);
        arr->PropSetByNum(TJS_MEMBERENSURE, 0, &a, arr);
        arr->PropSetByNum(TJS_MEMBERENSURE, 1, &b, arr);
        *r = tTJSVariant(arr, arr);
        arr->Release();
        return TJS_S_OK;
    });
    RunTjsShim("fstat.dll", TJS_W(R"TJS(
if (typeof Storages.fstat == "undefined") Storages.fstat = function(name) {
    var a = Storages.kk4sStat(name);
    if (a === void) return void;
    var d = new Date();
    d.setTime(a[1]);
    return %[ size: a[0], mtime: d, ctime: d, atime: d ];
};
)TJS"));
}

static void LinkUtilDllAll() {
    LinkUtilDll();
    LinkFstatDll(); // CopyFile / CreateDirectory / ExistDirectory use Storages helpers
    RunTjsShim("util.dll", kUtilShim);
}

// ---------------------------------------------------------------------------
// p.dll ("phib", used by fan translation patches) - readFile

static void LinkPDll() {
    // readFile(name) -> file contents as string, or -1 if it does not exist
    DefineFunction(Global(), TJS_W("readFile"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        ttstr storage = ResolveStorage(*p[0]);
        if (storage.IsEmpty()) {
            if (r) *r = (tjs_int)-1;
            return TJS_S_OK;
        }
        iTJSTextReadStream* s = TVPCreateTextStreamForRead(storage, TJS_W(""));
        ttstr text;
        try { s->Read(text, 0); } catch (...) { s->Destruct(); throw; }
        s->Destruct();
        if (r) *r = text;
        return TJS_S_OK;
    });
}

// ---------------------------------------------------------------------------
// krkrsteam.dll - the "Steam" object for Steam builds (ATRI and others)
//
// steam.tjs links this DLL and turns KAG's [実績 name=...] (achievement) calls
// into Steam calls through a global "Steam" object. Without the object the
// game itself calls kag.errorCmd("実績:...") and dies (ATRI). We have no Steam
// client, so achievements are only logged and cloud files are off; that is
// enough for the game to run. Keep the members steam.tjs touches: it checks
// typeof Steam != 'undefined' and Steam.cloudEnabled, and uses setAchievement,
// clearAchievement, achievementsCount and the cloud* methods.
static void LinkKrrkSteamDll() {
    iTJSDispatch2* g = Global();
    tTJSVariant existing;
    if (TJS_SUCCEEDED(g->PropGet(0, TJS_W("Steam"), nullptr, &existing, g)) && existing.Type() != tvtVoid)
        return; // a real Steam plugin is already there

    iTJSDispatch2* steam = TJSCreateCustomObject();

    auto prop = [steam](const tjs_char* name, const tTJSVariant& v) {
        steam->PropSet(TJS_MEMBERENSURE, name, nullptr, &v, steam);
    };
    tTJSVariant zero((tjs_int)0);
    prop(TJS_W("cloudEnabled"), zero); // no Steam Cloud: steam.tjs hides its cloud menu
    prop(TJS_W("cloudFileCount"), zero);
    prop(TJS_W("achievementsCount"), zero);

    DefineFunction(steam, TJS_W("setAchievement"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        std::string name = n >= 1 ? Utf8(ttstr(*p[0])) : std::string();
        kk4s::Log("steam: achievement '%s' (logged only, no Steam client)", name.c_str());
        if (r) *r = (tjs_int)1;
        return TJS_S_OK;
    });
    DefineFunction(steam, TJS_W("clearAchievement"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        std::string name = n >= 1 ? Utf8(ttstr(*p[0])) : std::string();
        kk4s::Log("steam: clear achievement '%s'", name.c_str());
        if (r) *r = (tjs_int)1;
        return TJS_S_OK;
    });
    DefineFunction(steam, TJS_W("getAchievement"), [](tTJSVariant* r, tjs_int, tTJSVariant**) -> tjs_error {
        if (r) *r = (tjs_int)0;
        return TJS_S_OK;
    });
    // steam_r18hack.tjs (shipped in some Steam builds) hides screenshots and
    // broadcasting; there is no Steam client, so nothing to hook.
    DefineFunction(steam, TJS_W("hookScreenshots"), [](tTJSVariant* r, tjs_int, tTJSVariant**) -> tjs_error {
        if (r) *r = (tjs_int)1;
        return TJS_S_OK;
    });
    DefineFunction(steam, TJS_W("hookBroadcasting"), [](tTJSVariant* r, tjs_int, tTJSVariant**) -> tjs_error {
        if (r) *r = (tjs_int)1;
        return TJS_S_OK;
    });
    DefineFunction(steam, TJS_W("isBroadcasting"), [](tTJSVariant* r, tjs_int, tTJSVariant**) -> tjs_error {
        if (r) *r = (tjs_int)0;
        return TJS_S_OK;
    });
    // Cloud files: no Steam client, so there are none.
    DefineFunction(steam, TJS_W("getCloudFileInfo"), [](tTJSVariant* r, tjs_int, tTJSVariant**) -> tjs_error {
        if (r) r->Clear();
        return TJS_S_OK;
    });
    DefineFunction(steam, TJS_W("deleteCloudFile"), [](tTJSVariant* r, tjs_int, tTJSVariant**) -> tjs_error {
        if (r) *r = (tjs_int)0;
        return TJS_S_OK;
    });
    DefineFunction(steam, TJS_W("copyCloudFile"), [](tTJSVariant* r, tjs_int, tTJSVariant**) -> tjs_error {
        if (r) *r = (tjs_int)0;
        return TJS_S_OK;
    });

    tTJSVariant sv(steam, steam);
    steam->Release();
    g->PropSet(TJS_MEMBERENSURE, TJS_W("Steam"), nullptr, &sv, g);
    kk4s::Log("compat: Steam object registered (achievements logged, no cloud)");
}

// ---------------------------------------------------------------------------
// textrender.dll - KiriKiri Z's text renderer
//
// KAGZ's system/textrender.tjs defines "class TextRender extends TextRenderBase"
// and does most of the work itself (layout, links, ruby, layers); TextRenderBase
// is only the native low-level part. Without the class the script fails to load
// and the game stops at start.ks. Provide the class with no-op drawing for now:
// the game runs, text effects may be missing until the real drawing is ported.
static void LinkTextRenderDll() {
    // Note: a class declared inside "if (...) { }" is block-scoped in TJS2 and
    // its static members are not visible outside, so define it at top level.
    RunTjsShim("textrender.dll", TJS_W(R"TJS(
    class TextRenderBase {
        function TextRenderBase(owner) {}
        function finalize() {}
        function setDefault(options) {}
        function setOption(name, value) {}
        function setFont(face, size, bold, italic, vertical, angle) {}
        function setBase(base) {}
        function onEval(text) { return text; }
        function onLabel(text) { return text; }
        function getDelayLabelTime(text) { return 0; }
        function onFontChange() {}
        function onGetTextWidth(text) { return 0; }
        function getTextWidth(text) { return 0; }
        function onGetGraphSize(name) { return void; }
        function getGraphLayer(name) { return void; }
        function setRenderSize(width, height) {}
        function getRenderWidth() { return 0; }
        function getRenderHeight() { return 0; }
        function drawAll(target) { return 0; }
        // The wrapper calls TextRenderBase.render() as a static (class member).
        function getTextColor() { return 0xFFFFFFFF; }
        function getLinkRects() { return []; }
        function isLinkContains(x, y) { return false; }
        function getLinkOfPosition(x, y) { return void; }
        function clear() {}
        function clearLinkColor() {}
        function setLinkColor(color) {}
        function drawText(layer, x, y) { return 0; }
        function internalDrawText(layer, x, y) { return 0; }
        function drawCh(layer, x, y, ch) { return 0; }
    }
    TextRenderBase.render = function(text, mode, a, b, c) { return 0; };
    TextRenderBase.drawAllMode = 0;
)TJS"));
}

// ---------------------------------------------------------------------------
// registry

// compat/kk4s_layerfx.cpp
void LinkFilterDll();
void LinkDrawerDll();
void LinkSlideOpenDll();
void LinkFireSparkDll();

// KAGParserEx.dll replaces the KAG parser: the engine's own parser does the work; the one
// visible difference games rely on is "taglist" in tag elements (KAGParser.cpp).
static void LinkKAGParserExDll() { ::TVPKAGParserExMode = true; }

// The engine already does what these DLLs did (ogg decoding, video).
static void ProvidedByEngine() {}

struct PluginEntry {
    const char* dll;      // lower case
    void (*link)();
};

static const PluginEntry kPlugins[] = {
    {"util.dll", LinkUtilDllAll},
    {"kagparserex.dll", LinkKAGParserExDll},
    {"fstat.dll", LinkFstatDll},
    {"p.dll", LinkPDll},
    {"krkrsteam.dll", LinkKrrkSteamDll},
    {"textrender.dll", LinkTextRenderDll},
    {"filter.dll", LinkFilterDll},
    {"drawer.dll", LinkDrawerDll},
    {"slideopen.dll", LinkSlideOpenDll},
    {"firespark.dll", LinkFireSparkDll},
    {"wuvorbis.dll", ProvidedByEngine},
    {"krmovie.dll", ProvidedByEngine},
    // extrans transitions (wave, ripple, mosaic, rotate*...) fall back to
    // crossfade in the engine until they are implemented.
    {"extrans.dll", ProvidedByEngine},
};

// One link per game: a plugin registered twice would overwrite globals. This
// must be cleared for every new game (the TJS engine and its globals are new,
// but this file stays loaded when the player returns to the launcher).
static std::set<std::string> g_linked_plugins;

void ResetPluginLinks() { g_linked_plugins.clear(); }

bool LinkBuiltinPlugin(const ttstr& name) {
    std::string n = Utf8(TVPExtractStorageName(name));
    for (char& c : n) c = (char)tolower((unsigned char)c);
    for (const PluginEntry& e : kPlugins) {
        if (n == e.dll) {
            if (g_linked_plugins.insert(n).second) {
                e.link();
                kk4s::Log("compat: plugin %s -> kk4s built-in", n.c_str());
            }
            return true;
        }
    }
    return false;
}

} // namespace compat
} // namespace kk4s
