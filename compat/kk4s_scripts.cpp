// kk4s: KiriKiri 2/Z `Scripts` APIs that this engine lacks and KAG-based games call.
//
//   Scripts.foreach(obj, func)   func(name, value, extra...) for each member; the first non-void
//                                return value of func stops the loop and is returned
//   Scripts.getObjectKeys(obj)   member names (Array) of a Dictionary / Array
//   Scripts.getObjectCount(obj)  number of members (Dictionary) or elements (Array)
//   Scripts.equalStruct(a, b)    structural equality of Dictionary/Array trees
//   Scripts.clone(obj)           recursive copy of Dictionary / Array trees; other values as is
//
// Each is only added when the engine has no such member. Implemented natively because
// TJS function expressions here cannot see the locals of the function around them.
#include "kk4s_compat.h"

#include "ScriptMgnIntf.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "tjsObject.h"

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
bool EqualStruct(const tTJSVariant& a, const tTJSVariant& b, int depth) {
    if (a.Type() != tvtObject || b.Type() != tvtObject) return a.Type() != tvtObject && b.Type() != tvtObject && a.DiscernCompare(b);
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
            if (!EqualStruct(ea, eb, depth + 1)) return false;
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
            if (!EqualStruct(kv.second, other, depth + 1)) return false;
        }
        return true;
    }
    return false;
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

    Define(scripts, TJS_W("clone"), [](tTJSVariant* r, tjs_int n, tTJSVariant** p, iTJSDispatch2*) -> tjs_error {
        if (n < 1) return TJS_E_BADPARAMCOUNT;
        if (r) *r = Clone(*p[0], 0);
        return TJS_S_OK;
    });
}

}  // namespace compat
}  // namespace kk4s
