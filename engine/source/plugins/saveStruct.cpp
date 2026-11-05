// saveStruct.dll (wamsoft/saveStruct, W.Dee / miahmie, Kirikiri license).
// kk4s: replaced krkrs' copy of an old version with the current contract:
// - Array/Dictionary.saveStruct2(file, utf, newline, option) and
//   toStructString(newline, option), Scripts.toStructString(value, newline,
//   option), Array.save2(file, utf, newline)
// - options ssoIndent / ssoConst / ssoSort / ssoHidden (globals set on link)
// - non-Array objects are written through EnumMembers as %[...]
// - reals as hex with the decimal value in a comment, as upstream (writer.hex)
// The old copy wrote raw UTF-16 without BOM to files and returned an
// unterminated buffer from toStructString. Files are written as UTF-8 in both
// modes: Windows used the ANSI code page when utf was false, which would lose
// characters of translated games here; the engine's text reader detects UTF-8.
#include "ncbind/ncbind.hpp"
#include "PluginIntf.h"
#include "BinaryStream.h"
#include "CharacterSet.h"
#include "MsgIntf.h"
#include "ScriptMgnIntf.h"
#include "tjsUtils.h"
#include <vector>
#define NCB_MODULE_NAME TJS_W("saveStruct.dll")

#define OPTION_INDENT  (1<<0)
#define OPTION_CONST   (1<<1)
#define OPTION_KEYSORT (1<<2)
#define OPTION_HIDDEN  (1<<3)

// not public in TJS
#define OPTION_NOPREFIX      (1<<10) // no int/real prefix (only without (const))
#define OPTION_NOARRAYINDENT (1<<11) // arrays ignore the indent option

#define TJS_CONST TJS_W("(const)")

//---------------------------------------------------------------------------
// writers (json/Writer.hpp upstream)
class IWriter {
protected:
	const tjs_char *newlinestr;
public:
	int indent;
	bool hex;
	IWriter(int newlinetype = 0) {
		indent = 0;
		hex = false;
		newlinestr = newlinetype == 1 ? TJS_W("\n") : TJS_W("\r\n");
	}
	virtual ~IWriter() {}
	virtual void write(const tjs_char *str) = 0;
	virtual void write(tjs_char ch) = 0;

	void newline() {
		write(newlinestr);
		for (int i = 0; i < indent; i++) write((tjs_char)' ');
	}
	void addIndent() { indent++; newline(); }
	void delIndent() { indent--; newline(); }
};

class IStringWriter : public IWriter {
public:
	ttstr buf;
	IStringWriter(int newlinetype = 0) : IWriter(newlinetype) {}
	void write(const tjs_char *str) override { buf += str; }
	void write(tjs_char ch) override { buf += ch; }
};

class IFileWriter : public IWriter {
	ttstr buf;
	tTJSBinaryStream *stream;
	std::vector<char> dat;
public:
	IFileWriter(const ttstr &filename, int newlinetype = 0) : IWriter(newlinetype) {
		stream = TVPCreateBinaryStreamForWrite(filename, TJS_W(""));
	}
	~IFileWriter() {
		if (stream) {
			output();
			delete stream;
		}
	}
	void output() {
		if (stream && buf.length() > 0) {
			tjs_int len = TVPWideCharToUtf8String(buf.c_str(), NULL);
			if (len > 0) {
				dat.resize(len + 1);
				len = TVPWideCharToUtf8String(buf.c_str(), &dat[0]);
				stream->Write(&dat[0], len);
			}
		}
		buf.Clear();
	}
	void write(const tjs_char *str) override {
		if (stream) {
			buf += str;
			if (buf.length() >= 1024) output();
		}
	}
	void write(tjs_char ch) override { buf += ch; }
};

static void getVariantString(const tTJSVariant &var, IWriter *writer, tjs_uint32 option = 0);

//---------------------------------------------------------------------------
// Array class members
static iTJSDispatch2 *ArrayClass     = NULL; // Array
static iTJSDispatch2 *ArrayCountProp = NULL; // Array.count
static iTJSDispatch2 *ArrayAddFunc   = NULL; // Array.add
static iTJSDispatch2 *ArraySortFunc  = NULL; // Array.sort

static iTJSDispatch2* getArrayChildObject(const tjs_char *name)
{
	if (!ArrayClass) {
		iTJSDispatch2 *dummy = TJSCreateArrayObject(&ArrayClass);
		dummy->Release();
		if (!ArrayClass) TVPThrowExceptionMessage(TJS_W("can't get Array object"));
	}
	tTJSVariant val;
	if (TJS_FAILED(ArrayClass->PropGet(TJS_IGNOREPROP, name, NULL, &val, ArrayClass)))
		TVPThrowExceptionMessage(TJS_W("can't get Array.%1 member"), ttstr(name));
	return val.AsObject();
}

static tjs_int getArrayCount(iTJSDispatch2 *array) {
	tjs_int count = 0;
	tTJSVariant result;
	if (!ArrayCountProp) {
		ArrayCountProp = getArrayChildObject(TJS_W("count"));
		if (!ArrayCountProp) return 0;
	}
	if (TJS_SUCCEEDED(ArrayCountProp->PropGet(0, NULL, NULL, &result, array))) {
		count = result;
	} else if (TJS_SUCCEEDED(array->PropGet(0, TJS_W("count"), NULL, &result, array))) {
		count = result;
	}
	return count;
}

static inline tjs_error invokeArrayFunc(iTJSDispatch2 *array, iTJSDispatch2* &func, const tjs_char *method,
	tTJSVariant *result, tjs_int numparams, tTJSVariant **param)
{
	if (!func) {
		func = getArrayChildObject(method);
		if (!func) return TJS_E_MEMBERNOTFOUND;
	}
	return func->FuncCall(0, NULL, NULL, result, numparams, param, array);
}

//---------------------------------------------------------------------------
// dictionary members (EnumMembers callback)
class DictMemberDispCaller : public tTJSDispatch
{
protected:
	IWriter *writer;
	tjs_uint32 option;
	bool first;
	iTJSDispatch2 *keys;
public:
	DictMemberDispCaller(IWriter *writer, tjs_uint32 option)
		: writer(writer), option(option), first(true), keys(0) {}
	virtual ~DictMemberDispCaller() {
		if (keys) keys->Release();
	}
	tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32 flag, const tjs_char *membername, tjs_uint32 *hint,
		tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) override {
		if (numparams > 1) {
			tTVInteger flags = param[1]->AsInteger();
			if (!(flags & TJS_HIDDENMEMBER) || (option & OPTION_HIDDEN)) {
				if (option & OPTION_KEYSORT) {
					if (!keys) keys = TJSCreateArrayObject();
					invokeArrayFunc(keys, ArrayAddFunc, TJS_W("add"), NULL, 1, param); // keys.add(name)
				} else if (numparams > 2) {
					write(*param[0], *param[2]);
				}
			}
		}
		if (result) *result = true;
		return TJS_S_OK;
	}
	void write(const tTJSVariant &key, const tTJSVariant &value) {
		if (first) {
			first = false;
			if (option & OPTION_INDENT) writer->addIndent(); // indent only with at least one member
		} else {
			writer->write((tjs_char)',');
			writer->newline();
		}
		ttstr name(key.GetString()), escape;
		escape = name.EscapeC();
		writer->write((tjs_char)'"');
		writer->write(escape.c_str());
		writer->write(TJS_W("\"=>"));
		getVariantString(value, writer, option);
	}
	void cleanup(iTJSDispatch2 *dict) {
		if (keys) {
			// second pass for sorted keys
			invokeArrayFunc(keys, ArraySortFunc, TJS_W("sort"), NULL, 0, NULL);
			tjs_int count = getArrayCount(keys);
			for (tjs_int i = 0; i < count; i++) {
				tTJSVariant key, value;
				if (TJS_SUCCEEDED(keys->PropGetByNum(TJS_IGNOREPROP, i, &key, keys)) &&
					TJS_SUCCEEDED(dict->PropGet(TJS_IGNOREPROP, key.GetString(), key.GetHint(), &value, dict)))
					write(key, value);
			}
		}
		if (!first && (option & OPTION_INDENT)) writer->delIndent();
	}
};

static void getDictString(iTJSDispatch2 *dict, IWriter *writer, tjs_uint32 option = 0)
{
	if (option & OPTION_CONST) writer->write(TJS_CONST);
	writer->write(TJS_W("%["));
	DictMemberDispCaller *caller = new DictMemberDispCaller(writer, option);
	tTJSVariantClosure closure(caller);
	tjs_uint32 novalue = (option & OPTION_KEYSORT) ? TJS_ENUM_NO_VALUE : 0; // sorting fetches values later
	dict->EnumMembers(TJS_IGNOREPROP | novalue, &closure, dict);
	caller->cleanup(dict);
	caller->Release();
	writer->write((tjs_char)']');
}

static void getArrayString(iTJSDispatch2 *array, IWriter *writer, tjs_uint32 option = 0)
{
	if (option & OPTION_CONST) writer->write(TJS_CONST);
	writer->write((tjs_char)'[');
	tjs_int count = getArrayCount(array);
	bool indent = (count > 0) && (option & OPTION_INDENT) && !(option & OPTION_NOARRAYINDENT);
	if (indent) writer->addIndent();
	for (tjs_int i = 0; i < count; i++) {
		if (i != 0) {
			writer->write((tjs_char)',');
			if (indent) writer->newline();
		}
		tTJSVariant result;
		if (array->PropGetByNum(TJS_IGNOREPROP, i, &result, array) == TJS_S_OK)
			getVariantString(result, writer, option);
	}
	if (indent) writer->delIndent();
	writer->write((tjs_char)']');
}

static void getVariantString(const tTJSVariant &var, IWriter *writer, tjs_uint32 option)
{
	tTJSVariantType type = var.Type();
	if (type == tvtObject) {
		iTJSDispatch2 *obj = var.AsObjectNoAddRef();
		if (obj == NULL) {
			writer->write(TJS_W("null"));
		} else if (obj->IsInstanceOf(TJS_IGNOREPROP, NULL, NULL, TJS_W("Array"), obj) == TJS_S_TRUE) {
			getArrayString(obj, writer, option);
		} else {
			getDictString(obj, writer, option);
		}
	} else {
		if (!(option & (OPTION_CONST | OPTION_NOPREFIX))) { // (const) would not parse with them
			if (type == tvtInteger) writer->write(TJS_W("int "));
			else if (type == tvtReal) writer->write(TJS_W("real "));
		}
		writer->write(TJSVariantToExpressionString(var).c_str());
	}
}

//---------------------------------------------------------------------------
class ArrayAdd {
public:
	ArrayAdd() {}

	// save2(filename, utf = false, newline = 0): one element per line
	static tjs_error TJS_INTF_METHOD save2(tTJSVariant *result, tjs_int numparams,
		tTJSVariant **param, iTJSDispatch2 *objthis) {
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		IFileWriter writer(param[0]->AsStringNoAddRef(), numparams > 2 ? (int)*param[2] : 0);
		writer.hex = true;
		tjs_int count = getArrayCount(objthis);
		for (tjs_int i = 0; i < count; i++) {
			tTJSVariant v;
			if (objthis->PropGetByNum(TJS_IGNOREPROP, i, &v, objthis) == TJS_S_OK) {
				writer.write(v.GetString());
				writer.newline();
			}
		}
		return TJS_S_OK;
	}

	// saveStruct2(filename, utf = false, newline = 0, option = 0)
	static tjs_error TJS_INTF_METHOD saveStruct2(tTJSVariant *result, tjs_int numparams,
		tTJSVariant **param, iTJSDispatch2 *objthis) {
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		IFileWriter writer(param[0]->AsStringNoAddRef(), numparams > 2 ? (int)*param[2] : 0);
		writer.hex = true;
		getArrayString(objthis, &writer, numparams > 3 ? (int)*param[3] : 0);
		return TJS_S_OK;
	}

	// toStructString(newline = 1, option = 0)
	static tjs_error TJS_INTF_METHOD toStructString(tTJSVariant *result, tjs_int numparams,
		tTJSVariant **param, iTJSDispatch2 *objthis) {
		if (result) {
			IStringWriter writer(TJS_PARAM_EXIST(0) ? (int)*param[0] : 1);
			writer.hex = true;
			getArrayString(objthis, &writer, numparams > 1 ? (int)*param[1] : 0);
			*result = writer.buf;
		}
		return TJS_S_OK;
	}
};

NCB_ATTACH_CLASS(ArrayAdd, Array) {
	RawCallback("save2", &ArrayAdd::save2, 0);
	RawCallback("saveStruct2", &ArrayAdd::saveStruct2, 0);
	RawCallback("toStructString", &ArrayAdd::toStructString, 0);
};

class DictAdd {
public:
	DictAdd() {}

	// (Dictionary.saveStruct2 incontextof obj)(filename, utf = false, newline = 0, option = 0)
	static tjs_error TJS_INTF_METHOD saveStruct2(tTJSVariant *result, tjs_int numparams,
		tTJSVariant **param, iTJSDispatch2 *objthis) {
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		IFileWriter writer(param[0]->AsStringNoAddRef(), numparams > 2 ? (int)*param[2] : 0);
		writer.hex = true;
		getDictString(objthis, &writer, numparams > 3 ? (int)*param[3] : 0);
		return TJS_S_OK;
	}

	// (Dictionary.toStructString incontextof obj)(newline = 1, option = 0)
	static tjs_error TJS_INTF_METHOD toStructString(tTJSVariant *result, tjs_int numparams,
		tTJSVariant **param, iTJSDispatch2 *objthis) {
		if (result) {
			IStringWriter writer(TJS_PARAM_EXIST(0) ? (int)*param[0] : 1);
			writer.hex = true;
			getDictString(objthis, &writer, numparams > 1 ? (int)*param[1] : 0);
			*result = writer.buf;
		}
		return TJS_S_OK;
	}
};

NCB_ATTACH_CLASS(DictAdd, Dictionary) {
	RawCallback("saveStruct2", &DictAdd::saveStruct2, TJS_STATICMEMBER);
	RawCallback("toStructString", &DictAdd::toStructString, TJS_STATICMEMBER);
};

class ScriptsAddStruct {
public:
	ScriptsAddStruct() {}

	// Scripts.toStructString(value, newline = 1, option = ssoConst... see readme: default 0)
	static tjs_error TJS_INTF_METHOD toStructString(tTJSVariant *result, tjs_int numparams,
		tTJSVariant **param, iTJSDispatch2 *objthis) {
		if (numparams < 1) return TJS_E_BADPARAMCOUNT;
		if (result) {
			IStringWriter writer(TJS_PARAM_EXIST(1) ? (int)*param[1] : 1);
			writer.hex = true;
			getVariantString(*param[0], &writer, numparams > 2 ? (int)*param[2] : 0);
			*result = writer.buf;
		}
		return TJS_S_OK;
	}
};

NCB_ATTACH_CLASS(ScriptsAddStruct, Scripts) {
	RawCallback("toStructString", &ScriptsAddStruct::toStructString, TJS_STATICMEMBER);
};

//---------------------------------------------------------------------------
static void PostRegistCallback()
{
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	if (global) {
		tTJSVariant option_indent((tjs_int)OPTION_INDENT);
		tTJSVariant option_const((tjs_int)OPTION_CONST);
		tTJSVariant option_keysort((tjs_int)OPTION_KEYSORT);
		tTJSVariant option_hidden((tjs_int)OPTION_HIDDEN);
		global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_W("ssoIndent"), NULL, &option_indent, global);
		global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_W("ssoConst"), NULL, &option_const, global);
		global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_W("ssoSort"), NULL, &option_keysort, global);
		global->PropSet(TJS_MEMBERENSURE | TJS_IGNOREPROP, TJS_W("ssoHidden"), NULL, &option_hidden, global);
		global->Release();
	}
}

static void PreUnregistCallback()
{
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	if (global) {
		global->DeleteMember(0, TJS_W("ssoIndent"), NULL, global);
		global->DeleteMember(0, TJS_W("ssoConst"), NULL, global);
		global->DeleteMember(0, TJS_W("ssoSort"), NULL, global);
		global->DeleteMember(0, TJS_W("ssoHidden"), NULL, global);
		global->Release();
	}
	if (ArraySortFunc) ArraySortFunc->Release();
	if (ArrayAddFunc) ArrayAddFunc->Release();
	if (ArrayCountProp) ArrayCountProp->Release();
	if (ArrayClass) ArrayClass->Release();
	ArrayClass = ArrayCountProp = ArrayAddFunc = ArraySortFunc = NULL;
}

NCB_POST_REGIST_CALLBACK(PostRegistCallback);
NCB_PRE_UNREGIST_CALLBACK(PreUnregistCallback);
