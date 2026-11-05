// kk4s: what the psdfile sources take from tp_stub / Windows headers, for
// building them inside the krkrs engine (engine/source/plugins/psdfile).
#pragma once

#include "tjsCommHead.h"
#include "EventIntf.h"              // tTVPCompactEventCallbackIntf
#include "visual/win32/BitmapInfomation.h" // TVPBITMAPINFOHEADER

#ifndef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("psdfile.dll")
#endif

#ifndef S_OK
#define S_OK 0
#endif

#if !defined(_WIN32)
typedef TVPBITMAPINFOHEADER BITMAPINFOHEADER;
#endif

static inline const tjs_char *kk4s_psd_strrchr(const tjs_char *s, tjs_char c)
{
	const tjs_char *last = nullptr;
	for (; *s; ++s) if (*s == c) last = s;
	return c == 0 ? s : last;
}
#define TJS_strrchr kk4s_psd_strrchr

// krkrs layers keep RGBA bytes; psdparse's BGRA_LE is Windows' order. Direct
// writes into Layer.mainImageBufferForWrite use this one (the psd:// storage
// still produces BMP files, which are BGRA by definition).
#include "psdparse/psdbase.h"
namespace psd { static const ColorFormat KK4S_LAYER_FORMAT(0, 8, 16, 24); }
