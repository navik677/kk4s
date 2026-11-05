// kk4s: KAGParserEx.dll (wamsoft/krkrz KAGParserEx, miahmie / wtnbgo, Kirikiri
// license). Plugins.link("KAGParserEx.dll") replaces the global KAGParser class
// with the extended parser, as the plugin's Main.cpp does: multi-line tags
// (multiLineTagEnabled, " \" + ";"), "taglist" in every tag, parameter macros
// (paramMacros, [pmacro] / [erasepmacro]), [emb escape=false], macro "*"
// keeping the parameters written before it, hasLabel(). The parser source
// (KAGParserEx_parser.*, upstream KAGParser.cpp / .h) is compiled in its own
// namespace next to the engine's parser. Before, kk4s only emulated "taglist".
#include "tjsCommHead.h"
#include "StorageIntf.h"
#include "tjsDictionary.h"
#include "MsgIntf.h"
#include "DebugIntf.h"
#include "ScriptMgnIntf.h"
#include "tjsHashSearch.h"
#include "TextStream.h"
#include "tjsGlobalStringMap.h"
#include "EventIntf.h"
#include <cstring> // before the namespace: the parser header includes it too
#include <vector>

#include "kagparserex.h"

// the parser messages are defined by the engine's own KAGParser.cpp
extern const tjs_char *TVPKAGNoLine, *TVPKAGCannotOmmitFirstLabelName, *TVPKAGMalformedSaveData,
	*TVPKAGLabelNotFound, *TVPLabelOrScriptInMacro, *TVPKAGInlineScriptNotEnd, *TVPKAGSyntaxError,
	*TVPKAGCallStackUnderflow, *TVPKAGReturnLostSync, *TVPKAGSpecifyKAGParser, *TVPUnknownMacroName;

namespace kk4s_kagparserex {
using namespace TJS;

#define TVP_KAGPARSER_EX_PLUGIN
#define TVP_KAGPARSER_EX_CLASSNAME TJS_W("KAGParser")
#define TVPThrowInternalError TVPThrowExceptionMessage(TVPInternalError, __FILE__, __LINE__)

#include "KAGParserEx_parser.h"
// the parser registers its methods the way a tp_stub plugin does (on a
// tTJSNativeClassForPlugin object named classobj), not as the engine's classes
#undef TJS_NCM_REG_THIS
#define TJS_NCM_REG_THIS classobj
#undef TJS_NATIVE_SET_ClassID
#define TJS_NATIVE_SET_ClassID TJS_NATIVE_CLASSID_NAME = TJS_NCM_CLASSID;
#undef TJS_NATIVE_CLASSID_NAME
#include "KAGParserEx_parser.inc"

} // namespace kk4s_kagparserex

// Replace the global KAGParser class (once per game).
void TVPLinkKAGParserEx()
{
	using namespace kk4s_kagparserex;
	static bool initialised = false;
	if (!initialised) {
		initialised = true;
		tTJSNI_KAGParser::initMethod();
	}
	iTJSDispatch2 *global = TVPGetScriptDispatch();
	if (!global) return;
	iTJSDispatch2 *tjsclass = tTJSNC_KAGParser::CreateNativeClass();
	tTJSVariant val(tjsclass);
	tjsclass->Release();
	global->PropSet(TJS_MEMBERENSURE, TVP_KAGPARSER_EX_CLASSNAME, NULL, &val, global);
	global->Release();
}
