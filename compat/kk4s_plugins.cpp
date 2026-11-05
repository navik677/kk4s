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
#include "GraphicsLoaderIntf.h"
#include "StorageIntf.h"
#include "TextStream.h"
#include "ScriptMgnIntf.h"
#include "tjsObject.h"
#include "tjsArray.h"
#include "kk4s_platform.h"
#include "kk4s_fs.h"
#include "md5.h"

#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <vector>

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

// ---------------------------------------------------------------------------
// fstat.dll, the rest of its contract (wamsoft/fstat manual.tjs): listings,
// timestamps, attributes, MD5, temporary files. Native helpers return plain
// numbers/arrays; the TJS part below builds the Date objects and dictionaries.
// Timestamps are milliseconds since 1970 (Date.setTime); FILETIME values are
// 100 ns ticks since 1601 like on Windows.

// Times are whole seconds: st_mtime / st_atime / st_ctime exist on Linux, MinGW
// and the Switch's newlib alike (st_mtim does not).
static tjs_int64 MsOf(time_t t) { return (tjs_int64)t * 1000; }

// Windows-style attributes of a native path (0 if missing).
static tjs_int AttributesOf(const std::string& path, const struct stat& sb) {
    tjs_int a = S_ISDIR(sb.st_mode) ? 0x10 : 0x20;               // DIRECTORY / ARCHIVE
#ifndef __SWITCH__
    if (!(sb.st_mode & S_IWUSR)) a |= 0x01;                       // READONLY (no permissions on the SD card)
#endif
    const char* base = strrchr(path.c_str(), '/');
    base = base ? base + 1 : path.c_str();
    if (base[0] == '.' && strcmp(base, ".") && strcmp(base, "..")) a |= 0x02; // HIDDEN
    return a;
}

// [size, mtime, atime, ctime, attrib] of a native file, or void.
static bool StatArray(const std::string& path, tTJSVariant* r) {
    struct stat sb;
    if (path.empty() || stat(path.c_str(), &sb) != 0) return false;
    if (!r) return true;
    iTJSDispatch2* arr = TJSCreateArrayObject();
    tTJSVariant v[5] = {(tjs_int64)(S_ISDIR(sb.st_mode) ? 0 : sb.st_size), MsOf(sb.st_mtime), MsOf(sb.st_atime),
                        MsOf(sb.st_ctime), AttributesOf(path, sb)};
    for (int i = 0; i < 5; ++i) arr->PropSetByNum(TJS_MEMBERENSURE, i, &v[i], arr);
    *r = tTJSVariant(arr, arr);
    arr->Release();
    return true;
}

// The Switch's SD card file system cannot set times through libnx; false there.
static bool SetFileTimesMs(const std::string& path, tjs_int64 mtime_ms, tjs_int64 atime_ms) {
#ifdef __SWITCH__
    (void)path; (void)mtime_ms; (void)atime_ms;
    return false;
#else
    struct utimbuf t;
    t.actime = (time_t)(atime_ms / 1000);
    t.modtime = (time_t)(mtime_ms / 1000);
    return utime(path.c_str(), &t) == 0;
#endif
}

static bool InsideGameDir(const std::string& path) {
    char a[PATH_MAX], b[PATH_MAX];
    if (!realpath(path.c_str(), a) || !realpath(kk4s::GameDir().c_str(), b)) return false;
    size_t n = strlen(b);
    return strncmp(a, b, n) == 0 && (a[n] == '/' || a[n] == 0);
}

static void LinkFstatExtras(iTJSDispatch2* st) {
    // kk4sStatEx(name) -> [size, mtime, atime, ctime, attrib] of a real file/folder, or void
    DefineFunction(st, TJS_W("kk4sStatEx"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        StatArray(NativeDir(*p[0]), r);
        return TJS_S_OK;
    });
    // kk4sArchiveSize(name) -> size of a file inside an archive, or -1
    DefineFunction(st, TJS_W("kk4sArchiveSize"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        tjs_int64 size = -1;
        ttstr placed = TVPGetPlacedPath(*p[0]);
        if (!placed.IsEmpty()) {
            tTJSBinaryStream* s = TVPCreateStream(placed, TJS_BS_READ);
            if (s) { size = (tjs_int64)s->GetSize(); delete s; }
        }
        if (r) *r = size;
        return TJS_S_OK;
    });
    // kk4sListDir(dir) -> [[name, size, mtime, atime, ctime, attrib], ...] (with "." and "..",
    // as FindFirstFile returns them), or void if the folder does not exist
    DefineFunction(st, TJS_W("kk4sListDir"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        std::string dir = NativeDir(*p[0]);
        DIR* d = dir.empty() ? nullptr : opendir(dir.c_str());
        if (!d) return TJS_S_OK;
        iTJSDispatch2* arr = TJSCreateArrayObject();
        tjs_int count = 0;
        std::vector<std::string> names;
        while (struct dirent* e = readdir(d)) names.push_back(e->d_name);
        closedir(d);
        for (const std::string& name : names) {
            std::string full = dir + "/" + name;
            struct stat sb;
            if (stat(full.c_str(), &sb) != 0) continue;
            iTJSDispatch2* item = TJSCreateArrayObject();
            tjs_string wide;
            ttstr wname = TVPUtf8ToUtf16(wide, name) ? ttstr(wide.c_str()) : ttstr(name.c_str());
            tTJSVariant v[6] = {wname, (tjs_int64)(S_ISDIR(sb.st_mode) ? 0 : sb.st_size), MsOf(sb.st_mtime),
                                MsOf(sb.st_atime), MsOf(sb.st_ctime), AttributesOf(full, sb)};
            for (int i = 0; i < 6; ++i) item->PropSetByNum(TJS_MEMBERENSURE, i, &v[i], item);
            tTJSVariant iv(item, item);
            item->Release();
            arr->PropSetByNum(TJS_MEMBERENSURE, count++, &iv, arr);
        }
        if (r) *r = tTJSVariant(arr, arr);
        arr->Release();
        return TJS_S_OK;
    });
    // kk4sSetTimes(name, mtimeMs, atimeMs) -> true if the file exists
    DefineFunction(st, TJS_W("kk4sSetTimes"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 3) return TJS_E_BADPARAMCOUNT;
        std::string path = NativeDir(*p[0]);
        struct stat sb;
        bool ok = !path.empty() && stat(path.c_str(), &sb) == 0 && !InsideGameDir(path);
        if (ok) {
            tjs_int64 m = p[1]->Type() == tvtVoid ? MsOf(sb.st_mtime) : p[1]->AsInteger();
            tjs_int64 a = p[2]->Type() == tvtVoid ? MsOf(sb.st_atime) : p[2]->AsInteger();
            ok = SetFileTimesMs(path, m, a);
        }
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });
    DefineFunction(st, TJS_W("truncateFile"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 2) return TJS_E_BADPARAMCOUNT;
        std::string path = NativePath(*p[0]);
        bool ok = false;
        if (!path.empty() && !InsideGameDir(path)) {
            FILE* f = fopen(path.c_str(), "r+b");
            if (f) {
                ok = ftruncate(fileno(f), (off_t)p[1]->AsInteger()) == 0;
                fclose(f);
            }
        }
        TVPClearStorageCaches();
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });
    // Attributes: READONLY maps to the write permission, the rest are reported only.
    auto change_attr = [](tjs_int n, tTJSVariant** p, bool set) -> bool {
        std::string path = NativeDir(*p[0]);
        struct stat sb;
        if (path.empty() || stat(path.c_str(), &sb) != 0) return false;
        tjs_int attr = (tjs_int)p[1]->AsInteger();
#ifndef __SWITCH__ // the SD card has no permissions to change
        if ((attr & 0x01) && !InsideGameDir(path)) {
            mode_t m = set ? (sb.st_mode & ~(mode_t)0222) : (sb.st_mode | S_IWUSR);
            chmod(path.c_str(), m);
        }
#else
        (void)attr; (void)set;
#endif
        return true;
    };
    DefineFunction(st, TJS_W("setFileAttributes"), [change_attr](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 2) return TJS_E_BADPARAMCOUNT;
        bool ok = change_attr(n, p, true);
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });
    DefineFunction(st, TJS_W("resetFileAttributes"), [change_attr](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 2) return TJS_E_BADPARAMCOUNT;
        bool ok = change_attr(n, p, false);
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });
    DefineFunction(st, TJS_W("getFileAttributes"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        std::string path = NativeDir(*p[0]);
        struct stat sb;
        tjs_int a = (!path.empty() && stat(path.c_str(), &sb) == 0) ? AttributesOf(path, sb) : 0;
        if (r) *r = a;
        return TJS_S_OK;
    });
    // getMD5HashString(file): lower-case hex MD5 of the storage (archives included)
    DefineFunction(st, TJS_W("getMD5HashString"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        tTJSBinaryStream* in = TVPCreateStream(TVPGetPlacedPath(*p[0]), TJS_BS_READ);
        md5_state_t state;
        md5_init(&state);
        try {
            tjs_uint8 buf[65536];
            tjs_uint got;
            while ((got = in->Read(buf, sizeof(buf))) > 0) md5_append(&state, buf, (int)got);
        } catch (...) {
            delete in;
            throw;
        }
        delete in;
        md5_byte_t digest[16];
        md5_finish(&state, digest);
        char hex[33];
        for (int i = 0; i < 16; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
        if (r) *r = ttstr(hex);
        return TJS_S_OK;
    });
    DefineFunction(st, TJS_W("getTemporaryName"), [](tTJSVariant* r, tjs_int, tTJSVariant**) -> tjs_error {
        if (r) *r = TVPGetTemporaryName();
        return TJS_S_OK;
    });
    DefineFunction(st, TJS_W("isExistentStorageNoSearchNoNormalize"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        if (r) *r = (tjs_int)TVPIsExistentStorageNoSearchNoNormalize(*p[0]);
        return TJS_S_OK;
    });
    // kk4sRemoveTemp(name): TemporaryFiles' delete-on-close, never inside the game folder
    DefineFunction(st, TJS_W("kk4sRemoveTemp"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        std::string path = NativeDir(*p[0]);
        bool ok = !path.empty() && !InsideGameDir(path) && remove(path.c_str()) == 0;
        TVPClearStorageCaches();
        if (r) *r = (tjs_int)ok;
        return TJS_S_OK;
    });
    DefineFunction(st, TJS_W("clearStorageCaches"), [](tTJSVariant*, tjs_int, tTJSVariant**) -> tjs_error {
        TVPClearStorageCaches();
        return TJS_S_OK;
    });

    RunTjsShim("fstat.dll", TJS_W(R"TJS(
{
	var attrs = %[ FILE_ATTRIBUTE_READONLY: 0x01, FILE_ATTRIBUTE_HIDDEN: 0x02, FILE_ATTRIBUTE_SYSTEM: 0x04,
	               FILE_ATTRIBUTE_DIRECTORY: 0x10, FILE_ATTRIBUTE_ARCHIVE: 0x20, FILE_ATTRIBUTE_NORMAL: 0x80,
	               FILE_ATTRIBUTE_TEMPORARY: 0x100 ];
	var keys = [];
	keys.assign(attrs);
	for (var i = 0; i < keys.count; i += 2)
		if (typeof global[keys[i]] == "undefined") global[keys[i]] = keys[i + 1];
}

global.kk4s_fstat_date = function(ms) { var d = new Date(); d.setTime(ms); return d; };
global.kk4s_fstat_dir = function(dir) {
	dir = Storages.getFullPath(dir);
	if (dir.charAt(dir.length - 1) != "/")
		throw new Exception("'/' must be specified at the end of given directory name.");
	var list = Storages.kk4sListDir(dir);
	if (list === void) throw new Exception("Directory not found.");
	return list;
};

// fstat.dll always replaces these three (its versions add atime / ctime).
Storages.fstat = function(name) {
	var placed = Storages.getPlacedPath(name);
	var a = Storages.kk4sStatEx(placed != "" ? placed : name);
	if (a === void) {
		var size = Storages.kk4sArchiveSize(name);   // inside an archive: size only
		if (size >= 0) return %[ size: size ];
		return void;	// the DLL throws; kk4s always returned void here (util.dll games such as FSN use it)
	}
	var r = %[ mtime: kk4s_fstat_date(a[1]), atime: kk4s_fstat_date(a[2]), ctime: kk4s_fstat_date(a[3]) ];
	if (!(a[4] & 0x10)) r.size = a[0];
	return r;
};
Storages.getTime = function(name) {
	var a = Storages.kk4sStatEx(name);
	if (a === void) throw new Exception("cannot open : " + name);
	return %[ mtime: kk4s_fstat_date(a[1]), atime: kk4s_fstat_date(a[2]), ctime: kk4s_fstat_date(a[3]) ];
};
Storages.setTime = function(name, times) {
	var m = times.mtime !== void ? times.mtime.getTime() : void;
	var a = times.atime !== void ? times.atime.getTime() : void;
	return Storages.kk4sSetTimes(name, m, a);	// ctime cannot be set on POSIX
};
if (typeof Storages.getLastModifiedFileTime == "undefined") Storages.getLastModifiedFileTime = function(name) {
	var a = Storages.kk4sStatEx(name);
	if (a === void) throw new Exception("cannot open : " + name);
	return a[1] * 10000 + 116444736000000000;
};
if (typeof Storages.setLastModifiedFileTime == "undefined") Storages.setLastModifiedFileTime = function(name, ft) {
	return Storages.kk4sSetTimes(name, (ft - 116444736000000000) \ 10000, void);
};
if (typeof Storages.dirlist == "undefined") Storages.dirlist = function(dir) {
	var list = kk4s_fstat_dir(dir), r = [];
	for (var i = 0; i < list.count; i++) r.add(list[i][0] + ((list[i][5] & 0x10) ? "/" : ""));
	return r;
};
if (typeof Storages.dirlistEx == "undefined") Storages.dirlistEx = function(dir) {
	var list = kk4s_fstat_dir(dir), r = [];
	for (var i = 0; i < list.count; i++) {
		var e = list[i];
		r.add(%[ name: e[0], size: e[1], attrib: e[5], mtime: kk4s_fstat_date(e[2]),
		         atime: kk4s_fstat_date(e[3]), ctime: kk4s_fstat_date(e[4]) ]);
	}
	return r;
};
global.kk4s_fstat_walk = function(root, rel, dironly, r) {
	var list = Storages.kk4sListDir(root + rel);
	if (list === void) return;
	for (var i = 0; i < list.count; i++) {
		var name = list[i][0];
		if (name == "." || name == "..") continue;
		if (list[i][5] & 0x10) { r.add(rel + name + "/"); kk4s_fstat_walk(root, rel + name + "/", dironly, r); }
		else if (!dironly) r.add(rel + name);
	}
};
if (typeof Storages.dirtree == "undefined") Storages.dirtree = function(dir, dironly = false) {
	var root = Storages.getFullPath(dir), r = [];
	if (root.charAt(root.length - 1) != "/")
		throw new Exception("'/' must be specified at the end of given directory name.");
	kk4s_fstat_walk(root, "", dironly, r);
	return r;
};
if (typeof Storages.createDirectoryNoNormalize == "undefined")
	Storages.createDirectoryNoNormalize = function(dir) { return Storages.createDirectory(dir); };
if (typeof Storages.copyFileNoNormalize == "undefined")
	Storages.copyFileNoNormalize = function(from, to, failIfExist) { return Storages.copyFile(from, to, failIfExist); };
if (typeof Storages.changeDirectory == "undefined")
	Storages.changeDirectory = function(dir) { return Storages.isExistentDirectory(dir); };	// one virtual folder tree
if (typeof Storages.getDisplayName == "undefined")
	Storages.getDisplayName = function(name) { return Storages.extractStorageName(name); };
if (typeof Storages.searchPath == "undefined")
	Storages.searchPath = function(name, path) { return void; };	// no Windows search path
if (typeof Storages.selectDirectory == "undefined")
	Storages.selectDirectory = function(params) { return false; };	// no folder dialog
class kk4s_TemporaryFiles {
	var kk4s_names = [];
	function kk4s_TemporaryFiles() {}
	function finalize() {
		for (var i = kk4s_names.count - 1; i >= 0; i--) Storages.kk4sRemoveTemp(kk4s_names[i]);
	}
	function entry(name) {
		if (!Storages.isExistentStorage(name)) return false;
		kk4s_names.add(name); return true;
	}
	function entryFolder(name) {
		if (!Storages.isExistentDirectory(name)) return false;
		kk4s_names.add(name); return true;
	}
}
if (typeof global.TemporaryFiles == "undefined") global.TemporaryFiles = kk4s_TemporaryFiles;
)TJS"));
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
    LinkFstatExtras(st);
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
// and calls its static render entry point for the low-level layout. This
// replacement keeps per-instance layout results and draws glyphs through the
// engine Layer API. It is a generic first implementation; native edge cases
// still need a broader conformance suite.
static void LinkTextRenderDll() {
    // Note: a class declared inside "if (...) { }" is block-scoped in TJS2 and
    // its static members are not visible outside, so define it at top level.
    RunTjsShim("textrender.dll", TJS_W(R"TJS(
    global.__kk4sTextRenderActive = false;
    class TextRenderBase {
        function TextRenderBase(owner) { this.owner = owner; }
        // All render data is TJS-managed; no native allocation needs releasing.
        function finalize() {}
        // KiriKiri Z's langrender.tjs sets this on TextRender instances while
        // selecting main/sub-language font sizes. It is a per-renderer scale,
        // not a game-specific setting.
        var fontScale = 1.0;
        // Layout results the native renderer publishes after render()/drawText();
        // textrender.tjs getRect() reads them (renderRight - renderLeft etc.).
        var renderLeft = 0, renderTop = 0, renderRight = 0, renderBottom = 0;
        var renderCount = 0, renderLines = 0, renderText = "";
        var renderDelay = 0, renderOver = 0, kinsoku_max = 1;
        var defaultFontSize = 24, defaultLineSize = 0, lineSize = 0;
        var defaultLineSpacing = 6, lineSpacing = 6, defaultPitch = 0, pitch = 0;
        var defaultChColor = 0xFFFFFF, chColor = 0xFFFFFF;
        var defaultRubySize = 10, rubySize = 10, defaultRubyOffset = -2, rubyOffset = -2;
        var defaultShadow = true, shadow = true, defaultShadowColor = 0, shadowColor = 0;
        var defaultEdge = false, edge = false, defaultEdgeColor = 0x0080FF, edgeColor = 0x0080FF;
        var defaultBold = false, defaultItalic = false, bold = false, italic = false;
        var vertical = false;
        var align = -1, antialiased = true;
        var edgeExtent = 1, edgeEmphasis = 512;
        var shadowOffsetX = 2, shadowOffsetY = 2, shadowWidth = 0;
        var marginRCh = 1, following = "", leading = "";
        var begin = "", end = "";
        var window = void, defaultFace = "", userFace = "", forceRubyFontFace = "";
        var defaultAlign = -1;
        var font = void, base = void;
        var _kk4sLinkColor = void, _kk4sDefaultLinkColor = void;
        var _kk4sWidth = 0, _kk4sHeight = 0, _kk4sChars = [];
        function setDefault(options) {
            if (options === void) return;
            if (options.size !== void) defaultFontSize = int(options.size);
            if (options.color !== void) defaultChColor = int(options.color);
            if (options.bold !== void) defaultBold = !!options.bold;
            if (options.italic !== void) defaultItalic = !!options.italic;
            if (options.rubysize !== void) defaultRubySize = int(options.rubysize);
            if (options.rubyoffset !== void) defaultRubyOffset = int(options.rubyoffset);
            if (options.shadow !== void) defaultShadow = !!options.shadow;
            if (options.shadowcolor !== void) defaultShadowColor = int(options.shadowcolor);
            if (options.edge !== void) defaultEdge = !!options.edge;
            if (options.edgecolor !== void) defaultEdgeColor = int(options.edgecolor);
            if (options.linespacing !== void) defaultLineSpacing = int(options.linespacing);
            if (options.pitch !== void) defaultPitch = int(options.pitch);
            if (options.linesize !== void) defaultLineSize = int(options.linesize);
        }
        function setOption(options) {
            if (options === void) return;
            if (options.following !== void) following = options.following;
            if (options.leading !== void) leading = options.leading;
            if (options.marginRCh !== void) marginRCh = int(options.marginRCh);
            if (options.kinsoku_max !== void) kinsoku_max = int(options.kinsoku_max);
            if (options.vertical !== void) vertical = !!options.vertical;
            if (options.window !== void) window = options.window;
            if (options.begin !== void) begin = options.begin;
            if (options.end !== void) end = options.end;
            if (options.defaultFace !== void) defaultFace = options.defaultFace;
            if (options.userFace !== void) userFace = options.userFace;
            if (options.forceRubyFontFace !== void) forceRubyFontFace = options.forceRubyFontFace;
            if (options.defaultAlign !== void) defaultAlign = options.defaultAlign;
            if (options.antialiased !== void) antialiased = !!options.antialiased;
            if (options.edgeExtent !== void) edgeExtent = int(options.edgeExtent);
            if (options.edgeEmphasis !== void) edgeEmphasis = int(options.edgeEmphasis);
            if (options.shadowOffsetX !== void) shadowOffsetX = int(options.shadowOffsetX);
            if (options.shadowOffsetY !== void) shadowOffsetY = int(options.shadowOffsetY);
            if (options.shadowWidth !== void) shadowWidth = int(options.shadowWidth);
        }
        function setFont(face) { font = face; }
        // langrender.tjs calls this after applying kag.getLanguageFont() to defaultFace;
        // the native renderer rebuilds its font from the current settings here.
        function resetFont() {
            bold = defaultBold; italic = defaultItalic; chColor = defaultChColor;
            rubySize = defaultRubySize; rubyOffset = defaultRubyOffset;
            shadow = defaultShadow; shadowColor = defaultShadowColor;
            edge = defaultEdge; edgeColor = defaultEdgeColor;
            if (font !== void) font.height = defaultFontSize;
        }
        function setBase(value) { base = value; }
        function onEval(text) { return text; }
        function onLabel(text) { return text; }
        function getDelayLabelTime(text) {
            if (base !== void && typeof base.getDelayLabelTime == "Object")
                return base.getDelayLabelTime(text);
            return 0;
        }
        function onFontChange() {
            if (font === void) return;
            font.bold = !!bold; font.italic = !!italic;
        }
        function onGetTextWidth(text) { return getTextWidth(text); }
        function getTextWidth(text) {
            if (font === void || typeof font.getTextWidth != "Object") return 0;
            return font.getTextWidth(text);
        }
        function onGetGraphSize(name) {
            var layer = getGraphLayer(name);
            if (layer === void) return void;
            var size = %[width:layer.imageWidth, height:layer.imageHeight];
            invalidate layer;
            return size;
        }
        function getGraphLayer(name) {
            var win = global.Window.mainWindow;
            var layer = new Layer(win, win.poolLayer);
            try {
                var comma = name.indexOf(",");
                if (name.length > 4 && name.charAt(0) == "<" && name.charAt(name.length - 1) == ">" && comma > 1) {
                    var widthText = name.substr(1, comma - 1);
                    var heightText = name.substr(comma + 1, name.length - comma - 2);
                    var width = int(widthText), height = int(heightText);
                    if (width > 0 && height > 0 && "" + width == widthText && "" + height == heightText) {
                        layer.setImageSize(width, height);
                        layer.fillRect(0, 0, width, height, 0);
                    } else {
                        layer.loadImages(name);
                    }
                } else {
                    layer.loadImages(name);
                }
            }
            catch { invalidate layer; return void; }
            return layer;
        }
        function setRenderSize(width, height) {
            _kk4sWidth = int(width); _kk4sHeight = int(height);
            global.__kk4sTextRenderActive = this;
        }
        function getRenderWidth() { return _kk4sWidth; }
        function getRenderHeight() { return _kk4sHeight; }
        function drawAll(target, x, y) {
            if (x === void) x = 0;
            if (y === void) y = 0;
            var chars = getCharacters(0, 0);
            for (var i = 0; i < chars.count; i++) drawCh(target, x, y, chars[i]);
            return chars.count;
        }
        // The wrapper calls TextRenderBase.render() as a static (class member).
        function getTextColor(ch) {
            if (ch !== void && typeof ch.link != "undefined" && ch.link == "") {
                var linkColor = _kk4sLinkColor;
                if (linkColor !== void) return linkColor;
                linkColor = _kk4sDefaultLinkColor;
                if (linkColor !== void) return linkColor;
            }
            return ch !== void && typeof ch.color != "undefined" ? ch.color : chColor;
        }
        function clear() {
            _kk4sChars = [];
            renderLeft = renderTop = renderRight = renderBottom = 0;
            renderCount = renderLines = renderDelay = renderOver = 0;
            renderText = "";
            resetFont();
            resetStyle();
            global.__kk4sTextRenderActive = this;
        }
        function clearLinkColor() { _kk4sLinkColor = void; _kk4sDefaultLinkColor = void; }
        function setLinkColor(color, link) {
            if (link === void) link = "default";
            if (link == "") _kk4sLinkColor = color;
            else if (link == "default") _kk4sDefaultLinkColor = color;
        }
        function drawText(layer, x, y) { return drawAll(layer, x, y); }
        function internalDrawText(layer, x, y) { return drawText(layer, x, y); }
        function drawCh(layer, x, y, ch) {
            if (ch === void || ch.text === void) return false;
            var f = layer.font;
            var oldHeight = void, oldBold = void, oldItalic = void;
            if (f !== void) {
                oldHeight = f.height; oldBold = f.bold; oldItalic = f.italic;
                f.height = ch.size;
                if (ch.bold !== void) f.bold = ch.bold;
                if (ch.italic !== void) f.italic = ch.italic;
            }
            var color = getTextColor(ch);
            if (color === void) color = 0xFFFFFF;
            try {
                if (ch.graph !== void && ch.graph != "") {
                    kk4sDrawGraph(layer, x, y, ch);
                } else if (ch.edge !== void) {
                    layer.drawText(x + ch.x, y + ch.y, ch.text, color, 255, antialiased,
                        edgeEmphasis, ch.edge, edgeExtent, 0, 0);
                } else if (ch.shadow !== void) {
                    layer.drawText(x + ch.x, y + ch.y, ch.text, color, 255, antialiased,
                        255, ch.shadow, shadowWidth, shadowOffsetX, shadowOffsetY);
                } else {
                    layer.drawText(x + ch.x, y + ch.y, ch.text, color, 255, antialiased);
                }
                if (ch.ruby !== void) {
                    if (ch.ruby instanceof "Array") {
                        for (var ri = 0; ri < ch.ruby.count; ri++) drawRuby(layer, x, y, ch, ch.ruby[ri]);
                    } else {
                        drawRuby(layer, x, y, ch, ch.ruby);
                    }
                }
            } catch (e) {
                if (f !== void) { f.height = oldHeight; f.bold = oldBold; f.italic = oldItalic; }
                throw e;
            }
            if (f !== void) { f.height = oldHeight; f.bold = oldBold; f.italic = oldItalic; }
            return true;
        }
        function drawRuby(layer, ox, oy, ch, ruby) {
            if (ruby === void) return;
            var r = new Dictionary();
            r.text = ruby.text; r.x = ch.x + ruby.x; r.y = ch.y + ruby.y;
            r.cw = ruby.cw !== void ? ruby.cw : ruby.size;
            r.size = ruby.size; r.color = ch.color; r.bold = ch.bold; r.italic = ch.italic;
            r.link = ch.link !== void ? ch.link : "";
            if (ch.edge !== void) r.edge = ch.edge;
            if (ch.shadow !== void) r.shadow = ch.shadow;
            drawCh(layer, ox, oy, r);
        }
        function kk4sDrawGraph(layer, ox, oy, ch) {
            var graphLayer = getGraphLayer(ch.graph);
            if (graphLayer === void) return false;
            try {
                var gw = ch.cw, gh = ch.size;
                graphLayer.face = dfOpaque;
                graphLayer.holdAlpha = true;
                if (ch.edge !== void) {
                    graphLayer.fillRect(0, 0, gw, gh, ch.edge);
                    layer.operateRect(ox + ch.x + 1, oy + ch.y, graphLayer, 0, 0, gw, gh);
                    layer.operateRect(ox + ch.x, oy + ch.y + 1, graphLayer, 0, 0, gw, gh);
                    layer.operateRect(ox + ch.x - 1, oy + ch.y, graphLayer, 0, 0, gw, gh);
                    layer.operateRect(ox + ch.x, oy + ch.y - 1, graphLayer, 0, 0, gw, gh);
                } else if (ch.shadow !== void) {
                    graphLayer.fillRect(0, 0, gw, gh, ch.shadow);
                    layer.operateRect(ox + ch.x + shadowOffsetX, oy + ch.y + shadowOffsetY,
                        graphLayer, 0, 0, gw, gh);
                }
                graphLayer.fillRect(0, 0, gw, gh, ch.color);
                layer.operateRect(ox + ch.x, oy + ch.y, graphLayer, 0, 0, gw, gh);
            } catch (e) { invalidate graphLayer; throw e; }
            invalidate graphLayer;
            return true;
        }
        // Called after render() to finish the native render pass.
        function done() {
            if (global.__kk4sTextRenderActive === this) global.__kk4sTextRenderActive = false;
        }
        function getCharacters(start, count) {
            var first = start === void ? 0 : int(start);
            if (first < 0) first = 0;
            var end = (count === void || int(count) == 0) ? _kk4sChars.count : first + int(count);
            if (end > _kk4sChars.count) end = _kk4sChars.count;
            var out = [];
            for (var i = first; i < end; i++) out.add(_kk4sChars[i]);
            return out;
        }
        function getLinkCharacters(start, count) {
            var source = getCharacters(start, count), out = [];
            for (var i = 0; i < source.count; i++) if (source[i].link !== void && source[i].link != "") out.add(source[i]);
            return out;
        }
        function getLinkNames() {
            var out = [];
            for (var i = 0; i < _kk4sChars.count; i++) {
                var name = _kk4sChars[i].link;
                if (name !== void && name != "" && out.indexOf(name) == -1) out.add(name);
            }
            return out;
        }
        function getLinkRects(name) {
            var out = [];
            for (var i = 0; i < _kk4sChars.count; i++) {
                var ch = _kk4sChars[i];
                if (name != "" && ch.link === name)
                    out.add(%[left:ch.x, top:ch.y, right:ch.x + ch.cw, bottom:ch.y + ch.size]);
            }
            return out;
        }
        function isLinkContains(name, x, y) {
            var rects = getLinkRects(name);
            for (var i = 0; i < rects.count; i++) {
                var r = rects[i];
                if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return true;
            }
            return false;
        }
        function getLinkOfPosition(x, y) {
            for (var i = _kk4sChars.count - 1; i >= 0; i--) {
                var ch = _kk4sChars[i];
                if (ch.link !== void && ch.link != "" && x >= ch.x && x < ch.x + ch.cw && y >= ch.y && y < ch.y + ch.size)
                    return ch.link;
            }
            return void;
        }
        function getKeyWait() { return renderDelay; }
        function onGetTextHeight(text) { return font !== void ? font.height : defaultFontSize; }
        function calcLineOffset(text, line) {
            var wanted = int(line);
            if (wanted <= 0) return 0;
            var lineNumber = -1, lastY = void;
            for (var i = 0; i < _kk4sChars.count; i++) {
                var top = vertical ? _kk4sChars[i].x : _kk4sChars[i].y;
                if (lastY === void || top != lastY) { lastY = top; lineNumber++; }
                if (lineNumber == wanted) return i;
            }
            return _kk4sChars.count;
        }
        function calcShowCount(count) {
            var visible = count === void ? 0 : int(count);
            if (visible < 0) return 0;
            if (visible > renderCount) return renderCount;
            return visible;
        }
        function resetStyle() {
            lineSize = defaultLineSize; lineSpacing = defaultLineSpacing; pitch = defaultPitch;
            align = -1;
            if (defaultAlign == "center") align = 0;
            else if (defaultAlign == "right" || defaultAlign == "bottom") align = 1;
            else if (defaultAlign !== void && typeof defaultAlign != "String") align = int(defaultAlign);
        }
        function setStyle(style) {
            if (style === void) return;
            if (style.linespacing !== void) lineSpacing = int(style.linespacing);
            if (style.pitch !== void) pitch = int(style.pitch);
            if (style.linesize !== void) lineSize = int(style.linesize);
            if (style.align == "default") {
                align = -1;
                if (defaultAlign == "center") align = 0;
                else if (defaultAlign == "right" || defaultAlign == "bottom") align = 1;
                else if (defaultAlign !== void && typeof defaultAlign != "String") align = int(defaultAlign);
            } else if (style.align == "center") align = 0;
            else if (style.align == "right" || style.align == "bottom") align = 1;
            else if (style.align == "left" || style.align == "top") align = -1;
        }

        // TJS function expressions do not close over this method's locals, so
        // keep layout state in a Dictionary and use instance methods as helpers.
        function _kk4sMeasure(state, text, size) {
            var f = font;
            if (f === void || typeof f.getTextWidth != "Object") return size * text.length;
            var oldHeight = f.height, oldBold = f.bold, oldItalic = f.italic;
            f.height = size; f.bold = state.bold; f.italic = state.italic;
            var measured = f.getTextWidth(text);
            f.height = oldHeight; f.bold = oldBold; f.italic = oldItalic;
            return int(measured);
        }
        function _kk4sAlignLine(state) {
            var used = state.vertical ? state.y : state.x, offset = 0;
            if (state.width > 0 && state.align == 0) offset = (state.width - used) / 2;
            else if (state.width > 0 && state.align == 1) offset = state.width - used;
            if (!state.vertical) {
                for (var j = state.lineStart; j < state.chars.count; j++)
                    state.chars[j].x += offset;
            }
            state.maxRight = Math.max(state.maxRight,
                state.vertical ? state.x + state.currentSize : used + offset);
            state.maxBottom = Math.max(state.maxBottom,
                state.vertical ? state.y : state.y + state.lineHeight);
        }
        function _kk4sNextLine(state, resetRuby) {
            this._kk4sAlignLine(state);
            var step = Math.max(state.lineHeight, state.lineSize) + state.lineSpacing;
            if (state.vertical) {
                state.x -= step; state.y = 0;
                if (state.width > 0 && state.x < 0) { state.over = true; return false; }
            } else {
                state.y += step; state.x = state.indent;
                if (state.height > 0 && state.y + state.currentSize > state.height) {
                    state.over = true; return false;
                }
            }
            state.lineStart = state.chars.count;
            state.lineHeight = state.currentSize;
            state.lineCount++;
            state.kinsokuCount = 0;
            if (resetRuby) {
                state.pendingRuby = ""; state.pendingRubyIndex = 0; state.pendingRubyCount = 0;
            }
            return true;
        }
        function _kk4sEmitChar(state, ch) {
            var cw = this._kk4sMeasure(state, ch, state.currentSize);
            if (cw < 0) cw = 0;
            if (!state.vertical && state.width > 0 && state.x + cw >= state.width && state.x > state.indent) {
                var following = this.following !== void ? this.following : "";
                var leading = this.leading !== void ? this.leading : "";
                var kinsokuMax = this.kinsoku_max !== void ? int(this.kinsoku_max) : 1;
                if (following.indexOf(ch) >= 0 && state.kinsokuCount < kinsokuMax) {
                    state.kinsokuCount++;
                } else {
                    var carry = void;
                    if (state.chars.count > state.lineStart + 1) {
                        var last = state.chars[state.chars.count - 1];
                        if (leading.indexOf(last.text) >= 0) {
                            carry = state.chars.pop(); state.x = carry.x;
                        }
                    }
                    if (!this._kk4sNextLine(state, false)) return false;
                    state.kinsokuCount = 0;
                    if (carry !== void) {
                        carry.x = state.x; carry.y = state.y - carry.size;
                        state.chars.add(carry);
                        state.x += carry.cw + state.pitch;
                        state.lineHeight = Math.max(state.lineHeight, carry.size);
                    }
                }
            }
            if (state.vertical && state.height > 0 && state.y + cw > state.height) {
                if (!this._kk4sNextLine(state, false)) return false;
            }
            if (!state.vertical && state.height > 0 && state.y + state.currentSize > state.height) {
                state.over = true; return false;
            }
            var item = new Dictionary();
            item.x = state.x; item.y = state.vertical ? state.y : state.y - state.currentSize;
            item.text = ch; item.cw = cw; item.size = state.currentSize; item.link = "";
            item.color = state.color; item.bold = state.bold; item.italic = state.italic;
            if (state.edge) item.edge = state.edgeColor;
            if (state.shadow) item.shadow = state.shadowColor;
            if (state.pendingRuby != "") {
                var rubyStart = int(state.pendingRubyIndex * state.pendingRuby.length / state.pendingRubyCount);
                var rubyEnd = int((state.pendingRubyIndex + 1) * state.pendingRuby.length / state.pendingRubyCount);
                if (rubyEnd <= rubyStart && rubyStart < state.pendingRuby.length) rubyEnd = rubyStart + 1;
                var rubyText = state.pendingRuby.substr(rubyStart, rubyEnd - rubyStart);
                if (rubyText != "") {
                    var rw = this._kk4sMeasure(state, rubyText, state.rubySize);
                    item.ruby = %[text:rubyText, x:int((cw-rw)/2),
                        y:int(-state.rubySize-state.rubyOffset), size:state.rubySize];
                }
                state.pendingRubyIndex++;
                if (state.pendingRubyIndex >= state.pendingRubyCount) {
                    state.pendingRuby = ""; state.pendingRubyIndex = 0; state.pendingRubyCount = 0;
                }
            }
            state.chars.add(item);
            if (state.vertical) state.y += cw + state.pitch;
            else state.x += cw + state.pitch;
            state.lineHeight = Math.max(state.lineHeight, state.currentSize);
            return true;
        }
        function _kk4sEmitString(state, text) {
            for (var j = 0; j < text.length; j++)
                if (!this._kk4sEmitChar(state, text.charAt(j))) return false;
            return true;
        }
        function _kk4sEmitGraph(state, name) {
            var graph = this.onGetGraphSize(name);
            if (graph === void) return true;
            var cw = int(graph.width), gh = int(graph.height);
            if (cw <= 0 || gh <= 0) return true;
            if (!state.vertical && state.width > 0 && state.x + cw > state.width && state.x > state.indent) {
                if (!this._kk4sNextLine(state, false)) return false;
            }
            if (state.vertical && state.height > 0 && state.y + gh > state.height) {
                if (!this._kk4sNextLine(state, false)) return false;
            }
            if (!state.vertical && state.height > 0 && state.y + gh > state.height) {
                state.over = true; return false;
            }
            var item = new Dictionary();
            item.x = state.x; item.y = state.vertical ? state.y : state.y - gh;
            item.graph = name; item.text = ""; item.cw = cw; item.size = gh; item.link = "";
            item.color = state.color;
            if (state.edge) item.edge = state.edgeColor;
            if (state.shadow) item.shadow = state.shadowColor;
            state.chars.add(item);
            if (state.vertical) state.y += cw + state.pitch;
            else state.x += cw + state.pitch;
            state.lineHeight = Math.max(state.lineHeight, gh);
            return true;
        }
        function _kk4sRenderText(text) {
            if (text === void) text = "";
            if (typeof text != "String") text = "" + text;
            var scale = fontScale !== void ? +fontScale : 1.0;
            if (scale <= 0) scale = 1.0;
            var defaultSize = font !== void && font.height !== void ? int(font.height) : int(defaultFontSize);
            if (defaultSize <= 0) defaultSize = 24;
            var state = new Dictionary();
            state.width = _kk4sWidth; state.height = _kk4sHeight;
            state.defaultSize = defaultSize; state.currentSize = int(defaultSize * scale);
            if (state.currentSize <= 0) state.currentSize = defaultSize;
            state.lineSpacing = lineSpacing !== void ? int(lineSpacing) : int(defaultLineSpacing);
            state.pitch = pitch !== void ? int(pitch) : int(defaultPitch);
            state.lineSize = lineSize !== void ? int(lineSize) : int(defaultLineSize);
            state.align = align !== void ? int(align) : -1;
            state.color = chColor !== void ? chColor : defaultChColor;
            if (state.color === void) state.color = 0xFFFFFF;
            state.bold = font !== void ? !!font.bold : !!defaultBold;
            state.italic = font !== void ? !!font.italic : !!defaultItalic;
            state.edge = edge !== void ? !!edge : !!defaultEdge;
            state.edgeColor = edgeColor !== void ? edgeColor : defaultEdgeColor;
            state.shadow = shadow !== void ? !!shadow : !!defaultShadow;
            state.shadowColor = shadowColor !== void ? shadowColor : defaultShadowColor;
            state.rubySize = rubySize !== void ? int(rubySize) : int(defaultRubySize);
            state.rubyOffset = rubyOffset !== void ? int(rubyOffset) : int(defaultRubyOffset);
            state.vertical = !!vertical;
            state.chars = []; state.x = 0; state.y = 0; state.indent = 0; state.lineStart = 0;
            state.lineHeight = state.currentSize; state.lineCount = 1;
            state.maxRight = 0; state.maxBottom = 0; state.kinsokuCount = 0;
            state.over = false; state.pendingRuby = ""; state.pendingRubyIndex = 0; state.pendingRubyCount = 0;
            state.textBase = base !== void ? base : global;
            if (state.vertical && state.width > 0) state.x = state.width - state.currentSize;

            var i = 0;
            while (i < text.length && !state.over) {
                var ch = text.charAt(i++);
                if (ch == "\r") {
                    if (i < text.length && text.charAt(i) == "\n") i++;
                    ch = "\n";
                }
                if (ch == "\n") { if (!this._kk4sNextLine(state, true)) break; continue; }
                if (ch == "\\") {
                    if (i >= text.length) break;
                    var esc = text.charAt(i++);
                    if (esc == "n") { if (!this._kk4sNextLine(state, true)) break; continue; }
                    if (esc == "i") { state.indent = state.vertical ? state.y : state.x; continue; }
                    if (esc == "r") { state.indent = 0; continue; }
                    if (esc == "t") ch = "\t";
                    else if (esc == "\\") ch = "\\";
                    else continue;
                } else if (ch == "[") {
                    var ruby = "";
                    while (i < text.length) { var rc = text.charAt(i++); if (rc == "]") break; ruby += rc; }
                    var comma = ruby.indexOf(",");
                    state.pendingRuby = comma >= 0 ? ruby.substr(0, comma) : ruby;
                    state.pendingRubyIndex = 0;
                    state.pendingRubyCount = comma >= 0 ? int(ruby.substr(comma + 1)) : 1;
                    if (state.pendingRubyCount <= 0) state.pendingRubyCount = 1;
                    continue;
                } else if (ch == "$") {
                    var variableName = "";
                    while (i < text.length) {
                        var variableChar = text.charAt(i++);
                        if (variableChar == ";") break;
                        variableName += variableChar;
                    }
                    var value = void;
                    try { value = state.textBase[variableName]; } catch (e) { value = void; }
                    if (typeof value == "String" && !this._kk4sEmitString(state, value)) break;
                    continue;
                } else if (ch == "&") {
                    var graphName = "";
                    while (i < text.length) {
                        var graphChar = text.charAt(i++);
                        if (graphChar == ";") break;
                        graphName += graphChar;
                    }
                    if (graphName != "" && !this._kk4sEmitGraph(state, graphName)) break;
                    continue;
                } else if (ch == "%") {
                    if (i >= text.length) continue;
                    var cmd = text.charAt(i++);
                    if (cmd == "b") state.bold = true;
                    else if (cmd == "i") state.italic = true;
                    else if (cmd == "n") { state.bold = false; state.italic = false; }
                    else if (cmd == "s") state.shadow = true;
                    else if (cmd == "e") state.edge = true;
                    else if (cmd == "r") {
                        state.bold = !!defaultBold; state.italic = !!defaultItalic;
                        state.shadow = !!defaultShadow; state.edge = !!defaultEdge;
                        state.shadowColor = defaultShadowColor !== void ? defaultShadowColor : 0;
                        state.edgeColor = defaultEdgeColor !== void ? defaultEdgeColor : 0x0080FF;
                        state.rubySize = defaultRubySize !== void ? int(defaultRubySize) : 10;
                        state.rubyOffset = defaultRubyOffset !== void ? int(defaultRubyOffset) : -2;
                        state.color = defaultChColor !== void ? defaultChColor : 0xFFFFFF;
                        state.currentSize = defaultSize;
                    } else if (cmd == "C") state.align = 0;
                    else if (cmd == "R") state.align = 1;
                    else if (cmd == "L") state.align = -1;
                    else if ("0123456789".indexOf(cmd) >= 0) {
                        var digits = cmd;
                        while (i < text.length && "0123456789".indexOf(text.charAt(i)) >= 0) digits += text.charAt(i++);
                        state.currentSize = int(digits);
                        if (state.currentSize <= 0) state.currentSize = defaultSize;
                    }
                    continue;
                }
                if (!this._kk4sEmitChar(state, ch)) break;
            }
            this._kk4sAlignLine(state);
            _kk4sChars = state.chars;
            renderCount = state.chars.count;
            renderLines = state.chars.count > 0 ? state.lineCount : 0;
            renderText = text;
            renderLeft = state.vertical ? Math.min(0, state.x) : 0;
            renderTop = state.vertical ? 0 : -state.currentSize;
            renderRight = state.maxRight; renderBottom = state.maxBottom;
            renderOver = state.over ? 1 : 0; renderDelay = 0;
            return !state.over;
        }
    }
    TextRenderBase.render = function(text, mode, a, b, c) {
        var target = global.__kk4sTextRenderActive;
        if (target === false) return false;
        try {
            var result = target._kk4sRenderText(text);
            global.__kk4sTextRenderActive = false;
            return result;
        } catch (e) {
            global.__kk4sTextRenderActive = false;
            throw e;
        }
    };
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
void LinkEmotePlayerDll();

// compat/kk4s_layerex.cpp
void LinkPackinOneDll();
void LinkLayerExBtoADll();
void LinkLayerExImageDll();
void LinkLayerExRasterDll();
void LinkLayerExSaveDll();
void LinkShrinkCopyDll();
void LinkLayerExAreaAverageDll();

// compat/kk4s_json.cpp
void LinkJsonDll();

// menu.dll: the Win32 menu bar. The engine already has MenuItem and Window.menu
// (engine/source/core/visual/MenuItemIntf.cpp), so items added by KAGZ work; the
// one member menu.dll adds that scripts rely on is Window.exSystemMenu, which KAGZ
// fills with its "menu bar visibility" entries. kk4s stores it (per window) for
// listeners such as KAG's onExSystemMenuSelected and has no OS menu to put it in.
static void EnsureWindowMember(iTJSDispatch2* window, const tjs_char* name, const tTJSVariant& value) {
    tTJSVariant existing;
    if (TJS_FAILED(window->PropGet(0, name, nullptr, &existing, window)))
        window->PropSet(TJS_MEMBERENSURE, name, nullptr, &value, window);
}

void InstallWindowMenuCompat() {
    iTJSDispatch2* g = Global();
    tTJSVariant v;
    if (TJS_FAILED(g->PropGet(0, TJS_W("Window"), nullptr, &v, g)) || v.Type() != tvtObject) return;
    iTJSDispatch2* window = v.AsObjectNoAddRef();
    // menu.dll: KAGZ stores its menu-bar entry tree here.
    EnsureWindowMember(window, TJS_W("exSystemMenu"), tTJSVariant());
    // windowEx.dll: KAGZ's window assigns these while constructing. kk4s windows
    // have no OS decorations, so they are stored flags (the window cannot be
    // resized or moved by dragging anyway); declaring them keeps the plain
    // `name = value` assignment inside KAGZ methods from failing (it compiles to
    // a property set that requires the member to exist).
    tTJSVariant no(false);
    EnsureWindowMember(window, TJS_W("disableResize"), no);
    EnsureWindowMember(window, TJS_W("disableMove"), no);
}

static void LinkMenuDll() { InstallWindowMenuCompat(); }

// windowEx.dll (wamsoft/windowEx): Window / System / MenuItem / Pad extensions.
// kk4s has one display and windows without OS decorations, so the plugin's
// queries answer as Windows would for a single monitor of System.screenWidth x
// screenHeight and a frameless window; icons, hot keys, IME, z-order and
// message hooks have nothing to act on and succeed quietly. Events: kk4s already
// delivers mouse/keyboard events to Window, so registerExEvent only reports
// success (KAGZ's custom.tjs stops at `kag.registerExEvent()` without it).
// %APPDATA%-style folders map to the game's save folder (System.dataPath), so a
// game that builds its save path from them keeps writing outside the game folder.
static const tjs_char* kWindowExShim = TJS_W(R"TJS(
// ---- System
global.kk4s_wx_monitor = function() {
	var sw = System.screenWidth, sh = System.screenHeight;
	return %[ name: "\\\\.\\DISPLAY1", primary: true,
	          monitor: %[ x: 0, y: 0, w: sw, h: sh ], work: %[ x: 0, y: 0, w: sw, h: sh ] ];
};
if (typeof System.getMonitorInfo == "undefined")
	System.getMonitorInfo = function(near, x, y, w, h) { return kk4s_wx_monitor(); };
if (typeof System.getDisplayMonitors == "undefined")
	System.getDisplayMonitors = function(x, y, w, h) {
		var m = kk4s_wx_monitor();
		if (x !== void && y !== void && w !== void && h !== void) {
			var mw = m.monitor.w, mh = m.monitor.h;
			var x1 = x > 0 ? x : 0, y1 = y > 0 ? y : 0;
			var x2 = x + w < mw ? x + w : mw, y2 = y + h < mh ? y + h : mh;
			if (x2 <= x1 || y2 <= y1) return [];
			m.intersect = %[ x: x1, y: y1, w: x2 - x1, h: y2 - y1 ];
		}
		return [ m ];
	};
global.kk4s_wx_cursor_layer = function() {
	var w = Window.mainWindow;
	if (w === void || w === null || !isvalid w) return void;
	return w.primaryLayer;
};
if (typeof System.getCursorPos == "undefined")
	System.getCursorPos = function() {
		var p = kk4s_wx_cursor_layer();
		if (p === void || p === null) return %[ x: 0, y: 0 ];
		return %[ x: Window.mainWindow.left + p.cursorX, y: Window.mainWindow.top + p.cursorY ];
	};
if (typeof System.setCursorPos == "undefined")
	System.setCursorPos = function(x, y) {
		var p = kk4s_wx_cursor_layer();
		if (p !== void && p !== null) p.setCursorPos(x - Window.mainWindow.left, y - Window.mainWindow.top);
	};
if (typeof System.setClipCursor == "undefined") System.setClipCursor = function() { return true; };
global.kk4s_wx_metrics = %[
	cxborder: 1, cyborder: 1, cxedge: 2, cyedge: 2, cxfixedframe: 3, cyfixedframe: 3,
	cxdlgframe: 3, cydlgframe: 3, cxsizeframe: 4, cysizeframe: 4, cxframe: 4, cyframe: 4,
	cxpaddedborder: 4, cycaption: 23, cysmcaption: 23, cymenu: 20, cxvscroll: 17, cyhscroll: 17,
	cyvscroll: 17, cxhscroll: 17, cxicon: 32, cyicon: 32, cxsmicon: 16, cysmicon: 16,
	cxcursor: 32, cycursor: 32, cmousebuttons: 3, mousepresent: 1, mousewheelpresent: 1,
	swapbutton: 0, cxdoubleclk: 4, cydoubleclk: 4, cxdrag: 4, cydrag: 4, cxmin: 136, cymin: 39,
	cxmintrack: 136, cymintrack: 39, xvirtualscreen: 0, yvirtualscreen: 0, cmonitors: 1,
	samedisplayformat: 1, remotesession: 0, tabletpc: 0, mediacenter: 0, digitizer: 0,
	maximumtouches: 0, dbcsenabled: 1, immenabled: 1, cleanboot: 0, network: 1, slowmachine: 0,
	mideastenabled: 0, penwindows: 0, debug: 0, showsounds: 0, menudropalignment: 0 ];
if (typeof System.getSystemMetrics == "undefined")
	System.getSystemMetrics = function(index) {
		var k = ("" + index).toLowerCase();
		if (k.substring(0, 3) == "sm_") k = k.substring(3);
		var sw = System.screenWidth, sh = System.screenHeight;
		if (k == "cxscreen" || k == "cxfullscreen" || k == "cxvirtualscreen" || k == "cxmaximized" || k == "cxmaxtrack") return sw;
		if (k == "cyscreen" || k == "cyvirtualscreen" || k == "cymaximized" || k == "cymaxtrack") return sh;
		if (k == "cyfullscreen") return sh;	// no caption above a full-screen client area here
		var v = kk4s_wx_metrics[k];
		return v === void ? 0 : v;
	};
if (typeof System.getDoubleClickTime == "undefined") System.getDoubleClickTime = function() { return 500; };
global.kk4s_wx_folder = function() {
	var p = Storages.getLocalName(System.dataPath);
	while (p.length > 1 && (p.charAt(p.length - 1) == "/" || p.charAt(p.length - 1) == "\\")) p = p.substring(0, p.length - 1);
	return p;
};
global.kk4s_wx_env = %[ os: "Windows_NT", username: "player", computername: "KK4S", userdomain: "KK4S" ];
if (typeof System.readEnvValue == "undefined")
	System.readEnvValue = function(name) {
		var k = ("" + name).toLowerCase();
		if (k == "appdata" || k == "localappdata" || k == "userprofile" || k == "homepath" || k == "temp" ||
		    k == "tmp" || k == "allusersprofile" || k == "programdata" || k == "public")
			return kk4s_wx_folder();
		var v = System.kk4sGetEnv(name);
		if (v !== void) return v;
		return kk4s_wx_env[k];
	};
if (typeof System.expandEnvString == "undefined")
	System.expandEnvString = function(text) {
		var out = "", i = 0;
		text = "" + text;
		while (true) {
			var a = text.indexOf("%", i);
			if (a < 0) break;
			var b = text.indexOf("%", a + 1);
			if (b < 0) break;
			var v = System.readEnvValue(text.substring(a + 1, b - a - 1));
			if (v === void) {	// undefined names stay as they are, as on Windows
				out += text.substring(i, b - i);
				i = b;
				continue;
			}
			out += text.substring(i, a - i) + v;
			i = b + 1;
		}
		return out + text.substring(i);
	};
if (typeof System.setApplicationIcon == "undefined") System.setApplicationIcon = function() {};
if (typeof System.setIconicPreview == "undefined") System.setIconicPreview = function() { return false; };
if (typeof System.setDpiAwareness == "undefined") System.setDpiAwareness = function() { return void; };
if (typeof System.findWindowEx == "undefined") System.findWindowEx = function() { return void; };
if (typeof System.classLongPtr == "undefined") System.classLongPtr = function() { return void; };
if (typeof System.loadCursor == "undefined") System.loadCursor = function() { return 0; };
if (typeof System.mapVirtualKey == "undefined")
	System.mapVirtualKey = function(code, type) {
		if (type == 2) return ((code >= 0x30 && code <= 0x39) || (code >= 0x41 && code <= 0x5a)) ? code : 0;
		return code;
	};
if (typeof System.breathe == "undefined") System.breathe = function() {};
if (typeof System.isBreathing == "undefined") System.isBreathing = function() { return false; };
if (typeof System.getAboutString == "undefined")
	System.getAboutString = function() { return "kk4s (Kirikiri-compatible runtime)\n" + System.versionInformation; };
if (typeof System.getCPUType == "undefined") System.getCPUType = function() { return System.platformName; };
if (typeof Scripts.setEvalErrorLog == "undefined") Scripts.setEvalErrorLog = function(on) { return false; };

// ---- Window (members reach instances created after the link, as with the DLL)
Window.maximize = function() {} if typeof Window.maximize == "undefined";
Window.minimize = function() {} if typeof Window.minimize == "undefined";
Window.showRestore = function() {} if typeof Window.showRestore == "undefined";
Window.resetWindowIcon = function() {} if typeof Window.resetWindowIcon == "undefined";
Window.setWindowIcon = function(file, withapp) {} if typeof Window.setWindowIcon == "undefined";
Window.setWindowCornerPreference = function() { return false; } if typeof Window.setWindowCornerPreference == "undefined";
Window.getClientRect = function() { return %[ x: this.left, y: this.top, w: this.innerWidth, h: this.innerHeight ]; } if typeof Window.getClientRect == "undefined";
Window.getWindowRect = function() { return %[ x: this.left, y: this.top, w: this.width, h: this.height ]; } if typeof Window.getWindowRect == "undefined";
Window.getNormalRect = function(nofix) { return %[ x: this.left, y: this.top, w: this.width, h: this.height ]; } if typeof Window.getNormalRect == "undefined";
Window.setClientRect = function(r) { this.setPos(r.x, r.y); this.setInnerSize(r.w, r.h); } if typeof Window.setClientRect == "undefined";
Window.bringTo = function(win, activate) {} if typeof Window.bringTo == "undefined";
Window.sendToBack = function() {} if typeof Window.sendToBack == "undefined";
Window.acquireImeControl = function() { return true; } if typeof Window.acquireImeControl == "undefined";
Window.resetImeContext = function() { return true; } if typeof Window.resetImeContext == "undefined";
Window.focusMenuByKey = function() { return false; } if typeof Window.focusMenuByKey == "undefined";
Window.setMessageHook = function() {} if typeof Window.setMessageHook == "undefined";
Window.getNotificationNum = function() { return 0; } if typeof Window.getNotificationNum == "undefined";
Window.getNotificationName = function() { return ""; } if typeof Window.getNotificationName == "undefined";
Window.setOverlayBitmap = function() {} if typeof Window.setOverlayBitmap == "undefined";
Window.ncHitTest = function(x, y) { return 1; } if typeof Window.ncHitTest == "undefined";	// nchtClient
Window.registerHotKey = function() { return false; } if typeof Window.registerHotKey == "undefined";
Window.registerDeviceChange = function() {} if typeof Window.registerDeviceChange == "undefined";

// ---- MenuItem / Pad / Debug.console
if (typeof MenuItem != "undefined") {
	MenuItem.popupEx = function() { return void; } if typeof MenuItem.popupEx == "undefined";
}
if (typeof Pad != "undefined") {
	Pad.registerExEvent = function() { return true; } if typeof Pad.registerExEvent == "undefined";
}
if (typeof Debug.console != "undefined" && Debug.console !== void) {
	var c = Debug.console;
	c.restoreMaximize = function() {} if typeof c.restoreMaximize == "undefined";
	c.maximize = function() {} if typeof c.maximize == "undefined";
	c.getRect = function() { return %[ x: 0, y: 0, w: 0, h: 0 ]; } if typeof c.getRect == "undefined";
	c.setPos = function() {} if typeof c.setPos == "undefined";
	c.getPlacement = function() { return %[]; } if typeof c.getPlacement == "undefined";
	c.setPlacement = function() {} if typeof c.setPlacement == "undefined";
	c.bringAfter = function() {} if typeof c.bringAfter == "undefined";
}
)TJS");

static void EnsureClassMember(const tjs_char* cls, const tjs_char* name, const tTJSVariant& value) {
    iTJSDispatch2* g = Global();
    tTJSVariant v;
    if (TJS_FAILED(g->PropGet(0, cls, nullptr, &v, g)) || v.Type() != tvtObject) return;
    EnsureWindowMember(v.AsObjectNoAddRef(), name, value);
}

static void LinkWindowExDll() {
    iTJSDispatch2* g = Global();
    tTJSVariant v;
    if (TJS_FAILED(g->PropGet(0, TJS_W("Window"), nullptr, &v, g)) || v.Type() != tvtObject) return;
    iTJSDispatch2* window = v.AsObjectNoAddRef();
    DefineFunction(window, TJS_W("registerExEvent"), [](tTJSVariant* r, tjs_int, tTJSVariant**) -> tjs_error {
        if (r) *r = (tjs_int)1;
        return TJS_S_OK;
    });
    DefineFunction(window, TJS_W("unregisterExEvent"), [](tTJSVariant* r, tjs_int, tTJSVariant**) -> tjs_error {
        if (r) *r = (tjs_int)1;
        return TJS_S_OK;
    });
    // Properties with nothing behind them here: stored values (readable, settable).
    tTJSVariant no(false);
    for (const tjs_char* name : {TJS_W("maximized"), TJS_W("minimized"), TJS_W("enableNCMouseEvent")})
        EnsureWindowMember(window, name, no);
    static const struct { const tjs_char* name; tjs_int value; } kHit[] = {
        {TJS_W("nchtError"), -2}, {TJS_W("nchtTransparent"), -1}, {TJS_W("nchtNoWhere"), 0},
        {TJS_W("nchtClient"), 1}, {TJS_W("nchtCaption"), 2}, {TJS_W("nchtSysMenu"), 3},
        {TJS_W("nchtSize"), 4}, {TJS_W("nchtGrowBox"), 4}, {TJS_W("nchtMenu"), 5},
        {TJS_W("nchtHScroll"), 6}, {TJS_W("nchtVScroll"), 7}, {TJS_W("nchtMinButton"), 8},
        {TJS_W("nchtReduce"), 8}, {TJS_W("nchtMaxButton"), 9}, {TJS_W("nchtZoom"), 9},
        {TJS_W("nchtLeft"), 10}, {TJS_W("nchtRight"), 11}, {TJS_W("nchtTop"), 12},
        {TJS_W("nchtTopLeft"), 13}, {TJS_W("nchtTopRight"), 14}, {TJS_W("nchtBottom"), 15},
        {TJS_W("nchtBottomLeft"), 16}, {TJS_W("nchtBottomRight"), 17}, {TJS_W("nchtBorder"), 18}};
    for (const auto& h : kHit) EnsureWindowMember(window, h.name, tTJSVariant(h.value));
    // MenuItem: bitmaps / right-justify are stored; bi* are the HBMMENU_* ids.
    tTJSVariant empty;
    EnsureClassMember(TJS_W("MenuItem"), TJS_W("rightJustify"), no);
    EnsureClassMember(TJS_W("MenuItem"), TJS_W("bmpChecked"), empty);
    EnsureClassMember(TJS_W("MenuItem"), TJS_W("bmpUnchecked"), empty);
    static const struct { const tjs_char* name; tjs_int value; } kBi[] = {
        {TJS_W("biSystem"), 1}, {TJS_W("biRestore"), 2}, {TJS_W("biMinimize"), 3}, {TJS_W("biClose"), 5},
        {TJS_W("biCloseDisabled"), 6}, {TJS_W("biMinimizeDisabled"), 7}, {TJS_W("biPopupClose"), 8},
        {TJS_W("biPopupRestore"), 9}, {TJS_W("biPopupMaximize"), 10}, {TJS_W("biPopupMinimize"), 11}};
    for (const auto& b : kBi) EnsureClassMember(TJS_W("MenuItem"), b.name, tTJSVariant(b.value));

    tTJSVariant sv;
    if (TJS_SUCCEEDED(g->PropGet(0, TJS_W("System"), nullptr, &sv, g)) && sv.Type() == tvtObject) {
        iTJSDispatch2* sys = sv.AsObjectNoAddRef();
        // kk4sGetEnv(name): the process environment (desktop); void if unset
        DefineFunction(sys, TJS_W("kk4sGetEnv"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
            if (n < 1) return TJS_E_BADPARAMCOUNT;
            const char* e = getenv(Utf8(*p[0]).c_str());
            if (r && e) {
                tjs_string w;
                *r = TVPUtf8ToUtf16(w, e) ? ttstr(w.c_str()) : ttstr(e);
            }
            return TJS_S_OK;
        });
        DefineFunction(sys, TJS_W("clearGraphicCache"), [](tTJSVariant*, tjs_int, tTJSVariant**) -> tjs_error {
            TVPClearGraphicCache();
            return TJS_S_OK;
        });
    }
    RunTjsShim("windowEx.dll", kWindowExShim);
}

// Members Kirikiri Z has natively (the fstat.dll and windowEx.dll contracts:
// Storages.dirlist / fstat / getTime / ..., Window.maximize / getClientRect /
// ..., System.readEnvValue / expandEnvString / getMonitorInfo / ...) and
// Kirikiri 2's System.inputString, installed for every game before startup.tjs
// so scripts written for those engines find them without linking a plugin.
// Every member is only added when missing. (tools/native-api.py lists what the
// engine itself lacks compared with a krkrz / krkr2 source tree.)
static std::set<std::string> g_linked_plugins; // see LinkBuiltinPlugin()
static const tjs_char* kEngineNativesShim = TJS_W(R"TJS(
if (typeof System.systemLanguage == "undefined") System.systemLanguage = "ja-JP";	// the games' home platform
if (typeof System.uiLanguage == "undefined") System.uiLanguage = "ja";
if (typeof System.uiLanguageCandidates == "undefined") System.uiLanguageCandidates = [ "ja" ];
if (typeof System.writeEnvValue == "undefined") System.writeEnvValue = function(name, value) {};
if (typeof System.registerHotKey == "undefined") System.registerHotKey = function() { return false; };
if (typeof System.unregisterHotKey == "undefined") System.unregisterHotKey = function() {};
)TJS");

void InstallEngineNativesCompat() {
    LinkFstatDll();
    g_linked_plugins.insert("fstat.dll");
    LinkWindowExDll();
    g_linked_plugins.insert("windowex.dll");

    iTJSDispatch2* g = Global();
    tTJSVariant sv;
    if (TJS_SUCCEEDED(g->PropGet(0, TJS_W("System"), nullptr, &sv, g)) && sv.Type() == tvtObject) {
        // System.inputString(caption, prompt, initial) -> text, or void when cancelled
        // (Kirikiri 2's TVPInputQuery dialog; games use it for name entry). Switch: the
        // system keyboard; PC: the initial text as if confirmed, or $KK4S_INPUT_STRING.
        DefineFunction(sv.AsObjectNoAddRef(), TJS_W("inputString"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p) -> tjs_error {
            if (n < 3) return TJS_E_BADPARAMCOUNT;
            ttstr caption = *p[0], prompt = *p[1], initial = *p[2];
            std::string text = Utf8(initial);
            bool ok;
            if (kk4s::EditTextAvailable()) {
                ok = kk4s::EditText(Utf8(prompt.IsEmpty() ? caption : prompt), text);
            } else {
                if (const char* env = getenv("KK4S_INPUT_STRING")) text = env;
                ok = true;
            }
            kk4s::Log("compat: System.inputString(\"%s\") -> %s", Utf8(prompt).c_str(), ok ? text.c_str() : "(cancelled)");
            if (r) {
                if (ok) {
                    tjs_string w;
                    *r = TVPUtf8ToUtf16(w, text) ? ttstr(w.c_str()) : ttstr(text.c_str());
                } else {
                    r->Clear();
                }
            }
            return TJS_S_OK;
        });
    }
    tTJSVariant no(false);
    EnsureClassMember(TJS_W("Debug"), TJS_W("visible"), no); // the debug console is never shown
    RunTjsShim("engine natives", kEngineNativesShim);
}

// KAGParserEx.dll replaces the KAG parser: the engine's own parser does the work; the one
// visible difference games rely on is "taglist" in tag elements (KAGParser.cpp).
static void LinkKAGParserExDll() { ::TVPKAGParserExMode = true; }

// scriptsEx.dll: most of it is installed for every game (kk4s_scripts.cpp,
// InstallScriptsCompat); the link adds safeEvalStorage, which only reads data.
static void LinkScriptsExDll() {
    RunTjsShim("scriptsEx.dll", TJS_W(R"TJS(
if (typeof Scripts.safeEvalStorage == "undefined")
	Scripts.safeEvalStorage = function(storage, mode = "", context = void) {
		return context === void ? Scripts.evalStorage(storage, mode) : Scripts.evalStorage(storage, mode, context);
	};
)TJS"));
}

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
    {"packinone.dll", LinkPackinOneDll},
    {"layerexbtoa.dll", LinkLayerExBtoADll},
    {"layereximage.dll", LinkLayerExImageDll},
    {"layerexraster.dll", LinkLayerExRasterDll},
    {"layerexsave.dll", LinkLayerExSaveDll},
    {"shrinkcopy.dll", LinkShrinkCopyDll},
    {"layerexareaaverage.dll", LinkLayerExAreaAverageDll},
    {"json.dll", LinkJsonDll},
    {"scriptsex.dll", LinkScriptsExDll},
    {"windowex.dll", LinkWindowExDll},
    {"menu.dll", LinkMenuDll},
    {"emoteplayer.dll", LinkEmotePlayerDll},
    {"motionplayer.dll", LinkEmotePlayerDll},
    {"wuvorbis.dll", ProvidedByEngine},
    {"krmovie.dll", ProvidedByEngine},
    {"alphamovie.dll", ProvidedByEngine},
    // extrans transitions (wave, ripple, mosaic, rotate*...) fall back to
    // crossfade in the engine until they are implemented.
    {"extrans.dll", ProvidedByEngine},
};

// One link per game: a plugin registered twice would overwrite globals. This
// must be cleared for every new game (the TJS engine and its globals are new,
// but this file stays loaded when the player returns to the launcher).
// (g_linked_plugins is defined above InstallEngineNativesCompat.)

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
