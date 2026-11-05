// kk4s: layerExDraw.dll without GDI+ (see LayerExDraw.hpp). Paths, pens and
// brushes are rendered with plutovg into a scratch surface the size of the
// drawn area and composited onto the layer's straight-alpha RGBA pixels
// (GDI+ SourceOver). Text comes from glyph outlines of the fonts kk4s knows
// (system fonts, fonts in the game folder, addPrivateFont / System.addFont).
//
// Not reproduced: EMF recording (record stays false), view transforms beyond
// matrix composition, hatch patterns (drawn with their fore colour), path
// gradient details (a radial gradient from the centre colour to the first
// surround colour), custom line caps / anchors (flat), pen alignment inset,
// compound pens, aliased drawing (SmoothingModeNone draws anti-aliased).
#include "ncbind/ncbind.hpp"
#include "LayerExDraw.hpp"
#define NCB_MODULE_NAME TJS_W("layerExDraw.dll")

#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "FontSystem.h"
#include "MsgIntf.h"
#include "StorageIntf.h"

#include <algorithm>
#include <functional>
#include <string>

// kk4s: the real declarations. A hand-written "std::string GameDir()" (it returns
// const std::string&) linked fine - the return type is not mangled - and freed a
// string that was never constructed (GCC build: double free in ensureFonts).
#include "kk4s_platform.h"
#include "kk4s_internal.h"
extern FontSystem *TVPFontSystem;

extern bool IsArray(const tTJSVariant &var); // main.cpp
extern PointF getPoint(const tTJSVariant &var);
extern RectF getRect(const tTJSVariant &var);

// ---------------------------------------------------------------- fonts

static plutovg_font_face_cache_t *g_fonts = nullptr;
static plutovg_font_face_t *g_fallback = nullptr;
static std::vector<ttstr> g_privateFonts;

static std::string Utf8(const ttstr &s) { return s.AsNarrowStdString(); } // krkrs: narrow = UTF-8

static void ensureFonts()
{
	if (g_fonts) return;
	g_fonts = plutovg_font_face_cache_create();
	std::vector<std::string> files = kk4s::SystemFontFiles();
	if (!kk4s::GameDir().empty()) kk4s::detail::ListFontFiles(kk4s::GameDir(), false, files);
	for (const std::string &f : files) {
		plutovg_font_face_cache_load_file(g_fonts, f.c_str());
		if (!g_fallback) g_fallback = plutovg_font_face_load_from_file(f.c_str(), 0);
	}
}

static void freeOctet(void *closure) { delete[] (uint8_t *)closure; }

void GdiPlus::addPrivateFont(const tjs_char *fontFileName)
{
	ensureFonts();
	ttstr name(fontFileName);
	// the engine registers the faces (and tells their family names)
	std::vector<ttstr> faces;
	if (TVPFontSystem) TVPFontSystem->AddExtraFont(name.AsStdString(), &faces);
	tTJSBinaryStream *stream = TVPCreateStream(name, TJS_BS_READ);
	if (!stream) return;
	tjs_uint64 size = stream->GetSize();
	for (size_t i = 0; i < (faces.empty() ? 1 : faces.size()); i++) {
		uint8_t *data = new uint8_t[(size_t)size];
		stream->SetPosition(0);
		stream->ReadBuffer(data, (tjs_uint)size);
		plutovg_font_face_t *face = plutovg_font_face_load_from_data(data, (unsigned int)size, (int)i, freeOctet, data);
		if (!face) { delete[] data; continue; }
		if (!g_fallback) g_fallback = plutovg_font_face_reference(face);
		if (i < faces.size()) {
			plutovg_font_face_cache_add(g_fonts, Utf8(faces[i]).c_str(), false, false, face);
			g_privateFonts.push_back(faces[i]);
		}
		plutovg_font_face_destroy(face);
	}
	delete stream;
}

tTJSVariant GdiPlus::getFontList(bool privateOnly)
{
	iTJSDispatch2 *array = TJSCreateArrayObject();
	std::vector<ttstr> names = g_privateFonts;
	if (!privateOnly && TVPFontSystem) {
		std::vector<ttstr> all;
		tTVPFont f;
		f.Height = 12; f.Flags = 0; f.Angle = 0;
		TVPFontSystem->GetFontList(all, 0, f);
		for (const ttstr &n : all)
			if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
	}
	for (tjs_int i = 0; i < (tjs_int)names.size(); i++) {
		tTJSVariant v(names[i]);
		array->PropSetByNum(TJS_MEMBERENSURE, i, &v, array);
	}
	tTJSVariant ret(array, array);
	array->Release();
	return ret;
}

FontInfo::FontInfo() : emSize(12), style(0), forceSelfPathDraw(false), face(nullptr) { resolve(); }
FontInfo::FontInfo(const tjs_char *familyName, REAL emSize, INT style)
	: familyName(familyName), emSize(emSize), style(style), forceSelfPathDraw(false), face(nullptr) { resolve(); }
FontInfo::FontInfo(const FontInfo &o)
	: familyName(o.familyName), emSize(o.emSize), style(o.style), forceSelfPathDraw(o.forceSelfPathDraw), face(o.face) {}
FontInfo::~FontInfo() {}

void FontInfo::setFamilyName(const tjs_char *name) { familyName = name; resolve(); }

void FontInfo::resolve()
{
	ensureFonts();
	std::string name = Utf8(familyName);
	if (!name.empty() && name[0] == '@') name.erase(0, 1); // vertical-writing alias
	bool bold = (style & FontStyleBold) != 0, italic = (style & FontStyleItalic) != 0;
	face = name.empty() ? nullptr : plutovg_font_face_cache_get(g_fonts, name.c_str(), bold, italic);
	if (!face && !name.empty()) face = plutovg_font_face_cache_get(g_fonts, name.c_str(), false, false);
	if (!face) face = g_fallback;
}

static void metrics(const FontInfo *f, float &ascent, float &descent, float &gap)
{
	ascent = descent = gap = 0;
	if (f->getFace()) plutovg_font_face_get_metrics(f->getFace(), const_cast<FontInfo *>(f)->getEmSize(), &ascent, &descent, &gap, nullptr);
	if (descent < 0) descent = -descent;
}
REAL FontInfo::getAscent() const { float a, d, g; metrics(this, a, d, g); return a; }
REAL FontInfo::getDescent() const { float a, d, g; metrics(this, a, d, g); return d; }
REAL FontInfo::getAscentLeading() const { float a, d, g; metrics(this, a, d, g); return g / 2; }
REAL FontInfo::getDescentLeading() const { float a, d, g; metrics(this, a, d, g); return g / 2; }
REAL FontInfo::getLineSpacing() const { float a, d, g; metrics(this, a, d, g); return a + d + g; }

// ---------------------------------------------------------------- images

Status Image::RotateFlip(RotateFlipType type)
{
	int rot = type & 3;
	bool flipx = (type & 4) != 0;
	Image src(*this);
	if (rot & 1) { width = src.height; height = src.width; }
	for (UINT y = 0; y < src.height; y++) {
		for (UINT x = 0; x < src.width; x++) {
			UINT sx = flipx ? src.width - 1 - x : x, nx, ny;
			switch (rot) {
			case 1: nx = src.height - 1 - y; ny = x; break;
			case 2: nx = src.width - 1 - x; ny = src.height - 1 - y; break;
			case 3: nx = y; ny = src.width - 1 - x; break;
			default: nx = x; ny = y; break;
			}
			memcpy(&rgba[((size_t)ny * width + nx) * 4], &src.rgba[((size_t)y * src.width + sx) * 4], 4);
		}
	}
	return Ok;
}

plutovg_surface_t *Image::ToSurface() const
{
	plutovg_surface_t *s = plutovg_surface_create((int)width, (int)height);
	if (!s) return nullptr;
	uint8_t *d = plutovg_surface_get_data(s);
	int stride = plutovg_surface_get_stride(s);
	for (UINT y = 0; y < height; y++) {
		uint32_t *row = (uint32_t *)(d + (size_t)y * stride);
		const uint8_t *p = &rgba[(size_t)y * width * 4];
		for (UINT x = 0; x < width; x++, p += 4) {
			uint32_t a = p[3];
			row[x] = (a << 24) | ((p[0] * a / 255) << 16) | ((p[1] * a / 255) << 8) | (p[2] * a / 255);
		}
	}
	return s;
}

Image *loadImage(const tjs_char *name)
{
	try {
		tTVPBaseBitmap bmp(32, 32, 32);
		TVPLoadGraphic(&bmp, ttstr(name), TVP_clNone, 0, 0, glmNormal);
		Image *image = new Image(bmp.GetWidth(), bmp.GetHeight());
		for (UINT y = 0; y < image->height; y++)
			memcpy(&image->rgba[(size_t)y * image->width * 4], bmp.GetScanLine(y), (size_t)image->width * 4);
		return image;
	} catch (...) {
		return nullptr;
	}
}

RectF *getBounds(Image *image)
{
	return new RectF(0, 0, image ? (REAL)image->width : 0, image ? (REAL)image->height : 0);
}

void initGdiPlus() {}
void deInitGdiPlus() {}

// ---------------------------------------------------------------- brushes and pens

static plutovg_color_t toColor(ARGB c)
{
	plutovg_color_t r;
	r.r = ((c >> 16) & 0xff) / 255.f;
	r.g = ((c >> 8) & 0xff) / 255.f;
	r.b = (c & 0xff) / 255.f;
	r.a = ((c >> 24) & 0xff) / 255.f;
	return r;
}

static plutovg_spread_method_t toSpread(int wrap)
{
	switch (wrap) {
	case WrapModeClamp: return PLUTOVG_SPREAD_METHOD_PAD;
	case WrapModeTile: return PLUTOVG_SPREAD_METHOD_REPEAT;
	default: return PLUTOVG_SPREAD_METHOD_REFLECT;
	}
}

void PaintInfo::apply(plutovg_canvas_t *canvas) const
{
	switch (kind) {
	case Linear:
	case Radial: {
		plutovg_gradient_stop_t stops[2];
		stops[0].offset = 0; stops[0].color = toColor(color1);
		stops[1].offset = 1; stops[1].color = toColor(color2);
		if (kind == Linear)
			plutovg_canvas_set_linear_gradient(canvas, x1, y1, x2, y2, toSpread(wrap), stops, 2, nullptr);
		else
			plutovg_canvas_set_radial_gradient(canvas, x1, y1, r, x1, y1, 0, PLUTOVG_SPREAD_METHOD_PAD, stops, 2, nullptr);
		break;
	}
	case Texture:
		if (image) {
			plutovg_surface_t *s = image->ToSurface();
			if (s) {
				plutovg_matrix_t m;
				plutovg_matrix_init_translate(&m, hasDstRect ? dstRect.X : 0, hasDstRect ? dstRect.Y : 0);
				plutovg_canvas_set_texture(canvas, s, PLUTOVG_TEXTURE_TYPE_TILED, 1, &m);
				plutovg_surface_destroy(s);
				break;
			}
		}
		plutovg_canvas_set_color(canvas, nullptr);
		break;
	default: {
		plutovg_color_t c = toColor(color);
		plutovg_canvas_set_rgba(canvas, c.r, c.g, c.b, c.a);
		break;
	}
	}
}

void PenInfo::apply(plutovg_canvas_t *canvas) const
{
	paint.apply(canvas);
	plutovg_canvas_set_line_width(canvas, width);
	int cap = startCap & ~LineCapAnchorMask;
	plutovg_canvas_set_line_cap(canvas, cap == LineCapRound ? PLUTOVG_LINE_CAP_ROUND
	                                    : cap == LineCapSquare ? PLUTOVG_LINE_CAP_SQUARE : PLUTOVG_LINE_CAP_BUTT);
	plutovg_canvas_set_line_join(canvas, lineJoin == LineJoinRound ? PLUTOVG_LINE_JOIN_ROUND
	                                     : lineJoin == LineJoinBevel ? PLUTOVG_LINE_JOIN_BEVEL : PLUTOVG_LINE_JOIN_MITER);
	plutovg_canvas_set_miter_limit(canvas, miterLimit);
	std::vector<float> dashes;
	switch (dashStyle) {
	case DashStyleDash: dashes = {3, 1}; break;
	case DashStyleDot: dashes = {1, 1}; break;
	case DashStyleDashDot: dashes = {3, 1, 1, 1}; break;
	case DashStyleDashDotDot: dashes = {3, 1, 1, 1, 1, 1}; break;
	case DashStyleCustom: dashes.assign(dashPattern.begin(), dashPattern.end()); break;
	default: break;
	}
	for (float &d : dashes) d *= width; // GDI+ dash lengths are in pen widths
	plutovg_canvas_set_dash(canvas, dashOffset * width, dashes.empty() ? nullptr : dashes.data(), (int)dashes.size());
}

static void getPoints(const tTJSVariant &var, std::vector<PointF> &points)
{
	points.clear();
	if (var.Type() != tvtObject) return;
	ncbPropAccessor info(var);
	tjs_int count = info.GetArrayCount();
	for (tjs_int i = 0; i < count; i++) {
		tTJSVariant p;
		if (info.checkVariant(i, p)) points.push_back(getPoint(p));
	}
}

static void getColors(const tTJSVariant &var, std::vector<ARGB> &colors)
{
	colors.clear();
	if (var.Type() != tvtObject) return;
	ncbPropAccessor info(var);
	tjs_int count = info.GetArrayCount();
	for (tjs_int i = 0; i < count; i++) colors.push_back((ARGB)info.getIntValue(i));
}

static void createPaint(const tTJSVariant &colorOrBrush, PaintInfo &p)
{
	if (colorOrBrush.Type() != tvtObject) {
		p.kind = PaintInfo::Solid;
		p.color = (ARGB)(tjs_int)colorOrBrush;
		return;
	}
	ncbPropAccessor info(colorOrBrush);
	int type = info.getIntValue(TJS_W("type"), BrushTypeSolidColor);
	tTJSVariant var;
	switch (type) {
	case BrushTypeHatchFill: // drawn with the fore colour
		p.kind = PaintInfo::Solid;
		p.color = (ARGB)info.getIntValue(TJS_W("foreColor"), (tjs_int)0xffffffff);
		break;
	case BrushTypeTextureFill: {
		ttstr name = info.GetValue(TJS_W("image"), ncbTypedefs::Tag<ttstr>());
		p.kind = PaintInfo::Texture;
		p.image = loadImage(name.c_str());
		p.wrap = info.getIntValue(TJS_W("wrapMode"), WrapModeTile);
		if (info.checkVariant(TJS_W("dstRect"), var)) { p.dstRect = getRect(var); p.hasDstRect = true; }
		break;
	}
	case BrushTypePathGradient: {
		std::vector<PointF> points;
		if (info.checkVariant(TJS_W("points"), var)) getPoints(var, points);
		if (points.empty()) TVPThrowExceptionMessage(TJS_W("must set poins"));
		REAL cx = 0, cy = 0;
		for (const PointF &q : points) { cx += q.X; cy += q.Y; }
		cx /= points.size(); cy /= points.size();
		if (info.checkVariant(TJS_W("centerPoint"), var)) { PointF c = getPoint(var); cx = c.X; cy = c.Y; }
		REAL r = 0;
		for (const PointF &q : points) r = std::max(r, (REAL)hypot(q.X - cx, q.Y - cy));
		std::vector<ARGB> surround;
		if (info.checkVariant(TJS_W("surroundColors"), var)) getColors(var, surround);
		p.kind = PaintInfo::Radial;
		p.x1 = cx; p.y1 = cy; p.r = r > 0 ? r : 1;
		p.color1 = (ARGB)info.getIntValue(TJS_W("centerColor"), (tjs_int)0xffffffff);
		p.color2 = surround.empty() ? p.color1 : surround[0];
		break;
	}
	case BrushTypeLinearGradient: {
		p.kind = PaintInfo::Linear;
		p.color1 = (ARGB)info.getIntValue(TJS_W("color1"), 0);
		p.color2 = (ARGB)info.getIntValue(TJS_W("color2"), 0);
		p.wrap = info.getIntValue(TJS_W("wrapMode"), WrapModeTile);
		if (info.checkVariant(TJS_W("point1"), var)) {
			PointF a = getPoint(var);
			info.checkVariant(TJS_W("point2"), var);
			PointF b = getPoint(var);
			p.x1 = a.X; p.y1 = a.Y; p.x2 = b.X; p.y2 = b.Y;
		} else if (info.checkVariant(TJS_W("rect"), var)) {
			RectF rc = getRect(var);
			double angle = 0;
			if (info.HasValue(TJS_W("angle"))) angle = info.getRealValue(TJS_W("angle"), 0);
			else switch (info.getIntValue(TJS_W("mode"), LinearGradientModeHorizontal)) {
				case LinearGradientModeVertical: angle = 90; break;
				case LinearGradientModeForwardDiagonal: angle = 45; break;
				case LinearGradientModeBackwardDiagonal: angle = 135; break;
				default: angle = 0; break;
			}
			// the gradient line goes through the rect centre at that angle and
			// covers the rect's projection on it
			double a = angle * 3.14159265358979323846 / 180, dx = cos(a), dy = sin(a);
			double cx = rc.X + rc.Width / 2, cy = rc.Y + rc.Height / 2;
			double half = (fabs(rc.Width * dx) + fabs(rc.Height * dy)) / 2;
			p.x1 = (REAL)(cx - dx * half); p.y1 = (REAL)(cy - dy * half);
			p.x2 = (REAL)(cx + dx * half); p.y2 = (REAL)(cy + dy * half);
		} else {
			TVPThrowExceptionMessage(TJS_W("must set point1,2 or rect"));
		}
		break;
	}
	default:
		p.kind = PaintInfo::Solid;
		p.color = (ARGB)info.getIntValue(TJS_W("color"), (tjs_int)0xffffffff);
		break;
	}
}

Appearance::Appearance() {}
Appearance::~Appearance() {}
void Appearance::clear() { drawInfos.clear(); }

void Appearance::addBrush(tTJSVariant colorOrBrush, REAL ox, REAL oy)
{
	drawInfos.emplace_back();
	DrawInfo &d = drawInfos.back();
	d.type = 1;
	d.ox = ox; d.oy = oy;
	createPaint(colorOrBrush, d.brush);
}

void Appearance::addPen(tTJSVariant colorOrBrush, tTJSVariant widthOrOption, REAL ox, REAL oy)
{
	drawInfos.emplace_back();
	DrawInfo &d = drawInfos.back();
	d.type = 0;
	d.ox = ox; d.oy = oy;
	createPaint(colorOrBrush, d.pen.paint);
	if (widthOrOption.Type() != tvtObject) {
		d.pen.width = (REAL)(tjs_real)widthOrOption;
		return;
	}
	ncbPropAccessor info(widthOrOption);
	tTJSVariant var;
	if (info.checkVariant(TJS_W("width"), var)) d.pen.width = (REAL)(tjs_real)var;
	if (info.checkVariant(TJS_W("dashOffset"), var)) d.pen.dashOffset = (REAL)(tjs_real)var;
	if (info.checkVariant(TJS_W("dashStyle"), var)) {
		if (IsArray(var)) {
			ncbPropAccessor dinfo(var);
			tjs_int n = dinfo.GetArrayCount();
			for (tjs_int i = 0; i < n; i++) d.pen.dashPattern.push_back((REAL)dinfo.getRealValue(i));
			d.pen.dashStyle = DashStyleCustom;
		} else {
			d.pen.dashStyle = (int)(tjs_int)var;
		}
	}
	if (info.checkVariant(TJS_W("dashCap"), var)) d.pen.dashCap = (int)(tjs_int)var;
	if (info.checkVariant(TJS_W("startCap"), var) && var.Type() != tvtObject) d.pen.startCap = (int)(tjs_int)var;
	if (info.checkVariant(TJS_W("endCap"), var) && var.Type() != tvtObject) d.pen.endCap = (int)(tjs_int)var;
	if (info.checkVariant(TJS_W("lineJoin"), var)) d.pen.lineJoin = (int)(tjs_int)var;
	if (info.checkVariant(TJS_W("miterLimit"), var)) d.pen.miterLimit = (REAL)(tjs_real)var;
}

// ---------------------------------------------------------------- paths

Path::Path() {}
Path::~Path() {}
void Path::startFigure() { path.StartFigure(); }
void Path::closeFigure() { path.CloseFigure(); }
void Path::drawArc(REAL x, REAL y, REAL w, REAL h, REAL s, REAL sw) { path.AddArc(x, y, w, h, s, sw); }
void Path::drawPie(REAL x, REAL y, REAL w, REAL h, REAL s, REAL sw) { path.AddPie(x, y, w, h, s, sw); }
void Path::drawBezier(REAL x1, REAL y1, REAL x2, REAL y2, REAL x3, REAL y3, REAL x4, REAL y4) { path.AddBezier(x1, y1, x2, y2, x3, y3, x4, y4); }
void Path::drawBeziers(tTJSVariant points) { std::vector<PointF> p; getPoints(points, p); path.AddBeziers(p); }
void Path::drawClosedCurve(tTJSVariant points) { std::vector<PointF> p; getPoints(points, p); path.AddCurve(p, 0.5f, true); }
void Path::drawClosedCurve2(tTJSVariant points, REAL t) { std::vector<PointF> p; getPoints(points, p); path.AddCurve(p, t, true); }
void Path::drawCurve(tTJSVariant points) { std::vector<PointF> p; getPoints(points, p); path.AddCurve(p, 0.5f, false); }
void Path::drawCurve2(tTJSVariant points, REAL t) { std::vector<PointF> p; getPoints(points, p); path.AddCurve(p, t, false); }
void Path::drawCurve3(tTJSVariant points, int offset, int n, REAL t) { std::vector<PointF> p; getPoints(points, p); path.AddCurve(p, t, false, offset, n); }
void Path::drawEllipse(REAL x, REAL y, REAL w, REAL h) { path.AddEllipse(x, y, w, h); }
void Path::drawLine(REAL x1, REAL y1, REAL x2, REAL y2) { path.AddLine(x1, y1, x2, y2); }
void Path::drawLines(tTJSVariant points) { std::vector<PointF> p; getPoints(points, p); path.AddLines(p); }
void Path::drawPolygon(tTJSVariant points) { std::vector<PointF> p; getPoints(points, p); path.AddPolygon(p); }
void Path::drawRectangle(REAL x, REAL y, REAL w, REAL h) { path.AddRectangle(RectF(x, y, w, h)); }
void Path::drawRectangles(tTJSVariant rects)
{
	if (rects.Type() != tvtObject) return;
	ncbPropAccessor info(rects);
	tjs_int n = info.GetArrayCount();
	for (tjs_int i = 0; i < n; i++) {
		tTJSVariant r;
		if (info.checkVariant(i, r)) path.AddRectangle(getRect(r));
	}
}

// ---------------------------------------------------------------- the layer

LayerExDraw::LayerExDraw(DispatchT obj)
	: obj(obj), width(0), height(0), pitch(0), buffer(nullptr), smoothingMode(SmoothingModeAntiAlias),
	  textRenderingHint(TextRenderingHintAntiAlias), updateWhenDraw(true), record(false) {}

LayerExDraw::~LayerExDraw() {}

void LayerExDraw::reset()
{
	tTJSVariant layer(obj, obj);
	ncbPropAccessor p(layer);
	width = p.GetValue(TJS_W("imageWidth"), ncbTypedefs::Tag<tjs_int>());
	height = p.GetValue(TJS_W("imageHeight"), ncbTypedefs::Tag<tjs_int>());
	buffer = (uint8_t *)(tjs_intptr_t)p.GetValue(TJS_W("mainImageBufferForWrite"), ncbTypedefs::Tag<tjs_intptr_t>());
	pitch = p.GetValue(TJS_W("mainImageBufferPitch"), ncbTypedefs::Tag<tjs_int>());
}

void LayerExDraw::snapshot()
{
	reset();
	layerImage = Image(width, height);
	for (tjs_int y = 0; y < height && buffer; y++)
		memcpy(&layerImage.rgba[(size_t)y * width * 4], buffer + (size_t)y * pitch, (size_t)width * 4);
}

void LayerExDraw::updateRect(RectF &rect)
{
	if (!updateWhenDraw || rect.IsEmptyArea()) return;
	tTJSVariant vars[4] = { (tjs_int)floor(rect.X), (tjs_int)floor(rect.Y),
	                        (tjs_int)ceil(rect.Width) + 1, (tjs_int)ceil(rect.Height) + 1 };
	tTJSVariant *varsp[4] = { vars, vars + 1, vars + 2, vars + 3 };
	obj->FuncCall(0, TJS_W("update"), NULL, NULL, 4, varsp, obj);
}

void LayerExDraw::updateTransformMatrix()
{
	calcTransform = transform;
	calcTransform.Multiply(&viewTransform, MatrixOrderAppend);
}
void LayerExDraw::setViewTransform(const Matrix *t) { if (t) viewTransform = *t; updateTransformMatrix(); }
void LayerExDraw::resetViewTransform() { viewTransform.Reset(); updateTransformMatrix(); }
void LayerExDraw::rotateViewTransform(REAL a) { viewTransform.Rotate(a, MatrixOrderAppend); updateTransformMatrix(); }
void LayerExDraw::scaleViewTransform(REAL sx, REAL sy) { viewTransform.Scale(sx, sy, MatrixOrderAppend); updateTransformMatrix(); }
void LayerExDraw::translateViewTransform(REAL dx, REAL dy) { viewTransform.Translate(dx, dy, MatrixOrderAppend); updateTransformMatrix(); }
void LayerExDraw::setTransform(const Matrix *t) { if (t) transform = *t; updateTransformMatrix(); }
void LayerExDraw::resetTransform() { transform.Reset(); updateTransformMatrix(); }
void LayerExDraw::rotateTransform(REAL a) { transform.Rotate(a, MatrixOrderAppend); updateTransformMatrix(); }
void LayerExDraw::scaleTransform(REAL sx, REAL sy) { transform.Scale(sx, sy, MatrixOrderAppend); updateTransformMatrix(); }
void LayerExDraw::translateTransform(REAL dx, REAL dy) { transform.Translate(dx, dy, MatrixOrderAppend); updateTransformMatrix(); }

// GDI+ SourceOver of a premultiplied ARGB surface onto straight-alpha RGBA.
void LayerExDraw::composite(plutovg_surface_t *s, int sx, int sy, bool)
{
	int w = plutovg_surface_get_width(s), h = plutovg_surface_get_height(s), stride = plutovg_surface_get_stride(s);
	const uint8_t *data = plutovg_surface_get_data(s);
	for (int y = 0; y < h; y++) {
		int dy = sy + y;
		if (dy < 0 || dy >= height) continue;
		const uint32_t *srow = (const uint32_t *)(data + (size_t)y * stride);
		uint8_t *drow = buffer + (size_t)dy * pitch;
		for (int x = 0; x < w; x++) {
			int dx = sx + x;
			if (dx < 0 || dx >= width) continue;
			uint32_t p = srow[x], sa = p >> 24;
			if (!sa) continue;
			uint32_t sr = ((p >> 16) & 0xff) * 255 / sa, sg = ((p >> 8) & 0xff) * 255 / sa, sb = (p & 0xff) * 255 / sa;
			uint8_t *d = drow + dx * 4;
			if (sa == 255) { d[0] = (uint8_t)sr; d[1] = (uint8_t)sg; d[2] = (uint8_t)sb; d[3] = 255; continue; }
			uint32_t da = d[3], ea = da * (255 - sa) / 255, oa = sa + ea;
			d[0] = (uint8_t)((sr * sa + d[0] * ea) / oa);
			d[1] = (uint8_t)((sg * sa + d[1] * ea) / oa);
			d[2] = (uint8_t)((sb * sa + d[2] * ea) / oa);
			d[3] = (uint8_t)oa;
		}
	}
}

// Draws the path with one pen or brush through matrix m; returns the touched rect.
RectF LayerExDraw::drawWithPaint(const GraphicsPath &gp, const Matrix &m, const PenInfo *pen, const PaintInfo *brush)
{
	plutovg_path_t *path = plutovg_path_clone(gp.path);
	plutovg_matrix_t pm = m.ToPlutovg();
	plutovg_path_transform(path, &pm);
	plutovg_rect_t ext;
	plutovg_path_extents(path, &ext, false);
	plutovg_path_destroy(path);
	double scale = sqrt(fabs(m.m[0] * m.m[3] - m.m[1] * m.m[2]));
	double grow = pen ? pen->width * scale * (pen->lineJoin == LineJoinMiter ? std::max(1.0f, pen->miterLimit) : 1.0) / 2 + 1 : 1;
	RectF bounds((REAL)(ext.x - grow), (REAL)(ext.y - grow), (REAL)(ext.w + 2 * grow), (REAL)(ext.h + 2 * grow));
	if (!buffer || width <= 0 || height <= 0) return bounds;
	int l = std::max(0, (int)floor(bounds.X)), t = std::max(0, (int)floor(bounds.Y));
	int r = std::min((int)width, (int)ceil(bounds.GetRight())), b = std::min((int)height, (int)ceil(bounds.GetBottom()));
	if (l >= r || t >= b) return bounds;
	plutovg_surface_t *surface = plutovg_surface_create(r - l, b - t);
	if (!surface) return bounds;
	plutovg_canvas_t *canvas = plutovg_canvas_create(surface);
	plutovg_matrix_t cm;
	plutovg_matrix_init_translate(&cm, (float)-l, (float)-t);
	plutovg_matrix_multiply(&cm, &pm, &cm);
	plutovg_canvas_set_matrix(canvas, &cm);
	if (pen) {
		pen->apply(canvas);
		plutovg_canvas_stroke_path(canvas, gp.path);
	} else {
		brush->apply(canvas);
		plutovg_canvas_set_fill_rule(canvas, PLUTOVG_FILL_RULE_EVEN_ODD); // GDI+ FillModeAlternate
		plutovg_canvas_fill_path(canvas, gp.path);
	}
	plutovg_canvas_destroy(canvas);
	composite(surface, l, t, smoothingMode != SmoothingModeNone);
	plutovg_surface_destroy(surface);
	return bounds;
}

RectF LayerExDraw::_drawPath(const Appearance *app, const GraphicsPath *path)
{
	reset();
	RectF rect;
	bool first = true;
	for (const Appearance::DrawInfo &i : app->drawInfos) {
		Matrix m(1, 0, 0, 1, i.ox, i.oy);
		m.Multiply(&calcTransform, MatrixOrderAppend);
		RectF r = drawWithPaint(*path, m, i.type == 0 ? &i.pen : nullptr, i.type == 1 ? &i.brush : nullptr);
		if (first) { rect = r; first = false; }
		else RectF::Union(rect, rect, r);
	}
	updateRect(rect);
	return rect;
}

void LayerExDraw::clear(ARGB argb)
{
	reset();
	uint8_t px[4] = { (uint8_t)(argb >> 16), (uint8_t)(argb >> 8), (uint8_t)argb, (uint8_t)(argb >> 24) };
	for (tjs_int y = 0; y < height && buffer; y++)
		for (tjs_int x = 0; x < width; x++) memcpy(buffer + (size_t)y * pitch + x * 4, px, 4);
	RectF r(0, 0, (REAL)width, (REAL)height);
	updateRect(r);
}

RectF LayerExDraw::drawPath(const Appearance *app, const Path *path) { return _drawPath(app, &path->path); }

#define KK4S_SHAPE(body) GraphicsPath path; body; return _drawPath(app, &path)
RectF LayerExDraw::drawArc(const Appearance *app, REAL x, REAL y, REAL w, REAL h, REAL s, REAL sw) { KK4S_SHAPE(path.AddArc(x, y, w, h, s, sw)); }
RectF LayerExDraw::drawPie(const Appearance *app, REAL x, REAL y, REAL w, REAL h, REAL s, REAL sw) { KK4S_SHAPE(path.AddPie(x, y, w, h, s, sw)); }
RectF LayerExDraw::drawBezier(const Appearance *app, REAL x1, REAL y1, REAL x2, REAL y2, REAL x3, REAL y3, REAL x4, REAL y4) { KK4S_SHAPE(path.AddBezier(x1, y1, x2, y2, x3, y3, x4, y4)); }
RectF LayerExDraw::drawBeziers(const Appearance *app, tTJSVariant points) { std::vector<PointF> p; getPoints(points, p); KK4S_SHAPE(path.AddBeziers(p)); }
RectF LayerExDraw::drawClosedCurve(const Appearance *app, tTJSVariant points) { std::vector<PointF> p; getPoints(points, p); KK4S_SHAPE(path.AddCurve(p, 0.5f, true)); }
RectF LayerExDraw::drawClosedCurve2(const Appearance *app, tTJSVariant points, REAL t) { std::vector<PointF> p; getPoints(points, p); KK4S_SHAPE(path.AddCurve(p, t, true)); }
RectF LayerExDraw::drawCurve(const Appearance *app, tTJSVariant points) { std::vector<PointF> p; getPoints(points, p); KK4S_SHAPE(path.AddCurve(p, 0.5f, false)); }
RectF LayerExDraw::drawCurve2(const Appearance *app, tTJSVariant points, REAL t) { std::vector<PointF> p; getPoints(points, p); KK4S_SHAPE(path.AddCurve(p, t, false)); }
RectF LayerExDraw::drawCurve3(const Appearance *app, tTJSVariant points, int o, int n, REAL t) { std::vector<PointF> p; getPoints(points, p); KK4S_SHAPE(path.AddCurve(p, t, false, o, n)); }
RectF LayerExDraw::drawEllipse(const Appearance *app, REAL x, REAL y, REAL w, REAL h) { KK4S_SHAPE(path.AddEllipse(x, y, w, h)); }
RectF LayerExDraw::drawLine(const Appearance *app, REAL x1, REAL y1, REAL x2, REAL y2) { KK4S_SHAPE(path.AddLine(x1, y1, x2, y2)); }
RectF LayerExDraw::drawLines(const Appearance *app, tTJSVariant points) { std::vector<PointF> p; getPoints(points, p); KK4S_SHAPE(path.AddLines(p)); }
RectF LayerExDraw::drawPolygon(const Appearance *app, tTJSVariant points) { std::vector<PointF> p; getPoints(points, p); KK4S_SHAPE(path.AddPolygon(p)); }
RectF LayerExDraw::drawRectangle(const Appearance *app, REAL x, REAL y, REAL w, REAL h) { KK4S_SHAPE(path.AddRectangle(RectF(x, y, w, h))); }
RectF LayerExDraw::drawRectangles(const Appearance *app, tTJSVariant rects)
{
	GraphicsPath path;
	if (rects.Type() == tvtObject) {
		ncbPropAccessor info(rects);
		tjs_int n = info.GetArrayCount();
		for (tjs_int i = 0; i < n; i++) {
			tTJSVariant r;
			if (info.checkVariant(i, r)) path.AddRectangle(getRect(r));
		}
	}
	return _drawPath(app, &path);
}
#undef KK4S_SHAPE

// Text: lines split at '\n', the first baseline at y + ascent, glyph outlines
// side by side by their advances.
static RectF textPath(const FontInfo *font, REAL x, REAL y, const tjs_char *text, GraphicsPath *out)
{
	float ascent, descent, gap;
	metrics(font, ascent, descent, gap);
	float line = ascent + descent + gap;
	plutovg_font_face_t *face = font->getFace();
	float size = const_cast<FontInfo *>(font)->getEmSize();
	float maxw = 0, cx = x, cy = y + ascent;
	int lines = 1;
	for (const tjs_char *p = text; p && *p; p++) {
		uint32_t cp = *p;
		if (cp >= 0xd800 && cp < 0xdc00 && p[1] >= 0xdc00 && p[1] < 0xe000) { cp = 0x10000 + ((cp - 0xd800) << 10) + (p[1] - 0xdc00); p++; }
		if (cp == '\n') { maxw = std::max(maxw, cx - x); cx = x; cy += line; lines++; continue; }
		if (cp == '\r') continue;
		if (face) {
			plutovg_path_t *target = out ? out->path : nullptr;
			plutovg_path_t *tmp = target ? nullptr : plutovg_path_create();
			cx += plutovg_font_face_get_glyph_path(face, size, cx, cy, cp, target ? target : tmp);
			if (tmp) plutovg_path_destroy(tmp);
		}
	}
	if (out) out->open = false;
	maxw = std::max(maxw, cx - x);
	return RectF(x, y, maxw, line * lines);
}

RectF LayerExDraw::drawPathString(const FontInfo *font, const Appearance *app, REAL x, REAL y, const tjs_char *text)
{
	GraphicsPath path;
	textPath(font, x, y, text, &path);
	return _drawPath(app, &path);
}

// GDI+ DrawString fills with the first brush only (no pens)
RectF LayerExDraw::drawString(const FontInfo *font, const Appearance *app, REAL x, REAL y, const tjs_char *text)
{
	reset();
	for (const Appearance::DrawInfo &i : app->drawInfos) {
		if (i.type != 1) continue;
		GraphicsPath path;
		textPath(font, x + i.ox, y + i.oy, text, &path);
		drawWithPaint(path, calcTransform, nullptr, &i.brush);
		RectF rect = textPath(font, x + i.ox, y + i.oy, text, nullptr);
		PointF pts[4] = { PointF(rect.X, rect.Y), PointF(rect.GetRight(), rect.Y), PointF(rect.X, rect.GetBottom()), PointF(rect.GetRight(), rect.GetBottom()) };
		calcTransform.TransformPoints(pts, 4);
		REAL l = pts[0].X, t = pts[0].Y, r = l, b = t;
		for (const PointF &q : pts) { l = std::min(l, q.X); t = std::min(t, q.Y); r = std::max(r, q.X); b = std::max(b, q.Y); }
		RectF out(l, t, r - l, b - t);
		updateRect(out);
		return out;
	}
	return RectF();
}

RectF LayerExDraw::measureString(const FontInfo *font, const tjs_char *text) { return textPath(font, 0, 0, text, nullptr); }

RectF LayerExDraw::measureStringInternal(const FontInfo *font, const tjs_char *text)
{
	GraphicsPath path;
	textPath(font, 0, 0, text, &path);
	plutovg_rect_t e;
	plutovg_path_extents(path.path, &e, true);
	return RectF(e.x, e.y, e.w, e.h);
}

// images: a texture-filled rectangle mapping the source rect onto the destination
static RectF drawImageMapped(LayerExDraw *self, Image *src, REAL sl, REAL st, REAL sw, REAL sh, const Matrix &dst,
                             std::function<RectF(const GraphicsPath &, const Matrix &, const PaintInfo &)> draw)
{
	if (!src || sw <= 0 || sh <= 0) return RectF();
	PaintInfo p;
	p.kind = PaintInfo::Texture;
	p.image = new Image(*src);
	p.dstRect = RectF(0, 0, (REAL)src->width, (REAL)src->height);
	GraphicsPath path;
	path.AddRectangle(RectF(sl, st, sw, sh));
	return draw(path, dst, p);
}

RectF LayerExDraw::drawImageStretch(REAL dl, REAL dt, REAL dw, REAL dh, Image *src, REAL sl, REAL st, REAL sw, REAL sh)
{
	reset();
	if (!src || sw == 0 || sh == 0) return RectF();
	// source coordinates -> destination: scale then translate, then the layer transform
	Matrix m(dw / sw, 0, 0, dh / sh, dl - sl * dw / sw, dt - st * dh / sh);
	m.Multiply(&calcTransform, MatrixOrderAppend);
	RectF r = drawImageMapped(this, src, sl, st, sw, sh, m, [this](const GraphicsPath &gp, const Matrix &mm, const PaintInfo &p) {
		return drawWithPaint(gp, mm, nullptr, &p);
	});
	updateRect(r);
	return r;
}

RectF LayerExDraw::drawImage(REAL x, REAL y, Image *src)
{
	if (!src) return RectF();
	return drawImageStretch(x, y, (REAL)src->width, (REAL)src->height, src, 0, 0, (REAL)src->width, (REAL)src->height);
}

RectF LayerExDraw::drawImageRect(REAL dl, REAL dt, Image *src, REAL sl, REAL st, REAL sw, REAL sh)
{
	return drawImageStretch(dl, dt, sw, sh, src, sl, st, sw, sh);
}

// affine = true: A..F are the matrix (x' = A x + C y + E, y' = B x + D y + F);
// false: (A, B), (C, D), (E, F) are the destination upper-left, upper-right, lower-left points
RectF LayerExDraw::drawImageAffine(Image *src, REAL sl, REAL st, REAL sw, REAL sh, bool affine, REAL A, REAL B, REAL C, REAL D, REAL E, REAL F)
{
	reset();
	if (!src || sw == 0 || sh == 0) return RectF();
	Matrix m;
	if (affine) {
		m = Matrix(A, B, C, D, E, F);
	} else {
		PointF plg[3] = { PointF(A, B), PointF(C, D), PointF(E, F) };
		m = Matrix(RectF(sl, st, sw, sh), plg);
	}
	m.Multiply(&calcTransform, MatrixOrderAppend);
	RectF r = drawImageMapped(this, src, sl, st, sw, sh, m, [this](const GraphicsPath &gp, const Matrix &mm, const PaintInfo &p) {
		return drawWithPaint(gp, mm, nullptr, &p);
	});
	updateRect(r);
	return r;
}

// saveImage(filename, mime = "image/png", params): the engine writes the layer
tjs_error TJS_INTF_METHOD LayerExDraw::saveImage(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis)
{
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	ttstr mime = numparams > 1 && param[1]->Type() == tvtString ? ttstr(*param[1]) : ttstr(TJS_W("image/png"));
	ttstr type = TJS_W("png");
	if (mime == TJS_W("image/bmp")) type = TJS_W("bmp");
	tTJSVariant name = *param[0], fmt = type;
	tTJSVariant *args[2] = { &name, &fmt };
	tjs_error er = objthis->FuncCall(0, TJS_W("saveLayerImage"), NULL, NULL, 2, args, objthis);
	if (result) *result = TJS_SUCCEEDED(er);
	return TJS_S_OK;
}

// [x, y, w, h] arrays covering the pixels of exactly that colour, row runs
// merged vertically like GDI+ region scans
tTJSVariant LayerExDraw::getColorRegionRects(ARGB color)
{
	reset();
	struct Run { int x0, x1, y0, y1; };
	std::vector<Run> done, open, next;
	uint8_t want[4] = { (uint8_t)(color >> 16), (uint8_t)(color >> 8), (uint8_t)color, (uint8_t)(color >> 24) };
	for (tjs_int y = 0; y <= height && buffer; y++) {
		next.clear();
		if (y < height) {
			const uint8_t *row = buffer + (size_t)y * pitch;
			for (tjs_int x = 0; x < width;) {
				if (memcmp(row + x * 4, want, 4)) { x++; continue; }
				int x0 = x;
				while (x < width && !memcmp(row + x * 4, want, 4)) x++;
				next.push_back({x0, (int)x, (int)y, (int)y + 1});
			}
		}
		std::vector<Run> merged;
		for (Run &n : next) {
			bool joined = false;
			for (Run &o : open)
				if (o.x0 == n.x0 && o.x1 == n.x1 && o.y1 == n.y0) { o.y1 = n.y1; merged.push_back(o); o.x0 = -1; joined = true; break; }
			if (!joined) merged.push_back(n);
		}
		for (Run &o : open) if (o.x0 >= 0) done.push_back(o);
		open = merged;
	}
	iTJSDispatch2 *array = TJSCreateArrayObject();
	tjs_int idx = 0;
	for (const Run &r : done) {
		iTJSDispatch2 *ra = TJSCreateArrayObject();
		tTJSVariant v[4] = { r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0 };
		for (int i = 0; i < 4; i++) ra->PropSetByNum(TJS_MEMBERENSURE, i, &v[i], ra);
		tTJSVariant rv(ra, ra);
		ra->Release();
		array->PropSetByNum(TJS_MEMBERENSURE, idx++, &rv, array);
	}
	tTJSVariant ret(array, array);
	array->Release();
	return ret;
}
