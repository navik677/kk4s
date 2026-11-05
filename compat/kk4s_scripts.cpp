// kk4s: KiriKiri 2/Z `Scripts` APIs that this engine lacks and KAG-based games call.
//
//   Scripts.foreach(obj, func)   func(name, value, extra...) for each member; the first non-void
//                                return value of func stops the loop and is returned
//   Scripts.getObjectKeys(obj)   member names (Array) of a Dictionary / Array
//   Scripts.getObjectCount(obj)  number of members (Dictionary) or elements (Array)
//   Scripts.equalStruct(a, b)    structural equality of Dictionary/Array trees
//   Scripts.clone(obj)           recursive copy of Dictionary / Array trees; other values as is
//   Scripts.isNullContext(obj)   true when obj is a closure with no object context
//   Scripts.getObjectContext(obj) the object context a closure is bound to, or void
//   Scripts.loadDataPack(storage) decode a TJS/ns0 or TJS/4s0 `.pbd` data pack
//
// Each is only added when the engine has no such member. Implemented natively because
// TJS function expressions here cannot see the locals of the function around them.
#include "kk4s_compat.h"

#include "CharacterSet.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "pbd/pbd.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "tjsObject.h"
#include "md5.h"

#include <cstdlib>
#include <functional>
#include <utility>
#include <vector>

namespace kk4s {
namespace compat {

namespace {

using NativeFn = std::function<tjs_error(tTJSVariant* result, tjs_int numparams, tTJSVariant** param, iTJSDispatch2* objthis)>;

class tFunction : public tTJSDispatch {
    NativeFn Fn;
public:
    explicit tFunction(NativeFn fn) : Fn(std::move(fn)) {}
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char* membername, tjs_uint32*, tTJSVariant* result,
                                       tjs_int numparams, tTJSVariant** param, iTJSDispatch2* objthis) override {
        if (membername) return TJS_E_MEMBERNOTFOUND;
        if (result) result->Clear();
        return Fn(result, numparams, param, objthis);
    }
};

// Collects (name, value) of the members seen by EnumMembers.
class tCollector : public tTJSDispatch {
public:
    std::vector<std::pair<ttstr, tTJSVariant>> items;
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant* result, tjs_int numparams,
                                       tTJSVariant** param, iTJSDispatch2*) override {
        if (numparams >= 3 && !(param[1]->AsInteger() & TJS_HIDDENMEMBER)) items.emplace_back(ttstr(*param[0]), *param[2]);
        if (result) *result = (tjs_int)1;
        return TJS_S_OK;
    }
};

bool IsInstance(iTJSDispatch2* o, const tjs_char* cls) {
    return o->IsInstanceOf(0, nullptr, nullptr, cls, o) == TJS_S_TRUE;
}

void Enumerate(iTJSDispatch2* o, std::vector<std::pair<ttstr, tTJSVariant>>& out) {
    tCollector* c = new tCollector;
    tTJSVariantClosure closure(c);
    o->EnumMembers(TJS_IGNOREPROP, &closure, o);
    out = c->items;
    closure.Release();
}

tTJSVariant Clone(const tTJSVariant& v, int depth) {
    if (v.Type() != tvtObject || depth > 64) return v;
    iTJSDispatch2* o = v.AsObjectNoAddRef();
    if (!o) return v;
    if (IsInstance(o, TJS_W("Array"))) {
        tTJSVariant count;
        o->PropGet(0, TJS_W("count"), nullptr, &count, o);
        iTJSDispatch2* a = TJSCreateArrayObject();
        tTJSVariant out(a, a);
        a->Release();
        for (tjs_int i = 0; i < (tjs_int)count; i++) {
            tTJSVariant e;
            o->PropGetByNum(0, i, &e, o);
            tTJSVariant c = Clone(e, depth + 1);
            a->PropSetByNum(TJS_MEMBERENSURE, i, &c, a);
        }
        return out;
    }
    if (IsInstance(o, TJS_W("Dictionary"))) {
        std::vector<std::pair<ttstr, tTJSVariant>> items;
        Enumerate(o, items);
        iTJSDispatch2* d = TJSCreateDictionaryObject();
        tTJSVariant out(d, d);
        d->Release();
        for (auto& kv : items) {
            tTJSVariant c = Clone(kv.second, depth + 1);
            d->PropSet(TJS_MEMBERENSURE, kv.first.c_str(), nullptr, &c, d);
        }
        return out;
    }
    return v;
}

// Structural equality: Dictionary/Array trees are compared member by member, everything else
// by type and value (strings, numbers, octets); other objects must be the same object.
// `loose` (equalStructNumericLoose) also treats an Integer and a Real of the same value as equal.
bool EqualStruct(const tTJSVariant& a, const tTJSVariant& b, int depth, bool loose = false) {
    if (a.Type() != tvtObject || b.Type() != tvtObject) {
        if (a.Type() == tvtObject || b.Type() == tvtObject) return false;
        if (loose && (a.Type() == tvtInteger || a.Type() == tvtReal) && (b.Type() == tvtInteger || b.Type() == tvtReal))
            return a.NormalCompare(b);
        return a.DiscernCompare(b);
    }
    iTJSDispatch2* oa = a.AsObjectNoAddRef();
    iTJSDispatch2* ob = b.AsObjectNoAddRef();
    if (oa == ob) return true;
    if (!oa || !ob || depth > 64) return false;
    if (IsInstance(oa, TJS_W("Array"))) {
        if (!IsInstance(ob, TJS_W("Array"))) return false;
        tTJSVariant ca, cb;
        oa->PropGet(0, TJS_W("count"), nullptr, &ca, oa);
        ob->PropGet(0, TJS_W("count"), nullptr, &cb, ob);
        if ((tjs_int)ca != (tjs_int)cb) return false;
        for (tjs_int i = 0; i < (tjs_int)ca; i++) {
            tTJSVariant ea, eb;
            oa->PropGetByNum(0, i, &ea, oa);
            ob->PropGetByNum(0, i, &eb, ob);
            if (!EqualStruct(ea, eb, depth + 1, loose)) return false;
        }
        return true;
    }
    if (IsInstance(oa, TJS_W("Dictionary"))) {
        if (!IsInstance(ob, TJS_W("Dictionary"))) return false;
        std::vector<std::pair<ttstr, tTJSVariant>> ma, mb;
        Enumerate(oa, ma);
        Enumerate(ob, mb);
        if (ma.size() != mb.size()) return false;
        for (auto& kv : ma) {
            tTJSVariant other;
            if (TJS_FAILED(ob->PropGet(TJS_MEMBERMUSTEXIST, kv.first.c_str(), nullptr, &other, ob))) return false;
            if (!EqualStruct(kv.second, other, depth + 1, loose)) return false;
        }
        return true;
    }
    return false;
}

// Converts a decoded data pack value tree to TJS values. Arrays and
// Dictionaries are real TJS containers, so the game can index them normally.
tTJSVariant ToVariant(const kk4s::pbd::Value& v, int depth) {
    using Type = kk4s::pbd::Value::Type;
    if (depth > 256) return tTJSVariant();
    switch (v.type) {
        case Type::Void:
            return tTJSVariant();
        case Type::String: {
            tjs_string w;
            if (!TVPUtf8ToUtf16(w, v.string)) w.clear();
            return tTJSVariant(ttstr(w.c_str()));
        }
        case Type::Integer:
            return tTJSVariant((tTVInteger)v.integer);
        case Type::Double:
            return tTJSVariant((tTVReal)v.real);
        case Type::Array: {
            iTJSDispatch2* a = TJSCreateArrayObject();
            tTJSVariant out(a, a);
            a->Release();
            for (std::size_t i = 0; i < v.array.size(); i++) {
                tTJSVariant e = ToVariant(v.array[i], depth + 1);
                a->PropSetByNum(TJS_MEMBERENSURE, (tjs_int)i, &e, a);
            }
            return out;
        }
        case Type::Dictionary: {
            iTJSDispatch2* d = TJSCreateDictionaryObject();
            tTJSVariant out(d, d);
            d->Release();
            for (auto& kv : v.dictionary) {
                tjs_string key;
                if (!TVPUtf8ToUtf16(key, kv.first)) continue;
                tTJSVariant e = ToVariant(kv.second, depth + 1);
                ttstr name(key.c_str());
                d->PropSet(TJS_MEMBERENSURE, name.c_str(), nullptr, &e, d);
            }
            return out;
        }
    }
    return tTJSVariant();
}

// Reads a whole storage (file or archive member).
bool ReadStorage(const ttstr& storage, std::vector<tjs_uint8>& out) {
    tTJSBinaryStream* stream = nullptr;
    try {
        stream = TVPCreateStream(TVPNormalizeStorageName(storage), TJS_BS_READ);
    } catch (...) {
        stream = nullptr;
    }
    if (!stream) return false;
    try {
        tjs_uint64 size = stream->GetSize();
        if (size > 0x7fffffff) {
            delete stream;
            return false;
        }
        out.resize((std::size_t)size);
        if (size) stream->ReadBuffer(out.data(), (tjs_uint)size);
    } catch (...) {
        delete stream;
        return false;
    }
    delete stream;
    return true;
}

void Define(iTJSDispatch2* scripts, const tjs_char* name, NativeFn fn) {
    tTJSVariant existing;
    if (TJS_SUCCEEDED(scripts->PropGet(0, name, nullptr, &existing, scripts)) && existing.Type() != tvtVoid) return;
    iTJSDispatch2* f = new tFunction(std::move(fn));
    tTJSVariant v(f, nullptr);
    f->Release();
    scripts->PropSet(TJS_MEMBERENSURE, name, nullptr, &v, scripts);
}

}  // namespace

void InstallScriptsCompat() {
    iTJSDispatch2* global = TVPGetScriptEngine()->GetGlobalNoAddRef();
    tTJSVariant sv;
    if (TJS_FAILED(global->PropGet(0, TJS_W("Scripts"), nullptr, &sv, global)) || sv.Type() != tvtObject) return;
    iTJSDispatch2* scripts = sv.AsObjectNoAddRef();
    if (!scripts) return;

    // Scripts.foreach(obj, func, extra...): func(name, value, extra...) for every member of a
    // Dictionary (index, value for an Array). Note Dictionary.forEach of this engine passes
    // (value, name), the opposite order. The first non-void value func returns stops the
    // loop and is the result (KAG uses it to look things up); otherwise the result is void.
    Define(scripts, TJS_W("foreach"), [global](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2* objthis) -> tjs_error {
        if (n < 2) return TJS_E_BADPARAMCOUNT;
        if (p[0]->Type() != tvtObject || !p[0]->AsObjectNoAddRef() || p[1]->Type() != tvtObject) return TJS_E_INVALIDPARAM;
        iTJSDispatch2* o = p[0]->AsObjectNoAddRef();
        tTJSVariantClosure fn = p[1]->AsObjectClosureNoAddRef();
        // like Dictionary.forEach: a bare function expression runs with the caller's this
        iTJSDispatch2* fthis = fn.ObjThis ? fn.ObjThis : (objthis ? objthis : global);
        std::vector<std::pair<tTJSVariant, tTJSVariant>> items;
        if (IsInstance(o, TJS_W("Array"))) {
            tTJSVariant count;
            o->PropGet(0, TJS_W("count"), nullptr, &count, o);
            for (tjs_int i = 0; i < (tjs_int)count; i++) {
                tTJSVariant e;
                o->PropGetByNum(0, i, &e, o);
                items.emplace_back(tTJSVariant((tjs_int)i), e);
            }
        } else {
            std::vector<std::pair<ttstr, tTJSVariant>> members;
            Enumerate(o, members);
            for (auto& kv : members) items.emplace_back(tTJSVariant(kv.first), kv.second);
        }
        tjs_int extra = n - 2;
        for (auto& it : items) {
            std::vector<tTJSVariant*> args(2 + extra);
            args[0] = &it.first;
            args[1] = &it.second;
            for (tjs_int i = 0; i < extra; i++) args[2 + i] = p[2 + i];
            tTJSVariant res;
            tjs_error hr = fn.FuncCall(0, nullptr, nullptr, &res, 2 + extra, args.data(), fthis);
            if (TJS_FAILED(hr)) return hr;
            if (res.Type() != tvtVoid) {  // a value stops the loop and becomes the result
                if (r) *r = res;
                return TJS_S_OK;
            }
        }
        return TJS_S_OK;  // result stays void
    });

    Define(scripts, TJS_W("getObjectKeys"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        iTJSDispatch2* arr = TJSCreateArrayObject();
        tTJSVariant out(arr, arr);
        arr->Release();
        if (p[0]->Type() == tvtObject && p[0]->AsObjectNoAddRef()) {
            std::vector<std::pair<ttstr, tTJSVariant>> items;
            Enumerate(p[0]->AsObjectNoAddRef(), items);
            tjs_int i = 0;
            for (auto& kv : items) {
                tTJSVariant name(kv.first);
                arr->PropSetByNum(TJS_MEMBERENSURE, i++, &name, arr);
            }
        }
        if (r) *r = out;
        return TJS_S_OK;
    });

    Define(scripts, TJS_W("getObjectCount"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        tjs_int count = 0;
        if (p[0]->Type() == tvtObject && p[0]->AsObjectNoAddRef()) {
            iTJSDispatch2* o = p[0]->AsObjectNoAddRef();
            if (IsInstance(o, TJS_W("Array"))) {
                tTJSVariant c;
                if (TJS_SUCCEEDED(o->PropGet(0, TJS_W("count"), nullptr, &c, o))) count = (tjs_int)c;
            } else {
                std::vector<std::pair<ttstr, tTJSVariant>> items;
                Enumerate(o, items);
                count = (tjs_int)items.size();
            }
        }
        if (r) *r = count;
        return TJS_S_OK;
    });

    Define(scripts, TJS_W("equalStruct"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 2) return TJS_E_BADPARAMCOUNT;
        if (r) *r = (tjs_int)(EqualStruct(*p[0], *p[1], 0) ? 1 : 0);
        return TJS_S_OK;
    });

    Define(scripts, TJS_W("equalStructNumericLoose"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 2) return TJS_E_BADPARAMCOUNT;
        if (r) *r = (tjs_int)(EqualStruct(*p[0], *p[1], 0, true) ? 1 : 0);
        return TJS_S_OK;
    });

    // scriptsEx.dll rest: rehash (the engine's hash tables need no rehashing), MD5 of an
    // octet, flag-driven member access and the pf* flags (TJS_MEMBERENSURE and friends).
    Define(scripts, TJS_W("rehash"), [](tTJSVariant*, tjs_int, tTJSVariant**, iTJSDispatch2*) -> tjs_error {
        return TJS_S_OK;
    });
    Define(scripts, TJS_W("getMD5HashString"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        tTJSVariantOctet* o = p[0]->AsOctetNoAddRef();
        md5_state_t st;
        md5_init(&st);
        if (o) md5_append(&st, o->GetData(), (int)o->GetLength());
        md5_byte_t d[16];
        md5_finish(&st, d);
        static const char* hex = "0123456789abcdef";
        tjs_char out[33];
        for (int i = 0; i < 16; ++i) { out[i * 2] = hex[d[i] >> 4]; out[i * 2 + 1] = hex[d[i] & 15]; }
        out[32] = 0;
        if (r) *r = ttstr(out);
        return TJS_S_OK;
    });
    Define(scripts, TJS_W("propSet"), [](tTJSVariant*, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 3) return TJS_E_BADPARAMCOUNT;
        tTJSVariantClosure c = p[0]->AsObjectClosureNoAddRef();
        tjs_uint32 flag = n > 3 ? (tjs_uint32)(tjs_int)p[3]->AsInteger() : TJS_MEMBERENSURE;
        if (p[1]->Type() == tvtInteger)
            return c.Object->PropSetByNum(flag, (tjs_int)p[1]->AsInteger(), p[2], c.ObjThis ? c.ObjThis : c.Object);
        ttstr name = *p[1];
        return c.Object->PropSet(flag, name.c_str(), nullptr, p[2], c.ObjThis ? c.ObjThis : c.Object);
    });
    Define(scripts, TJS_W("propGet"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 2) return TJS_E_BADPARAMCOUNT;
        tTJSVariantClosure c = p[0]->AsObjectClosureNoAddRef();
        tjs_uint32 flag = n > 2 ? (tjs_uint32)(tjs_int)p[2]->AsInteger() : TJS_MEMBERMUSTEXIST;
        if (p[1]->Type() == tvtInteger)
            return c.Object->PropGetByNum(flag, (tjs_int)p[1]->AsInteger(), r, c.ObjThis ? c.ObjThis : c.Object);
        ttstr name = *p[1];
        return c.Object->PropGet(flag, name.c_str(), nullptr, r, c.ObjThis ? c.ObjThis : c.Object);
    });
    {
        struct { const tjs_char* name; tjs_int value; } flags[] = {
            {TJS_W("pfMemberEnsure"), TJS_MEMBERENSURE}, {TJS_W("pfMemberMustExist"), TJS_MEMBERMUSTEXIST},
            {TJS_W("pfIgnoreProp"), TJS_IGNOREPROP}, {TJS_W("pfHiddenMember"), TJS_HIDDENMEMBER},
            {TJS_W("pfStaticMember"), TJS_STATICMEMBER}};
        for (auto& f : flags) {
            tTJSVariant existing;
            if (TJS_SUCCEEDED(scripts->PropGet(0, f.name, nullptr, &existing, scripts)) && existing.Type() != tvtVoid) continue;
            tTJSVariant v(f.value);
            scripts->PropSet(TJS_MEMBERENSURE, f.name, nullptr, &v, scripts);
        }
    }

    Define(scripts, TJS_W("clone"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        if (r) *r = Clone(*p[0], 0);
        return TJS_S_OK;
    });

    // Scripts.isNullContext(obj): true when obj is a closure (function) that is not bound to
    // any object context. KAG's action code (Action.tjs getCompleteFunc) uses this to decide
    // whether a stored completion callback has to be bound with `chgthis` before it is called.
    // A plain function expression has ObjThis == null; a function taken from an instance
    // (`this.callback`) carries that instance. Non-function values have no context either.
    Define(scripts, TJS_W("isNullContext"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        bool isnull = true;
        if (p[0]->Type() == tvtObject) {
            tTJSVariantClosure c = p[0]->AsObjectClosureNoAddRef();
            isnull = (c.ObjThis == nullptr);
        }
        if (r) *r = (tjs_int)(isnull ? 1 : 0);
        return TJS_S_OK;
    });

    // Scripts.getObjectContext(obj): the object a closure is bound to, or void when it has no
    // context (Action.tjs copyCompleteFunc compares this with the context it wants to replace).
    Define(scripts, TJS_W("getObjectContext"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        if (p[0]->Type() == tvtObject) {
            tTJSVariantClosure c = p[0]->AsObjectClosureNoAddRef();
            if (c.ObjThis && r) *r = tTJSVariant(c.ObjThis, c.ObjThis);
        }
        return TJS_S_OK;
    });

    // Scripts.loadDataPack(storage): KAGZ's psdlayer.tjs uses this for `.pbd`
    // files. They are TJS value streams (TJS/ns0) or PackinOne-encrypted,
    // LZ4-framed streams (TJS/4s0); the decoded tree is exactly what loadDATA
    // consumes: root[0] is the page size and the remaining items are layers.
    Define(scripts, TJS_W("loadDataPack"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        if (p[0]->Type() == tvtVoid) return TJS_E_INVALIDPARAM;
        ttstr storage = *p[0];

        std::vector<tjs_uint8> data;
        if (!ReadStorage(storage, data)) {
            LogTJS(ttstr(TJS_W("Scripts.loadDataPack: cannot read ")) + storage);
            return TJS_E_FAIL;
        }

        kk4s::pbd::Value root;
        std::string error;
        if (!kk4s::pbd::Decode(data.data(), data.size(), root, error)) {
            tjs_string message;
            TVPUtf8ToUtf16(message, error);
            LogTJS(ttstr(TJS_W("Scripts.loadDataPack: ")) + storage + TJS_W(": ") + ttstr(message.c_str()));
            return TJS_E_FAIL;
        }
        if (getenv("KK4S_TRACE_DATAPACK")) {
            LogTJS(ttstr(TJS_W("Scripts.loadDataPack: ")) + storage + TJS_W(" ok"));
        }
        if (r) *r = ToVariant(root, 0);
        return TJS_S_OK;
    });
}

}  // namespace compat
}  // namespace kk4s
