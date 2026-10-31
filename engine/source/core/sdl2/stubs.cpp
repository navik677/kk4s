
#include "tjsCommHead.h"
#include "StorageIntf.h"
#include "EventIntf.h"
#include "SysInitImpl.h"
#include "WindowImpl.h"
#include "LayerBitmapIntf.h"
#include "LayerBitmapImpl.h"
#include "TickCount.h"
#include "Random.h"
#include "UtilStreams.h"
#include "vkdefine.h"
#include "Platform.h"
#include "Application.h"
#include "ScriptMgnIntf.h"
#include "win32/TVPWindow.h"
#include "VelocityTracker.h"
#include "SystemImpl.h"
#include "RenderManager.h"
#include "VideoOvlIntf.h"
#include "Exception.h"
#include "win32/MenuItemImpl.h"
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <SDL.h>

#include <unistd.h>
#include <stdio.h>
#include <limits.h>



#include <wchar.h>

#include <string> 
#include <locale> 
#ifdef __SWITCH__
#include <switch.h>
#endif
#include "kk4s_platform.h"
#include "kk4s_compat.h"
#include "kk4s_settings.h"
#include "kk4s_overlay.h"
#include "kk4s_i18n.h"
#include "SysInitIntf.h" // kk4s: TVPSystemUninit
#include "kk4s_internal.h" // kk4s: MakeDir
#include "ThreadIntf.h" // kk4s: TVPStopAllThreads


class TVPWindowLayer;

// static TVPWindowManagerOverlay *_windowMgrOverlay = nullptr;
// static TVPConsoleWindow* _consoleWin = nullptr;
static float _touchMoveThresholdSq;
static float _mouseCursorScale;
// static Vec2 _mouseTouchPoint, _mouseBeginPoint;
// static std::set<Touch*> _mouseTouches;
static tTVPMouseButton _mouseBtn;
static int _touchBeginTick;
static bool _virutalMouseMode = false;
static bool _mouseMoved, _mouseClickedDown;
static tjs_uint8 _scancode[0x200];
static tjs_uint16 _keymap[0x200];
static TVPWindowLayer *_lastWindowLayer, *_currentWindowLayer;

bool sdlProcessEvents();
bool sdlProcessEventsForFrames(int frames);

// kk4s: input. KAG scripts expect Windows virtual-key codes (VK_*), SDL gives
// its own keycodes (they only match for Enter/Esc/Space). kk4sKeyState backs
// System.getKeyState(), which KAG polls for "skip while Ctrl is held".
static tjs_uint8 kk4sKeyState[256]; // bit 0: down now, bit 4: went down since last query
static unsigned kk4sPadHeld = 0;     // kk4s::kPad* bits held right now
static unsigned kk4sFramesShown = 0; // presents so far (main loop idles when nothing changed)
static void kk4sSetKeyState(tjs_uint vk, bool down) {
	if (vk >= 256) return;
	if (down) kk4sKeyState[vk] |= 0x11;
	else kk4sKeyState[vk] &= ~0x01;
}
static tjs_uint32 kk4sShiftState() {
	tjs_uint32 s = 0;
	if (kk4sKeyState[VK_SHIFT] & 1) s |= TVP_SS_SHIFT;
	if (kk4sKeyState[VK_CONTROL] & 1) s |= TVP_SS_CTRL;
	if (kk4sKeyState[VK_MENU] & 1) s |= TVP_SS_ALT;
	return s;
}
static tjs_uint kk4sSdlKeyToVK(SDL_Keycode k) {
	if (k >= SDLK_a && k <= SDLK_z) return 0x41 + (k - SDLK_a); // VK_A..VK_Z
	if (k >= SDLK_0 && k <= SDLK_9) return 0x30 + (k - SDLK_0); // VK_0..VK_9
	if (k >= SDLK_F1 && k <= SDLK_F12) return VK_F1 + (k - SDLK_F1);
	switch (k) {
		case SDLK_RETURN: case SDLK_KP_ENTER: return VK_RETURN;
		case SDLK_ESCAPE: return VK_ESCAPE;
		case SDLK_SPACE: return VK_SPACE;
		case SDLK_TAB: return VK_TAB;
		case SDLK_BACKSPACE: return VK_BACK;
		case SDLK_LEFT: return VK_LEFT;
		case SDLK_UP: return VK_UP;
		case SDLK_RIGHT: return VK_RIGHT;
		case SDLK_DOWN: return VK_DOWN;
		case SDLK_PAGEUP: return VK_PRIOR;
		case SDLK_PAGEDOWN: return VK_NEXT;
		case SDLK_HOME: return VK_HOME;
		case SDLK_END: return VK_END;
		case SDLK_INSERT: return VK_INSERT;
		case SDLK_DELETE: return VK_DELETE;
		case SDLK_LSHIFT: case SDLK_RSHIFT: return VK_SHIFT;
		case SDLK_LCTRL: case SDLK_RCTRL: return VK_CONTROL;
		case SDLK_LALT: case SDLK_RALT: return VK_MENU;
	}
	return 0;
}

#include <iostream>

SDL_Window *window;
SDL_Renderer* renderer;

class TVPWindowLayer : public iWindowLayer {
protected:
	tTVPMouseCursorState MouseCursorState = mcsVisible;
	tjs_int HintDelay = 500;
	tjs_int ZoomDenom = 1; // Zooming factor denominator (setting)
	tjs_int ZoomNumer = 1; // Zooming factor numerator (setting)
	double TouchScaleThreshold = 5, TouchRotateThreshold = 5;
	

	
	tjs_int ActualZoomDenom; // Zooming factor denominator (actual)
	tjs_int ActualZoomNumer; // Zooming factor numerator (actual)
	// Sprite *DrawSprite = nullptr;
	// Node *PrimaryLayerArea = nullptr;
	int LayerWidth = 0, LayerHeight = 0;
	//iTVPTexture2D *DrawTexture = nullptr;
	TVPWindowLayer *_prevWindow, *_nextWindow;
	friend class TVPWindowManagerOverlay;
	friend class TVPMainScene;
	int _LastMouseX = 0, _LastMouseY = 0;
	std::string _caption;
//	std::map<tTJSNI_BaseVideoOverlay*, Sprite*> _AllOverlay;
	float _drawSpriteScaleX = 1.0f, _drawSpriteScaleY = 1.0f;
	float _drawTextureScaleX = 1.f, _drawTextureScaleY = 1.f;
	bool UseMouseKey = false, MouseLeftButtonEmulatedPushed = false, MouseRightButtonEmulatedPushed = false;
	bool LastMouseMoved = false, Visible = false;
	tjs_uint32 LastMouseKeyTick = 0;
	tjs_int MouseKeyXAccel = 0;
	tjs_int MouseKeyYAccel = 0;
	int LastMouseDownX = 0, LastMouseDownY = 0;
	VelocityTrackers TouchVelocityTracker;
	VelocityTracker MouseVelocityTracker;
	static const int TVP_MOUSE_MAX_ACCEL = 30;
	static const int TVP_MOUSE_SHIFT_ACCEL = 40;
	static const int TVP_TOOLTIP_SHOW_DELAY = 500;
	SDL_Texture* framebuffer;
	// SDL_Texture* framebuffer;
	
	tTJSNI_Window *TJSNativeInstance;
	bool hasDrawn = false;
	bool isBeingDeleted = false;
	int curWindowW = 640, curWindowH = 480;

public:
	TVPWindowLayer(tTJSNI_Window *w)
	{
		_nextWindow = nullptr;
		_prevWindow = _lastWindowLayer;
		_lastWindowLayer = this;
		// ActualZoomDenom = 1;
		// ActualZoomNumer = 1;
		if (_prevWindow) {
			_prevWindow->_nextWindow = this;
		}
		if (!_currentWindowLayer) {
			_currentWindowLayer = this;
		}
		if (w) {
			TJSNativeInstance = w;
		}
		else {
			TJSNativeInstance = nullptr;
		}
		
		
		
		framebuffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, curWindowW, curWindowH);
		SDL_SetRenderDrawColor( renderer, 0xFF, 0xFF, 0xFF, 0xFF );
		// SDL_ShowWindow(window);
	}

	virtual ~TVPWindowLayer() {
		if (_lastWindowLayer == this) _lastWindowLayer = _prevWindow;
		if (_nextWindow) _nextWindow->_prevWindow = _prevWindow;
		if (_prevWindow) {_prevWindow->_nextWindow = _nextWindow;}

		if (_currentWindowLayer == this) {
			// TVPWindowLayer *anotherWin = _lastWindowLayer;
			// while (anotherWin && !anotherWin->isVisible()) {
			// 	anotherWin = anotherWin->_prevWindow;
			// }
			// if (anotherWin && anotherWin->isVisible()) {
			// 	anotherWin->setPosition(0, 0);
			// 	//anotherWin->setVisible(true);
			// }
			_currentWindowLayer = _lastWindowLayer;
			// SDL_SetWindowSize(window, _currentWindowLayer->curWindowW, _currentWindowLayer->curWindowH);
		}
		SDL_DestroyTexture(framebuffer);
		framebuffer = NULL;


	}

	bool init() {
		// bool ret = inherit::init();
		// setClippingToBounds(false);
		// DrawSprite = Sprite::create();
		// DrawSprite->setAnchorPoint(Vec2(0, 1)); // top-left
		// PrimaryLayerArea = Node::create();
		// addChild(PrimaryLayerArea);
		// PrimaryLayerArea->addChild(DrawSprite);
		// setAnchorPoint(Size::ZERO);
		// EventListenerMouse *evmouse = EventListenerMouse::create();
		// evmouse->onMouseScroll = std::bind(&TVPWindowLayer::onMouseScroll, this, std::placeholders::_1);
		// evmouse->onMouseDown = std::bind(&TVPWindowLayer::onMouseDownEvent, this, std::placeholders::_1);
		// evmouse->onMouseUp = std::bind(&TVPWindowLayer::onMouseUpEvent, this, std::placeholders::_1);
		// evmouse->onMouseMove = std::bind(&TVPWindowLayer::onMouseMoveEvent, this, std::placeholders::_1);
		// _eventDispatcher->addEventListenerWithSceneGraphPriority(evmouse, this);
		// setTouchEnabled(false);
		// //_touchListener->setSwallowTouches(true);
		// setVisible(false);
		

		return true;
	}

	static TVPWindowLayer *create(tTJSNI_Window *w) {
		TVPWindowLayer *ret = new TVPWindowLayer (w);
		ret->init();
		// ret->autorelease();
		return ret;
	}

	virtual void SetPaintBoxSize(tjs_int w, tjs_int h) override {
		// LayerWidth = w; LayerHeight = h;
		// RecalcPaintBox();
	}
	virtual bool GetFormEnabled() override {
		return SDL_GetWindowFlags(window) & SDL_WINDOW_SHOWN;

	}
	virtual void SetDefaultMouseCursor() override {

	}
	virtual void GetCursorPos(tjs_int &x, tjs_int &y) override {
		SDL_GetMouseState(&x, &y);
		// x = _LastMouseX;
		// y = _LastMouseY;
	}
	virtual void SetCursorPos(tjs_int x, tjs_int y) override {
		SDL_WarpMouseInWindow(window, x, y);
		// _LastMouseX = x;
		// _LastMouseY = y;
	}
	virtual void SetHintText(const ttstr &text) override {

	}
	// tjs_int _textInputPosY;
	virtual void SetAttentionPoint(tjs_int left, tjs_int top, const struct tTVPFont * font) override {
		// _textInputPosY = top;
	}
	virtual void ZoomRectangle(
		tjs_int & left, tjs_int & top,
		tjs_int & right, tjs_int & bottom) override {

	}
	virtual void BringToFront() override {
		// left = tjs_int64(left) * ActualZoomNumer / ActualZoomDenom;
		// top = tjs_int64(top) * ActualZoomNumer / ActualZoomDenom;
		// right = tjs_int64(right) * ActualZoomNumer / ActualZoomDenom;
		// bottom = tjs_int64(bottom) * ActualZoomNumer / ActualZoomDenom;
		if (_currentWindowLayer != this) {
			if (_currentWindowLayer && _currentWindowLayer->TJSNativeInstance) { // kk4s: null once closed
				// const Size &size = _currentWindowLayer->getViewSize();
				// _currentWindowLayer->setPosition(Vec2(size.width, 0));
				_currentWindowLayer->TJSNativeInstance->OnReleaseCapture();
			}
			_currentWindowLayer = this;
		}
		SDL_RaiseWindow(window);
	}
	virtual void ShowWindowAsModal() override {
		in_mode_ = true;
		BringToFront();
		modal_result_ = 0;
		while (this == _currentWindowLayer && !modal_result_) {
			sdlProcessEvents();
			if (::Application->IsTarminate()) {
				modal_result_ = mrCancel;
			} else if (modal_result_ != 0) {
				break;
			}
		}
		in_mode_ = false;
	}
	virtual bool GetVisible() override {
		return SDL_GetWindowFlags(window) & SDL_WINDOW_SHOWN;
	}
	virtual void SetVisible(bool bVisible) override {
		if (bVisible) {
			SDL_ShowWindow(window);
			BringToFront();
		}
		else {
			SDL_HideWindow(window);
			if (_currentWindowLayer == this) {
				_currentWindowLayer = _prevWindow ? _prevWindow : _nextWindow;
			}
		}

	}
	virtual const char *GetCaption() override {
		return SDL_GetWindowTitle(window);

	}
	virtual void SetCaption(const tjs_string & ws) override {
		SDL_SetWindowTitle(window, ttstr(ws).AsNarrowStdString().c_str());
	}
	virtual void SetWidth(tjs_int w) override {
		//int h;
		//SDL_GetWindowSize(window, NULL, &h);
		curWindowW = w;
		//SDL_SetWindowSize(window, curWindowW, curWindowH);
		if (framebuffer) {
			SDL_DestroyTexture(framebuffer);
			framebuffer = NULL;
		}
		framebuffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, curWindowW, curWindowH);
	}
	virtual void SetHeight(tjs_int h) override {
		//int w;
		//SDL_GetWindowSize(window, &w, NULL);
		curWindowH = h;
		//SDL_SetWindowSize(window, curWindowW, curWindowH);
		if (framebuffer) {
			SDL_DestroyTexture(framebuffer);
			framebuffer = NULL;
		}
		framebuffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, curWindowW, curWindowH);
	}
	virtual void SetSize(tjs_int w, tjs_int h) override {
		curWindowW = w;
		curWindowH = h;
		//SDL_SetWindowSize(window, w, h);
		if (framebuffer) {
			SDL_DestroyTexture(framebuffer);
			framebuffer = NULL;
		}
		framebuffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, curWindowW, curWindowH);
	}
	virtual void GetSize(tjs_int &w, tjs_int &h) override {
		w = curWindowW;
		h = curWindowH;
		//SDL_GetWindowSize(window, &w, &h);
	}
	virtual tjs_int GetWidth() const override {
		//int w;
		//SDL_GetWindowSize(window, &w, NULL);
		return curWindowW;
	}
	virtual tjs_int GetHeight() const override {
		//int h;
		//SDL_GetWindowSize(window, NULL, &h);
		return curWindowH;
	}
	virtual void GetWinSize(tjs_int &w, tjs_int &h) override {
		w = curWindowW;
		h = curWindowH;
		//SDL_GetWindowSize(window, &w, &h);
	}
	virtual void SetZoom(tjs_int numer, tjs_int denom) override {
	}
	virtual void UpdateDrawBuffer(iTVPTexture2D *tex) override {
		if (!tex) return;
		//TODO: Update and swap SDL
		// SDL_LockTexture(framebuffer, NULL, NULL, NULL);
		Uint32 kk4sStart = SDL_GetTicks(); // kk4s: frame stats
		SDL_UpdateTexture(framebuffer, NULL, tex->GetPixelData(), tex->GetPitch());
		kk4s::CurrentFrameStats().upload += SDL_GetTicks() - kk4sStart;
		// SDL_UnlockTexture(framebuffer);
		if (kk4s::overlay::Visible()) { // kk4s: the menu loop presents this frame with the menu on top
			hasDrawn = true;
			return;
		}
		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, framebuffer, NULL, NULL);
		kk4sStart = SDL_GetTicks();
		SDL_RenderPresent(renderer);
		kk4s::CurrentFrameStats().present += SDL_GetTicks() - kk4sStart;
		++kk4sFramesShown;
		hasDrawn = true;
	}
#if 0
	virtual void AddOverlay(tTJSNI_BaseVideoOverlay *ovl) = 0;
	virtual void RemoveOverlay(tTJSNI_BaseVideoOverlay *ovl) = 0;
	virtual void UpdateOverlay() = 0;
#endif
	virtual void InvalidateClose() override {
		isBeingDeleted = true;
	}
	virtual bool GetWindowActive() override {
		return _currentWindowLayer == this;
	}
	bool Closing = false, ProgramClosing = false, CanCloseWork = false;
	bool in_mode_ = false; // is modal
	int modal_result_ = 0;
	enum CloseAction {
		caNone,
		caHide,
		caFree,
		caMinimize
	};
	void OnClose(CloseAction& action) {
		if (modal_result_ == 0)
			action = caNone;
		else
			action = caHide;

		if (ProgramClosing) {
			if (TJSNativeInstance) {
				if (TJSNativeInstance->IsMainWindow()) {
					// this is the main window
				} else 			{
					// not the main window
					action = caFree;
				}
				//if (TVPFullScreenedWindow != this) {
					// if this is not a fullscreened window
				//	SetVisible(false);
				//}
				iTJSDispatch2 * obj = TJSNativeInstance->GetOwnerNoAddRef();
				TJSNativeInstance->NotifyWindowClose();
				obj->Invalidate(0, NULL, NULL, obj);
				TJSNativeInstance = NULL;
				SetVisible(false);
				isBeingDeleted = true;
			}
		}
	}
	bool OnCloseQuery() {
		// closing actions are 3 patterns;
		// 1. closing action by the user
		// 2. "close" method
		// 3. object invalidation

		if (TVPGetBreathing()) {
			return false;
		}

		// the default event handler will invalidate this object when an onCloseQuery
		// event reaches the handler.
		if (TJSNativeInstance && (modal_result_ == 0 ||
			modal_result_ == mrCancel/* mrCancel=when close button is pushed in modal window */)) {
			iTJSDispatch2 * obj = TJSNativeInstance->GetOwnerNoAddRef();
			if (obj) {
				tTJSVariant arg[1] = { true };
				static ttstr eventname(TJS_W("onCloseQuery"));

				if (!ProgramClosing) {
					// close action does not happen immediately
					if (TJSNativeInstance) {
						TVPPostInputEvent(new tTVPOnCloseInputEvent(TJSNativeInstance));
					}

					Closing = true; // waiting closing...
				//	TVPSystemControl->NotifyCloseClicked();
					return false;
				} else {
					CanCloseWork = true;
					TVPPostEvent(obj, obj, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
					sdlProcessEvents(); // for post event
					// this event happens immediately
					// and does not return until done
					return CanCloseWork; // CanCloseWork is set by the event handler
				}
			} else {
				return true;
			}
		} else {
			return true;
		}
	}
	virtual void Close() override {
		// closing action by "close" method
		if (Closing) return; // already waiting closing...

		ProgramClosing = true;
		try {
			//tTVPWindow::Close();
			if (in_mode_) {
				modal_result_ = mrCancel;
			} 
			else if (OnCloseQuery()) {
				CloseAction action = caFree;
				OnClose(action);
				switch (action) {
				case caNone:
					break;
				case caHide:
					SetVisible(false);
					break;
				case caMinimize:
					//::ShowWindow(GetHandle(), SW_MINIMIZE);
					break;
				case caFree:
				default:
					isBeingDeleted = true;
					//::PostMessage(GetHandle(), TVP_EV_WINDOW_RELEASE, 0, 0);
					break;
				}
			}
		}
		catch (...) {
			ProgramClosing = false;
			throw;
		}
		ProgramClosing = false;
	}
	virtual void OnCloseQueryCalled(bool b) override {
		// closing is allowed by onCloseQuery event handler
		if (!ProgramClosing) {
			// closing action by the user
			if (b) {
				if (in_mode_)
					modal_result_ = 1; // when modal
				else
					SetVisible(false);  // just hide

				Closing = false;
				if (TJSNativeInstance) {
					if (TJSNativeInstance->IsMainWindow()) {
						// this is the main window
						iTJSDispatch2 * obj = TJSNativeInstance->GetOwnerNoAddRef();
						obj->Invalidate(0, NULL, NULL, obj);
						// TJSNativeInstance = NULL; // ¤³¤Î¶ÎëA¤Ç¤Ï¼È¤Ëthis¤¬Ï÷³ý¤µ¤ì¤Æ¤¤¤ë¤¿¤á¡¢\á\ó\Ð©`¤Ø\¢\¯\»\¹¤·¤Æ¤Ï¤¤¤±¤Ê¤¤
					}
				} else {
					delete this;
				}
			} else {
				Closing = false;
			}
		} else {
			// closing action by the program
			CanCloseWork = b;
		}
	}
	virtual void InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) override {
	}
	virtual void OnKeyUp(tjs_uint16 vk, int shift) override {
	}
	virtual void OnKeyPress(tjs_uint16 vk, int repeat, bool prevkeystate, bool convertkey) override {
	}
	tTVPImeMode LastSetImeMode = ::imDisable;
	tTVPImeMode DefaultImeMode = ::imDisable;
	virtual tTVPImeMode GetDefaultImeMode() const override {
		return DefaultImeMode;
	}
	virtual void SetImeMode(tTVPImeMode mode) override {
		switch (mode) {
		case ::imDisable:
		case ::imClose:
// //#ifdef _MSC_VER
// 			TVPMainScene::GetInstance()->detachWithIME();
// #endif
			break;
		case ::imOpen:
			//TVPMainScene::GetInstance()->attachWithIME();
			//break;
		default:
// #ifdef _MSC_VER
// 			TVPMainScene::GetInstance()->attachWithIME();
// #endif
			break;
		}
	}
	virtual void ResetImeMode() override {
	}
	virtual void UpdateWindow(tTVPUpdateType type) override {
		if (TJSNativeInstance) {
			tTVPRect r;
			r.left = 0;
			r.top = 0;
			r.right = curWindowW;
			r.bottom = curWindowH;
			//SDL_GetWindowSize(window, &, &r.bottom);
			TJSNativeInstance->NotifyWindowExposureToLayer(r);
			TVPDeliverWindowUpdateEvents();
		}
	}
	virtual void SetVisibleFromScript(bool b) override {
		SetVisible(b);
	}
	virtual void SetUseMouseKey(bool b) override {
	}
	virtual bool GetUseMouseKey() const override {
		return false;
	}
	virtual void ResetMouseVelocity() override {
	}
	virtual void ResetTouchVelocity(tjs_int id) override {
	}
	virtual bool GetMouseVelocity(float& x, float& y, float& speed) const override {
		return false;
	}
	virtual void TickBeat() override {
	}
	Sint32 mouseLastX = 0;
	Sint32 mouseLastY = 0;

	// kk4s: key / mouse button events from keyboard and gamepad
	void kk4sPostKey(tjs_uint vk, bool down) {
		if (vk == 0 || vk >= 256) return;
		bool was = kk4sKeyState[vk] & 1;
		kk4sSetKeyState(vk, down);
		if (down)
			TVPPostInputEvent(new tTVPOnKeyDownInputEvent(TJSNativeInstance, vk, kk4sShiftState() | (was ? TVP_SS_REPEAT : 0)));
		else if (was)
			TVPPostInputEvent(new tTVPOnKeyUpInputEvent(TJSNativeInstance, vk, kk4sShiftState()));
	}
	void kk4sPostMouseButton(tTVPMouseButton btn, bool down) {
		if (down) {
			TVPPostInputEvent(new tTVPOnMouseDownInputEvent(TJSNativeInstance, mouseLastX, mouseLastY, btn, 0));
		} else {
			TVPPostInputEvent(new tTVPOnClickInputEvent(TJSNativeInstance, mouseLastX, mouseLastY));
			TVPPostInputEvent(new tTVPOnMouseUpInputEvent(TJSNativeInstance, mouseLastX, mouseLastY, btn, 0));
		}
	}
	// Gamepad: D-pad / left stick = arrows; the other buttons do what the
	// launcher's controls settings say (kk4s::Settings::pad). Defaults:
	//   A = Enter (next / OK)         B = right click (menu / cancel / hide text)
	//   X, ZR = Ctrl held (skip)      Y = Space
	//   L = Page Up (backlog)         R = Page Down
	//   ZL = Shift                    + = Esc, - = Tab
	// L3 + R3 together open the kk4s menu (kk4sPadChange).
	void kk4sPadPress(unsigned button, bool down) {
		static int logged = 0; // diagnostics for the first presses
		if (down && logged < 40) { ++logged; kk4s::Log("input: pad 0x%x -> game", button); }
		switch (button) {
			case kk4s::kPadLeft: kk4sPostKey(VK_LEFT, down); return;
			case kk4s::kPadUp: kk4sPostKey(VK_UP, down); return;
			case kk4s::kPadRight: kk4sPostKey(VK_RIGHT, down); return;
			case kk4s::kPadDown: kk4sPostKey(VK_DOWN, down); return;
		}
		switch (kk4s::LiveSettings().PadFor(button)) {
			case kk4s::PadAction::Overlay: if (down) kk4sOpenOverlay(); break;
			case kk4s::PadAction::Enter: kk4sPostKey(VK_RETURN, down); break;
			case kk4s::PadAction::RightClick: kk4sPostMouseButton(tTVPMouseButton::mbRight, down); break;
			case kk4s::PadAction::Ctrl: kk4sPostKey(VK_CONTROL, down); break;
			case kk4s::PadAction::Space: kk4sPostKey(VK_SPACE, down); break;
			case kk4s::PadAction::PageUp: kk4sPostKey(VK_PRIOR, down); break;
			case kk4s::PadAction::PageDown: kk4sPostKey(VK_NEXT, down); break;
			case kk4s::PadAction::Shift: kk4sPostKey(VK_SHIFT, down); break;
			case kk4s::PadAction::Esc: kk4sPostKey(VK_ESCAPE, down); break;
			case kk4s::PadAction::Tab: kk4sPostKey(VK_TAB, down); break;
			default: break;
		}
	}
	// One button changed (Switch: libnx, PC: SDL controller events).
	void kk4sPadChange(unsigned bit, bool down) {
		unsigned before = kk4sPadHeld;
		kk4sPadHeld = down ? (kk4sPadHeld | bit) : (kk4sPadHeld & ~bit);
		if (bit & kk4s::kPadMenuCombo) {
			// both sticks pressed: open / close the menu; single stick clicks do nothing
			bool now_both = (kk4sPadHeld & kk4s::kPadMenuCombo) == kk4s::kPadMenuCombo;
			bool was_both = (before & kk4s::kPadMenuCombo) == kk4s::kPadMenuCombo;
			if (now_both && !was_both) {
				if (kk4s::overlay::IsOpen()) kk4s::overlay::Close();
				else kk4sOpenOverlay();
			}
			return;
		}
		if (kk4s::overlay::IsOpen()) kk4s::overlay::Pad(bit, down); // kk4s: menu takes the pad
		else kk4sPadPress(bit, down);
	}
	// kk4s in-game menu: let go of keys the game thinks are held, then open it.
	void kk4sOpenOverlay() {
		for (tjs_uint vk = 1; vk < 256; ++vk)
			if (kk4sKeyState[vk] & 1) kk4sPostKey(vk, false);
		std::string title = kk4s::GameDir();
		while (!title.empty() && title.back() == '/') title.pop_back();
		title = title.substr(title.find_last_of('/') + 1);
		kk4s::overlay::Open(title, kk4sQuickSaveExists());
	}
	static bool kk4sQuickSaveExists() {
		try {
			tTJSVariant v;
			TVPExecuteExpression(TJS_W("typeof global.kag != 'undefined' && kag !== void && typeof kag.getBookMarkFileNameAtNum != 'undefined' && Storages.isExistentStorage(kag.getBookMarkFileNameAtNum(900))"), &v);
			return v.operator bool();
		} catch (...) {
			return false;
		}
	}
	// Draw the last game frame (and the menu) and present.
	void kk4sPresent(bool with_overlay, const char* shot_bmp = nullptr) {
		if (!framebuffer) return;
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, framebuffer, NULL, NULL);
		if (with_overlay) kk4s::overlay::Draw(renderer);
		if (shot_bmp) { // the back buffer holds the full frame now
			int ow = 0, oh = 0;
			SDL_GetRendererOutputSize(renderer, &ow, &oh);
			SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
			if (surf && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0)
				SDL_SaveBMP(surf, shot_bmp);
			if (surf) SDL_FreeSurface(surf);
		}
		SDL_RenderPresent(renderer);
		++kk4sFramesShown;
	}

	// Switch: poll libnx once per frame and turn button changes into presses.
	void kk4sPadFrame() {
		static unsigned prev = 0;
		unsigned held;
		if (!kk4s::ReadPad(held)) return;
		// A closed window (e.g. the save dialog) has no TJS instance until it is deleted.
		if (isBeingDeleted || !window || !hasDrawn || !TJSNativeInstance || !TJSNativeInstance->CanDeliverEvents()) {
			prev = kk4sPadHeld = held;
			return;
		}
		unsigned changed = held ^ prev;
		prev = held;
		for (unsigned bit = 1; changed && bit <= kk4s::kPadLast; bit <<= 1)
			if (changed & bit) kk4sPadChange(bit, (held & bit) != 0);
	}
	// PC pads: analog input -> keys, with hysteresis so a resting stick does not chatter.
	void kk4sAxisKey(Sint16 value, tjs_uint negKey, tjs_uint posKey) {
		const int press = 20000, release = 12000;
		bool negDown = kk4sKeyState[negKey] & 1, posDown = posKey ? (kk4sKeyState[posKey] & 1) : false;
		if (negKey) {
			if (!negDown && value <= -press) kk4sPostKey(negKey, true);
			else if (negDown && value > -release) kk4sPostKey(negKey, false);
		}
		if (posKey) {
			if (!posDown && value >= press) kk4sPostKey(posKey, true);
			else if (posDown && value < release) kk4sPostKey(posKey, false);
		}
	}
	void kk4sPadAxis(int axis, Sint16 value) {
		switch (axis) {
			case SDL_CONTROLLER_AXIS_LEFTX: kk4sAxisKey(value, VK_LEFT, VK_RIGHT); break;
			case SDL_CONTROLLER_AXIS_LEFTY: kk4sAxisKey(value, VK_UP, VK_DOWN); break;
			case SDL_CONTROLLER_AXIS_TRIGGERLEFT: kk4sAxisKey(value, 0, VK_SHIFT); break;
			case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: kk4sAxisKey(value, 0, VK_CONTROL); break;
		}
	}

	void sdlRecvEvent(SDL_Event event) {
		if (isBeingDeleted) {
			delete this;
			return;
		}
		if (window && _prevWindow) {
			uint32_t windowID = SDL_GetWindowID(window);
			bool tryParentWindow = false;
			switch (event.type) {
				case SDL_DROPFILE:
				case SDL_DROPTEXT:
				case SDL_DROPBEGIN:
				case SDL_DROPCOMPLETE:
					tryParentWindow = event.drop.windowID != windowID;
					break;
				case SDL_KEYDOWN:
				case SDL_KEYUP:
					tryParentWindow = event.key.windowID != windowID;
					break;
				case SDL_MOUSEMOTION:
					tryParentWindow = event.motion.windowID != windowID;
					break;
				case SDL_MOUSEBUTTONDOWN:
				case SDL_MOUSEBUTTONUP:
					tryParentWindow = event.button.windowID != windowID;
					break;
				case SDL_MOUSEWHEEL:
					tryParentWindow = event.wheel.windowID != windowID;
					break;
				case SDL_TEXTEDITING:
					tryParentWindow = event.edit.windowID != windowID;
					break;
				case SDL_TEXTINPUT:
					tryParentWindow = event.text.windowID != windowID;
					break;
			}
			if (tryParentWindow) {
				if (!in_mode_) {
					_prevWindow->sdlRecvEvent(event);
				}
				return;
			}
		}
		if (window && hasDrawn) {
			if (TJSNativeInstance && TJSNativeInstance->CanDeliverEvents()) { // kk4s: null once the window is closed
				switch (event.type) { 
					// kk4s: touch = left mouse button; each case used to fall through
					// into the next, so one tap clicked twice
					case SDL_FINGERDOWN: {
						mouseLastX = curWindowW * event.tfinger.x;
						mouseLastY = curWindowH * event.tfinger.y;
						TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(TJSNativeInstance, mouseLastX, mouseLastY, 0));
						TVPPostInputEvent(new tTVPOnMouseDownInputEvent(TJSNativeInstance, mouseLastX, mouseLastY, tTVPMouseButton::mbLeft, 0));
						break;
					}
					case SDL_FINGERUP: {
						mouseLastX = curWindowW * event.tfinger.x;
						mouseLastY = curWindowH * event.tfinger.y;
						TVPPostInputEvent(new tTVPOnClickInputEvent(TJSNativeInstance, mouseLastX, mouseLastY));
						TVPPostInputEvent(new tTVPOnMouseUpInputEvent(TJSNativeInstance, mouseLastX, mouseLastY, tTVPMouseButton::mbLeft, 0));
						break;
					}
					case SDL_FINGERMOTION: {
						mouseLastX = curWindowW * event.tfinger.x;
						mouseLastY = curWindowH * event.tfinger.y;
						TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(TJSNativeInstance, mouseLastX, mouseLastY, 0));
						break;
					}
					case SDL_MOUSEMOTION: {
						mouseLastX = event.motion.x;
						mouseLastY = event.motion.y;
						TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(TJSNativeInstance, event.motion.x, event.motion.y, 0));
						break;
					}
					case SDL_MOUSEBUTTONDOWN: {
						tTVPMouseButton btn;
						bool hasbtn = false;
						switch(event.button.button) {
							case SDL_BUTTON_RIGHT:
								btn = tTVPMouseButton::mbRight;
								hasbtn = true;
								break;
							case SDL_BUTTON_MIDDLE:
								btn = tTVPMouseButton::mbMiddle;
								hasbtn = true;
								break;
							case SDL_BUTTON_LEFT:
								btn = tTVPMouseButton::mbLeft;
								hasbtn = true;
								break;
						}
						if (hasbtn) {
							mouseLastX = event.button.x;
							mouseLastY = event.button.y;
							TVPPostInputEvent(new tTVPOnMouseDownInputEvent(TJSNativeInstance, event.button.x, event.button.y, btn, 0));
						}
						break;
					}
					case SDL_MOUSEBUTTONUP: {
						tTVPMouseButton btn;
						bool hasbtn = false;
						switch(event.button.button) {
							case SDL_BUTTON_RIGHT:
								btn = tTVPMouseButton::mbRight;
								hasbtn = true;
								break;
							case SDL_BUTTON_MIDDLE:
								btn = tTVPMouseButton::mbMiddle;
								hasbtn = true;
								break;
							case SDL_BUTTON_LEFT:
								btn = tTVPMouseButton::mbLeft;
								hasbtn = true;
								break;
						}
						if (hasbtn) {
							mouseLastX = event.button.x;
							mouseLastY = event.button.y;
							TVPPostInputEvent(new tTVPOnClickInputEvent(TJSNativeInstance, event.button.x, event.button.y));
							TVPPostInputEvent(new tTVPOnMouseUpInputEvent(TJSNativeInstance, event.button.x, event.button.y, btn, 0));
						}
						break;
					}
					case SDL_KEYDOWN:
					case SDL_KEYUP: { // kk4s: SDL keycode -> VK
						kk4sPostKey(kk4sSdlKeyToVK(event.key.keysym.sym), event.type == SDL_KEYDOWN);
						break;
					}
#ifndef __SWITCH__
					// kk4s: PC gamepads (the Switch reads libnx directly, see kk4sPadFrame)
					case SDL_CONTROLLERBUTTONDOWN:
					case SDL_CONTROLLERBUTTONUP: {
						unsigned b = kk4s::PadFromSdlButton(event.cbutton.button);
						if (b) kk4sPadChange(b, event.type == SDL_CONTROLLERBUTTONDOWN);
						break;
					}
					case SDL_CONTROLLERAXISMOTION: {
						kk4sPadAxis(event.caxis.axis, event.caxis.value);
						break;
					}
#endif
					case SDL_MOUSEWHEEL: {
						TVPPostInputEvent(new tTVPOnMouseWheelInputEvent(TJSNativeInstance, 0, event.wheel.y, mouseLastX, mouseLastY));
						break;
					}
					case SDL_WINDOWEVENT_CLOSE:
					case SDL_QUIT: {
						TVPPostInputEvent(new tTVPOnCloseInputEvent(TJSNativeInstance));
					}
				}
			}
		}
	}

	// void SetZoomNumer(tjs_int n) { SetZoom(n, ZoomDenom); }
	// tjs_int GetZoomNumer() const { return ZoomNumer; }
	// void SetZoomDenom(tjs_int d) { SetZoom(ZoomNumer, d); }
	// tjs_int GetZoomDenom() const { return ZoomDenom; }

	// dummy function
	// TODO
};




//TODO: Frame update use tTVPApplication::Run, then iTVPTexture2D::RecycleProcess
//TODO: Rendering: On every frame copy from GetAdapterTexture
//TODO: Startup game at path ::Application->StartApplication(path);

// kk4s: open gamepads as they appear (SDL also reports the ones present at start)
static void kk4sOpenInputDevice(const SDL_Event& event) {
	if (event.type == SDL_CONTROLLERDEVICEADDED) {
		if (SDL_GameControllerOpen(event.cdevice.which))
			kk4s::Log("input: controller '%s'", SDL_GameControllerNameForIndex(event.cdevice.which));
	} else if (event.type == SDL_JOYDEVICEADDED && !SDL_IsGameController(event.jdevice.which)) {
		if (SDL_JoystickOpen(event.jdevice.which))
			kk4s::Log("input: joystick '%s' (no controller mapping)", SDL_JoystickNameForIndex(event.jdevice.which));
	}
}

// kk4s: carry out in-game menu requests, then draw the menu over the game.
static tTJSVariant kk4sEval(const tjs_char* expr, bool* ok) {
	tTJSVariant v;
	*ok = true;
	try {
		TVPExecuteExpression(ttstr(expr), &v);
	} catch (const eTJS& e) {
		*ok = false;
		kk4s::Log("overlay: script error: %s", e.GetMessage().AsNarrowStdString().c_str());
	} catch (...) {
		*ok = false;
	}
	return v;
}

static void kk4sOverlayFrame() {
	using kk4s::overlay::Request;
	for (Request req = kk4s::overlay::TakeRequest(); req != Request::None; req = kk4s::overlay::TakeRequest()) {
		bool ok = true;
		switch (req) {
			case Request::QuickSave: {
				// KAG bookmark 900: a slot the game's own save screens do not show
				tTJSVariant v = kk4sEval(TJS_W("typeof global.kag == 'undefined' || kag === void || typeof kag.saveBookMark == 'undefined' ? -2 : (kag.saveBookMark(900) ? 1 : 0)"), &ok);
				tjs_int64 res = ok && v.Type() != tvtVoid ? v.AsInteger() : -2;
				kk4s::overlay::Toast(res == 1 ? kk4s::i18n::T(kk4s::i18n::kSaved)
									 : res == -2 ? kk4s::i18n::T(kk4s::i18n::kNotSupported)
												 : kk4s::i18n::T(kk4s::i18n::kCannotSaveNow));
				if (res == 1) {
					kk4s::overlay::SetQuickSaveExists(true);
					// remember how many click / key hooks the game had (see QuickLoad)
					kk4sEval(TJS_W("(function() { if (typeof kag.leftClickHook == 'undefined' || typeof kag.rightClickHook == 'undefined' || typeof kag.keyDownHook == 'undefined') return;")
							 TJS_W(" [kag.leftClickHook.count, kag.rightClickHook.count, kag.keyDownHook.count].saveStruct(kag.getBookMarkFileNameAtNum(900) + '.kk4s'); } incontextof global)()"), &ok);
				}
				kk4s::Log("overlay: quick save -> %d", (int)res);
				break;
			}
			case Request::QuickLoad: {
				tTJSVariant v = kk4sEval(TJS_W("typeof global.kag == 'undefined' || kag === void || typeof kag.loadBookMark == 'undefined' ? -2 : (!Storages.isExistentStorage(kag.getBookMarkFileNameAtNum(900)) ? -1 : (kag.loadBookMark(900), 1))"), &ok);
				tjs_int64 res = ok && v.Type() != tvtVoid ? v.AsInteger() : -2;
				kk4s::Log("overlay: quick load -> %d", (int)res);
				if (res == 1) {
					// A bookmark does not hold the click / key hooks. Hooks the game added
					// for a moment (e.g. "click skips the logo, jump to *pre_title") stayed
					// on after loading and broke the first click. Drop hooks added after
					// the quick save was made; the game's own load menu is never shown at
					// such moments, ours is.
					tTJSVariant n = kk4sEval(TJS_W("(function() { var name = kag.getBookMarkFileNameAtNum(900) + '.kk4s';")
											 TJS_W(" if (typeof kag.leftClickHook == 'undefined' || !Storages.isExistentStorage(name)) return 0;")
											 TJS_W(" var c = Scripts.evalStorage(name), h = [kag.leftClickHook, kag.rightClickHook, kag.keyDownHook], r = 0;")
											 TJS_W(" for (var i = 0; i < 3; i++) if (h[i].count > +c[i]) { r += h[i].count - c[i]; h[i].count = +c[i]; }")
											 TJS_W(" return r; } incontextof global)()"), &ok);
					if (ok && n.Type() != tvtVoid && n.AsInteger() > 0)
						kk4s::Log("overlay: quick load dropped %d hook(s) added after the save", (int)n.AsInteger());
					kk4s::overlay::Close();
				}
				else kk4s::overlay::Toast(res == -1 ? kk4s::i18n::T(kk4s::i18n::kNoQuickSave) : kk4s::i18n::T(kk4s::i18n::kNotSupported));
				break;
			}
			case Request::Volume: {
				ttstr expr = TJS_W("WaveSoundBuffer.globalVolume = ") + ttstr((tjs_int)(kk4s::LiveSettings().volume * 1000));
				kk4sEval(expr.c_str(), &ok);
				break;
			}
			case Request::ToLauncher:
			case Request::Quit: {
				// let KAG store its system variables (read flags, settings) first
				kk4sEval(TJS_W("typeof global.kag != 'undefined' && kag !== void && typeof kag.saveSystemVariables != 'undefined' ? (kag.saveSystemVariables(), 1) : 0"), &ok);
				kk4s::SetExitMode(req == Request::ToLauncher ? kk4s::ExitMode::Launcher : kk4s::ExitMode::Quit);
				kk4s::Log("overlay: %s", req == Request::ToLauncher ? "exit to the launcher" : "quit");
				TVPExitApplication(0);
				break;
			}
			default: break;
		}
	}
	static bool was_visible = false;
	static int visible_frames = 0;
	bool visible = kk4s::overlay::Visible();
	visible_frames = visible ? visible_frames + 1 : 0;
	if (_currentWindowLayer && (visible || was_visible)) {
		_currentWindowLayer->kk4sPresent(visible);
		// debugging aid (PC): KK4S_OVERLAY_SHOT=<file.bmp> saves the menu once it is fully shown
		const char* shot = getenv("KK4S_OVERLAY_SHOT");
		if (shot && visible_frames == 90) {
			int ow = 0, oh = 0;
			SDL_GetRendererOutputSize(renderer, &ow, &oh);
			_currentWindowLayer->kk4sPresent(true); // the back buffer now holds a full frame
			SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
			if (surf && SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch) == 0)
				SDL_SaveBMP(surf, shot);
			if (surf) SDL_FreeSurface(surf);
		}
	}
	was_visible = visible;
}

// kk4s: debugging aid (PC): KK4S_INPUT_SCRIPT=<file> plays gamepad presses,
// touches and screenshots at given times through the same code paths as the
// Switch pad and touch screen (docs/debugging.md). One command per line:
//   <ms> pad A [hold_ms]     A B X Y L R ZL ZR PLUS MINUS LEFT UP RIGHT DOWN L3 R3
//   <ms> tap <x> <y>         x, y in 0..1 of the screen
//   <ms> shot <file.bmp>     relative names go to $KK4S_ROOT/shots/
//   <ms> quit
// <ms> counts from the game start; "+<ms>" is relative to the previous line.
struct kk4sScriptCmd { Uint32 at; std::string op, a1, a2; };
static std::vector<kk4sScriptCmd> kk4sScript;
static size_t kk4sScriptPos = 0;
static Uint32 kk4sScriptStart = 0;
static std::vector<std::pair<Uint32, unsigned>> kk4sScriptReleases; // (time, pad bit)
static std::vector<std::pair<Uint32, SDL_Event>> kk4sScriptEvents;

static void kk4sScriptLoad() {
	static bool loaded = false;
	if (loaded) return;
	loaded = true;
	const char* path = getenv("KK4S_INPUT_SCRIPT");
	if (!path) return;
	FILE* f = fopen(path, "r");
	if (!f) { kk4s::Log("script: cannot open %s", path); return; }
	char line[512];
	Uint32 last = 0;
	while (fgets(line, sizeof(line), f)) {
		char t[32] = {0}, op[32] = {0}, a1[256] = {0}, a2[64] = {0};
		if (line[0] == '#' || sscanf(line, "%31s %31s %255s %63s", t, op, a1, a2) < 2) continue;
		Uint32 at = t[0] == '+' ? last + (Uint32)atoi(t + 1) : (Uint32)atoi(t);
		last = at;
		kk4sScript.push_back({at, op, a1, a2});
	}
	fclose(f);
	kk4s::Log("script: %zu command(s) from %s", kk4sScript.size(), path);
}

static unsigned kk4sPadByName(const std::string& n) {
	static const struct { const char* name; unsigned bit; } kNames[] = {
		{"A", kk4s::kPadA}, {"B", kk4s::kPadB}, {"X", kk4s::kPadX}, {"Y", kk4s::kPadY}, {"L", kk4s::kPadL},
		{"R", kk4s::kPadR}, {"ZL", kk4s::kPadZL}, {"ZR", kk4s::kPadZR}, {"PLUS", kk4s::kPadPlus},
		{"MINUS", kk4s::kPadMinus}, {"LEFT", kk4s::kPadLeft}, {"UP", kk4s::kPadUp}, {"RIGHT", kk4s::kPadRight},
		{"DOWN", kk4s::kPadDown}, {"L3", kk4s::kPadLStick}, {"R3", kk4s::kPadRStick}};
	for (const auto& k : kNames) if (n == k.name) return k.bit;
	return 0;
}

static void kk4sScriptTick() {
	kk4sScriptLoad();
	if (kk4sScript.empty() || !_currentWindowLayer) return;
	Uint32 now = SDL_GetTicks();
	if (!kk4sScriptStart) kk4sScriptStart = now;
	Uint32 t = now - kk4sScriptStart;
	for (size_t i = 0; i < kk4sScriptReleases.size();) {
		if (t >= kk4sScriptReleases[i].first) {
			_currentWindowLayer->kk4sPadChange(kk4sScriptReleases[i].second, false);
			kk4sScriptReleases.erase(kk4sScriptReleases.begin() + i);
		} else ++i;
	}
	for (size_t i = 0; i < kk4sScriptEvents.size();) {
		if (t >= kk4sScriptEvents[i].first) {
			SDL_PushEvent(&kk4sScriptEvents[i].second);
			kk4sScriptEvents.erase(kk4sScriptEvents.begin() + i);
		} else ++i;
	}
	while (kk4sScriptPos < kk4sScript.size() && t >= kk4sScript[kk4sScriptPos].at) {
		const kk4sScriptCmd& c = kk4sScript[kk4sScriptPos++];
		kk4s::Log("script: %u %s %s %s", (unsigned)c.at, c.op.c_str(), c.a1.c_str(), c.a2.c_str());
		if (c.op == "pad") {
			unsigned bit = kk4sPadByName(c.a1);
			if (!bit) continue;
			if (c.a2 == "down") { _currentWindowLayer->kk4sPadChange(bit, true); continue; }
			if (c.a2 == "up") { _currentWindowLayer->kk4sPadChange(bit, false); continue; }
			_currentWindowLayer->kk4sPadChange(bit, true);
			kk4sScriptReleases.push_back({t + (c.a2.empty() ? 100 : (Uint32)atoi(c.a2.c_str())), bit});
		} else if (c.op == "tap") {
			SDL_Event e;
			memset(&e, 0, sizeof(e));
			e.type = SDL_FINGERDOWN;
			e.tfinger.x = (float)atof(c.a1.c_str());
			e.tfinger.y = (float)atof(c.a2.c_str());
			SDL_PushEvent(&e);
			e.type = SDL_FINGERUP;
			kk4sScriptEvents.push_back({t + 80, e});
		} else if (c.op == "shot") {
			std::string file = c.a1;
			if (!file.empty() && file[0] != '/') {
				kk4s::detail::MakeDir(kk4s::RootDir() + "/shots");
				file = kk4s::RootDir() + "/shots/" + file;
			}
			_currentWindowLayer->kk4sPresent(kk4s::overlay::Visible(), file.c_str());
		} else if (c.op == "quit") {
			kk4s::SetExitMode(kk4s::ExitMode::Quit);
			TVPExitApplication(0);
		}
	}
}

bool sdlProcessEvents() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		kk4sOpenInputDevice(event); // kk4s
		// kk4s: in-game menu (F1 on PC; pad buttons in kk4sPadFrame / sdlRecvEvent)
		if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F1 && !kk4s::overlay::IsOpen() &&
			_currentWindowLayer && !event.key.repeat) {
			_currentWindowLayer->kk4sOpenOverlay();
			continue;
		}
		if (kk4s::overlay::IsOpen() && event.type != SDL_QUIT && event.type != SDL_CONTROLLERBUTTONDOWN &&
			event.type != SDL_CONTROLLERBUTTONUP) {
			int ow = 0, oh = 0;
			SDL_GetRendererOutputSize(renderer, &ow, &oh);
			if (kk4s::overlay::Event(event, ow, oh)) continue;
		}
		if (_currentWindowLayer) {
			_currentWindowLayer->sdlRecvEvent(event);
		}
	}
	if (_currentWindowLayer) _currentWindowLayer->kk4sPadFrame(); // kk4s: Switch buttons
	kk4sScriptTick(); // kk4s: KK4S_INPUT_SCRIPT (debugging)
	Uint32 kk4sStart = SDL_GetTicks();
	unsigned kk4sShownBefore = kk4sFramesShown;
	::Application->Run();
	iTVPTexture2D::RecycleProcess();
	// kk4s: log frames that stall (helps find freezes on Switch)
	Uint32 kk4sTime = SDL_GetTicks() - kk4sStart;
	static const Uint32 kk4sSlowMs = getenv("KK4S_SLOW_MS") ? (Uint32)atoi(getenv("KK4S_SLOW_MS")) : 200; // PC: lower to find stalls
	kk4s::FrameStats& kk4sStats = kk4s::CurrentFrameStats();
	if (kk4sTime >= kk4sSlowMs) {
		// the rest (scripts, events, timers, image loading, sound start) = total - listed parts
		unsigned parts = kk4sStats.compose + kk4sStats.upload + kk4sStats.present + kk4sStats.read +
						 kk4sStats.write + kk4sStats.gc;
		kk4s::Log("slow frame: %u ms (compose %u, upload %u, present %u, file reads %u ms / %u, writes %u ms / %u, gc %u, other %u)",
				  (unsigned)kk4sTime, kk4sStats.compose, kk4sStats.upload, kk4sStats.present, kk4sStats.read,
				  kk4sStats.reads, kk4sStats.write, kk4sStats.writes, kk4sStats.gc,
				  (unsigned)kk4sTime > parts ? (unsigned)kk4sTime - parts : 0);
	}
	kk4sStats = kk4s::FrameStats();
	kk4sOverlayFrame();
	// kk4s: nothing new on screen -> a short sleep instead of spinning. A present
	// waits for vsync by itself; without one this loop ran flat out.
	if (kk4sFramesShown == kk4sShownBefore) SDL_Delay(1);
	return true;
}

bool sdlProcessEventsForFrames(int frames) {
	for (int i = 0; i < frames; ++i) {
		sdlProcessEvents();
	}
	return true; // kk4s: missing return was UB
}

// kk4s: copy a narrow string into a new wide string for _wargv
static tjs_char* kk4sWiden(const char* s) {
	ttstr w(s);
	size_t len = w.length();
	tjs_char* out = new tjs_char[len + 1];
	memcpy(out, w.c_str(), sizeof(tjs_char) * len);
	out[len] = TJS_W('\0');
	return out;
}

int main(int argc, char **argv) {
	// kk4s: platform init + game launcher before the engine starts
	kk4s::Init(argc, argv);

	// kk4s: the old code set hints named "SDL_HINT_..." (not the real names),
	// so SDL also turned every touch into mouse events: double clicks
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
	SDL_Init(SDL_INIT_EVERYTHING);
	// kk4s: gamepads are opened on their device-added events (launcher, then
	// sdlProcessEvents); romfs/socket/nxlink are initialized in kk4s::Init()

	// kk4s: window size/flags come from the platform layer. The window exists
	// before the game is chosen: the launcher draws into it.
	int kk4sW = 1920, kk4sH = 1080;
	unsigned kk4sFlags = 0;
	kk4s::WindowConfig(kk4sW, kk4sH, kk4sFlags);
	window = SDL_CreateWindow("kk4s", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kk4sW, kk4sH, kk4sFlags);
	if (!window) {
		kk4s::Log("SDL_CreateWindow failed: %s", SDL_GetError());
		kk4s::Shutdown();
		return 1;
	}
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) {
		// kk4s: no GPU renderer (VM, CI, old PC) -> software renderer
		kk4s::Log("accelerated renderer failed (%s), trying software", SDL_GetError());
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	}
	if (!renderer) {
		kk4s::Log("SDL_CreateRenderer failed: %s", SDL_GetError());
		kk4s::Shutdown();
		return 1;
	}
	{
		SDL_RendererInfo kk4sInfo;
		if (SDL_GetRendererInfo(renderer, &kk4sInfo) == 0)
			kk4s::Log("renderer: %s, window %dx%d", kk4sInfo.name, kk4sW, kk4sH);
	}

	// kk4s: game launcher
	std::string kk4sGameDir = kk4s::SelectGame(renderer);
	if (kk4sGameDir.empty()) {
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_Quit();
		kk4s::Shutdown();
		return 0;
	}

	int kk4sArgc = kk4s::EngineArgc();
	char** kk4sArgv = kk4s::EngineArgv();
	_argc = kk4sArgc > 0 ? kk4sArgc : 1;
	_wargv = new tjs_char*[_argc];
	for (int i = 0; i < _argc; i += 1) {
		_wargv[i] = kk4sWiden(i < kk4sArgc ? kk4sArgv[i] : "");
	}
	// kk4s: pretend the exe lives inside the game folder, like krkr.exe on
	// Windows. System.exePath, patch.xp3 lookup and plugin paths then point
	// at the game folder.
	delete[] _wargv[0];
	_wargv[0] = kk4sWiden((kk4sGameDir + "krkr.exe").c_str());

	kk4s::SetGamePerformance(kk4s::LiveSettings().cpu_boost); // kk4s: Switch CPU boost while the game runs

	::Application = new tTVPApplication();
	::Application->StartApplication( _argc, _wargv );

    Uint32 startTime = 0;
    Uint32 endTime = 0;
    Uint32 delta = 0;
    short timePerFrame = 16; // miliseconds
    
	while (sdlProcessEvents()) {
#if 0
		if (!startTime) {
            startTime = SDL_GetTicks(); 
        } else {
            delta = endTime - startTime; // how many ms for a frame
        }
        
        if (delta < timePerFrame) {
            SDL_Delay(timePerFrame - delta);
        }
        
        startTime = endTime;
        endTime = SDL_GetTicks();
#endif
	}
	SDL_Quit();
	kk4s::Shutdown(); // kk4s
	return 0;
}




bool TVPGetKeyMouseAsyncState(tjs_uint keycode, bool getcurrent)
{
	// kk4s: getcurrent -> is it down now; otherwise -> was it pressed since the last query
	if (keycode >= sizeof(kk4sKeyState)) return false;
	tjs_uint8 code = kk4sKeyState[keycode];
	kk4sKeyState[keycode] &= 0x01;
	return code & (getcurrent ? 0x01 : 0x11);
}



#undef st_atime
#undef st_ctime
#undef st_mtime

//int stat64(const char* __path, struct stat64* __buf) __INTRODUCED_IN(21); // force link it !
bool TVP_stat(const char *name, tTVP_stat &s) {
	struct stat t;
	// static_assert(sizeof(t.st_size) == 4, "");
	// static_assert(sizeof(t.st_size) == 8, "");
	bool ret = !stat(name, &t);
	s.st_mode = t.st_mode;
	s.st_size = t.st_size;
#if 0
	s.st_atime = t.st_atime;
	s.st_mtime = t.st_mtime;
	s.st_ctime = t.st_ctime;
#endif
	return ret;
}


bool TVP_stat(const tjs_char *name, tTVP_stat &s) {
	tTJSNarrowStringHolder holder(name);
	return TVP_stat(holder, s);
}

// tjs_uint32 TVPGetRoughTickCount32()
// {
// 	// tjs_uint32 uptime = 0;
// 	// struct timespec on;
// 	// if (clock_gettime(CLOCK_MONOTONIC, &on) == 0)
// 	// 	uptime = on.tv_sec * 1000 + on.tv_nsec / 1000000;
// 	// return uptime;
// 	return SDL_GetTicks();
// }

bool TVPGetJoyPadAsyncState(tjs_uint keycode, bool getcurrent)
{
	// if (keycode >= sizeof(_scancode) / sizeof(_scancode[0])) return false;
	// tjs_uint8 code = _scancode[keycode];
	// _scancode[keycode] &= 1;
	return false;
}

void TVPExitApplication(int code) {
	// kk4s: shut the engine down first, like the normal "System.terminate" path
	// does. Sound buffers, timers etc. end their threads there; the in-game menu
	// and script errors came here directly and left the threads running, and on
	// the Switch they kept running inside the next NRO (crash in the launcher).
	if (!TVPSystemUninitCalled) {
		kk4s::Log("TVPExitApplication(%d): engine shutdown", code);
		try { TVPSystemUninit(); } catch (...) { kk4s::Log("engine shutdown failed"); }
	}
	TVPStopAllThreads(1000); // kk4s: sound decoders, image loader: not ended by the shutdown
	SDL_DestroyRenderer(renderer);
	renderer = NULL;
	SDL_DestroyWindow(window);
	window = NULL;
	SDL_Quit();
	kk4s::Log("TVPExitApplication(%d)", code); // kk4s
	kk4s::ExitAfterGame(code); // kk4s: back to the launcher (or exit), calls Shutdown()
}

void TVPRelinquishCPU() {
	SDL_Delay(0);
}

#include <string.h>

iWindowLayer *TVPCreateAndAddWindow(tTJSNI_Window *w) {
	TVPWindowLayer* ret = TVPWindowLayer::create(w);
	return ret;
}

void TVPConsoleLog(const ttstr &l, bool important) {
	kk4s::Log("%s", l.AsNarrowStdString().c_str()); // kk4s
}

namespace TJS {
	static const int MAX_LOG_LENGTH = 16 * 1024;
	void TVPConsoleLog(const tjs_char *l) {
		kk4s::Log("%s", ttstr(l).AsNarrowStdString().c_str()); // kk4s
	}

	void TVPConsoleLog(const tjs_nchar *format, ...) {
		va_list args;
		va_start(args, format);
		char buf[MAX_LOG_LENGTH];
		vsnprintf(buf, MAX_LOG_LENGTH - 3, format, args);
		kk4s::Log("%s", buf); // kk4s
		va_end(args);
	}
}

tjs_uint32 TVPGetCurrentShiftKeyState()
{
	tjs_uint32 f = 0;

	// if (_scancode[VK_SHIFT] & 1) f |= ssShift;
	// if (_scancode[VK_MENU] & 1) f |= ssAlt;
	// if (_scancode[VK_CONTROL] & 1) f |= ssCtrl;
	// if (_scancode[VK_LBUTTON] & 1) f |= ssLeft;
	// if (_scancode[VK_RBUTTON] & 1) f |= ssRight;
	//if (_scancode[VK_MBUTTON] & 1) f |= TVP_SS_MIDDLE;

	return f;
}

ttstr TVPGetPlatformName()
{
	return "Horizon";
}

ttstr TVPGetOSName()
{
	return TVPGetPlatformName();
}

void TVPShowPopMenu(tTJSNI_MenuItem* menu) {
	// TODO: STUB
};


#include <dirent.h>
extern void TVPInitializeFont();

static ttstr TVPDefaultFaceNames;
extern void TVPAddSystemFontToFreeType( const std::string& storage, std::vector<tjs_string>* faces );
extern void TVPGetSystemFontListFromFreeType( std::vector<tjs_string>& faces );
static bool TVPIsGetAllFontList = false;
static ttstr TVPDefaultFontNameX;
void TVPGetAllFontList( std::vector<tjs_string>& list ) {
	TVPInitializeFont();
	// kk4s: always return the list (the first call used to return nothing)
	kk4s::compat::RegisterFonts();
	TVPGetSystemFontListFromFreeType( list );
	TVPIsGetAllFontList = true;

#if 0
	DIR* dr;
	if( ( dr = opendir("romfs:/fonts/") ) != nullptr ) {
		fprintf(stderr, "Opened font directory\n");
		struct dirent* entry;
		while( ( entry = readdir( dr ) ) != nullptr ) {
			if( entry->d_type == DT_REG ) {
				std::string path(entry->d_name);
				std::string::size_type extp = path.find_last_of(".");
				if( extp != std::string::npos ) {
					std::string ext = path.substr(extp);
					if( ext == std::string(".ttf") || ext == std::string(".ttc") || ext == std::string(".otf") ) {
						// .ttf | .ttc | .otf
						fprintf(stderr, "Opened %s\n", path.c_str());
						std::string fullpath( std::string("romfs:/fonts/") + path );
						TVPAddSystemFontToFreeType( fullpath, &list );
					}
				}
			}
		}
		closedir( dr );
		TVPIsGetAllFontList = true;
	}
#endif
#if 0
	for( std::list<std::string>::const_iterator i = fontfiles.begin(); i != fontfiles.end(); ++i ) {
		FT_Face face = nullptr;
		std::string fullpath( std::string("/system/fonts/") + *i );
		FT_Open_Args args;
		memset(&args, 0, sizeof(args));
		args.flags = FT_OPEN_PATHNAME;
		args.pathname = fullpath.c_str();
		tjs_uint face_num = 1;
		std::list<std::string> facenames;
		for( tjs_uint f = 0; f < face_num; f++ ) {
			FT_Error err = FT_Open_Face( FreeTypeLibrary, &args, 0, &face);
			if( err == 0 ) {
				facenames.push_back( std::string(face->family_name) );
				std::string(face->style_name);	// スタイル名
				if( face->face_flags & FT_FACE_FLAG_SCALABLE ) {
					// 可変サイズフォントのみ採用
					if( face->num_glyphs > 2965 ) {
						// JIS第一水準漢字以上のグリフ数
						if( face->style_flags & FT_STYLE_FLAG_ITALIC ) {}
						if( face->style_flags & FT_STYLE_FLAG_BOLD ) {}
						face_num = face->num_faces;
						int numcharmap = face->num_charmaps;
						for( int c = 0; c < numcharmap; c++ ) {
							FT_Encoding enc = face->charmaps[c]->encoding;
							if( enc == FT_ENCODING_SJIS ) {
								// mybe japanese
							}
							if( enc == FT_ENCODING_UNICODE ) {
							}
						}
					}
				}
			}
			if(face) FT_Done_Face(face), face = nullptr;
		}
	}
#endif
}
static bool IsInitDefalutFontName = false;
static bool SelectFont( const std::vector<tjs_string>& faces, tjs_string& face ) {
	std::vector<tjs_string> fonts;
	TVPGetAllFontList( fonts );
	for( auto i = faces.begin(); i != faces.end(); ++i ) {
		auto found = std::find( fonts.begin(), fonts.end(), *i );
		if( found != fonts.end() ) {
			face = *i;
			return true;
		}
	}
	return false;
}
const tjs_char *TVPGetDefaultFontName() {
	if( IsInitDefalutFontName ) {
		return TVPDefaultFontNameX.c_str();
	}
	TVPDefaultFontNameX = TJS_W("Noto Sans CJK JP");
	IsInitDefalutFontName =  true;

	// コマンドラインで指定がある場合、そのフォントを使用する
	tTJSVariant opt;
	if(TVPGetCommandLine(TJS_W("-deffont"), &opt)) {
		ttstr str(opt);
		TVPDefaultFontNameX = str;
	} else {
		// kk4s: what Japanese Windows games expect first, then free CJK fonts
		tjs_string face;
		std::vector<tjs_string> facenames{
			TJS_W("MS Gothic"), TJS_W("ＭＳ ゴシック"), TJS_W("Meiryo"),
			TJS_W("Noto Sans CJK JP"), TJS_W("Noto Sans JP"), TJS_W("Source Han Sans JP"),
			TJS_W("IPAexGothic"), TJS_W("IPAGothic"), TJS_W("VL Gothic"), TJS_W("Yu Gothic"),
			TJS_W("Nintendo Standard"), TJS_W("NintendoStandard") };
		if( SelectFont( facenames, face ) ) {
			TVPDefaultFontNameX = face;
		} else {
			std::vector<tjs_string> all;
			TVPGetAllFontList( all );
			if( !all.empty() ) TVPDefaultFontNameX = all[0];
		}
		kk4s::Log("fonts: default font: %s", ttstr(TVPDefaultFontNameX).AsNarrowStdString().c_str());
	}
	return TVPDefaultFontNameX.c_str();
}
void TVPSetDefaultFontName( const tjs_char * name ) {
	TVPDefaultFontNameX = name;
}
const ttstr &TVPGetDefaultFaceNames() {
	static ttstr default_facenames = "Noto Sans,MotoyaLMaru,Roboto";
#if 0
	if( !TVPDefaultFaceNames.IsEmpty() ) {
		return TVPDefaultFaceNames;
	} else {
		TVPDefaultFaceNames = ttstr( TVPGetDefaultFontName() );
		return TVPDefaultFaceNames;
	}
#endif
	return default_facenames;
}
