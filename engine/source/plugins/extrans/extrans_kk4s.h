// kk4s: the extrans transitions (wave, mosaic, turn, rotatezoom, rotatevanish,
// rotateswap, ripple) from krkrz's extrans plugin (W.Dee and contributors,
// Kirikiri license), built into the engine. Kirikiri 2's plugin interface
// handed out scan lines (GetScanLine / GetPitchBytes); krkrs' scan line
// providers hold textures instead, which are plain CPU memory with the
// "software" render manager kk4s always uses. These helpers give the original
// code its scan lines back, so the transition code stays as upstream wrote it.
#ifndef EXTRANS_KK4S_H
#define EXTRANS_KK4S_H

#include "tjsCommHead.h"
#include "TransIntf.h"
#include "RenderManager.h"
#include "MsgIntf.h"
#include "tvpgl.h"

static inline tjs_error kk4sGetScanLine(iTVPScanLineProvider *p, tjs_int line, const void **out)
{
	iTVPTexture2D *t = p ? p->GetTexture() : nullptr;
	if(!t || !out || t->GetFormat() != TVPTextureFormat::RGBA || line < 0 || line >= (tjs_int)t->GetHeight())
		return TJS_E_FAIL;
	*out = t->GetScanLineForRead(line);
	return *out ? TJS_S_OK : TJS_E_FAIL;
}

// The destination: GetTextureForRender makes the bitmap writable (copy on write).
static inline tjs_error kk4sGetScanLineForWrite(iTVPScanLineProvider *p, tjs_int line, void **out)
{
	iTVPTexture2D *t = p ? p->GetTextureForRender() : nullptr;
	if(!t || !out || t->GetFormat() != TVPTextureFormat::RGBA || line < 0 || line >= (tjs_int)t->GetHeight())
		return TJS_E_FAIL;
	*out = t->GetScanLineForWrite(line);
	return *out ? TJS_S_OK : TJS_E_FAIL;
}

static inline tjs_error kk4sGetPitchBytes(iTVPScanLineProvider *p, tjs_int *pitch)
{
	iTVPTexture2D *t = p ? p->GetTexture() : nullptr;
	if(!t || !pitch) return TJS_E_FAIL;
	*pitch = t->GetPitch();
	return TJS_S_OK;
}

static inline tjs_error kk4sGetPitchBytesForWrite(iTVPScanLineProvider *p, tjs_int *pitch)
{
	iTVPTexture2D *t = p ? p->GetTextureForRender() : nullptr;
	if(!t || !pitch) return TJS_E_FAIL;
	*pitch = t->GetPitch();
	return TJS_S_OK;
}

// Option colours are 0xAARRGGBB as on Windows; krkrs keeps pixels as RGBA bytes
// (0xAABBGGRR in a 32-bit word), so red and blue swap places.
static inline tjs_uint32 kk4sColorFromOption(tjs_uint32 c)
{
	return (c & 0xff00ff00) | ((c >> 16) & 0xff) | ((c & 0xff) << 16);
}

#endif
