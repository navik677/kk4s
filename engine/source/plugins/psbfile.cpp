// kk4s: psbfile.dll — reads PSB containers (.pimg, .scn, .psb) from script.
//
//   Plugins.link("psbfile.dll");
//   var psb = new PSBFile();
//   psb.load("title.pimg");     // throws "invalid psb file." on a bad file
//   psb.root                    // Dictionary / Array / Octet / String / Integer / Real tree
//   psb.clearStorageCache();    // kept for compatibility (nothing is cached)
//
// The parser is compat/psb (generic, no game knowledge). The tree is built eagerly
// from ordinary TJS values: Dictionary, Array, Octet for resources (e.g. TLG images),
// void for "undefined", null for null.
//
// Loaded files are kept in a cache by storage name, and a "psb" storage media serves their
// values: "psb://<storage>/<key>" opens the Octet at root[<key>] (e.g.
// "psb://quickmenu.pimg/1040.tlg" is the TLG image stored under "1040.tlg"). Only top-level
// keys are addressed. clearStorageCache() drops the cache.
#include "ncbind/ncbind.hpp"

#include "CharacterSet.h"
#include "StorageIntf.h"
#include "tjsArray.h"
#include "tjsDictionary.h"

#include "psb/psb.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

#define NCB_MODULE_NAME TJS_W("psbfile.dll")

namespace {

// Builds TJS values from the PSB walk using a stack of open containers.
class TJSBuilder : public kk4s::psb::Visitor {
public:
    ~TJSBuilder() override {
        for (Frame& f : stack_) if (f.obj) f.obj->Release();
    }
    tTJSVariant result;

    void Undefined() override { Put(tTJSVariant()); }
    // kk4s: PSB null becomes void. KAGZ's world.tjs treats "value == void" as
    // "no value" and otherwise indexes the value; a TJS null object is not void
    // and made restore() crash in updateProperty ("Accessing to null object").
    void Null() override { Put(tTJSVariant()); }
    void Bool(bool v) override { Put(tTJSVariant((tTVInteger)(v ? 1 : 0))); }
    void Int(int64_t v) override { Put(tTJSVariant((tTVInteger)v)); }
    void Double(double v) override { Put(tTJSVariant((tTVReal)v)); }
    void String(const std::string& s) override {
        tjs_string w;
        if (!TVPUtf8ToUtf16(w, s)) w.clear();
        Put(tTJSVariant(ttstr(w.c_str())));
    }
    void Resource(uint32_t, const uint8_t* data, size_t size) override {
        Put(size ? tTJSVariant((const tjs_uint8*)data, (tjs_uint)size) : tTJSVariant((const tjs_uint8*)"", (tjs_uint)0));
    }
    void Extra(uint32_t) override { Put(tTJSVariant()); }
    void BeginObject(size_t) override { stack_.push_back(Frame{TJSCreateDictionaryObject(), true, 0, ttstr()}); }
    void Key(const std::string& k) override {
        tjs_string w;
        if (!TVPUtf8ToUtf16(w, k)) w.clear();
        stack_.back().key = ttstr(w.c_str());
    }
    void EndObject() override { Close(); }
    void BeginArray(size_t) override { stack_.push_back(Frame{TJSCreateArrayObject(), false, 0, ttstr()}); }
    void EndArray() override { Close(); }

private:
    struct Frame {
        iTJSDispatch2* obj;
        bool dict;
        tjs_int index;
        ttstr key;
    };
    void Put(const tTJSVariant& v) {
        if (stack_.empty()) { result = v; return; }
        Frame& f = stack_.back();
        tTJSVariant tmp(v);
        if (f.dict) f.obj->PropSet(TJS_MEMBERENSURE, f.key.c_str(), nullptr, &tmp, f.obj);
        else f.obj->PropSetByNum(TJS_MEMBERENSURE, f.index++, &tmp, f.obj);
    }
    void Close() {
        Frame f = stack_.back();
        stack_.pop_back();
        tTJSVariant v(f.obj, f.obj);
        f.obj->Release();
        Put(v);
    }
    std::vector<Frame> stack_;
};

} // namespace

// ---------------------------------------------------------------------------
// cache of loaded files (storage name, lower case -> root) and the "psb" media

static std::map<ttstr, tTJSVariant>& Cache() {
	static std::map<ttstr, tTJSVariant> c;
	return c;
}

static ttstr Lower(const ttstr& s) {
	ttstr r(s);
	r.ToLowerCase();
	return r;
}

static void CacheRoot(const ttstr& storage, const tTJSVariant& root) {
	ttstr key = Lower(storage);
	Cache()[key] = root;
	ttstr base = Lower(TVPExtractStorageName(storage));
	if (base != key) Cache()[base] = root;
}

// Octet at root[key] of the cached file, or void.
static tTJSVariant FindOctet(const ttstr& name) {
	tTJSVariant none;
	const tjs_char* p = name.c_str();
	const tjs_char* slash = nullptr;
	for (const tjs_char* q = p; *q; q++) if (*q == TJS_W('/')) slash = q;
	if (!slash || slash == p || !slash[1]) return none;
	ttstr domain = Lower(ttstr(p, (tjs_int)(slash - p)));
	std::map<ttstr, tTJSVariant>::iterator it = Cache().find(domain);
	if (it == Cache().end() || it->second.Type() != tvtObject || !it->second.AsObjectNoAddRef()) return none;
	tTJSVariant value;
	iTJSDispatch2* root = it->second.AsObjectNoAddRef();
	if (TJS_FAILED(root->PropGet(0, slash + 1, nullptr, &value, root)) || value.Type() != tvtOctet) return none;
	return value;
}

class PSBStream : public tTJSBinaryStream {
public:
	explicit PSBStream(const tTJSVariant& octet) : octet_(octet), pos_(0) {}
	virtual ~PSBStream() {}
	virtual tjs_uint TJS_INTF_METHOD Read(void* buf, tjs_uint n) {
		tjs_uint64 size = GetSize();
		if (pos_ >= size) return 0;
		if (n > size - pos_) n = (tjs_uint)(size - pos_);
		memcpy(buf, octet_.AsOctetNoAddRef()->GetData() + pos_, n);
		pos_ += n;
		return n;
	}
	virtual tjs_uint TJS_INTF_METHOD Write(const void*, tjs_uint) { return 0; }
	virtual tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 off, tjs_int whence) {
		tjs_int64 base = whence == TJS_BS_SEEK_CUR ? (tjs_int64)pos_ : whence == TJS_BS_SEEK_END ? (tjs_int64)GetSize() : 0;
		tjs_int64 np = base + off;
		pos_ = np < 0 ? 0 : (tjs_uint64)np;
		return pos_;
	}
	virtual tjs_uint64 TJS_INTF_METHOD GetSize() { return octet_.AsOctetNoAddRef()->GetLength(); }

private:
	tTJSVariant octet_;
	tjs_uint64 pos_;
};

class PSBStorage : public iTVPStorageMedia {
public:
	PSBStorage() : refs_(1) {}
	virtual void TJS_INTF_METHOD AddRef() { refs_++; }
	virtual void TJS_INTF_METHOD Release() { if (refs_ == 1) delete this; else refs_--; }
	virtual void TJS_INTF_METHOD GetName(ttstr& name) { name = TJS_W("psb"); }
	virtual void TJS_INTF_METHOD NormalizeDomainName(ttstr&) {}
	virtual void TJS_INTF_METHOD NormalizePathName(ttstr&) {}
	virtual bool TJS_INTF_METHOD CheckExistentStorage(const ttstr& name) { return FindOctet(name).Type() == tvtOctet; }
	virtual tTJSBinaryStream* TJS_INTF_METHOD Open(const ttstr& name, tjs_uint32 flags) {
		tTJSVariant v = FindOctet(name);
		if (flags != TJS_BS_READ || v.Type() != tvtOctet) TVPThrowExceptionMessage(TJS_W("invalid path:%1"), name);
		return new PSBStream(v);
	}
	virtual void TJS_INTF_METHOD GetListAt(const ttstr&, iTVPStorageLister*) {}
	virtual void TJS_INTF_METHOD GetLocallyAccessibleName(ttstr& name) { name = TJS_W(""); }

private:
	tjs_uint refs_;
};

static PSBStorage* g_media = nullptr;

static void PSBPreRegist() {
	if (!g_media) {
		g_media = new PSBStorage();
		TVPRegisterStorageMedia(g_media);
	}
}

static void PSBPostUnregist() {
	if (g_media) {
		TVPUnregisterStorageMedia(g_media);
		g_media->Release();
		g_media = nullptr;
	}
	Cache().clear();
}

NCB_PRE_REGIST_CALLBACK(PSBPreRegist);
NCB_POST_UNREGIST_CALLBACK(PSBPostUnregist);

class PSBFile {
public:
    PSBFile() {}

    // Reads the storage completely and builds root. Throws on failure.
    bool load(ttstr storage) {
        std::vector<uint8_t> data;
        {
            tTJSBinaryStream* st = nullptr;
            try {
                st = TVPCreateStream(storage);
            } catch (...) {
                st = nullptr;
            }
            if (!st) TVPThrowExceptionMessage(TJS_W("%1:cannot open psbfile"), storage);
            tjs_uint64 size = st->GetSize();
            data.resize((size_t)size);
            if (size) st->ReadBuffer(&data[0], (tjs_uint)size);
            delete st;
        }
        kk4s::psb::Reader reader;
        if (!reader.Open(data.data(), data.size())) TVPThrowExceptionMessage(TJS_W("invalid psb file."));
        data.clear();
        TJSBuilder b;
        if (!reader.Walk(b)) TVPThrowExceptionMessage(TJS_W("invalid psb file."));
        root_ = b.result;
        CacheRoot(storage, root_);
        return true;
    }

    tTJSVariant getRoot() const { return root_; }

    void clearStorageCache() { Cache().clear(); }

private:
    tTJSVariant root_;
};

// kk4s: KAGZ's storagedata.tjs uses "new PSBFile(storage)" and reads .root
// immediately; other code may use "new PSBFile()" + load(). A normal ncbind
// constructor has a fixed argument count, so use a factory that accepts both.
static tjs_error TJS_INTF_METHOD PSBFileCreate(PSBFile** result, tjs_int numparams, tTJSVariant** param,
                                              iTJSDispatch2* objthis) {
    PSBFile* file = new PSBFile();
    try {
        if (numparams >= 1 && param[0] && param[0]->Type() != tvtVoid) file->load(ttstr(*param[0]));
    } catch (...) {
        delete file;
        throw;
    }
    *result = file;
    return TJS_S_OK;
}

NCB_REGISTER_CLASS(PSBFile) {
    Factory(PSBFileCreate);
    NCB_METHOD(load);
    NCB_PROPERTY_RO(root, getRoot);
    NCB_METHOD(clearStorageCache);
}
