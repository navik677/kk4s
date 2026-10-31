// kk4s compatibility layer, part 1: Kirikiroid2 conventions and diagnostics.
//
// Kirikiroid2 (the Android Kirikiri runtime) lets game folders carry small
// TJS files instead of Windows DLLs. Supporting the same files means every
// folder prepared for Kirikiroid2 also runs on kk4s, unmodified:
//
//   xp3filter.tjs   run before startup.tjs; usually calls
//                   Storages.setXP3ArchiveExtractionFilter(func) where
//                   func(hash, offset, buf, len) decrypts buf in place.
//   Override2.tjs   run after the game's Override.tjs (patches KAG classes).
//   AfterInit2.tjs  run after the game's AfterInit.tjs (kag exists).
//
// See docs/kirikiroid-compat.md.
#include "kk4s_compat.h"

#include "CharacterSet.h"
#include "DebugIntf.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "XP3Archive.h"
#include "tjsObject.h"
#include "kk4s_platform.h"
#include "kk4s_internal.h"
#include "kk4s_files.h"
#include "kk4s_settings.h"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <thread>

// engine/source/core/visual/FreeType.cpp
extern void TVPAddSystemFontToFreeType(const std::string& storage, std::vector<tjs_string>* faces);

namespace kk4s {
namespace compat {

// ---------------------------------------------------------------------------
// helpers

static std::string ToUtf8(const ttstr& s) {
    std::string out;
    if (!TVPUtf16ToUtf8(out, s.AsStdString())) out = s.AsNarrowStdString();
    return out;
}

void LogTJS(const ttstr& line) {
    kk4s::Log("%s", ToUtf8(line).c_str());
}

ttstr NativeToStorage(const std::string& native_path) {
    return TVPNormalizeStorageName(ttstr(native_path.c_str()));
}

static std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// Run a .tjs file that sits directly in the game folder. Errors are logged,
// never fatal: a broken helper script must not stop the game.
static bool RunGameScript(const char* name) {
    std::string path = kk4s::FindGameFile(name);
    if (path.empty()) return false;
    // Debugging aid: KK4S_SKIP=AfterInit2.tjs,Override2.tjs disables helpers.
    if (const char* skip = getenv("KK4S_SKIP")) {
        if (Lower(skip).find(Lower(name)) != std::string::npos) {
            kk4s::Log("compat: %s skipped (KK4S_SKIP)", name);
            return false;
        }
    }
    kk4s::Log("compat: running %s", path.c_str());
    try {
        TVPExecuteStorage(NativeToStorage(path));
        return true;
    } catch (const eTJS& e) {
        kk4s::Log("compat: %s failed: %s", name, ToUtf8(e.GetMessage()).c_str());
    } catch (...) {
        kk4s::Log("compat: %s failed (unknown exception)", name);
    }
    return false;
}

// ---------------------------------------------------------------------------
// xp3 extraction filter written in TJS

// The `buf` object passed to the TJS filter (Kirikiroid2 "xp3buffer"):
//   buf[i]                 byte at ptr + i (get / set / ^= ...)
//   buf.xor(start,len,v)   xor bytes [ptr+start, ptr+start+len) with v
//   buf.ptr                base offset (read/write), buf.count = size
class tXP3FilterBuffer : public tTJSDispatch {
public:
    tjs_uint8* Data = nullptr;
    tjs_uint Size = 0;
    tjs_int Ptr = 0;

    void Attach(void* data, tjs_uint size) { Data = (tjs_uint8*)data; Size = size; Ptr = 0; }
    void Detach() { Data = nullptr; Size = 0; Ptr = 0; }

    bool At(tjs_int num, tjs_int64& index) const {
        index = (tjs_int64)Ptr + num;
        return Data && index >= 0 && index < (tjs_int64)Size;
    }

    tjs_error TJS_INTF_METHOD PropGetByNum(tjs_uint32 flag, tjs_int num, tTJSVariant* result,
                                           iTJSDispatch2* objthis) override {
        tjs_int64 i;
        if (result) {
            if (At(num, i)) *result = (tjs_int)Data[i];
            else result->Clear();
        }
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD PropSetByNum(tjs_uint32 flag, tjs_int num, const tTJSVariant* param,
                                           iTJSDispatch2* objthis) override {
        tjs_int64 i;
        if (param && At(num, i)) Data[i] = (tjs_uint8)param->AsInteger();
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD OperationByNum(tjs_uint32 flag, tjs_int num, tTJSVariant* result,
                                             const tTJSVariant* param, iTJSDispatch2* objthis) override {
        tjs_int64 i;
        if (!At(num, i)) return TJS_S_OK;
        tjs_uint32 op = flag & TJS_OP_MASK;
        if (op != TJS_OP_INC && op != TJS_OP_DEC && param == nullptr) return TJS_E_INVALIDPARAM;
        tTJSVariant tmp((tjs_int)Data[i]);
        TJSDoVariantOperation(op, tmp, param);
        Data[i] = (tjs_uint8)tmp.AsInteger();
        if (result) result->CopyRef(tmp);
        return TJS_S_OK;
    }

    tjs_error TJS_INTF_METHOD PropGet(tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
                                      tTJSVariant* result, iTJSDispatch2* objthis) override {
        if (!membername) return TJS_E_NOTIMPL;
        if (!TJS_strcmp(membername, TJS_W("ptr"))) { if (result) *result = (tjs_int)Ptr; return TJS_S_OK; }
        if (!TJS_strcmp(membername, TJS_W("count"))) { if (result) *result = (tjs_int64)Size; return TJS_S_OK; }
        return TJS_E_MEMBERNOTFOUND;
    }

    tjs_error TJS_INTF_METHOD PropSet(tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
                                      const tTJSVariant* param, iTJSDispatch2* objthis) override {
        if (!membername) return TJS_E_NOTIMPL;
        if (!TJS_strcmp(membername, TJS_W("ptr"))) { if (param) Ptr = (tjs_int)param->AsInteger(); return TJS_S_OK; }
        return TJS_E_MEMBERNOTFOUND;
    }

    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
                                       tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                       iTJSDispatch2* objthis) override {
        if (!membername) return TJS_E_NOTIMPL;
        if (!TJS_strcmp(membername, TJS_W("xor"))) {
            if (numparams < 3) return TJS_E_BADPARAMCOUNT;
            tjs_int64 start = (tjs_int64)Ptr + param[0]->AsInteger();
            tjs_int64 end = start + param[1]->AsInteger();
            tjs_uint8 v = (tjs_uint8)param[2]->AsInteger();
            if (start < 0) start = 0;
            if (end > (tjs_int64)Size) end = Size;
            if (Data)
                for (tjs_int64 i = start; i < end; ++i) Data[i] ^= v;
            if (result) result->Clear();
            return TJS_S_OK;
        }
        return TJS_E_MEMBERNOTFOUND;
    }
};

// The filter can be called from audio / image loader threads, so calls are
// serialized. The script function is expected to touch only `buf` and its
// own locals (all Kirikiroid2 filters seen so far do).
static std::recursive_mutex g_filter_mutex;
static tTJSVariant* g_filter_func = nullptr;
static std::atomic<unsigned> g_filter_calls{0};
static std::atomic<bool> g_filter_error_logged{false};

static std::thread::id g_main_thread = std::this_thread::get_id(); // set again in BeforeStartup
static std::atomic<bool> g_off_thread_logged{false};

static void TJS_INTF_METHOD ScriptExtractionFilter(tTVPXP3ExtractionFilterInfo* info) {
    if (!info || !info->Buffer || info->BufferSize == 0) return;
    // TJS is not thread-safe. Archive streams opened on the main thread are
    // decrypted completely there (XP3Archive.cpp), so this should only run on
    // the main thread; log once if a stream was opened elsewhere.
    if (std::this_thread::get_id() != g_main_thread && !g_off_thread_logged.exchange(true))
        kk4s::Log("compat: xp3 filter script called on a background thread (unsafe)");
    std::lock_guard<std::recursive_mutex> lock(g_filter_mutex);
    if (!g_filter_func) return;

    tXP3FilterBuffer* buf = new tXP3FilterBuffer();
    buf->Attach(info->Buffer, info->BufferSize);
    tTJSVariant vbuf(buf, buf);
    buf->Release(); // vbuf holds the reference now

    tTJSVariant a0((tjs_int64)info->FileHash);
    tTJSVariant a1((tjs_int64)info->Offset);
    tTJSVariant a3((tjs_int64)info->BufferSize);
    tTJSVariant* args[4] = {&a0, &a1, &vbuf, &a3};
    try {
        tTJSVariantClosure clo = g_filter_func->AsObjectClosureNoAddRef();
        clo.FuncCall(0, nullptr, nullptr, nullptr, 4, args, nullptr);
    } catch (const eTJS& e) {
        if (!g_filter_error_logged.exchange(true))
            kk4s::Log("compat: xp3 filter script error: %s", ToUtf8(e.GetMessage()).c_str());
    } catch (...) {
        if (!g_filter_error_logged.exchange(true))
            kk4s::Log("compat: xp3 filter script error (unknown exception)");
    }
    buf->Detach(); // the script must not keep using a stale buffer
    g_filter_calls++;
}

// Storages.setXP3ArchiveExtractionFilter(func) - void/null removes it.
class tSetXP3FilterFunction : public tTJSDispatch {
public:
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag, const tjs_char* membername, tjs_uint32* hint,
                                       tTJSVariant* result, tjs_int numparams, tTJSVariant** param,
                                       iTJSDispatch2* objthis) override {
        if (membername) return TJS_E_MEMBERNOTFOUND;
        if (numparams < 1) return TJS_E_BADPARAMCOUNT;
        std::lock_guard<std::recursive_mutex> lock(g_filter_mutex);
        const tTJSVariant& f = *param[0];
        bool clear = f.Type() != tvtObject || f.AsObjectNoAddRef() == nullptr;
        delete g_filter_func;
        g_filter_func = nullptr;
        if (clear) {
            TVPSetXP3ArchiveExtractionFilter(nullptr);
            kk4s::Log("compat: xp3 extraction filter removed");
        } else {
            g_filter_func = new tTJSVariant(f);
            TVPSetXP3ArchiveExtractionFilter(ScriptExtractionFilter);
            kk4s::Log("compat: xp3 extraction filter set by script");
        }
        if (result) result->Clear();
        return TJS_S_OK;
    }
};

static void RegisterStoragesExtensions() {
    iTJSDispatch2* global = TVPGetScriptEngine()->GetGlobalNoAddRef();
    if (!global) return;
    tTJSVariant storages;
    if (TJS_FAILED(global->PropGet(0, TJS_W("Storages"), nullptr, &storages, global)) ||
        storages.Type() != tvtObject) {
        kk4s::Log("compat: Storages object not found");
        return;
    }
    iTJSDispatch2* st = storages.AsObjectNoAddRef();
    tTJSVariant existing;
    if (TJS_SUCCEEDED(st->PropGet(0, TJS_W("setXP3ArchiveExtractionFilter"), nullptr, &existing, st)) &&
        existing.Type() == tvtObject)
        return; // engine already provides it

    iTJSDispatch2* fn = new tSetXP3FilterFunction();
    tTJSVariant v(fn, nullptr);
    fn->Release();
    st->PropSet(TJS_MEMBERENSURE, TJS_W("setXP3ArchiveExtractionFilter"), nullptr, &v, st);
}

// ---------------------------------------------------------------------------
// hook points

static bool g_override2_done = false;
static bool g_afterinit2_done = false;

static void RunOverride2() {
    if (g_override2_done) return;
    g_override2_done = true;
    RunGameScript("Override2.tjs");
}

static void RunAfterInit2() {
    if (g_afterinit2_done) return;
    RunOverride2(); // never skip it, even if no earlier hook point fired
    g_afterinit2_done = true;
    RunGameScript("AfterInit2.tjs");
}

std::string RedirectDataPath(const std::string& original) {
    Settings s;
    s.Load();
    if (!s.separate_saves || kk4s::GameDir().empty()) return "";
    std::string game = kk4s::GameDir();
    while (!game.empty() && game.back() == '/') game.pop_back();
    game = game.substr(game.find_last_of('/') + 1);
    std::string target = SaveFolderFor(s, game);
    if (!files::IsDirectory(target)) {
        files::MakeDirs(target);
        files::DirStats old = files::Stats(original);
        if (old.files > 0) {
            bool ok = files::CopyDir(original, target);
            kk4s::Log("compat: saves: copied %d file(s) from %s to %s%s", old.files, original.c_str(), target.c_str(),
                      ok ? "" : " (some failed)");
        }
    }
    kk4s::Log("compat: saves in %s", target.c_str());
    return target;
}

bool IsMainThread() { return std::this_thread::get_id() == g_main_thread; }

bool ScriptFilterActive() {
    std::lock_guard<std::recursive_mutex> lock(g_filter_mutex);
    return g_filter_func != nullptr;
}

void BeforeStartup() {
    g_main_thread = std::this_thread::get_id();
    RegisterFonts();
    RegisterStoragesExtensions();
    RunGameScript("xp3filter.tjs");
}

void AfterExecuteStorage(const ttstr& shortname) {
    std::string n = Lower(ToUtf8(shortname));
    if (n == "override.tjs") {
        RunOverride2();
    } else if (n == "mainwindow.tjs") {
        // KAG classes are defined now. If the game has no Override.tjs,
        // this is the last moment before `kag` is created.
        if (!TVPIsExistentStorage(TJS_W("Override.tjs"))) RunOverride2();
    } else if (n == "afterinit.tjs" || n == "initialize.tjs") {
        RunAfterInit2();
    }
}

void AfterStartup() {
    RunAfterInit2();
    if (g_filter_calls)
        kk4s::Log("compat: xp3 filter called %u times so far", g_filter_calls.load());
}

// ---------------------------------------------------------------------------
// fonts


void RegisterFonts() {
    static bool done = false;
    if (done) return;
    done = true;
    std::vector<std::string> files = kk4s::SystemFontFiles();
    // Fonts shipped next to the game exe (krkr.exe games often expect them
    // to be "installed"), e.g. monakrkr.ttf.
    if (!kk4s::GameDir().empty()) kk4s::detail::ListFontFiles(kk4s::GameDir(), false, files);
    unsigned count = 0;
    for (const std::string& f : files) {
        std::vector<tjs_string> faces;
        try {
            TVPAddSystemFontToFreeType(f, &faces);
        } catch (...) {
            kk4s::Log("fonts: cannot load %s", f.c_str());
            continue;
        }
        for (const tjs_string& face : faces)
            kk4s::Log("fonts: %s (%s)", ToUtf8(ttstr(face)).c_str(), f.c_str());
        count += (unsigned)faces.size();
    }
    kk4s::Log("fonts: %u face(s) from %zu file(s)", count, files.size());
}

void ReportFontFallback(const ttstr& requested, const ttstr& chosen) {
    static std::mutex m;
    static std::set<std::string> seen;
    std::string r = ToUtf8(requested);
    std::lock_guard<std::mutex> lock(m);
    if (seen.insert(r).second)
        kk4s::Log("fonts: '%s' not found, using '%s'", r.c_str(), ToUtf8(chosen).c_str());
}

void ReportCyrillicSubstitute(const ttstr& font, const ttstr& chosen) {
    static std::mutex m;
    static std::set<std::string> seen;
    std::string f = ToUtf8(font);
    std::lock_guard<std::mutex> lock(m);
    if (!seen.insert(f).second) return;
    if (chosen.IsEmpty())
        kk4s::Log("fonts: '%s' has full-width Cyrillic, no proportional font to replace it", f.c_str());
    else
        kk4s::Log("fonts: '%s' has full-width Cyrillic, Cyrillic taken from '%s'", f.c_str(), ToUtf8(chosen).c_str());
}

// ---------------------------------------------------------------------------
// diagnostics

static std::mutex g_missing_mutex;
static std::set<std::string> g_missing_plugins;

void ReportMissingPlugin(const ttstr& name) {
    std::string n = ToUtf8(TVPExtractStorageName(name));
    std::lock_guard<std::mutex> lock(g_missing_mutex);
    if (g_missing_plugins.insert(Lower(n)).second)
        kk4s::Log("compat: plugin not available, ignored: %s", n.c_str());
}

} // namespace compat
} // namespace kk4s
