// kk4s: extrans.dll as an engine internal plugin. Plugins.link("extrans.dll")
// registers the transitions krkrz's extrans plugin provides (Main.cpp there):
// wave, mosaic, turn, rotatezoom, rotatevanish, rotateswap, ripple. Before,
// kk4s ignored the DLL and every one of them fell back to a cross-fade.
#include "ncbind/ncbind.hpp"
#include "extrans_kk4s.h"

#include "wave.h"
#include "mosaic.h"
#include "turn.h"
#include "rotatetrans.h"
#include "ripple.h"

#define NCB_MODULE_NAME TJS_W("extrans.dll")

static bool ExtransRegistered = false;

static void ExtransPreRegist()
{
	if(ExtransRegistered) return;
	ExtransRegistered = true;
	RegisterWaveTransHandlerProvider();
	RegisterMosaicTransHandlerProvider();
	RegisterTurnTransHandlerProvider();
	RegisterRotateTransHandlerProvider();
	RegisterRippleTransHandlerProvider();
}

static void ExtransPostUnregist()
{
	if(!ExtransRegistered) return;
	ExtransRegistered = false;
	UnregisterWaveTransHandlerProvider();
	UnregisterMosaicTransHandlerProvider();
	UnregisterTurnTransHandlerProvider();
	UnregisterRotateTransHandlerProvider();
	UnregisterRippleTransHandlerProvider();
}

NCB_PRE_REGIST_CALLBACK(ExtransPreRegist);
NCB_POST_UNREGIST_CALLBACK(ExtransPostUnregist);
