// kk4s: layerExDraw.dll (wamsoft/layerExDraw, GoWatanabe, Kirikiri license)
// without GDI+. The classes and members main.cpp binds keep their upstream
// names and signatures; the drawing is done with plutovg into a scratch
// surface and composited onto the layer (straight alpha, RGBA bytes).
#pragma once

#include "tjsCommHead.h"
#include "gdip_kk4s.h"

#include <vector>

using std::vector;

struct GdiPlus {
	static void addPrivateFont(const tjs_char *fontFileName);
	static tTJSVariant getFontList(bool privateOnly);
};

class FontInfo {
	friend class LayerExDraw;
	friend class Path;
protected:
	ttstr familyName;
	REAL emSize;
	INT style;
	bool forceSelfPathDraw;
	plutovg_font_face_t *face; // not owned
	void resolve();
public:
	FontInfo();
	FontInfo(const tjs_char *familyName, REAL emSize, INT style);
	FontInfo(const FontInfo &orig);
	virtual ~FontInfo();
	void setFamilyName(const tjs_char *familyName);
	const tjs_char *getFamilyName() { return familyName.c_str(); }
	void setEmSize(REAL emSize) { this->emSize = emSize; }
	REAL getEmSize() { return emSize; }
	void setStyle(INT style) { this->style = style; resolve(); }
	INT getStyle() { return style; }
	void setForceSelfPathDraw(bool state) { forceSelfPathDraw = state; }
	bool getForceSelfPathDraw(void) const { return forceSelfPathDraw; }
	bool getSelfPathDraw(void) const { return true; }
	REAL getAscent() const;
	REAL getDescent() const;
	REAL getAscentLeading() const;
	REAL getDescentLeading() const;
	REAL getLineSpacing() const;
	plutovg_font_face_t *getFace() const { return face; }
};

// A brush or a pen, as Appearance keeps them.
struct PaintInfo {
	enum Kind { Solid, Linear, Radial, Texture } kind = Solid;
	ARGB color = 0xffffffff;          // Solid; also the fallback colour
	REAL x1 = 0, y1 = 0, x2 = 0, y2 = 0, r = 0; // Linear: points; Radial: centre x1/y1, radius r
	ARGB color1 = 0, color2 = 0;      // gradient ends (centre / surround for Radial)
	int wrap = WrapModeTile;
	Image *image = nullptr;           // Texture (owned)
	RectF dstRect;                    // Texture: where the image tile sits
	bool hasDstRect = false;
	PaintInfo() {}
	PaintInfo(const PaintInfo &o) : kind(o.kind), color(o.color), x1(o.x1), y1(o.y1), x2(o.x2), y2(o.y2), r(o.r),
		color1(o.color1), color2(o.color2), wrap(o.wrap), image(o.image ? o.image->Clone() : nullptr),
		dstRect(o.dstRect), hasDstRect(o.hasDstRect) {}
	PaintInfo &operator=(const PaintInfo &) = delete;
	~PaintInfo() { delete image; }
	void apply(plutovg_canvas_t *canvas) const;
};

struct PenInfo {
	PaintInfo paint;
	REAL width = 1;
	int dashStyle = DashStyleSolid;
	REAL dashOffset = 0;
	std::vector<REAL> dashPattern; // custom (in pen widths)
	int startCap = LineCapFlat, endCap = LineCapFlat, dashCap = DashCapFlat;
	int lineJoin = LineJoinMiter;
	REAL miterLimit = 10;
	void apply(plutovg_canvas_t *canvas) const;
};

class Appearance {
	friend class LayerExDraw;
public:
	struct DrawInfo {
		int type; // 0: pen, 1: brush (as upstream)
		PenInfo pen;
		PaintInfo brush;
		REAL ox, oy;
	};
	vector<DrawInfo> drawInfos;
public:
	Appearance();
	virtual ~Appearance();
	void clear();
	void addBrush(tTJSVariant colorOrBrush, REAL ox = 0, REAL oy = 0);
	void addPen(tTJSVariant colorOrBrush, tTJSVariant widthOrOption, REAL ox = 0, REAL oy = 0);
};

class Path {
	friend class LayerExDraw;
public:
	Path();
	virtual ~Path();
	void startFigure();
	void closeFigure();
	void drawArc(REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle);
	void drawBezier(REAL x1, REAL y1, REAL x2, REAL y2, REAL x3, REAL y3, REAL x4, REAL y4);
	void drawBeziers(tTJSVariant points);
	void drawClosedCurve(tTJSVariant points);
	void drawClosedCurve2(tTJSVariant points, REAL tension);
	void drawCurve(tTJSVariant points);
	void drawCurve2(tTJSVariant points, REAL tension);
	void drawCurve3(tTJSVariant points, int offset, int numberOfSegments, REAL tension);
	void drawPie(REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle);
	void drawEllipse(REAL x, REAL y, REAL width, REAL height);
	void drawLine(REAL x1, REAL y1, REAL x2, REAL y2);
	void drawLines(tTJSVariant points);
	void drawPolygon(tTJSVariant points);
	void drawRectangle(REAL x, REAL y, REAL width, REAL height);
	void drawRectangles(tTJSVariant rects);
protected:
	GraphicsPath path;
};

class LayerExDraw {
public:
	typedef iTJSDispatch2 *DispatchT;
protected:
	DispatchT obj;              // the layer (not referenced)
	tjs_int width, height, pitch;
	uint8_t *buffer;
	Image layerImage;           // the layer as an Image (drawImage(layer) / brushes)
	Matrix transform, viewTransform, calcTransform;
	SmoothingMode smoothingMode;
	TextRenderingHint textRenderingHint;
	bool updateWhenDraw;
	bool record;
	void updateTransformMatrix();
	void composite(plutovg_surface_t *surface, int sx, int sy, bool antialias);
	RectF drawWithPaint(const GraphicsPath &path, const Matrix &m, const PenInfo *pen, const PaintInfo *brush);
	void snapshot();
public:
	LayerExDraw(DispatchT obj);
	virtual ~LayerExDraw();
	virtual void reset();
	operator Image *() { snapshot(); return &layerImage; }
	operator const Image *() { snapshot(); return &layerImage; }

	int getSmoothingMode() { return (int)smoothingMode; }
	void setSmoothingMode(int mode) { smoothingMode = (SmoothingMode)mode; }
	int getTextRenderingHint() { return (int)textRenderingHint; }
	void setTextRenderingHint(int hint) { textRenderingHint = (TextRenderingHint)hint; }
	void setUpdateWhenDraw(int u) { updateWhenDraw = u != 0; }
	int getUpdateWhenDraw() { return updateWhenDraw ? 1 : 0; }
	void updateRect(RectF &rect);

	void setViewTransform(const Matrix *transform);
	void resetViewTransform();
	void rotateViewTransform(REAL angle);
	void scaleViewTransform(REAL sx, REAL sy);
	void translateViewTransform(REAL dx, REAL dy);
	void setTransform(const Matrix *transform);
	void resetTransform();
	void rotateTransform(REAL angle);
	void scaleTransform(REAL sx, REAL sy);
	void translateTransform(REAL dx, REAL dy);

	RectF _drawPath(const Appearance *app, const GraphicsPath *path);
	void clear(ARGB argb);
	RectF drawPath(const Appearance *app, const Path *path);
	RectF drawArc(const Appearance *app, REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle);
	RectF drawPie(const Appearance *app, REAL x, REAL y, REAL width, REAL height, REAL startAngle, REAL sweepAngle);
	RectF drawBezier(const Appearance *app, REAL x1, REAL y1, REAL x2, REAL y2, REAL x3, REAL y3, REAL x4, REAL y4);
	RectF drawBeziers(const Appearance *app, tTJSVariant points);
	RectF drawClosedCurve(const Appearance *app, tTJSVariant points);
	RectF drawClosedCurve2(const Appearance *app, tTJSVariant points, REAL tension);
	RectF drawCurve(const Appearance *app, tTJSVariant points);
	RectF drawCurve2(const Appearance *app, tTJSVariant points, REAL tension);
	RectF drawCurve3(const Appearance *app, tTJSVariant points, int offset, int numberOfSegments, REAL tension);
	RectF drawEllipse(const Appearance *app, REAL x, REAL y, REAL width, REAL height);
	RectF drawLine(const Appearance *app, REAL x1, REAL y1, REAL x2, REAL y2);
	RectF drawLines(const Appearance *app, tTJSVariant points);
	RectF drawPolygon(const Appearance *app, tTJSVariant points);
	RectF drawRectangle(const Appearance *app, REAL x, REAL y, REAL width, REAL height);
	RectF drawRectangles(const Appearance *app, tTJSVariant rects);
	RectF drawPathString(const FontInfo *font, const Appearance *app, REAL x, REAL y, const tjs_char *text);
	RectF drawString(const FontInfo *font, const Appearance *app, REAL x, REAL y, const tjs_char *text);
	RectF measureString(const FontInfo *font, const tjs_char *text);
	RectF measureStringInternal(const FontInfo *font, const tjs_char *text);

	RectF drawImage(REAL x, REAL y, Image *src);
	RectF drawImageRect(REAL dleft, REAL dtop, Image *src, REAL sleft, REAL stop, REAL swidth, REAL sheight);
	RectF drawImageStretch(REAL dleft, REAL dtop, REAL dwidth, REAL dheight, Image *src, REAL sleft, REAL stop, REAL swidth, REAL sheight);
	RectF drawImageAffine(Image *src, REAL sleft, REAL stop, REAL swidth, REAL sheight, bool affine, REAL A, REAL B, REAL C, REAL D, REAL E, REAL F);

	// EMF recording does not exist here: record stays false
	void setRecord(bool r) { record = r; }
	bool getRecord() { return false; }
	Image *getRecordImage() { return nullptr; }
	bool redrawRecord() { return false; }
	bool saveRecord(const tjs_char *filename) { return false; }
	bool loadRecord(const tjs_char *filename) { return false; }

	static tjs_error TJS_INTF_METHOD saveImage(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis);
	tTJSVariant getColorRegionRects(ARGB color);
};

extern Image *loadImage(const tjs_char *name);
extern RectF *getBounds(Image *image);
extern void initGdiPlus();
extern void deInitGdiPlus();
