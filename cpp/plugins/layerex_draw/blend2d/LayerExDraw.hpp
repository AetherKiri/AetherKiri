#ifndef _layerExText_hpp_
#define _layerExText_hpp_

#include "FreeTypeFace.h"


#include <blend2d.h>

#include <vector>

#include "layerExBase.hpp"

// gdiplus enum
#define PixelFormatIndexed 0x00010000 // Indexes into a palette
#define PixelFormatGDI 0x00020000 // Is a GDI-supported format
#define PixelFormatAlpha 0x00040000 // Has an alpha component
#define PixelFormatPAlpha 0x00080000 // Pre-multiplied alpha
#define PixelFormatExtended 0x00100000 // Extended color 16 bits/channel
#define PixelFormatCanonical 0x00200000
#define PixelFormatUndefined 0
#define PixelFormatDontCare 0
#define PixelFormat1bppIndexed                                                 \
    (1 | (1 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define PixelFormat4bppIndexed                                                 \
    (2 | (4 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define PixelFormat8bppIndexed                                                 \
    (3 | (8 << 8) | PixelFormatIndexed | PixelFormatGDI)
#define PixelFormat16bppGrayScale (4 | (16 << 8) | PixelFormatExtended)
#define PixelFormat16bppRGB555 (5 | (16 << 8) | PixelFormatGDI)
#define PixelFormat16bppRGB565 (6 | (16 << 8) | PixelFormatGDI)
#define PixelFormat16bppARGB1555                                               \
    (7 | (16 << 8) | PixelFormatAlpha | PixelFormatGDI)
#define PixelFormat24bppRGB (8 | (24 << 8) | PixelFormatGDI)
#define PixelFormat32bppRGB (9 | (32 << 8) | PixelFormatGDI)
#define PixelFormat32bppARGB                                                   \
    (10 | (32 << 8) | PixelFormatAlpha | PixelFormatGDI | PixelFormatCanonical)
#define PixelFormat32bppPARGB                                                  \
    (11 | (32 << 8) | PixelFormatAlpha | PixelFormatPAlpha | PixelFormatGDI)
#define PixelFormat48bppRGB (12 | (48 << 8) | PixelFormatExtended)
#define PixelFormat64bppARGB                                                   \
    (13 | (64 << 8) | PixelFormatAlpha | PixelFormatCanonical |                \
     PixelFormatExtended)
#define PixelFormat64bppPARGB                                                  \
    (14 | (64 << 8) | PixelFormatAlpha | PixelFormatPAlpha |                   \
     PixelFormatExtended)
#define PixelFormat32bppCMYK (15 | (32 << 8))
#define PixelFormatMax 16
typedef enum {
    ImageFlagsNone = 0,
    ImageFlagsScalable = 0x0001,
    ImageFlagsHasAlpha = 0x0002,
    ImageFlagsHasTranslucent = 0x0004,
    ImageFlagsPartiallyScalable = 0x0008,
    ImageFlagsColorSpaceRGB = 0x0010,
    ImageFlagsColorSpaceCMYK = 0x0020,
    ImageFlagsColorSpaceGRAY = 0x0040,
    ImageFlagsColorSpaceYCBCR = 0x0080,
    ImageFlagsColorSpaceYCCK = 0x0100,
    ImageFlagsHasRealDPI = 0x1000,
    ImageFlagsHasRealPixelSize = 0x2000,
    ImageFlagsReadOnly = 0x00010000,
    ImageFlagsCaching = 0x00020000,
    ImageFlagsUndocumented = 0x00040000
} ImageFlags;
enum Status {
    Ok = 0,
    GenericError = 1,
    InvalidParameter = 2,
    OutOfMemory = 3,
    ObjectBusy = 4,
    InsufficientBuffer = 5,
    NotImplemented = 6,
    Win32Error = 7,
    WrongState = 8,
    Aborted = 9,
    FileNotFound = 10,
    ValueOverflow = 11,
    AccessDenied = 12,
    UnknownImageFormat = 13,
    FontFamilyNotFound = 14,
    FontStyleNotFound = 15,
    NotTrueTypeFont = 16,
    UnsupportedGdiplusVersion = 17,
    GdiplusNotInitialized = 18,
    PropertyNotFound = 19,
    PropertyNotSupported = 20,
    ProfileNotFound = 21
};
enum FontStyle {
    FontStyleRegular = 0,
    FontStyleBold = 1,
    FontStyleItalic = 2,
    FontStyleBoldItalic = 3,
    FontStyleUnderline = 4,
    FontStyleStrikeout = 8
};
enum DashCap { DashCapFlat = 0, DashCapRound = 2, DashCapTriangle = 3 };
enum DashStyle {
    DashStyleSolid, // 0
    DashStyleDash, // 1
    DashStyleDot, // 2
    DashStyleDashDot, // 3
    DashStyleDashDotDot, // 4
    DashStyleCustom // 5
};
enum BrushType {
    BrushTypeNone = -1,
    BrushTypeSolidColor = 0,
    BrushTypeHatchFill = 1,
    BrushTypeTextureFill = 2,
    BrushTypePathGradient = 3,
    BrushTypeLinearGradient = 4
};
enum HatchStyle {
    HatchStyleHorizontal, // 0
    HatchStyleVertical, // 1
    HatchStyleForwardDiagonal, // 2
    HatchStyleBackwardDiagonal, // 3
    HatchStyleCross, // 4
    HatchStyleDiagonalCross, // 5
    HatchStyle05Percent, // 6
    HatchStyle10Percent, // 7
    HatchStyle20Percent, // 8
    HatchStyle25Percent, // 9
    HatchStyle30Percent, // 10
    HatchStyle40Percent, // 11
    HatchStyle50Percent, // 12
    HatchStyle60Percent, // 13
    HatchStyle70Percent, // 14
    HatchStyle75Percent, // 15
    HatchStyle80Percent, // 16
    HatchStyle90Percent, // 17
    HatchStyleLightDownwardDiagonal, // 18
    HatchStyleLightUpwardDiagonal, // 19
    HatchStyleDarkDownwardDiagonal, // 20
    HatchStyleDarkUpwardDiagonal, // 21
    HatchStyleWideDownwardDiagonal, // 22
    HatchStyleWideUpwardDiagonal, // 23
    HatchStyleLightVertical, // 24
    HatchStyleLightHorizontal, // 25
    HatchStyleNarrowVertical, // 26
    HatchStyleNarrowHorizontal, // 27
    HatchStyleDarkVertical, // 28
    HatchStyleDarkHorizontal, // 29
    HatchStyleDashedDownwardDiagonal, // 30
    HatchStyleDashedUpwardDiagonal, // 31
    HatchStyleDashedHorizontal, // 32
    HatchStyleDashedVertical, // 33
    HatchStyleSmallConfetti, // 34
    HatchStyleLargeConfetti, // 35
    HatchStyleZigZag, // 36
    HatchStyleWave, // 37
    HatchStyleDiagonalBrick, // 38
    HatchStyleHorizontalBrick, // 39
    HatchStyleWeave, // 40
    HatchStylePlaid, // 41
    HatchStyleDivot, // 42
    HatchStyleDottedGrid, // 43
    HatchStyleDottedDiamond, // 44
    HatchStyleShingle, // 45
    HatchStyleTrellis, // 46
    HatchStyleSphere, // 47
    HatchStyleSmallGrid, // 48
    HatchStyleSmallCheckerBoard, // 49
    HatchStyleLargeCheckerBoard, // 50
    HatchStyleOutlinedDiamond, // 51
    HatchStyleSolidDiamond, // 52

    HatchStyleTotal,
    HatchStyleLargeGrid = HatchStyleCross, // 4

    HatchStyleMin = HatchStyleHorizontal,
    HatchStyleMax = HatchStyleTotal - 1,
};
enum WrapMode {
    WrapModeTile, // 0
    WrapModeTileFlipX, // 1
    WrapModeTileFlipY, // 2
    WrapModeTileFlipXY, // 3
    WrapModeClamp // 4
};
enum LinearGradientMode {
    LinearGradientModeHorizontal, // 0
    LinearGradientModeVertical, // 1
    LinearGradientModeForwardDiagonal, // 2
    LinearGradientModeBackwardDiagonal // 3
};
enum LineCap {
    LineCapFlat = 0,
    LineCapSquare = 1,
    LineCapRound = 2,
    LineCapTriangle = 3,

    LineCapNoAnchor = 0x10, // corresponds to flat cap
    LineCapSquareAnchor = 0x11, // corresponds to square cap
    LineCapRoundAnchor = 0x12, // corresponds to round cap
    LineCapDiamondAnchor = 0x13, // corresponds to triangle cap
    LineCapArrowAnchor = 0x14, // no correspondence

    LineCapCustom = 0xff, // custom cap

    LineCapAnchorMask = 0xf0 // mask to check for anchor or not.
};
enum LineJoin {
    LineJoinMiter = 0,
    LineJoinBevel = 1,
    LineJoinRound = 2,
    LineJoinMiterClipped = 3
};
enum PenAlignment { PenAlignmentCenter = 0, PenAlignmentInset = 1 };
enum MatrixOrder { MatrixOrderPrepend = 0, MatrixOrderAppend = 1 };
enum ImageType {
    ImageTypeUnknown, // 0
    ImageTypeBitmap, // 1
    ImageTypeMetafile // 2
};
enum RotateFlipType {
    RotateNoneFlipNone = 0,
    Rotate90FlipNone = 1,
    Rotate180FlipNone = 2,
    Rotate270FlipNone = 3,

    RotateNoneFlipX = 4,
    Rotate90FlipX = 5,
    Rotate180FlipX = 6,
    Rotate270FlipX = 7,

    RotateNoneFlipY = Rotate180FlipX,
    Rotate90FlipY = Rotate270FlipX,
    Rotate180FlipY = RotateNoneFlipX,
    Rotate270FlipY = Rotate90FlipX,

    RotateNoneFlipXY = Rotate180FlipNone,
    Rotate90FlipXY = Rotate270FlipNone,
    Rotate180FlipXY = RotateNoneFlipNone,
    Rotate270FlipXY = Rotate90FlipNone
};
enum QualityMode {
    QualityModeInvalid = -1,
    QualityModeDefault = 0,
    QualityModeLow = 1, // Best performance
    QualityModeHigh = 2 // Best rendering quality
};
enum SmoothingMode {
    SmoothingModeInvalid = QualityModeInvalid,
    SmoothingModeDefault = QualityModeDefault,
    SmoothingModeHighSpeed = QualityModeLow,
    SmoothingModeHighQuality = QualityModeHigh,
    SmoothingModeNone,
    SmoothingModeAntiAlias,
    SmoothingModeAntiAlias8x4 = SmoothingModeAntiAlias,
    SmoothingModeAntiAlias8x8
};
enum TextRenderingHint {
    TextRenderingHintSystemDefault =
        0, // Glyph with system default rendering hint
    TextRenderingHintSingleBitPerPixelGridFit, // Glyph bitmap with hinting
    TextRenderingHintSingleBitPerPixel, // Glyph bitmap without hinting
    TextRenderingHintAntiAliasGridFit, // Glyph anti-alias bitmap with hinting
    TextRenderingHintAntiAlias, // Glyph anti-alias bitmap without hinting
    TextRenderingHintClearTypeGridFit // Glyph CT bitmap with hinting
};
// brush
struct BLBrush {
    BrushType type;
    BLRgba32 solidBrush; // 纯色
    BLPattern textureBrush; // 贴图
    BLGradient pathGradientBrush; // 渐变
    BLBrush *Clone() {
        switch(type) {
            case BrushTypeNone:
                return new BLBrush();
            case BrushTypeSolidColor:
                return new BLBrush(solidBrush.value);
            case BrushTypeHatchFill:
                return new BLBrush(textureBrush.getImage());
            case BrushTypeTextureFill:
                return new BLBrush(textureBrush);
            case BrushTypePathGradient:
            case BrushTypeLinearGradient:
                return new BLBrush(pathGradientBrush);
            default:
                return new BLBrush();
        }
    }
    BLBrush() { type = BrushTypeNone; }
    BLBrush(tjs_int color) {
        solidBrush = BLRgba32((uint32_t)(tjs_int)color);
        type = BrushTypeSolidColor;
    }
    BLBrush(BLImage img) {
        textureBrush = BLPattern(img);
        type = BrushTypeHatchFill;
    }
    BLBrush(BLPattern pat) {
        textureBrush = pat;
        type = BrushTypeTextureFill;
    }
    BLBrush(BLGradient gra) {
        pathGradientBrush = gra;
        type = BrushTypePathGradient;
    }
};
// pen
struct BLPen {
    // 配置与宽度
    BLStrokeOptions strokeOptions;
    tjs_real strokeWidth;

    // 键帽
    tjs_int startCapType = 0; // 0:BL内置键帽 1:自定义键帽
    BLPath startCap;
    tjs_int endCapType = 0;
    BLPath endCap;

    // 两种填充方式
    BLBrush *brush = nullptr;
    BLRgba32 color;

    BLPen *Clone() {
        BLPen *ret = nullptr;
        if(brush != nullptr)
            ret = new BLPen(brush, strokeWidth);
        else
            ret = new BLPen(color.value, strokeWidth);
        ret->startCapType = startCapType;
        ret->startCap = startCap;
        ret->endCapType = endCapType;
        ret->endCap = endCap;
        ret->strokeOptions = strokeOptions;
        return ret;
    }
    BLPen(BLBrush *b, tjs_real w) : brush(b), strokeWidth(w) {}
    BLPen(tjs_uint32 c, tjs_real w) : strokeWidth(w) { color = BLRgba32(c); }
};
// gdiplus struct
class PointF {
public:
    tjs_real x;
    tjs_real y;
    PointF() {};
    PointF(tjs_real _x, tjs_real _y) : x(_x), y(_y) {}

    bool Equals(PointF tgt) { return x == tgt.x && y == tgt.y; };

    PointF operator+(PointF tgt) { return PointF(x + tgt.x, y + tgt.y); }
    PointF operator-(PointF tgt) { return PointF(x - tgt.x, y - tgt.y); }
    PointF operator*(tjs_real tgt) { return PointF(x * tgt, y * tgt); }
    PointF operator/(tjs_real tgt) { return PointF(x / tgt, y / tgt); }
};
class RectF {
public:
    tjs_real x = 0, y = 0, w = 0, h = 0;

    RectF() {};
    RectF(tjs_real _x, tjs_real _y, tjs_real _w, tjs_real _h) {
        x = _x, y = _y, w = _w, h = _h;
    }

    void reset(tjs_real _x, tjs_real _y, tjs_real _w, tjs_real _h) {
        x = _x, y = _y, w = _w, h = _h;
    }
    tjs_real GetLeft() { return x; }
    tjs_real GetTop() { return y; }
    tjs_real GetRight() { return x + w; }
    tjs_real GetBottom() { return y + h; }
    void GetLocation(PointF &p) { p.x = x, p.y = y; }
    void GetBounds(RectF &p) { p.x = x, p.y = y, p.w = x + w, p.h = x + h; }
    RectF Clone() { return RectF(x, y, w, h); }
    bool Equals(RectF p) {
        return x == p.x && y == p.y && w == p.w && h == p.h;
    };
    void Inflate(tjs_real width, tjs_real height) {
        x -= width;
        y -= height;
        w += width * 2;
        h += height * 2;
    }
    void Inflate(const PointF &point) { Inflate(point.x, point.y); }
    bool IntersectsWith(const RectF &rect) {
        return !(rect.x > x + w || rect.x + rect.w < x || rect.y > y + h ||
                 rect.y + rect.h < y);
    }
    bool IsEmptyArea() const { return w <= 0 || h <= 0; }
    void Offset(tjs_real dx, tjs_real dy) {
        x += dx;
        y += dy;
    }
    static bool Union(RectF &ret, const RectF &a, const RectF &b) {
        float minX = std::min(a.x, b.x);
        float minY = std::min(a.y, b.y);
        float maxX = std::max(a.x + a.w, b.x + b.w);
        float maxY = std::max(a.y + a.h, b.y + b.h);
        ret = RectF(minX, minY, maxX - minX, maxY - minY);
        return true;
    }
    static bool Intersect(RectF &ret, const RectF &a, const RectF &b) {
        tjs_real left = std::max(a.x, b.x);
        tjs_real top = std::max(a.y, b.y);
        tjs_real right = std::min(a.x + a.w, b.x + b.w);
        tjs_real bottom = std::min(a.y + a.h, b.y + b.h);

        if(right > left && bottom > top) {
            ret = RectF(left, top, right - left, bottom - top);
            return true;
        }
        ret = RectF();
        return false;
    }
    bool Contains(tjs_real px, tjs_real py) {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    bool Contains(const RectF &rect) {
        return rect.x >= x && rect.x + rect.w <= x + w && rect.y >= y &&
            rect.y + rect.h <= y + h;
    }
};
class GdipMatrix {
public:
    BLMatrix2D _core;
    tjs_real offsetx = 0, offsety = 0;
    tjs_real rotation = 0;
    tjs_real scale = 1;
    tjs_real shearx = 0, sheary = 0;

    GdipMatrix() {}
    GdipMatrix(BLMatrix2D m) : _core(m) {}
    GdipMatrix *Clone() { return new GdipMatrix(_core); }

    tjs_real OffsetX() { return offsetx; }
    tjs_real OffsetY() { return offsety; }
    bool Equals(const GdipMatrix &other) {
        return memcmp(&_core, &other._core, sizeof(BLMatrix2D)) == 0;
    }
    bool getElements(std::vector<tjs_real> &elements) {
        BLMatrix2D m = _core;
        elements.resize(6);
        elements[0] = m.m00; // m11
        elements[1] = m.m01; // m12
        elements[2] = m.m10; // m21
        elements[3] = m.m11; // m22
        elements[4] = m.m20; // dx
        elements[5] = m.m21; // dy
        return true;
    }
    Status SetElements(tjs_real m11, tjs_real m12, tjs_real m21, tjs_real m22,
                       tjs_real dx, tjs_real dy) {
        _core.reset();
        _core.m00 = m11;
        _core.m01 = m12;
        _core.m10 = m21;
        _core.m11 = m22;
        _core.m20 = dx;
        _core.m21 = dy;
        updateProperties();
        return Ok;
    }
    Status GetLastStatus() { return Ok; }
    Status Invert() {
        BLMatrix2D inverse;
        if(BLMatrix2D::invert(inverse, _core) == BL_SUCCESS) {
            _core = inverse;
            updateProperties();
            return Ok;
        }
        return InvalidParameter;
    }
    bool IsIdentity() {
        BLMatrix2D identity = BLMatrix2D::makeIdentity();
        return _core == identity;
    }
    bool IsInvertible() {
        BLMatrix2D inverse;
        return BLMatrix2D::invert(inverse, _core) == BL_SUCCESS;
    }
    Status Multiply(GdipMatrix *matrix,
                    MatrixOrder order = MatrixOrderPrepend) {
        if(!matrix)
            return InvalidParameter;

        if(order == MatrixOrderPrepend) {
            _core.transform(matrix->_core);
        } else {
            BLMatrix2D temp = matrix->_core;
            temp.transform(_core);
            _core = temp;
        }
        updateProperties();
        return Ok;
    }

    void Reset() {
        _core.reset();
        offsetx = offsety = 0;
        rotation = 0;
        scale = 1;
        shearx = sheary = 0;
    }

    void Rotate(tjs_real angle, MatrixOrder order = MatrixOrderPrepend) {
        float rad = angle * M_PI / 180.0f;
        rotation += rad;

        if(order == MatrixOrderPrepend) {
            _core.rotate(rad);
        } else {
            BLMatrix2D rot;
            rot.rotate(rad);
            rot.transform(_core);
            _core = rot;
        }
        updateOffset();
    }

    void RotateAt(tjs_real angle, const PointF &center,
                  MatrixOrder order = MatrixOrderPrepend) {
        float rad = angle * M_PI / 180.0f;

        if(order == MatrixOrderPrepend) {
            _core.translate(-center.x, -center.y);
            _core.rotate(rad);
            _core.translate(center.x, center.y);
        } else {
            BLMatrix2D mat;
            mat.translate(-center.x, -center.y);
            mat.rotate(rad);
            mat.translate(center.x, center.y);
            mat.transform(_core);
            _core = mat;
        }
        rotation += rad;
        updateProperties();
    }

    // 缩放
    void Scale(tjs_real sx, tjs_real sy,
               MatrixOrder order = MatrixOrderPrepend) {
        scale *= std::max(fabs(sx), fabs(sy)); // 取最大缩放

        if(order == MatrixOrderPrepend) {
            _core.scale(sx, sy);
        } else {
            BLMatrix2D scl;
            scl.scale(sx, sy);
            scl.transform(_core);
            _core = scl;
        }
        updateOffset();
    }

    void Shear(tjs_real shx, tjs_real shy,
               MatrixOrder order = MatrixOrderPrepend) {
        shearx += shx;
        sheary += shy;

        BLMatrix2D shearMat(1, shy, shx, 1, 0, 0);

        if(order == MatrixOrderPrepend) {
            _core.transform(shearMat);
        } else {
            shearMat.transform(_core);
            _core = shearMat;
        }
        updateOffset();
    }

    void Translate(tjs_real dx, tjs_real dy,
                   MatrixOrder order = MatrixOrderPrepend) {
        offsetx += dx;
        offsety += dy;

        if(order == MatrixOrderPrepend) {
            _core.translate(dx, dy);
        } else {
            BLMatrix2D trans;
            trans.translate(dx, dy);
            trans.transform(_core);
            _core = trans;
        }
    }

    PointF TransformPoint(const PointF &point) {
        BLPoint p(point.x, point.y);
        _core.mapPoint(p);
        return PointF(p.x, p.y);
    }

    void TransformPoints(std::vector<PointF> &points) {
        for(auto &p : points) {
            BLPoint bp(p.x, p.y);
            _core.mapPoint(bp);
            p.x = bp.x;
            p.y = bp.y;
        }
    }

    PointF TransformVector(const PointF &vector) {
        BLPoint v(vector.x, vector.y);
        BLMatrix2D m = _core;
        m.m20 = m.m21 = 0;
        m.mapPoint(v);
        return PointF(v.x, v.y);
    }

private:
    void updateProperties() {
        offsetx = _core.m20;
        offsety = _core.m21;
        float m00 = _core.m00;
        float m01 = _core.m01;
        float m10 = _core.m10;
        float m11 = _core.m11;
        scale = sqrt(m00 * m00 + m01 * m01);

        if(fabs(m00) > 1e-6) {
            rotation = atan2(m01, m00);
        } else {
            rotation = atan2(-m10, m11);
        }
        shearx = m10 / scale;
        sheary = m01 / scale;
    }

    void updateOffset() {
        offsetx = _core.m20;
        offsety = _core.m21;
    }
};
class Appearance;
class GdipImage {
public:
    int type = 0; // 0:位图 1:矢量图
    BLMatrix2D transMtx;
    tjs_int width = 0, height = 0;
    // 位图数据
    BLImage _core;
    // 矢量图数据
    struct Info {
        Appearance *app;
        BLPath *path;
    };
    BLRgba32 bgColor;
    std::vector<Info> vectorGraph;

    GdipImage(tjs_int w, tjs_int h) : width(w), height(h) {
        type = 1;
        transMtx.reset();
    }
    GdipImage(BLImage _i) : _core(_i) {
        type = 0;
        width = _i.width();
        height = _i.height();
    }

    GdipImage *Clone() {
        GdipImage *cloned = nullptr;
        if(type == 0) {
            cloned = new GdipImage(_core);
        } else if(type == 1) {
            cloned = new GdipImage(width, height);
            // 深拷贝？
            cloned->vectorGraph = vectorGraph;
            cloned->transMtx = transMtx;
        }
        return cloned;
    }

    RectF GetBounds() {
        if(type == 0) {
            if(_core.empty())
                return RectF(0, 0, 0, 0);
            return RectF(0, 0, static_cast<tjs_real>(_core.width()),
                         static_cast<tjs_real>(_core.height()));
        } else if(type == 1) {
            RectF last(0, 0, 0, 0);
            for(auto itm : vectorGraph) {
                if(itm.path) {
                    BLBox bounding(0, 0, 0, 0);
                    itm.path->getBoundingBox(&bounding);
                    RectF org = last;
                    RectF::Union(last, org,
                                 RectF(bounding.x0, bounding.y0,
                                       bounding.x1 - bounding.x0,
                                       bounding.y1 - bounding.y0));
                }
            }
            return last;
        }
        return RectF(0, 0, 0, 0);
    }

    tjs_uint GetFlags() {
        if(type == 0) {
            if(_core.format() == BL_FORMAT_PRGB32 ||
               _core.format() == BL_FORMAT_XRGB32) {
                return ImageFlagsHasAlpha | ImageFlagsColorSpaceRGB;
            }
        } else if(type == 1)
            return ImageFlagsReadOnly | ImageFlagsColorSpaceRGB;
        return ImageFlagsNone;
    }

    tjs_int GetHeight() { return height; }
    tjs_int GetWidth() { return width; }

    tjs_real GetHorizontalResolution() {
        // Blend2D 没有DPI信息，返回默认值
        return 96.0f;
    }

    Status GetLastStatus() { return _core.empty() ? GenericError : Ok; }

    tjs_int GetPixelFormat() {
        switch(_core.format()) {
            case BL_FORMAT_PRGB32:
                return PixelFormat32bppARGB;
            case BL_FORMAT_XRGB32:
                return PixelFormat32bppRGB;
            case BL_FORMAT_A8:
                return PixelFormat8bppIndexed;
            default:
                return PixelFormatUndefined;
        }
    }

    ImageType GetType() {
        if(type == 0)
            return _core.empty() ? ImageTypeUnknown : ImageTypeBitmap;
        else if(type == 1)
            return ImageTypeMetafile;
        return ImageTypeUnknown;
    }

    tjs_real GetVerticalResolution() { return 96.0f; }

    Status RotateFlip(RotateFlipType type) {
        switch(type) {
            case RotateNoneFlipNone:
                return Ok;
            case Rotate90FlipNone: {
                transMtx = BLMatrix2D::makeRotation(M_PI * 0.5);
                break;
            }
            case Rotate180FlipNone: {
                transMtx = BLMatrix2D::makeRotation(M_PI);
                break;
            }
            case Rotate270FlipNone: {
                transMtx = BLMatrix2D::makeRotation(M_PI * 1.5);
                break;
            }
            case RotateNoneFlipX: {
                transMtx = BLMatrix2D::makeScaling(-1, 1);
                break;
            }
            case Rotate90FlipX: {
                transMtx = BLMatrix2D::makeRotation(M_PI * 0.5);
                transMtx.transform(BLMatrix2D::makeScaling(-1, 1));
                break;
            }
            case RotateNoneFlipY: {
                transMtx = BLMatrix2D::makeScaling(1, -1);
                break;
            }
            default:
                return NotImplemented;
        }
        return Ok;
    }
};

/**
 * GDIPlus 固有処理用
 */
struct GdiPlus {
    /**
     * プライベートフォントの追加
     * @param fontFileName フォントファイル名
     */
    static void addPrivateFont(const tjs_char *fontFileName);

    /**
     * フォントファミリー名を取得
     * @param privateOnly true ならプライベートフォントのみ取得
     */
    static tTJSVariant getFontList(bool privateOnly);
};

/**
 * フォント情報
 */
class FontInfo {
    friend class LayerExDraw;

protected:
    FT_Face ftFace{};
    // FontFamily* fontFamily; //< フォントフェイス
    ttstr familyName;
    tjs_real emSize; //< フォントサイズ
    tjs_int style; //< フォントスタイル
    bool gdiPlusUnsupportedFont; //< GDI+未サポートフォント
    bool forceSelfPathDraw; // 自前パス描画強制
    mutable bool propertyModified;
    mutable tjs_real ascent;
    mutable tjs_real descent;
    mutable tjs_real lineSpacing;
    mutable tjs_real ascentLeading;
    mutable tjs_real descentLeading;

    /**
     * フォント情報のクリア
     */
    void clear();

public:
    FontInfo();
    /**
     * コンストラクタ
     * @param familyName フォントファミリー
     * @param emSize フォントのサイズ
     * @param style フォントスタイル
     */
    FontInfo(const tjs_char *familyName, tjs_real emSize, tjs_int style);
    FontInfo(const FontInfo &orig);

    /**
     * デストラクタ
     */
    virtual ~FontInfo();

    void setFamilyName(const tjs_char *familyName);
    const tjs_char *getFamilyName() { return familyName.c_str(); }
    void setEmSize(tjs_real emSize);
    tjs_real getEmSize() { return emSize; }
    void setStyle(tjs_int style) {
        this->style = style;
        propertyModified = true;
    }
    tjs_int getStyle() { return style; }
    void setForceSelfPathDraw(bool state);
    bool getForceSelfPathDraw(void) const;
    bool getSelfPathDraw(void) const;

    void updateSizeParams(void) const;
    tjs_real getAscent() const;
    tjs_real getDescent() const;
    tjs_real getAscentLeading() const;
    tjs_real getDescentLeading() const;
    tjs_real getLineSpacing() const;

    BLFont getBLFont() const;

private:
    std::unique_ptr<FT_Byte[]> buffer{};
};

/**
 * 描画外観情報
 */
class Appearance {
    friend class LayerExDraw;

public:
    // 描画情報
    struct DrawInfo {
        int type; // 0:ブラシ 1:ペン
        void *info; // 情報オブジェクト
        tjs_real ox; //< 表示オフセット
        tjs_real oy; //< 表示オフセット
        DrawInfo() : ox(0), oy(0), type(0), info(NULL) {}
        DrawInfo(tjs_real ox, tjs_real oy, void *data, int ty) :
            ox(ox), oy(oy), type(ty), info(data) {}
        DrawInfo(const DrawInfo &orig) {
            ox = orig.ox;
            oy = orig.oy;
            type = orig.type;
            if(orig.info) {
                switch(type) {
                    case 0:
                        info = (void *)((BLPen *)orig.info)->Clone();
                        break;
                    case 1:
                        info = (void *)((BLBrush *)orig.info)->Clone();
                        break;
                }
            } else {
                info = NULL;
            }
        }
        virtual ~DrawInfo() {
            if(info) {
                switch(type) {
                    case 0:
                        delete(BLPen *)info;
                        break;
                    case 1:
                        delete(BLBrush *)info;
                        break;
                }
            }
        }
    };
    std::vector<DrawInfo> drawInfos;

public:
    Appearance();
    virtual ~Appearance();
    Appearance *Clone() const;

    /**
     * 情報のクリア
     */
    void clear();

    /**
     * ブラシの追加
     * @param colorOrBrush ARGB色指定またはブラシ情報（辞書）
     * @param ox 表示オフセットX
     * @param oy 表示オフセットY
     */
    void addBrush(tTJSVariant colorOrBrush, tjs_real ox = 0, tjs_real oy = 0);

    /**
     * ペンの追加
     * @param colorOrBrush ARGB色指定またはブラシ情報（辞書）
     * @param widthOrOption ペン幅またはペン情報（辞書）
     * @param ox 表示オフセットX
     * @param oy 表示オフセットY
     */
    void addPen(tTJSVariant colorOrBrush, tTJSVariant widthOrOption,
                tjs_real ox = 0, tjs_real oy = 0);

protected:
    /**
     * LineCapの取得
     */
    bool getLineCap(tTJSVariant &in, BLStrokeCap &cap, BLPath &custom,
                    tjs_real pw);
};

/**
 * 描画外観情報
 */
class Path {
    friend class LayerExDraw;
    bool figureStarted = false;

public:
    Path();
    virtual ~Path();
    void startFigure();
    void closeFigure();
    void drawArc(tjs_real x, tjs_real y, tjs_real width, tjs_real height,
                 tjs_real startAngle, tjs_real sweepAngle);
    void drawBezier(tjs_real x1, tjs_real y1, tjs_real x2, tjs_real y2,
                    tjs_real x3, tjs_real y3, tjs_real x4, tjs_real y4);
    void drawBeziers(tTJSVariant points);
    void drawClosedCurve(tTJSVariant points);
    void drawClosedCurve2(tTJSVariant points, tjs_real tension);
    void drawCurve(tTJSVariant pointstjs_real);
    void drawCurve2(tTJSVariant points, tjs_real tension);
    void drawCurve3(tTJSVariant points, int offset, int numberOfSegments,
                    tjs_real tension);
    void drawPie(tjs_real x, tjs_real y, tjs_real width, tjs_real height,
                 tjs_real startAngle, tjs_real sweepAngle);
    void drawEllipse(tjs_real x, tjs_real y, tjs_real width, tjs_real height);
    void drawLine(tjs_real x1, tjs_real y1, tjs_real x2, tjs_real y2);
    void drawLines(tTJSVariant points);
    void drawPolygon(tTJSVariant points);
    void drawRectangle(tjs_real x, tjs_real y, tjs_real width, tjs_real height);
    void drawRectangles(tTJSVariant rects);

protected:
    BLPath path;
};

/*
 * アウトラインベースのテキスト描画メソッドの追加
 */
class LayerExDraw : public layerExBase_GL {
protected:
    // 情報保持用
    GeometryT width, height;
    BufferT buffer;
    PitchT pitch;

    // クリップ情報
    // ObjectT   _pClipLeft, _pClipTop, _pClipWidth, _pClipHeight;
    GeometryT clipLeft, clipTop, clipWidth, clipHeight;

    /// レイヤを参照するビットマップ
    BLImage *bitmap;
    /// レイヤに対して描画するコンテキスト
    BLContext *context;

    // Transform 指定
    BLMatrix2D transform;
    BLMatrix2D viewTransform;
    BLMatrix2D calcTransform;

protected:
    // 描画スムージング指定
    SmoothingMode smoothingMode;
    // drawString のアンチエイリアス指定
    TextRenderingHint textRenderingHint;

public:
    int getSmoothingMode() { return (int)smoothingMode; }
    void setSmoothingMode(int mode) { smoothingMode = (SmoothingMode)mode; }

    int getTextRenderingHint() { return (int)textRenderingHint; }
    void setTextRenderingHint(int hint) {
        textRenderingHint = (TextRenderingHint)hint;
    }

protected:
    /// 描画内容記録用メタファイル
    GdipImage *metaGraphics;
    std::string cvName;

    /// 描画内容記録用メタファイル
    bool updateWhenDraw;
    void updateRect(RectF &rect);

public:
    void setUpdateWhenDraw(int updateWhenDraw) {
        this->updateWhenDraw = updateWhenDraw != 0;
    }
    int getUpdateWhenDraw() { return updateWhenDraw ? 1 : 0; }

    inline operator GdipImage *() const { return new GdipImage(*bitmap); }
    // inline operator Bitmap*() const { return bitmap; }
    // inline operator Graphics*() const { return graphics; }
    inline operator const GdipImage *() const { return new GdipImage(*bitmap); }
    // inline operator const Bitmap*() const { return bitmap; }
    // inline operator const Graphics*() const { return graphics; }

    template <class T>
    struct BridgeFunctor {
        T *operator()(LayerExDraw *p) const { return (T *)*p; }
    };

public:
    LayerExDraw(DispatchT obj);
    ~LayerExDraw();
    virtual void reset();

    // ------------------------------------------------------------------
    // 描画パラメータ指定
    // ------------------------------------------------------------------

protected:
    void updateViewTransform();
    void updateTransform();

public:
    /**
     * 表示トランスフォームの指定
     */
    void setViewTransform(const GdipMatrix *transform);
    void resetViewTransform();
    void rotateViewTransform(tjs_real angle);
    void scaleViewTransform(tjs_real sx, tjs_real sy);
    void translateViewTransform(tjs_real dx, tjs_real dy);

    /**
     * トランスフォームの指定
     * @param matrix トランスフォームマトリックス
     */
    void setTransform(const GdipMatrix *transform);
    void resetTransform();
    void rotateTransform(tjs_real angle);
    void scaleTransform(tjs_real sx, tjs_real sy);
    void translateTransform(tjs_real dx, tjs_real dy);

    // ------------------------------------------------------------------
    // 描画メソッド群
    // ------------------------------------------------------------------

protected:
    /**
     * パスの更新領域情報を取得
     * @param app 表示表現
     * @param path 描画するパス
     * @return 更新領域情報
     */
    RectF getPathExtents(const Appearance *app, const BLPath *path);

    /**
     * パスの描画用下請け処理
     * @param graphics 描画先
     * @param pen 描画用ペン
     * @param matrix 描画位置調整用matrix
     * @param path 描画内容
     */
    void draw(BLImage *app, const BLPen *pen, const BLMatrix2D *matrix,
              const BLPath *path);

    /**
     * 塗りの描画用下請け処理
     * @param graphics 描画先
     * @param brush 描画用ブラシ
     * @param matrix 描画位置調整用matrix
     * @param path 描画内容
     */
    void fill(BLImage *ctx, const BLBrush *brush, const BLMatrix2D *matrix,
              const BLPath *path);

    /**
     * パスの描画
     * @param app アピアランス
     * @param path 描画するパス
     * @return 更新領域情報
     */
    RectF _drawPath(const Appearance *app, const BLPath *path);

    /**
     * グリフアウトラインの取得
     * @param font フォント
     * @param offset オフセット
     * @param path グリフを書き出すパス
     * @param glyph 描画するグリフ
     */
    void getGlyphOutline(const FontInfo *font, PointF &offset, BLPath *path,
                         tjs_uint glyph);

    /*
     * テキストアウトラインの取得
     * @param font フォント
     * @param offset オフセット
     * @param path グリフを書き出すパス
     * @param text 描画するテキスト
     */
    void getTextOutline(const FontInfo *font, PointF &offset, BLPath *path,
                        ttstr text);

public:
    /**
     * 画面の消去
     * @param argb 消去色
     */
    void clear(tjs_uint32 argb);

    /**
     * パスの描画
     * @param app アピアランス
     * @param path パス
     */
    RectF drawPath(const Appearance *app, const Path *path);

    /**
     * 円弧の描画
     * @param app アピアランス
     * @param x 左上座標
     * @param y 左上座標
     * @param width 横幅
     * @param height 縦幅
     * @param startAngle 時計方向円弧開始位置
     * @param sweepAngle 描画角度
     * @return 更新領域情報
     */
    RectF drawArc(const Appearance *app, tjs_real x, tjs_real y, tjs_real width,
                  tjs_real height, tjs_real startAngle, tjs_real sweepAngle);

    /**
     * 円錐の描画
     * @param app アピアランス
     * @param x 左上座標
     * @param y 左上座標
     * @param width 横幅
     * @param height 縦幅
     * @param startAngle 時計方向円弧開始位置
     * @param sweepAngle 描画角度
     * @return 更新領域情報
     */
    RectF drawPie(const Appearance *app, tjs_real x, tjs_real y, tjs_real width,
                  tjs_real height, tjs_real startAngle, tjs_real sweepAngle);

    /**
     * ベジェ曲線の描画
     * @param app アピアランス
     * @param x1
     * @param y1
     * @param x2
     * @param y2
     * @param x3
     * @param y3
     * @param x4
     * @param y4
     * @return 更新領域情報
     */
    RectF drawBezier(const Appearance *app, tjs_real x1, tjs_real y1,
                     tjs_real x2, tjs_real y2, tjs_real x3, tjs_real y3,
                     tjs_real x4, tjs_real y4);

    /**
     * 連続ベジェ曲線の描画
     * @param app アピアランス
     * @param points 点の配列
     * @return 更新領域情報
     */
    RectF drawBeziers(const Appearance *app, tTJSVariant points);

    /**
     * Closed cardinal spline の描画
     * @param app アピアランス
     * @param points 点の配列
     * @return 更新領域情報
     */
    RectF drawClosedCurve(const Appearance *app, tTJSVariant points);

    /**
     * Closed cardinal spline の描画
     * @param app アピアランス
     * @param points 点の配列
     * @pram tension tension
     * @return 更新領域情報
     */
    RectF drawClosedCurve2(const Appearance *app, tTJSVariant points,
                           tjs_real tension);

    /**
     * cardinal spline の描画
     * @param app アピアランス
     * @param points 点の配列
     * @return 更新領域情報
     */
    RectF drawCurve(const Appearance *app, tTJSVariant points);

    /**
     * cardinal spline の描画
     * @param app アピアランス
     * @param points 点の配列
     * @parma tension tension
     * @return 更新領域情報
     */
    RectF drawCurve2(const Appearance *app, tTJSVariant points,
                     tjs_real tension);

    /**
     * cardinal spline の描画
     * @param app アピアランス
     * @param points 点の配列
     * @param offset
     * @param numberOfSegment
     * @param tension tension
     * @return 更新領域情報
     */
    RectF drawCurve3(const Appearance *app, tTJSVariant points, int offset,
                     int numberOfSegments, tjs_real tension);

    /**
     * 楕円の描画
     * @param app アピアランス
     * @param x
     * @param y
     * @param width
     * @param height
     * @return 更新領域情報
     */
    RectF drawEllipse(const Appearance *app, tjs_real x, tjs_real y,
                      tjs_real width, tjs_real height);

    /**
     * 線分の描画
     * @param app アピアランス
     * @param x1 始点X座標
     * @param y1 始点Y座標
     * @param x2 終点X座標
     * @param y2 終点Y座標
     * @return 更新領域情報
     */
    RectF drawLine(const Appearance *app, tjs_real x1, tjs_real y1, tjs_real x2,
                   tjs_real y2);

    /**
     * 連続線分の描画
     * @param app アピアランス
     * @param points 点の配列
     * @return 更新領域情報
     */
    RectF drawLines(const Appearance *app, tTJSVariant points);

    /**
     * 多角形の描画
     * @param app アピアランス
     * @param points 点の配列
     * @return 更新領域情報
     */
    RectF drawPolygon(const Appearance *app, tTJSVariant points);

    /**
     * 矩形の描画
     * @param app アピアランス
     * @param x
     * @param y
     * @param width
     * @param height
     * @return 更新領域情報
     */
    RectF drawRectangle(const Appearance *app, tjs_real x, tjs_real y,
                        tjs_real width, tjs_real height);

    /**
     * 複数矩形の描画
     * @param app アピアランス
     * @param rects 矩形情報の配列
     * @return 更新領域情報
     */
    RectF drawRectangles(const Appearance *app, tTJSVariant rects);

    /**
     * 文字列の描画
     * @param font フォント
     * @param app アピアランス
     * @param x 描画位置X
     * @param y 描画位置Y
     * @param text 描画テキスト
     * @return 更新領域情報
     */
    RectF drawPathString(const FontInfo *font, const Appearance *app,
                         tjs_real x, tjs_real y, const tjs_char *text);

    /**
     * 文字列の描画(OpenTypeのPostScriptフォント対応)
     * @param font フォント
     * @param app アピアランス
     * @param x 描画位置X
     * @param y 描画位置Y
     * @param text 描画テキスト
     * @return 更新領域情報
     */
    RectF drawPathString2(const FontInfo *font, const Appearance *app,
                          tjs_real x, tjs_real y, const tjs_char *text);

    // -------------------------------------------------------------------------------

    /**
     * 文字列の描画
     * @param font フォント
     * @param app アピアランス
     * @param x 描画位置X
     * @param y 描画位置Y
     * @param text 描画テキスト
     * @return 更新領域情報
     */
    RectF drawString(const FontInfo *font, const Appearance *app, tjs_real x,
                     tjs_real y, const tjs_char *text);

    /**
     * 文字列の描画更新領域情報の取得
     * @param font フォント
     * @param text 描画テキスト
     * @return 更新領域情報の辞書 left, top, width, height
     */
    RectF measureString(const FontInfo *font, const tjs_char *text);

    /**
     * 文字列にぴったりと接っする矩形の取得
     * @param font フォント
     * @param text 描画テキスト
     * @return 領域情報の辞書 left, top, width, height
     */
    RectF measureStringInternal(const FontInfo *font, const tjs_char *text);

    /**
     * 文字列の描画更新領域情報の取得(OpenTypeのPostScriptフォント対応)
     * @param font フォント
     * @param text 描画テキスト
     * @return 更新領域情報の辞書 left, top, width, height
     */
    RectF measureString2(const FontInfo *font, const tjs_char *text);

    /**
     * 文字列にぴったりと接っする矩形の取得(OpenTypeのPostScriptフォント対応)
     * @param font フォント
     * @param text 描画テキスト
     * @return 領域情報の辞書 left, top, width, height
     */
    RectF measureStringInternal2(const FontInfo *font, const tjs_char *text);

    // -----------------------------------------------------------------------------

    /**
     * 画像の描画。コピー先は元画像の Bounds を配慮した位置、サイズは Pixel
     * 指定になります。
     * @param x コピー先原点X
     * @param y コピー先原点Y
     * @param image コピー元画像
     * @return 更新領域情報
     */
    RectF drawImage(tjs_real x, tjs_real y, GdipImage *src);

    /**
     * 画像の矩形コピー
     * @param dleft コピー先左端
     * @param dtop  コピー先上端
     * @param src コピー元画像
     * @param sleft 元矩形の左端
     * @param stop  元矩形の上端
     * @param swidth 元矩形の横幅
     * @param sheight  元矩形の縦幅
     * @return 更新領域情報
     */
    RectF drawImageRect(tjs_real dleft, tjs_real dtop, GdipImage *src,
                        tjs_real sleft, tjs_real stop, tjs_real swidth,
                        tjs_real sheight);

    /**
     * 画像の拡大縮小コピー
     * @param dleft コピー先左端
     * @param dtop  コピー先上端
     * @param dwidth コピー先の横幅
     * @param dheight  コピー先の縦幅
     * @param src コピー元画像
     * @param sleft 元矩形の左端
     * @param stop  元矩形の上端
     * @param swidth 元矩形の横幅
     * @param sheight  元矩形の縦幅
     * @return 更新領域情報
     */
    RectF drawImageStretch(tjs_real dleft, tjs_real dtop, tjs_real dwidth,
                           tjs_real dheight, GdipImage *src, tjs_real sleft,
                           tjs_real stop, tjs_real swidth, tjs_real sheight);

    /**
     * 画像のアフィン変換コピー
     * @param src コピー元画像
     * @param sleft 元矩形の左端
     * @param stop  元矩形の上端
     * @param swidth 元矩形の横幅
     * @param sheight  元矩形の縦幅
     * @param affine アフィンパラメータの種類(true:変換行列, false:座標指定),
     * @return 更新領域情報
     */
    RectF drawImageAffine(GdipImage *src, tjs_real sleft, tjs_real stop,
                          tjs_real swidth, tjs_real sheight, bool affine,
                          tjs_real A, tjs_real B, tjs_real C, tjs_real D,
                          tjs_real E, tjs_real F);

    // ------------------------------------------------
    // メタファイル操作
    // ------------------------------------------------

protected:
    /**
     * 記録情報の生成
     */
    void createRecord();

    /**
     * 記録情報の生成
     */
    void recreateRecord();

    /**
     * 記録情報の破棄
     */
    void destroyRecord();

    /**
     * 再描画用
     */
    bool redraw(GdipImage *image);

public:
    /**
     * @param record 描画内容を記録するかどうか
     */
    void setRecord(bool record);

    /**
     * @return record 描画内容を記録するかどうか
     */
    bool getRecord() { return metaGraphics != NULL; }

    /**
     * 記録内容を Image として取得
     * @return 成功したら true
     */
    GdipImage *getRecordImage();

    /**
     * 記録内容の再描画
     * @return 再描画したら true
     */
    bool redrawRecord();

    /**
     * 記録内容の保存
     * @param filename 保存ファイル名
     * @return 成功したら true
     */
    bool saveRecord(const tjs_char *filename);

    /**
     * 記録内容の読み込み
     * @param filename 読み込みファイル名
     * @return 成功したら true
     */
    bool loadRecord(const tjs_char *filename);
};

#endif
