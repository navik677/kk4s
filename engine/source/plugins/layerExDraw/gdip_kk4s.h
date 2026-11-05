// kk4s: the parts of GDI+ that layerExDraw's TJS binding (main.cpp, from
// wamsoft/layerExDraw) uses, implemented without Windows: value types with
// GDI+'s names and members (PointF, RectF, Matrix), the enum values of
// gdiplusenums.h, an Image holding straight-alpha RGBA pixels (krkrs' layer
// order) and a GraphicsPath over a plutovg path. Rendering is plutovg's
// (sammycage/plutovg, MIT) in LayerExDraw.cpp.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "plutovg/plutovg.h"

typedef float REAL;
typedef int INT;
typedef unsigned int UINT;
typedef uint32_t ARGB;
typedef int BOOL;

enum Status {
	Ok = 0, GenericError, InvalidParameter, OutOfMemory, ObjectBusy, InsufficientBuffer,
	NotImplemented, Win32Error, WrongState, Aborted, FileNotFound, ValueOverflow, AccessDenied,
	UnknownImageFormat, FontFamilyNotFound, FontStyleNotFound, NotTrueTypeFont,
	UnsupportedGdiplusVersion, GdiplusNotInitialized, PropertyNotFound, PropertyNotSupported,
	ProfileNotFound
};
enum FontStyle {
	FontStyleRegular = 0, FontStyleBold = 1, FontStyleItalic = 2, FontStyleBoldItalic = 3,
	FontStyleUnderline = 4, FontStyleStrikeout = 8
};
enum BrushType {
	BrushTypeSolidColor = 0, BrushTypeHatchFill = 1, BrushTypeTextureFill = 2,
	BrushTypePathGradient = 3, BrushTypeLinearGradient = 4
};
enum DashCap { DashCapFlat = 0, DashCapRound = 2, DashCapTriangle = 3 };
enum DashStyle {
	DashStyleSolid = 0, DashStyleDash, DashStyleDot, DashStyleDashDot, DashStyleDashDotDot, DashStyleCustom
};
enum HatchStyle {
	HatchStyleHorizontal = 0, HatchStyleVertical, HatchStyleForwardDiagonal, HatchStyleBackwardDiagonal,
	HatchStyleCross, HatchStyleDiagonalCross, HatchStyle05Percent, HatchStyle10Percent, HatchStyle20Percent,
	HatchStyle25Percent, HatchStyle30Percent, HatchStyle40Percent, HatchStyle50Percent, HatchStyle60Percent,
	HatchStyle70Percent, HatchStyle75Percent, HatchStyle80Percent, HatchStyle90Percent,
	HatchStyleLightDownwardDiagonal, HatchStyleLightUpwardDiagonal, HatchStyleDarkDownwardDiagonal,
	HatchStyleDarkUpwardDiagonal, HatchStyleWideDownwardDiagonal, HatchStyleWideUpwardDiagonal,
	HatchStyleLightVertical, HatchStyleLightHorizontal, HatchStyleNarrowVertical, HatchStyleNarrowHorizontal,
	HatchStyleDarkVertical, HatchStyleDarkHorizontal, HatchStyleDashedDownwardDiagonal,
	HatchStyleDashedUpwardDiagonal, HatchStyleDashedHorizontal, HatchStyleDashedVertical,
	HatchStyleSmallConfetti, HatchStyleLargeConfetti, HatchStyleZigZag, HatchStyleWave,
	HatchStyleDiagonalBrick, HatchStyleHorizontalBrick, HatchStyleWeave, HatchStylePlaid, HatchStyleDivot,
	HatchStyleDottedGrid, HatchStyleDottedDiamond, HatchStyleShingle, HatchStyleTrellis, HatchStyleSphere,
	HatchStyleSmallGrid, HatchStyleSmallCheckerBoard, HatchStyleLargeCheckerBoard,
	HatchStyleOutlinedDiamond, HatchStyleSolidDiamond,
	HatchStyleTotal, HatchStyleLargeGrid = HatchStyleCross, HatchStyleMin = HatchStyleHorizontal,
	HatchStyleMax = HatchStyleTotal - 1
};
enum LinearGradientMode {
	LinearGradientModeHorizontal = 0, LinearGradientModeVertical, LinearGradientModeForwardDiagonal,
	LinearGradientModeBackwardDiagonal
};
enum LineCap {
	LineCapFlat = 0, LineCapSquare = 1, LineCapRound = 2, LineCapTriangle = 3, LineCapNoAnchor = 0x10,
	LineCapSquareAnchor = 0x11, LineCapRoundAnchor = 0x12, LineCapDiamondAnchor = 0x13,
	LineCapArrowAnchor = 0x14, LineCapCustom = 0xff, LineCapAnchorMask = 0xf0
};
enum LineJoin { LineJoinMiter = 0, LineJoinBevel = 1, LineJoinRound = 2, LineJoinMiterClipped = 3 };
enum PenAlignment { PenAlignmentCenter = 0, PenAlignmentInset = 1 };
enum WrapMode { WrapModeTile = 0, WrapModeTileFlipX, WrapModeTileFlipY, WrapModeTileFlipXY, WrapModeClamp };
enum MatrixOrder { MatrixOrderPrepend = 0, MatrixOrderAppend = 1 };
enum ImageType { ImageTypeUnknown = 0, ImageTypeBitmap, ImageTypeMetafile };
enum RotateFlipType {
	RotateNoneFlipNone = 0, Rotate90FlipNone = 1, Rotate180FlipNone = 2, Rotate270FlipNone = 3,
	RotateNoneFlipX = 4, Rotate90FlipX = 5, Rotate180FlipX = 6, Rotate270FlipX = 7,
	RotateNoneFlipY = Rotate180FlipX, Rotate90FlipY = Rotate270FlipX, Rotate180FlipY = RotateNoneFlipX,
	Rotate270FlipY = Rotate90FlipX, RotateNoneFlipXY = Rotate180FlipNone, Rotate90FlipXY = Rotate270FlipNone,
	Rotate180FlipXY = RotateNoneFlipNone, Rotate270FlipXY = Rotate90FlipNone
};
enum SmoothingMode {
	SmoothingModeInvalid = -1, SmoothingModeDefault = 0, SmoothingModeHighSpeed = 1,
	SmoothingModeHighQuality = 2, SmoothingModeNone = 3, SmoothingModeAntiAlias = 4
};
enum TextRenderingHint {
	TextRenderingHintSystemDefault = 0, TextRenderingHintSingleBitPerPixelGridFit,
	TextRenderingHintSingleBitPerPixel, TextRenderingHintAntiAliasGridFit, TextRenderingHintAntiAlias,
	TextRenderingHintClearTypeGridFit
};
enum EncoderValue {
	EncoderValueColorTypeCMYK = 0, EncoderValueColorTypeYCCK, EncoderValueCompressionLZW,
	EncoderValueCompressionCCITT3, EncoderValueCompressionCCITT4, EncoderValueCompressionRle,
	EncoderValueCompressionNone, EncoderValueScanMethodInterlaced, EncoderValueScanMethodNonInterlaced,
	EncoderValueVersionGif87, EncoderValueVersionGif89, EncoderValueRenderProgressive,
	EncoderValueRenderNonProgressive, EncoderValueTransformRotate90, EncoderValueTransformRotate180,
	EncoderValueTransformRotate270, EncoderValueTransformFlipHorizontal, EncoderValueTransformFlipVertical
};

struct PointF {
	REAL X, Y;
	PointF() : X(0), Y(0) {}
	PointF(REAL x, REAL y) : X(x), Y(y) {}
	BOOL Equals(const PointF &p) const { return X == p.X && Y == p.Y; }
};

struct RectF {
	REAL X, Y, Width, Height;
	RectF() : X(0), Y(0), Width(0), Height(0) {}
	RectF(REAL x, REAL y, REAL w, REAL h) : X(x), Y(y), Width(w), Height(h) {}
	REAL GetLeft() const { return X; }
	REAL GetTop() const { return Y; }
	REAL GetRight() const { return X + Width; }
	REAL GetBottom() const { return Y + Height; }
	void GetLocation(PointF *p) const { p->X = X; p->Y = Y; }
	void GetBounds(RectF *r) const { *r = *this; }
	RectF *Clone() const { return new RectF(*this); }
	BOOL Equals(const RectF &r) const { return X == r.X && Y == r.Y && Width == r.Width && Height == r.Height; }
	void Inflate(REAL dx, REAL dy) { X -= dx; Y -= dy; Width += 2 * dx; Height += 2 * dy; }
	void Inflate(const PointF &p) { Inflate(p.X, p.Y); }
	BOOL IntersectsWith(const RectF &r) const {
		return GetLeft() < r.GetRight() && GetTop() < r.GetBottom() && GetRight() > r.GetLeft() && GetBottom() > r.GetTop();
	}
	BOOL IsEmptyArea() const { return Width <= 0 || Height <= 0; }
	void Offset(REAL dx, REAL dy) { X += dx; Y += dy; }
	static BOOL Union(RectF &c, const RectF &a, const RectF &b) {
		REAL l = a.X < b.X ? a.X : b.X, t = a.Y < b.Y ? a.Y : b.Y;
		REAL r = a.GetRight() > b.GetRight() ? a.GetRight() : b.GetRight();
		REAL bt = a.GetBottom() > b.GetBottom() ? a.GetBottom() : b.GetBottom();
		c = RectF(l, t, r - l, bt - t);
		return !c.IsEmptyArea();
	}
};

// GDI+ matrix: a row vector [x y 1] times [[m11 m12 0][m21 m22 0][dx dy 1]].
class Matrix {
public:
	REAL m[6];
	Matrix() { Reset(); }
	Matrix(REAL m11, REAL m12, REAL m21, REAL m22, REAL dx, REAL dy) { SetElements(m11, m12, m21, m22, dx, dy); }
	// maps rect onto the parallelogram dstplg[0..2] (upper-left, upper-right, lower-left)
	Matrix(const RectF &rect, const PointF *dstplg) {
		REAL w = rect.Width ? rect.Width : 1, h = rect.Height ? rect.Height : 1;
		REAL m11 = (dstplg[1].X - dstplg[0].X) / w, m12 = (dstplg[1].Y - dstplg[0].Y) / w;
		REAL m21 = (dstplg[2].X - dstplg[0].X) / h, m22 = (dstplg[2].Y - dstplg[0].Y) / h;
		SetElements(m11, m12, m21, m22, dstplg[0].X - rect.X * m11 - rect.Y * m21, dstplg[0].Y - rect.X * m12 - rect.Y * m22);
	}
	Matrix *Clone() const { return new Matrix(*this); }
	REAL OffsetX() const { return m[4]; }
	REAL OffsetY() const { return m[5]; }
	BOOL Equals(const Matrix *o) const { return o && memcmp(m, o->m, sizeof(m)) == 0; }
	Status SetElements(REAL m11, REAL m12, REAL m21, REAL m22, REAL dx, REAL dy) {
		m[0] = m11; m[1] = m12; m[2] = m21; m[3] = m22; m[4] = dx; m[5] = dy;
		return Ok;
	}
	Status GetElements(REAL *out) const { memcpy(out, m, sizeof(m)); return Ok; }
	Status GetLastStatus() const { return Ok; }
	Status Reset() { return SetElements(1, 0, 0, 1, 0, 0); }
	BOOL IsIdentity() const { return m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 1 && m[4] == 0 && m[5] == 0; }
	BOOL IsInvertible() const { return (m[0] * m[3] - m[1] * m[2]) != 0; }
	Status Invert() {
		REAL d = m[0] * m[3] - m[1] * m[2];
		if (d == 0) return InvalidParameter;
		Matrix r(m[3] / d, -m[1] / d, -m[2] / d, m[0] / d, (m[2] * m[5] - m[3] * m[4]) / d, (m[1] * m[4] - m[0] * m[5]) / d);
		*this = r;
		return Ok;
	}
	// this = a * b (a applied first)
	static Matrix Product(const Matrix &a, const Matrix &b) {
		return Matrix(a.m[0] * b.m[0] + a.m[1] * b.m[2], a.m[0] * b.m[1] + a.m[1] * b.m[3],
		              a.m[2] * b.m[0] + a.m[3] * b.m[2], a.m[2] * b.m[1] + a.m[3] * b.m[3],
		              a.m[4] * b.m[0] + a.m[5] * b.m[2] + b.m[4], a.m[4] * b.m[1] + a.m[5] * b.m[3] + b.m[5]);
	}
	Status Multiply(const Matrix *o, MatrixOrder order = MatrixOrderPrepend) {
		if (!o) return InvalidParameter;
		*this = order == MatrixOrderPrepend ? Product(*o, *this) : Product(*this, *o);
		return Ok;
	}
	Status Translate(REAL dx, REAL dy, MatrixOrder order = MatrixOrderPrepend) {
		Matrix t(1, 0, 0, 1, dx, dy);
		return Multiply(&t, order);
	}
	Status Scale(REAL sx, REAL sy, MatrixOrder order = MatrixOrderPrepend) {
		Matrix t(sx, 0, 0, sy, 0, 0);
		return Multiply(&t, order);
	}
	Status Shear(REAL sx, REAL sy, MatrixOrder order = MatrixOrderPrepend) {
		Matrix t(1, sy, sx, 1, 0, 0);
		return Multiply(&t, order);
	}
	Status Rotate(REAL angle, MatrixOrder order = MatrixOrderPrepend) {
		double r = angle * 3.14159265358979323846 / 180;
		Matrix t((REAL)cos(r), (REAL)sin(r), (REAL)-sin(r), (REAL)cos(r), 0, 0);
		return Multiply(&t, order);
	}
	Status RotateAt(REAL angle, const PointF &c, MatrixOrder order = MatrixOrderPrepend) {
		Matrix t;
		t.Translate(-c.X, -c.Y, MatrixOrderAppend);
		t.Rotate(angle, MatrixOrderAppend);
		t.Translate(c.X, c.Y, MatrixOrderAppend);
		return Multiply(&t, order);
	}
	void Map(REAL x, REAL y, REAL &ox, REAL &oy) const {
		ox = x * m[0] + y * m[2] + m[4];
		oy = x * m[1] + y * m[3] + m[5];
	}
	Status TransformPoints(PointF *pts, INT count = 1) const {
		for (INT i = 0; i < count; i++) Map(pts[i].X, pts[i].Y, pts[i].X, pts[i].Y);
		return Ok;
	}
	plutovg_matrix_t ToPlutovg() const {
		plutovg_matrix_t r;
		plutovg_matrix_init(&r, m[0], m[1], m[2], m[3], m[4], m[5]);
		return r;
	}
};

// Straight-alpha pixels as krkrs keeps them in layers (RGBA bytes).
class Image {
public:
	UINT width = 0, height = 0;
	std::vector<uint8_t> rgba; // width * height * 4
	Image() {}
	Image(UINT w, UINT h) : width(w), height(h), rgba((size_t)w * h * 4, 0) {}
	Image *Clone() const { return new Image(*this); }
	UINT GetWidth() { return width; }
	UINT GetHeight() { return height; }
	REAL GetHorizontalResolution() { return 96; }
	REAL GetVerticalResolution() { return 96; }
	UINT GetFlags() { return 0; }
	Status GetLastStatus() { return Ok; }
	INT GetPixelFormat() { return 0x26200A; } // PixelFormat32bppARGB
	ImageType GetType() { return ImageTypeBitmap; }
	Status RotateFlip(RotateFlipType type);
	// a premultiplied ARGB surface for plutovg (caller destroys)
	plutovg_surface_t *ToSurface() const;
};

// Figures over a plutovg path; adding to an open figure connects to its last point.
class GraphicsPath {
public:
	plutovg_path_t *path;
	bool open = false;
	GraphicsPath() : path(plutovg_path_create()) {}
	GraphicsPath(const GraphicsPath &o) : path(plutovg_path_clone(o.path)), open(o.open) {}
	GraphicsPath &operator=(const GraphicsPath &o) {
		if (this != &o) { plutovg_path_destroy(path); path = plutovg_path_clone(o.path); open = o.open; }
		return *this;
	}
	~GraphicsPath() { plutovg_path_destroy(path); }
	void StartFigure() { open = false; }
	void CloseFigure() { if (open) plutovg_path_close(path); open = false; }
	void MoveOrLine(REAL x, REAL y) {
		if (open) plutovg_path_line_to(path, x, y);
		else { plutovg_path_move_to(path, x, y); open = true; }
	}
	void AddLine(REAL x1, REAL y1, REAL x2, REAL y2) { MoveOrLine(x1, y1); plutovg_path_line_to(path, x2, y2); }
	void AddLines(const std::vector<PointF> &p) {
		for (size_t i = 0; i < p.size(); i++) {
			if (i == 0) MoveOrLine(p[0].X, p[0].Y);
			else plutovg_path_line_to(path, p[i].X, p[i].Y);
		}
	}
	void AddBezier(REAL x1, REAL y1, REAL x2, REAL y2, REAL x3, REAL y3, REAL x4, REAL y4) {
		MoveOrLine(x1, y1);
		plutovg_path_cubic_to(path, x2, y2, x3, y3, x4, y4);
	}
	void AddBeziers(const std::vector<PointF> &p) {
		if (p.empty()) return;
		MoveOrLine(p[0].X, p[0].Y);
		for (size_t i = 1; i + 2 < p.size(); i += 3)
			plutovg_path_cubic_to(path, p[i].X, p[i].Y, p[i + 1].X, p[i + 1].Y, p[i + 2].X, p[i + 2].Y);
	}
	// cardinal spline through the points (GDI+ AddCurve / AddClosedCurve)
	void AddCurve(const std::vector<PointF> &p, REAL tension, bool closed, int offset = 0, int segments = -1) {
		int n = (int)p.size();
		if (n < 2) return;
		if (segments < 0) segments = closed ? n : n - 1;
		auto at = [&](int i) -> const PointF & {
			if (closed) return p[((i % n) + n) % n];
			return p[i < 0 ? 0 : (i >= n ? n - 1 : i)];
		};
		REAL t = tension / 3;
		MoveOrLine(at(offset).X, at(offset).Y);
		for (int s = 0; s < segments; s++) {
			int i = offset + s;
			const PointF &p0 = at(i - 1), &p1 = at(i), &p2 = at(i + 1), &p3 = at(i + 2);
			plutovg_path_cubic_to(path, p1.X + t * (p2.X - p0.X), p1.Y + t * (p2.Y - p0.Y),
			                      p2.X - t * (p3.X - p1.X), p2.Y - t * (p3.Y - p1.Y), p2.X, p2.Y);
		}
		if (closed) CloseFigure();
	}
	// elliptic arc in the box, degrees clockwise from the x axis (y down)
	void AddArc(REAL x, REAL y, REAL w, REAL h, REAL start, REAL sweep, bool connect = true) {
		double cx = x + w / 2, cy = y + h / 2, rx = w / 2, ry = h / 2;
		if (sweep > 360) sweep = 360;
		if (sweep < -360) sweep = -360;
		int parts = (int)ceil(fabs(sweep) / 90.0);
		if (parts < 1) parts = 1;
		double a0 = start * 3.14159265358979323846 / 180, step = sweep * 3.14159265358979323846 / 180 / parts;
		double k = 4.0 / 3.0 * tan(step / 4);
		double px = cx + rx * cos(a0), py = cy + ry * sin(a0);
		if (connect) MoveOrLine((REAL)px, (REAL)py);
		else { plutovg_path_move_to(path, (REAL)px, (REAL)py); open = true; }
		for (int i = 0; i < parts; i++) {
			double a1 = a0 + step;
			double x1 = cx + rx * (cos(a0) - k * sin(a0)), y1 = cy + ry * (sin(a0) + k * cos(a0));
			double x2 = cx + rx * (cos(a1) + k * sin(a1)), y2 = cy + ry * (sin(a1) - k * cos(a1));
			plutovg_path_cubic_to(path, (REAL)x1, (REAL)y1, (REAL)x2, (REAL)y2, (REAL)(cx + rx * cos(a1)), (REAL)(cy + ry * sin(a1)));
			a0 = a1;
		}
	}
	void AddPie(REAL x, REAL y, REAL w, REAL h, REAL start, REAL sweep) {
		StartFigure();
		plutovg_path_move_to(path, x + w / 2, y + h / 2);
		open = true;
		AddArc(x, y, w, h, start, sweep, true);
		CloseFigure();
	}
	void AddEllipse(REAL x, REAL y, REAL w, REAL h) {
		StartFigure();
		plutovg_path_add_ellipse(path, x + w / 2, y + h / 2, w / 2, h / 2);
		open = false;
	}
	void AddPolygon(const std::vector<PointF> &p) {
		if (p.empty()) return;
		StartFigure();
		AddLines(p);
		CloseFigure();
	}
	void AddRectangle(const RectF &r) {
		StartFigure();
		plutovg_path_add_rect(path, r.X, r.Y, r.Width, r.Height);
		open = false;
	}
};
