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

#include "CharacterSet.h"
#include "StorageIntf.h"
#include "TextStream.h"
#include "ScriptMgnIntf.h"
#include "tjsObject.h"
#include "kk4s_platform.h"
#include "kk4s_fs.h"

#include <cctype>
#include <cstdio>
#include <functional>
#include <string>

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
// registry

struct PluginEntry {
    const char* dll;      // lower case
    void (*link)();
};

static const PluginEntry kPlugins[] = {
    {"util.dll", LinkUtilDll},
    {"p.dll", LinkPDll},
};

bool LinkBuiltinPlugin(const ttstr& name) {
    std::string n = Utf8(TVPExtractStorageName(name));
    for (char& c : n) c = (char)tolower((unsigned char)c);
    for (const PluginEntry& e : kPlugins) {
        if (n == e.dll) {
            e.link();
            kk4s::Log("compat: plugin %s -> kk4s built-in", n.c_str());
            return true;
        }
    }
    return false;
}

} // namespace compat
} // namespace kk4s
