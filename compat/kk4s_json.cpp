// kk4s compatibility layer: json.dll (wamsoft/json) - Scripts.evalJSON,
// evalJSONStorage, saveJSON, toJSONString.
//
// The reader is the plugin's lenient one, not strict JSON: comments (# // /* */),
// "=" or "=>" between key and value, ";" as a separator, single-quoted strings,
// \x escapes, the keywords true/false/null/void, and a number is a Real only
// when it contains "." (so "1e5" reads as 1, as in the DLL). A parse error is
// logged and throws, as in the DLL. The writer copies the DLL's layout: one space
// of indent per level, a line break after "{" / "[" and before "}" / "]".
// saveJSON writes UTF-8 for both encodings (the DLL writes the ANSI code page
// when utf8 is false; kk4s' text reader detects either).
#include "kk4s_compat.h"

#include "CharacterSet.h"
#include "StorageIntf.h"
#include "TextStream.h"
#include "MsgIntf.h"
#include "ScriptMgnIntf.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "kk4s_platform.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace kk4s {
namespace compat {

namespace {

class JsonReader {
public:
    explicit JsonReader(const ttstr& text) : S(text.c_str()), N(text.length()) {}
    bool Error = false;

    void Parse(tTJSVariant& var) {
        int ch = Next();
        switch (ch) {
        case '"': case '\'':
            ParseQuoted(ch, var);
            break;
        case '{':
            ParseObject(var);
            break;
        case '[':
            ParseArray(var);
            break;
        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        case '.': case '-': case '+': {
            bool real = false;
            std::string s;
            while (IsNumber(ch)) {
                if (ch == '.') real = true;
                s += (char)ch;
                ch = Get();
            }
            Unget();
            if (real) var = (tTVReal)std::strtod(s.c_str(), nullptr);
            else var = (tTVInteger)std::strtoll(s.c_str(), nullptr, 10);
            break;
        }
        default:
            if (ch >= 'a' && ch <= 'z') {
                std::string s;
                while (ch >= 'a' && ch <= 'z') { s += (char)ch; ch = Get(); }
                Unget();
                if (s == "true") var = (tjs_int)1;
                else if (s == "false") var = (tjs_int)0;
                else if (s == "null" || s == "void") var.Clear();
                else Fail("json: unknown keyword: " + s);
            } else {
                Fail("json: unexpected character");
            }
        }
    }

    // Only blanks or comments may follow the value.
    bool AtEnd() { return Next() == -1; }

private:
    const tjs_char* S;
    size_t N, P = 0;

    int Get() { return P < N ? (int)S[P++] : (P++, -1); }
    void Unget() { if (P > 0) --P; }
    void Fail(const std::string& msg) {
        if (!Error) kk4s::Log("compat: %s (at character %u)", msg.c_str(), (unsigned)P);
        Error = true;
    }
    static bool IsNumber(int c) {
        return (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E';
    }
    void ToEol() {
        int c;
        do c = Get(); while (c != -1 && c != '\n' && c != '\r');
    }
    // Next character that is not a blank, a BOM or inside a comment.
    int Next() {
        for (;;) {
            int c = Get();
            if (c == '#') {
                ToEol();
            } else if (c == '/') {
                int d = Get();
                if (d == '/') {
                    ToEol();
                } else if (d == '*') {
                    for (;;) {
                        c = Get();
                        if (c == -1) { Fail("json: comment is not closed"); return -1; }
                        if (c == '*') {
                            if (Get() == '/') break;
                            Unget();
                        }
                    }
                } else {
                    Unget();
                    return '/';
                }
            } else if (c != 0xfeff && (c == -1 || c > ' ')) {
                return c;
            }
        }
    }
    int Hex(int digits) {
        int v = 0;
        for (int i = 0; i < digits; ++i) {
            int c = Get();
            if (c >= '0' && c <= '9') v = v * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') v = v * 16 + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v = v * 16 + (c - 'A' + 10);
            else { Unget(); break; }
        }
        return v;
    }
    void ParseQuoted(int quote, tTJSVariant& var) {
        tjs_string str;
        for (;;) {
            int c = Get();
            switch (c) {
            case -1: case 0: case '\n': case '\r':
                Fail("json: string is not terminated");
                var = ttstr(str.c_str());
                return;
            case '\\':
                c = Get();
                switch (c) {
                case 'b': str += (tjs_char)'\b'; break;
                case 'f': str += (tjs_char)'\f'; break;
                case 't': str += (tjs_char)'\t'; break;
                case 'r': str += (tjs_char)'\r'; break;
                case 'n': str += (tjs_char)'\n'; break;
                case 'u': str += (tjs_char)Hex(4); break;
                case 'x': str += (tjs_char)Hex(2); break;
                case -1: break;
                default: str += (tjs_char)c;
                }
                break;
            default:
                if (c == quote) {
                    var = ttstr(str.c_str());
                    return;
                }
                str += (tjs_char)c;
            }
        }
    }
    void ParseObject(tTJSVariant& var) {
        iTJSDispatch2* dict = TJSCreateDictionaryObject();
        var = tTJSVariant(dict, dict);
        dict->Release();
        for (;;) {
            int c = Next();
            if (c == -1) { Fail("json: object must end with '}'"); return; }
            if (c == '}') return;
            if (c == ',' || c == ';') {
                Unget();
            } else {
                Unget();
                tTJSVariant key;
                Parse(key);
                if (Error) return;
                c = Next();
                if (c == '=') {
                    if (Get() != '>') Unget();
                } else if (c != ':') {
                    Fail("json: ':', '=' or '=>' expected after a key");
                    return;
                }
                tTJSVariant value;
                Parse(value);
                if (Error) return;
                ttstr name = key.Type() == tvtVoid ? ttstr() : ttstr(key);
                dict->PropSet(TJS_MEMBERENSURE, name.c_str(), nullptr, &value, dict);
            }
            switch (Next()) {
            case ';': case ',': break;
            case '}': return;
            default: Fail("json: ',', ';' or '}' expected"); return;
            }
        }
    }
    void ParseArray(tTJSVariant& var) {
        iTJSDispatch2* arr = TJSCreateArrayObject();
        var = tTJSVariant(arr, arr);
        arr->Release();
        tjs_int count = 0;
        for (;;) {
            int c = Next();
            if (c == -1) { Fail("json: array must end with ']'"); return; }
            if (c == ']') return;
            tTJSVariant value;
            if (c == ',' || c == ';') {
                Unget(); // an empty column is void
            } else {
                Unget();
                Parse(value);
                if (Error) return;
            }
            arr->PropSetByNum(TJS_MEMBERENSURE, count++, &value, arr);
            switch (Next()) {
            case ';': case ',': break;
            case ']': return;
            default: Fail("json: ',', ';' or ']' expected"); return;
            }
        }
    }
};

class JsonWriter {
public:
    explicit JsonWriter(int newline) : NewlineStr(newline == 1 ? TJS_W("\n") : TJS_W("\r\n")) {}
    tjs_string Out;

    void Value(const tTJSVariant& v, int depth = 0) {
        if (depth > 256) { Out += TJS_W("null"); return; }
        switch (v.Type()) {
        case tvtObject: {
            iTJSDispatch2* o = v.AsObjectNoAddRef();
            if (!o) Out += TJS_W("null");
            else if (o->IsInstanceOf(TJS_IGNOREPROP, nullptr, nullptr, TJS_W("Array"), o) == TJS_S_TRUE) Array(o, depth);
            else Dict(o, depth);
            break;
        }
        case tvtString:
            Quote(ttstr(v));
            break;
        case tvtInteger: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)v.AsInteger());
            for (const char* c = buf; *c; ++c) Out += (tjs_char)*c;
            break;
        }
        case tvtReal: {
            ttstr s = v; // the engine's number formatting, without a leading '+'
            const tjs_char* p = s.c_str();
            if (*p == '+') ++p;
            Out += p;
            break;
        }
        default:
            Out += TJS_W("null");
        }
    }

private:
    const tjs_char* NewlineStr;
    int Indent = 0;

    void Newline() {
        Out += NewlineStr;
        Out.append((size_t)Indent, (tjs_char)' ');
    }
    void Quote(const ttstr& s) {
        Out += (tjs_char)'"';
        for (const tjs_char* p = s.c_str(); *p; ++p) {
            tjs_char c = *p;
            switch (c) {
            case '"': Out += TJS_W("\\\""); break;
            case '\\': Out += TJS_W("\\\\"); break;
            case 0x08: Out += TJS_W("\\b"); break;
            case 0x0c: Out += TJS_W("\\f"); break;
            case 0x0a: Out += TJS_W("\\n"); break;
            case 0x0d: Out += TJS_W("\\r"); break;
            case 0x09: Out += TJS_W("\\t"); break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    for (const char* b = buf; *b; ++b) Out += (tjs_char)*b;
                } else {
                    Out += c;
                }
            }
        }
        Out += (tjs_char)'"';
    }
    void Array(iTJSDispatch2* a, int depth) {
        Out += (tjs_char)'[';
        ++Indent; Newline();
        tTJSVariant cnt;
        a->PropGet(0, TJS_W("count"), nullptr, &cnt, a);
        tjs_int n = (tjs_int)cnt.AsInteger();
        for (tjs_int i = 0; i < n; ++i) {
            if (i) { Out += (tjs_char)','; Newline(); }
            tTJSVariant e;
            if (a->PropGetByNum(TJS_IGNOREPROP, i, &e, a) == TJS_S_OK) Value(e, depth + 1);
        }
        --Indent; Newline();
        Out += (tjs_char)']';
    }
    struct Enum : public tTJSDispatch {
        JsonWriter* W; int Depth; bool First = true;
        Enum(JsonWriter* w, int d) : W(w), Depth(d) {}
        tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*, tTJSVariant* result,
                                           tjs_int n, tTJSVariant** p, iTJSDispatch2*) override {
            if (n > 2 && !(p[1]->AsInteger() & TJS_HIDDENMEMBER)) {
                if (!First) { W->Out += (tjs_char)','; W->Newline(); }
                First = false;
                W->Quote(ttstr(*p[0]));
                W->Out += (tjs_char)':';
                W->Value(*p[2], Depth + 1);
            }
            if (result) *result = (tjs_int)1;
            return TJS_S_OK;
        }
    };
    void Dict(iTJSDispatch2* d, int depth) {
        Out += (tjs_char)'{';
        ++Indent; Newline();
        Enum* e = new Enum(this, depth);
        tTJSVariantClosure clo(e, nullptr);
        d->EnumMembers(TJS_IGNOREPROP, &clo, d);
        e->Release();
        --Indent; Newline();
        Out += (tjs_char)'}';
    }
};

using Fn = tjs_error (*)(tTJSVariant*, tjs_int, tTJSVariant**);

class tJsonFunction : public tTJSDispatch {
    Fn F;
public:
    explicit tJsonFunction(Fn f) : F(f) {}
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char* membername, tjs_uint32*, tTJSVariant* result,
                                       tjs_int n, tTJSVariant** p, iTJSDispatch2*) override {
        if (membername) return TJS_E_MEMBERNOTFOUND;
        if (result) result->Clear();
        return F(result, n, p);
    }
};

void EvalText(const ttstr& text, tTJSVariant* r, const char* what) {
    JsonReader reader(text);
    tTJSVariant v;
    reader.Parse(v);
    if (reader.Error) TVPThrowExceptionMessage(what[0] == 'f' ? TJS_W("JSON file parse error") : TJS_W("JSON parse error"));
    if (r) *r = v;
}

tjs_error EvalJSON(tTJSVariant* r, tjs_int n, tTJSVariant** p) {
    if (n < 1) return TJS_E_BADPARAMCOUNT;
    EvalText(ttstr(*p[0]), r, "text");
    return TJS_S_OK;
}

// evalJSONStorage(file, utf8 = false): kk4s' text reader detects UTF-8 / UTF-16 /
// Shift_JIS by itself, so the flag only matters for files it cannot tell apart.
tjs_error EvalJSONStorage(tTJSVariant* r, tjs_int n, tTJSVariant** p) {
    if (n < 1) return TJS_E_BADPARAMCOUNT;
    ttstr text;
    iTJSTextReadStream* s = TVPCreateTextStreamForRead(*p[0], TJS_W(""));
    try {
        s->Read(text, 0);
    } catch (...) {
        s->Destruct();
        throw;
    }
    s->Destruct();
    EvalText(text, r, "file");
    return TJS_S_OK;
}

tjs_error ToJSONString(tTJSVariant* r, tjs_int n, tTJSVariant** p) {
    if (n < 1) return TJS_E_BADPARAMCOUNT;
    JsonWriter w(n > 1 && p[1]->Type() != tvtVoid ? (int)p[1]->AsInteger() : 0);
    w.Value(*p[0]);
    if (r) *r = ttstr(w.Out.c_str());
    return TJS_S_OK;
}

// saveJSON(file, obj, utf8 = false, newline = 0)
tjs_error SaveJSON(tTJSVariant*, tjs_int n, tTJSVariant** p) {
    if (n < 2) return TJS_E_BADPARAMCOUNT;
    JsonWriter w(n > 3 && p[3]->Type() != tvtVoid ? (int)p[3]->AsInteger() : 0);
    w.Value(*p[1]);
    std::string bytes;
    if (!TVPUtf16ToUtf8(bytes, w.Out)) TVPThrowExceptionMessage(TJS_W("saveJSON: cannot encode the text"));
    tTJSBinaryStream* out = TVPCreateStream(TVPNormalizeStorageName(*p[0]), TJS_BS_WRITE);
    try {
        out->WriteBuffer(bytes.data(), (tjs_uint)bytes.size());
    } catch (...) {
        delete out;
        throw;
    }
    delete out;
    TVPClearStorageCaches();
    return TJS_S_OK;
}

void Define(iTJSDispatch2* obj, const tjs_char* name, Fn fn) {
    tTJSVariant existing;
    if (TJS_SUCCEEDED(obj->PropGet(0, name, nullptr, &existing, obj)) && existing.Type() != tvtVoid) return;
    iTJSDispatch2* f = new tJsonFunction(fn);
    tTJSVariant v(f, nullptr);
    f->Release();
    obj->PropSet(TJS_MEMBERENSURE, name, nullptr, &v, obj);
}

} // namespace

void LinkJsonDll() {
    iTJSDispatch2* g = TVPGetScriptEngine()->GetGlobalNoAddRef();
    tTJSVariant sv;
    if (TJS_FAILED(g->PropGet(0, TJS_W("Scripts"), nullptr, &sv, g)) || sv.Type() != tvtObject) return;
    iTJSDispatch2* scripts = sv.AsObjectNoAddRef();
    Define(scripts, TJS_W("evalJSON"), EvalJSON);
    Define(scripts, TJS_W("evalJSONStorage"), EvalJSONStorage);
    Define(scripts, TJS_W("saveJSON"), SaveJSON);
    Define(scripts, TJS_W("toJSONString"), ToJSONString);
}

} // namespace compat
} // namespace kk4s
