// kk4s: optional "soft mode" for plugins kk4s does not have.
//
// When a game links a DLL kk4s has no replacement for, the members that DLL
// would have added are missing and the first use throws "Member ... does not
// exist", which usually ends the game. With soft mode on (launcher setting,
// settings.ini soft_mode=1; off by default), reading or calling a member that
// does not exist gives void instead, but only when
//   - at least one plugin was ignored in this game (Plugins.link of an
//     unknown DLL), and
//   - the object is a native class (System, Layer, Window, ...) or an
//     instance of one (a KAG layer is a Layer). Plain script objects,
//     dictionaries, arrays and the global object still throw, so typos in
//     game scripts are not hidden.
// Each class.member is logged once. Only obj.member / obj.member() are
// affected (the TJS VM hook in tjsInterCodeExec.cpp); obj[name], typeof and
// "in" are not.
//
// The risk, and why it is off by default: a game that gets void where the
// plugin returned a value can misbehave silently later, for example store
// wrong data in a save.
#include "tjsCommHead.h"
#include "tjsNative.h"
#include "ScriptMgnIntf.h"

#include "kk4s_compat.h"
#include "kk4s_platform.h"
#include "kk4s_settings.h"

#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace TJS {
extern bool (*TJSSoftMissingMemberHook)(iTJSDispatch2* obj, const tjs_char* name, bool call);
}

namespace kk4s {
namespace compat {

namespace {

std::mutex g_mutex;
std::vector<ttstr> g_native_classes; // native classes in the global object at startup
std::set<std::string> g_logged;      // "Class.member" already reported

class tNativeClassCollector : public tTJSDispatch {
public:
    std::vector<ttstr> names;
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant* result,
                                       tjs_int numparams, tTJSVariant** param, iTJSDispatch2*) override {
        if (numparams >= 3 && param[2]->Type() == tvtObject) {
            iTJSDispatch2* o = param[2]->AsObjectNoAddRef();
            if (o && dynamic_cast<tTJSNativeClass*>(o)) names.emplace_back(*param[0]);
        }
        if (result) *result = (tjs_int)1;
        return TJS_S_OK;
    }
};

// The native class the object is, or is an instance of; empty if none.
ttstr NativeClassOf(iTJSDispatch2* obj) {
    if (tTJSNativeClass* c = dynamic_cast<tTJSNativeClass*>(obj)) return c->GetClassName();
    std::vector<ttstr> names;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        names = g_native_classes;
    }
    for (const ttstr& n : names)
        if (obj->IsInstanceOf(0, nullptr, nullptr, n.c_str(), obj) == TJS_S_TRUE) return n;
    return ttstr();
}

bool SoftMissingMember(iTJSDispatch2* obj, const tjs_char* name, bool call) {
    if (!obj || !name || !LiveSettings().soft_mode || !PluginIgnoredThisGame()) return false;
    ttstr cls = NativeClassOf(obj);
    if (cls.IsEmpty()) return false;
    std::string key = cls.AsNarrowStdString() + "." + ttstr(name).AsNarrowStdString();
    bool first;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        first = g_logged.insert(key).second;
    }
    if (first)
        kk4s::Log("compat: soft mode: %s does not exist, %s void", key.c_str(), call ? "call returns" : "reads as");
    return true;
}

} // namespace

void InstallSoftMode() {
    std::vector<ttstr> names;
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (global) {
        tNativeClassCollector* c = new tNativeClassCollector;
        tTJSVariantClosure closure(c);
        global->EnumMembers(TJS_IGNOREPROP, &closure, global);
        names = c->names;
        closure.Release();
        global->Release();
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_native_classes = names;
        g_logged.clear();
    }
    TJS::TJSSoftMissingMemberHook = &SoftMissingMember;
    if (LiveSettings().soft_mode)
        kk4s::Log("compat: soft mode on (%d native classes): missing members of native objects read as void "
                  "once a plugin was ignored", (int)names.size());
}

} // namespace compat
} // namespace kk4s
