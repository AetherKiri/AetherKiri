#include <algorithm>
#include <spdlog/spdlog.h>
#include <cmath>
#include <filesystem>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

#include "common/Defer.h"
#include "ncbind.hpp"
#include "LayerExDraw.hpp"
#include "FontImpl.h"
#include "WindowIntf.h"
#include "impl/DrawDevice.h"
#include <freetype/freetype.h>

#include "FontImpl.h"

using namespace layerex;
using namespace libgdiplus;

static layerExBase::GeometryT recordMaxGeometry(layerExBase::GeometryT a,
                                                layerExBase::GeometryT b) {
    return a > b ? a : b;
}

static REAL recordMinReal(REAL a, REAL b) { return a < b ? a : b; }
static REAL recordMaxReal(REAL a, REAL b) { return a > b ? a : b; }

static bool pointsAlmostEqual(const GpPointF &a, const GpPointF &b) {
    return std::fabs(a.X - b.X) <= 0.01f && std::fabs(a.Y - b.Y) <= 0.01f;
}

static bool pathHasClosedSubpath(GpPath *path) {
    if(!path) {
        return false;
    }
    int count = 0;
    if(GdipGetPointCount(path, &count) != Ok || count <= 0) {
        return false;
    }
    std::vector<BYTE> types(static_cast<size_t>(count));
    if(GdipGetPathTypes(path, types.data(), count) != Ok) {
        return false;
    }
    for(BYTE type : types) {
        if((type & PathPointTypeCloseSubpath) != 0) {
            return true;
        }
    }

    std::vector<GpPointF> points(static_cast<size_t>(count));
    if(GdipGetPathPoints(path, points.data(), count) != Ok) {
        return false;
    }
    int figureStart = 0;
    for(int i = 1; i < count; ++i) {
        if((types[static_cast<size_t>(i)] & PathPointTypePathTypeMask) ==
           PathPointTypeStart) {
            if(i - figureStart > 2 &&
               pointsAlmostEqual(points[static_cast<size_t>(figureStart)],
                                 points[static_cast<size_t>(i - 1)])) {
                return true;
            }
            figureStart = i;
        }
    }
    return count - figureStart > 2 &&
        pointsAlmostEqual(points[static_cast<size_t>(figureStart)],
                          points[static_cast<size_t>(count - 1)]);
}

static GpPen *penForPathDraw(const Pen *pen, bool closedPath) {
    if(!pen || !closedPath) {
        return pen ? static_cast<GpPen *>(*pen) : nullptr;
    }

    GpPen *source = static_cast<GpPen *>(*pen);
    GpPen *clone = nullptr;
    if(GdipClonePen(source, &clone) != Ok || !clone) {
        return source;
    }
    // libgdiplus draws custom caps even on closed paths, which makes closed
    // selection boxes sprout arrow/line artifacts. Windows GDI+ does not.
    GdipSetPenCustomStartCap(clone, nullptr);
    GdipSetPenCustomEndCap(clone, nullptr);
    GdipSetPenStartCap(clone, LineCapFlat);
    GdipSetPenEndCap(clone, LineCapFlat);
    return clone;
}

static void releasePathDrawPen(const Pen *pen, GpPen *drawPen) {
    if(drawPen && pen && drawPen != static_cast<GpPen *>(*pen)) {
        GdipDeletePen(drawPen);
    }
}

// GDI+ 基本情報
static GdiplusStartupInput gdiplusStartupInput;
static ULONG_PTR gdiplusToken;

/// プライベートフォント情報
// static PrivateFontCollection *privateFontCollection;

// GDI+ 初期化
void initGdiPlus() {
    // Initialize GDI+.
    gdiplusStartupInput.GdiplusVersion = 1;
    gdiplusStartupInput.DebugEventCallback = nullptr;
    gdiplusStartupInput.SuppressBackgroundThread = FALSE;
    gdiplusStartupInput.SuppressExternalCodecs = FALSE;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);
}

// GDI+ 終了
void deInitGdiPlus() { GdiplusShutdown(gdiplusToken); }

// KAG's vector affine source uses a small virtual-image protocol for solid
// fills (for example ``solid_black.emf`` and ``solid_white.emf``).  These
// names are intentionally not files in the game's archive: on Windows the
// original GDI+ backend can resolve the corresponding metafile through its
// storage layer, while libgdiplus has no such storage-backed metafile
// resolver.  Keep the protocol generic and materialise a one-pixel ARGB
// source; drawImageAffine stretches that pixel to the requested quadrilateral
// and therefore preserves the source's colour and alpha without baking any
// scene-specific layer names into the renderer.
static bool parseVirtualSolidImageName(const tjs_char *name,
                                       std::uint32_t &argb) {
    if(!name) {
        return false;
    }

    std::string value = ttstr(name).AsStdString();
    const auto slash = value.find_last_of("/\\");
    std::string base = slash == std::string::npos ? value :
                       value.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    if(dot == std::string::npos) {
        return false;
    }
    std::string ext = base.substr(dot);
    for(char &ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if(ext != ".emf" && ext != ".wmf") {
        return false;
    }
    base.resize(dot);
    for(char &ch : base) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    constexpr std::string_view prefix = "solid_";
    if(base.rfind(prefix, 0) != 0 || base.size() == prefix.size()) {
        return false;
    }

    const std::string token = base.substr(prefix.size());
    if(token == "black") {
        argb = 0xff000000u;
        return true;
    }
    if(token == "white") {
        argb = 0xffffffffu;
        return true;
    }
    if(token == "transparent") {
        argb = 0x00000000u;
        return true;
    }

    // Accept solid_RRGGBB, solid_AARRGGBB and the same forms with a leading
    // '#'/"0x".  This covers the colour-name convention used by KAG tools
    // while remaining useful for other games that emit virtual solid images.
    std::string digits = token;
    if(!digits.empty() && digits.front() == '#') {
        digits.erase(digits.begin());
    } else if(digits.size() > 2 && digits[0] == '0' && digits[1] == 'x') {
        digits.erase(0, 2);
    }
    if(digits.size() != 6 && digits.size() != 8) {
        return false;
    }
    std::uint32_t value32 = 0;
    for(char ch : digits) {
        if(!std::isxdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
        value32 <<= 4;
        if(ch >= '0' && ch <= '9') {
            value32 |= static_cast<std::uint32_t>(ch - '0');
        } else if(ch >= 'a' && ch <= 'f') {
            value32 |= static_cast<std::uint32_t>(ch - 'a' + 10);
        } else {
            value32 |= static_cast<std::uint32_t>(ch - 'A' + 10);
        }
    }
    argb = digits.size() == 6 ? (0xff000000u | value32) : value32;
    return true;
}

static ImageClass *createVirtualSolidImage(std::uint32_t argb) {
    GpBitmap *bitmap = nullptr;
    if(GdipCreateBitmapFromScan0(1, 1, 0, PixelFormat32bppARGB, nullptr,
                                 &bitmap) != Ok || !bitmap) {
        return nullptr;
    }

    GpGraphics *graphics = nullptr;
    if(GdipGetImageGraphicsContext(reinterpret_cast<GpImage *>(bitmap),
                                   &graphics) != Ok || !graphics) {
        GdipDisposeImage(reinterpret_cast<GpImage *>(bitmap));
        return nullptr;
    }
    GdipSetCompositingMode(graphics, CompositingModeSourceCopy);
    const GpStatus status = GdipGraphicsClear(graphics, argb);
    GdipDeleteGraphics(graphics);
    if(status != Ok) {
        GdipDisposeImage(reinterpret_cast<GpImage *>(bitmap));
        return nullptr;
    }
    return new ImageClass{ reinterpret_cast<GpImage *>(bitmap), 0.0f, 0.0f,
                           true, argb };
}

static bool layerExTraceEnabled() {
    const char *value = std::getenv("AETHERKIRI_LAYEREX_TRACE");
    return value && *value && *value != '0';
}

// AffineSourceVector loads its alpha mask immediately before the synthetic
// solid_<colour>.emf source.  Keep the most recent raster canvas as the
// logical bounds for that source.  The physical sample remains 1x1; only the
// vector geometry uses this size.  Reset when the primary width changes so a
// later game/session cannot inherit the previous game's canvas.
static REAL g_virtualCanvasWidth = 0.0f;
static REAL g_virtualCanvasHeight = 0.0f;
static REAL g_virtualCanvasPrimaryWidth = 0.0f;

// The alpha stencil used by AffineSourceVector is loaded by Layer.loadImages,
// not by this plugin's Image loader.  Consequently a load-time cache in this
// translation unit cannot see it.  At the point GetBounds is requested the
// stencil is already attached as a child of the primary layer, so inspect the
// live layer tree and recover the authored canvas dimensions from the largest
// image that has the primary width but is shorter than the (oversized) backing
// layer.  This keeps the bridge resolution-independent while avoiding a
// game-specific `cut_one` filename check.
static bool findVirtualCanvasInLayerTree(tTJSNI_BaseLayer *layer,
                                         REAL primaryWidth,
                                         REAL primaryHeight,
                                         REAL preferredHeight,
                                         REAL &candidateWidth,
                                         REAL &candidateHeight,
                                         REAL &candidateDistance) {
    if(!layer) {
        return false;
    }
    if(auto *image = layer->GetMainImage()) {
        const REAL width = static_cast<REAL>(image->GetWidth());
        const REAL height = static_cast<REAL>(image->GetHeight());
        if(width == primaryWidth && height > 1.0f &&
           height <= primaryHeight) {
            const REAL distance = std::fabs(height - preferredHeight);
            if(candidateHeight <= 0.0f || distance < candidateDistance ||
               (distance == candidateDistance && height > candidateHeight)) {
                candidateWidth = width;
                candidateHeight = height;
                candidateDistance = distance;
            }
        }
    }
    const tjs_int count = static_cast<tjs_int>(layer->GetCount());
    for(tjs_int index = 0; index < count; ++index) {
        findVirtualCanvasInLayerTree(
            layer->GetChildren(index), primaryWidth, primaryHeight,
            preferredHeight, candidateWidth, candidateHeight,
            candidateDistance);
    }
    return candidateWidth > 0.0f && candidateHeight > 0.0f;
}

static void rememberVirtualCanvasCandidate(const ImageClass *image) {
    if(!image || image->IsVirtualSolid()) {
        return;
    }
    const REAL width = static_cast<REAL>(image->GetWidth());
    const REAL height = static_cast<REAL>(image->GetHeight());
    if(width <= 1.0f || height <= 1.0f) {
        return;
    }

    REAL primaryWidth = 0.0f;
    if(TVPMainWindow && TVPMainWindow->GetDrawDevice()) {
        if(auto *primary = TVPMainWindow->GetDrawDevice()->GetPrimaryLayer()) {
            primaryWidth = static_cast<REAL>(primary->GetClipWidth());
        }
    }
    if(primaryWidth > 0.0f &&
       (g_virtualCanvasPrimaryWidth <= 0.0f ||
        g_virtualCanvasPrimaryWidth != primaryWidth)) {
        g_virtualCanvasPrimaryWidth = primaryWidth;
        g_virtualCanvasWidth = 0.0f;
        g_virtualCanvasHeight = 0.0f;
    }

    // A mask canvas has the primary width and is no taller than the primary
    // clip.  Prefer the smallest matching candidate: full-size scene images
    // may be loaded later, while the vector mask is the shorter viewport.
    if(primaryWidth <= 0.0f || width == primaryWidth) {
        if(g_virtualCanvasWidth <= 0.0f ||
           (width == g_virtualCanvasWidth && height < g_virtualCanvasHeight) ||
           (g_virtualCanvasWidth <= 0.0f)) {
            g_virtualCanvasWidth = width;
            g_virtualCanvasHeight = height;
        }
    }
}

static void traceLayerEx(const char *event, const ImageClass *image,
                         const char *extra = nullptr) {
    if(!layerExTraceEnabled()) {
        return;
    }
    static int count = 0;
    if(count++ >= 256) {
        return;
    }
    if(image) {
        spdlog::info("LayerExTrace {} image={} virtual={} size={}x{} {}", event,
                     static_cast<const void *>(image), image->IsVirtualSolid(),
                     image->GetWidth(), image->GetHeight(),
                     extra ? extra : "");
    } else {
        spdlog::info("LayerExTrace {} image=null {}", event,
                     extra ? extra : "");
    }
}

// KAG's vector/metafile sources are authored around the layer origin, with
// their logical canvas centered on that origin.  The raster fallback for a
// solid_*.emf/.wmf source is only a one-pixel colour sample, so its bounds
// must preserve the authored canvas *and* its centered origin.  Returning a
// top-left origin here shifts every affine quad by half a canvas and produces
// the characteristic right/bottom black block seen on the compatibility
// backend.
static RectFClass *virtualSolidBounds(REAL width, REAL height) {
    if(width <= 0.0f || height <= 0.0f) {
        width = height = 1.0f;
    }
    return new RectFClass{ -width * 0.5f, -height * 0.5f, width, height };
}

/**
 * 画像読み込み処理
 * @param name ファイル名
 * @return 画像情報
 */
ImageClass *loadImage(const tjs_char *name) {
    ImageClass *image = nullptr;
    ttstr filename = TVPGetPlacedPath(name);
    if(filename.length()) {
        ttstr localname(TVPGetLocallyAccessibleName(filename));
        if(localname.length()) {
            // 実ファイルが存在
            const auto *n = reinterpret_cast<const WCHAR *>(localname.c_str());
            image = ImageClass::FromFile(n, false);
        }
    }
    if(image && image->GetLastStatus() != Ok) {
        delete image;
        image = nullptr;
    }

    // Capture a raster canvas before the following synthetic solid source is
    // requested by AffineSourceVector.  This is deliberately based on the
    // runtime image dimensions rather than a game-specific filename.
    rememberVirtualCanvasCandidate(image);

    if(!image) {
        std::uint32_t argb = 0;
        if(parseVirtualSolidImageName(name, argb)) {
            image = createVirtualSolidImage(argb);
            if(image && layerExTraceEnabled()) {
                spdlog::info("layerExDraw: materialized virtual solid image '{}' as 0x{:08x}",
                             ttstr(name).AsStdString(), argb);
            }
        }
    }
    traceLayerEx("loadImage", image, name ? ttstr(name).AsStdString().c_str() : "");
    return image;
}

RectFClass *getBounds(ImageClass *image) {
    // libgdiplus reports an empty world-unit bound for the synthetic bitmap
    // used by KAG's solid_*.emf/.wmf protocol on some platforms.  The vector
    // source asks GetBounds before it calculates its affine destination, so
    // an empty bound collapses all three destination points to the centre of
    // the layer.  The bitmap is a one-pixel colour sample by definition, but
    // its logical vector canvas is KAG's design canvas.  AffineSourceVector
    // uses these bounds to calculate the destination quad, then samples the
    // one-pixel bitmap across that quad.
    if(image && image->IsVirtualSolid()) {
        // The vector source is asked for its bounds while a KAG primary layer
        // already exists.  Prefer the authored raster canvas attached to the
        // live layer tree.  The backing stage layer can be taller than the
        // actual design canvas (for example 2560x1920 backing a 2560x1440
        // scene), and using that backing height shifts the affine quad down.
        if(TVPMainWindow && TVPMainWindow->GetDrawDevice()) {
            if(auto *primary = TVPMainWindow->GetDrawDevice()->GetPrimaryLayer()) {
                const REAL primaryWidth =
                    static_cast<REAL>(primary->GetWidth());
                const REAL primaryHeight =
                    static_cast<REAL>(primary->GetHeight());
                tjs_int sourceWidth = 0;
                tjs_int sourceHeight = 0;
                TVPMainWindow->GetDrawDevice()->GetSrcSize(sourceWidth,
                                                            sourceHeight);
                const REAL preferredHeight =
                    sourceWidth > 0 && sourceHeight > 0
                        ? primaryWidth * static_cast<REAL>(sourceHeight) /
                              static_cast<REAL>(sourceWidth)
                        : primaryHeight;
                REAL treeWidth = 0.0f;
                REAL treeHeight = 0.0f;
                REAL treeDistance = 0.0f;
                if(primaryWidth > 0.0f && primaryHeight > 0.0f &&
                   findVirtualCanvasInLayerTree(primary, primaryWidth,
                                                primaryHeight, preferredHeight,
                                                treeWidth, treeHeight,
                                                treeDistance)) {
                    if(layerExTraceEnabled()) {
                        spdlog::info(
                            "LayerExTrace virtual bounds from layer canvas={}x{} primary={}x{} preferredHeight={}",
                            treeWidth, treeHeight, primaryWidth, primaryHeight,
                            preferredHeight);
                    }
                    return virtualSolidBounds(treeWidth, treeHeight);
                }
            }
        }

        // Fall back to the most recent raster canvas observed by this plugin,
        // then to the primary clip.  These paths cover engines which detach
        // the stencil before querying the virtual source.
        if(g_virtualCanvasWidth > 0.0f && g_virtualCanvasHeight > 0.0f) {
            if(layerExTraceEnabled()) {
                spdlog::info("LayerExTrace virtual bounds from raster canvas={}x{}",
                             g_virtualCanvasWidth, g_virtualCanvasHeight);
            }
            return virtualSolidBounds(g_virtualCanvasWidth,
                                      g_virtualCanvasHeight);
        }
        if(TVPMainWindow && TVPMainWindow->GetDrawDevice()) {
            if(auto *primary = TVPMainWindow->GetDrawDevice()->GetPrimaryLayer()) {
                const auto width = static_cast<REAL>(primary->GetClipWidth());
                const auto height = static_cast<REAL>(primary->GetClipHeight());
                if(width > 0.0f && height > 0.0f) {
                    if(layerExTraceEnabled()) {
                        spdlog::info("LayerExTrace virtual bounds from primary clip={}x{}",
                                     width, height);
                    }
                    return virtualSolidBounds(width, height);
                }
            }
        }
        return virtualSolidBounds(1.0f, 1.0f);
    }

    RectFClass srcRect;
    Unit srcUnit;
    image->GetBounds(&srcRect, &srcUnit);
    REAL dpix = image->GetHorizontalResolution();
    REAL dpiy = image->GetVerticalResolution();

    // ピクセルに変換
    REAL x, y, width, height;
    switch(srcUnit) {
        case UnitPoint: // 3 -- Each unit is a printer's point, or
                        // 1/72 inch.
            x = srcRect.X * dpix / 72;
            y = srcRect.Y * dpiy / 72;
            width = srcRect.Width * dpix / 72;
            height = srcRect.Height * dpix / 72;
            break;
        case UnitInch: // 4 -- Each unit is 1 inch.
            x = srcRect.X * dpix;
            y = srcRect.Y * dpiy;
            width = srcRect.Width * dpix;
            height = srcRect.Height * dpix;
            break;
        case UnitDocument: // 5 -- Each unit is 1/300 inch.
            x = srcRect.X * dpix / 300;
            y = srcRect.Y * dpiy / 300;
            width = srcRect.Width * dpix / 300;
            height = srcRect.Height * dpix / 300;
            break;
        case UnitMillimeter: // 6 -- Each unit is 1 millimeter.
            x = srcRect.X * dpix / 25.4F;
            y = srcRect.Y * dpiy / 25.4F;
            width = srcRect.Width * dpix / 25.4F;
            height = srcRect.Height * dpix / 25.4F;
            break;
        default:
            x = srcRect.X;
            y = srcRect.Y;
            width = srcRect.Width;
            height = srcRect.Height;
            break;
    }
    return new RectFClass{ x, y, width, height };
}

// --------------------------------------------------------
// フォント情報
// --------------------------------------------------------

/**
 * プライベートフォントの追加
 * @param fontFileName フォントファイル名
 */
void GdiPlus::addPrivateFont(const tjs_char *fontFileName) {
    spdlog::get("plugin")->info("tjs2 script want load: {}",
                                ttstr{ fontFileName }.AsNarrowStdString());
    // HOOK Font File

    //    addFontFile("NotoSansCJK"); // 中日韩字体
    //    addFontFile("Roboto"); // 英文字体
}

/**
 * 配列にフォントのファミリー名を格納
 * @param array 格納先配列
 * @param fontCollection フォント名を取得する元の FontCollection
 */
static void addFontFamilyName(iTJSDispatch2 *array,
                              GpFontCollection *fontCollection) {
    int count;
    GdipGetFontCollectionFamilyCount(fontCollection, &count);
    auto *families = new GpFontFamily[count];
    GdipGetFontCollectionFamilyList(fontCollection, count, &families, &count);
    for(int i = 0; i < count; i++) {
        WCHAR familyName[LF_FACESIZE];
        GpFontFamily family = families[i];
        auto status = GdipGetFamilyName(&family, familyName, 0);
        if(status == Ok) {
            tTJSVariant name(reinterpret_cast<tjs_char *>(familyName)),
                *param = &name;
            array->FuncCall(0, TJS_W("add"), nullptr, nullptr, 1, &param,
                            array);
        }
    }
    delete[] families;
}

/**
 * フォント一覧の取得
 * @param privateOnly true ならプライベートフォントのみ取得
 */
tTJSVariant GdiPlus::getFontList(bool privateOnly) {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    //    addFontFamilyName(array,
    //    privateFontCollection->getFontCollection());
    if(!privateOnly) {
        GpFontCollection installedFontCollection;
        addFontFamilyName(array, &installedFontCollection);
    }
    tTJSVariant ret(array, array);
    array->Release();
    return ret;
}

/**
 * コンストラクタ
 * @param familyName フォントファミリー
 * @param emSize フォントのサイズ
 * @param style フォントスタイル
 */
layerex::FontInfo::FontInfo(const tjs_char *fName, REAL emSize, INT style) {
    setFamilyName(fName);
    setEmSize(emSize);
    setStyle(style);
}

/**
 * コピーコンストラクタ
 */
layerex::FontInfo::FontInfo(const FontInfo &orig) {
    //    this->fontFamily = nullptr;
    //    if (orig.fontFamily) {
    //        GdipCloneFontFamily(orig.fontFamily, &this->fontFamily);
    //    }
    this->emSize = orig.emSize;
    this->style = orig.style;

    if(!orig.ftFace)
        return;

    FT_New_Face(TVPGetFontLibrary(), ftFace->family_name, 0, &this->ftFace);

    int dpi = gdip_get_display_dpi();
    FT_Set_Char_Size(this->ftFace, 0, emSize * 64, dpi, dpi);
}

/**
 * デストラクタ
 */
layerex::FontInfo::~FontInfo() { clear(); }

/**
 * フォント情報のクリア
 */
void layerex::FontInfo::clear() {
    //    GdipDeleteFontFamily(this->fontFamily);
    FT_Done_Face(this->ftFace);
    this->ftFace = nullptr;
    //    this->fontFamily = nullptr;
    this->familyName = "";
    this->gdiPlusUnsupportedFont = false;
    this->propertyModified = true;
}

/**
 * フォントの指定
 */
void layerex::FontInfo::setFamilyName(const tjs_char *fName) {
    // HOOK familyFont
    propertyModified = true;

    if(forceSelfPathDraw) {
        clear();
        gdiPlusUnsupportedFont = true;
        this->familyName = fName;
        return;
    }
    if(!fName || this->familyName == fName)
        return;

    clear();
    gdiPlusUnsupportedFont = true;
    this->familyName = fName;

    const std::unique_ptr<tTJSBinaryStream> stream{ TVPCreateFontStream(
        TVPGetDefaultFontName()) };
    const auto bufferSize = static_cast<FT_Long>(stream->GetSize());

    buffer = std::make_unique<FT_Byte[]>(bufferSize);
    stream->ReadBuffer(buffer.get(), bufferSize);

    FT_New_Memory_Face(TVPGetFontLibrary(), buffer.get(), bufferSize, 0,
                       &this->ftFace);

    const float dpi = gdip_get_display_dpi();
    FT_Set_Char_Size(this->ftFace, 0, emSize * 64, dpi, dpi);
}

void layerex::FontInfo::setForceSelfPathDraw(bool state) {
    forceSelfPathDraw = state;
}

bool layerex::FontInfo::getForceSelfPathDraw() const {
    return forceSelfPathDraw;
}

bool layerex::FontInfo::getSelfPathDraw() const {
    return forceSelfPathDraw || gdiPlusUnsupportedFont;
}

void layerex::FontInfo::updateSizeParams() const {
    if(!propertyModified)
        return;

    propertyModified = false;

    FT_Fixed scale = this->ftFace->size->metrics.y_scale;

    // 上升高度
    ascent =
        static_cast<float>(FT_MulFix(this->ftFace->ascender, scale)) / 64.0f;

    // 下降高度
    descent =
        static_cast<float>(FT_MulFix(this->ftFace->descender, scale)) / 64.0f;

    // 行间距
    lineSpacing =
        static_cast<float>(FT_MulFix(this->ftFace->height, scale)) / 64.0f;

    // 上升部件的 leading
    ascentLeading = (lineSpacing - ascent) / 2;
    // 下降部件的 leading
    descentLeading = -ascentLeading;
}

REAL layerex::FontInfo::getAscent() const {
    this->updateSizeParams();
    return ascent;
}

REAL layerex::FontInfo::getDescent() const {
    this->updateSizeParams();
    return descent;
}

REAL layerex::FontInfo::getAscentLeading() const {
    this->updateSizeParams();
    return ascentLeading;
}

REAL layerex::FontInfo::getDescentLeading() const {
    this->updateSizeParams();
    return descentLeading;
}

REAL layerex::FontInfo::getLineSpacing() const {
    this->updateSizeParams();
    return lineSpacing;
}

// --------------------------------------------------------
// アピアランス情報
// --------------------------------------------------------

Appearance::Appearance() = default;

Appearance::~Appearance() { clear(); }

/**
 * 情報のクリア
 */
void Appearance::clear() {
    drawInfos.clear();

    // customLineCapsも削除
    auto i = customLineCaps.begin();
    while(i != customLineCaps.end()) {
        delete *i;
        i++;
    }
    customLineCaps.clear();
}

// --------------------------------------------------------
// 各型変換処理
// --------------------------------------------------------

extern bool IsArray(const tTJSVariant &var);

/**
 * 座標情報の生成
 */
extern PointFClass getPoint(const tTJSVariant &var);

/**
 * 点の配列を取得
 */
void getPoints(const tTJSVariant &var, std::vector<PointFClass> &points) {
    ncbPropAccessor info(var);
    int c = info.GetArrayCount();
    for(int i = 0; i < c; i++) {
        tTJSVariant p;
        if(info.checkVariant(i, p)) {
            points.push_back(getPoint(p));
        }
    }
}

static void getPoints(ncbPropAccessor &info, int n,
                      std::vector<PointFClass> &points) {
    tTJSVariant var;
    if(info.checkVariant(n, var)) {
        getPoints(var, points);
    }
}

static void getPoints(ncbPropAccessor &info, const tjs_char *n,
                      std::vector<PointFClass> &points) {
    tTJSVariant var;
    if(info.checkVariant(n, var)) {
        getPoints(var, points);
    }
}

// -----------------------------

/**
 * 矩形情報の生成
 */
extern RectFClass getRect(const tTJSVariant &var);

/**
 * 矩形の配列を取得
 */
void getRects(const tTJSVariant &var, std::vector<RectFClass> &rects) {
    ncbPropAccessor info(var);
    int c = info.GetArrayCount();
    for(int i = 0; i < c; i++) {
        tTJSVariant p;
        if(info.checkVariant(i, p)) {
            rects.push_back(getRect(p));
        }
    }
}

// -----------------------------

/**
 * 実数の配列を取得
 */
static void getReals(tTJSVariant var, std::vector<REAL> &points) {
    ncbPropAccessor info(var);
    int c = info.GetArrayCount();
    for(int i = 0; i < c; i++) {
        points.push_back((REAL)info.getRealValue(i));
    }
}

static void getReals(ncbPropAccessor &info, int n, std::vector<REAL> &points) {
    tTJSVariant var;
    if(info.checkVariant(n, var)) {
        getReals(var, points);
    }
}

static void getReals(ncbPropAccessor &info, const tjs_char *n,
                     std::vector<REAL> &points) {
    tTJSVariant var;
    if(info.checkVariant(n, var)) {
        getReals(var, points);
    }
}

// -----------------------------

/**
 * 色の配列を取得
 */
static void getColors(tTJSVariant var, std::vector<Color> &colors) {
    ncbPropAccessor info(var);
    int c = info.GetArrayCount();
    for(int i = 0; i < c; i++) {
        colors.push_back(Color{ (ARGB)info.getIntValue(i) });
    }
}

static void getColors(ncbPropAccessor &info, int n,
                      std::vector<Color> &colors) {
    tTJSVariant var;
    if(info.checkVariant(n, var)) {
        getColors(var, colors);
    }
}

static void getColors(ncbPropAccessor &info, const tjs_char *n,
                      std::vector<Color> &colors) {
    tTJSVariant var;
    if(info.checkVariant(n, var)) {
        getColors(var, colors);
    }
}

template <typename T>
void commonBrushParameter(ncbPropAccessor &info, T *brush) {
    tTJSVariant var;
    // SetBlend
    if(info.checkVariant(TJS_W("blend"), var)) {
        std::vector<REAL> factors;
        std::vector<REAL> positions;
        ncbPropAccessor binfo(var);
        if(IsArray(var)) {
            getReals(binfo, 0, factors);
            getReals(binfo, 1, positions);
        } else {
            getReals(binfo, TJS_W("blendFactors"), factors);
            getReals(binfo, TJS_W("blendPositions"), positions);
        }
        int count = (int)factors.size();
        if((int)positions.size() > count) {
            count = (int)positions.size();
        }
        if(count > 0) {
            brush->SetBlend(&factors[0], &positions[0], count);
        }
    }
    // SetBlendBellShape
    if(info.checkVariant(TJS_W("blendBellShape"), var)) {
        ncbPropAccessor sinfo(var);
        if(IsArray(var)) {
            brush->SetBlendBellShape((REAL)sinfo.getRealValue(0),
                                     (REAL)sinfo.getRealValue(1));
        } else {
            brush->SetBlendBellShape((REAL)info.getRealValue(TJS_W("focus")),
                                     (REAL)info.getRealValue(TJS_W("scale")));
        }
    }
    // SetBlendTriangularShape
    if(info.checkVariant(TJS_W("blendTriangularShape"), var)) {
        ncbPropAccessor sinfo(var);
        if(IsArray(var)) {
            brush->SetBlendTriangularShape((REAL)sinfo.getRealValue(0),
                                           (REAL)sinfo.getRealValue(1));
        } else {
            brush->SetBlendTriangularShape(
                (REAL)info.getRealValue(TJS_W("focus")),
                (REAL)info.getRealValue(TJS_W("scale")));
        }
    }
    // SetGammaCorrection
    if(info.checkVariant(TJS_W("useGammaCorrection"), var)) {
        brush->SetGammaCorrection((bool)var);
    }
    // SetInterpolationColors
    if(info.checkVariant(TJS_W("interpolationColors"), var)) {
        std::vector<Color> colors;
        std::vector<REAL> positions;
        ncbPropAccessor binfo(var);
        if(IsArray(var)) {
            getColors(binfo, 0, colors);
            getReals(binfo, 1, positions);
        } else {
            getColors(binfo, TJS_W("presetColors"), colors);
            getReals(binfo, TJS_W("blendPositions"), positions);
        }
        int count = (int)colors.size();
        if((int)positions.size() > count) {
            count = (int)positions.size();
        }
        if(count > 0) {
            brush->SetInterpolationColors(&colors[0], &positions[0], count);
        }
    }
}

/**
 * ブラシの生成
 */
BrushBase *createBrush(tTJSVariant colorOrBrush) {
    BrushBase *brush = nullptr;
    if(colorOrBrush.Type() != tvtObject) {
        brush = new SolidBrush{ Color{ (ARGB)(tjs_int)colorOrBrush } };
    } else {
        // 種別ごとに作り分ける
        ncbPropAccessor info(colorOrBrush);
        auto type =
            (BrushType)info.getIntValue(TJS_W("type"), BrushTypeSolidColor);
        switch(type) {
            case BrushTypeSolidColor:
                brush = new SolidBrush{ Color{
                    (ARGB)info.getRealValue(TJS_W("color"), 0xffffffff) } };
                break;
            case BrushTypeHatchFill:
                brush = new HatchBrush(
                    (HatchStyle)info.getIntValue(TJS_W("hatchStyle"),
                                                 HatchStyleHorizontal),
                    Color{ (ARGB)info.getRealValue(TJS_W("foreColor"),
                                                   0xffffffff) },
                    Color{ (ARGB)info.getRealValue(TJS_W("backColor"),
                                                   0xff000000) });
                break;
            case BrushTypeTextureFill: {
                ttstr imgname =
                    info.GetValue(TJS_W("image"), ncbTypedefs::Tag<ttstr>());
                auto *image = loadImage(imgname.c_str());
                if(image) {
                    auto wrapMode = (WrapMode)info.getIntValue(
                        TJS_W("wrapMode"), WrapModeTile);
                    tTJSVariant dstRect;
                    if(info.checkVariant(TJS_W("dstRect"), dstRect)) {
                        brush = new TextureBrush{ image, wrapMode,
                                                  getRect(dstRect) };
                    } else {
                        brush = new TextureBrush{ image, wrapMode };
                    }
                    delete image;
                }
                break;
            }
            case BrushTypePathGradient: {
                PathGradientBrush *pbrush;
                std::vector<PointFClass> points;
                getPoints(info, TJS_W("points"), points);
                if((int)points.size() == 0)
                    TVPThrowExceptionMessage(TJS_W("must set poins"));
                auto wrapMode =
                    (WrapMode)info.getIntValue(TJS_W("wrapMode"), WrapModeTile);
                pbrush = new PathGradientBrush{ &points[0], (int)points.size(),
                                                wrapMode };

                // 共通パラメータ
                commonBrushParameter(info, pbrush);

                tTJSVariant var;
                // SetCenterColor
                if(info.checkVariant(TJS_W("centerColor"), var)) {
                    pbrush->SetCenterColor(Color{ (ARGB)(tjs_int)var });
                }
                // SetCenterPoint
                if(info.checkVariant(TJS_W("centerPoint"), var)) {
                    pbrush->SetCenterPoint(getPoint(var));
                }
                // SetFocusScales
                if(info.checkVariant(TJS_W("focusScales"), var)) {
                    ncbPropAccessor sinfo(var);
                    if(IsArray(var)) {
                        pbrush->SetFocusScales((REAL)sinfo.getRealValue(0),
                                               (REAL)sinfo.getRealValue(1));
                    } else {
                        pbrush->SetFocusScales(
                            (REAL)info.getRealValue(TJS_W("xScale")),
                            (REAL)info.getRealValue(TJS_W("yScale")));
                    }
                }
                // SetSurroundColors
                if(info.checkVariant(TJS_W("surroundColors"), var)) {
                    std::vector<Color> colors;
                    getColors(var, colors);
                    int size = (int)colors.size();
                    pbrush->SetSurroundColors(&colors[0], &size);
                }
                brush = pbrush;
            } break;
            case BrushTypeLinearGradient: {
                LinearGradientBrush *lbrush{};
                Color color1{ (ARGB)(tjs_int)info.getIntValue(TJS_W("color1"),
                                                              0) };
                Color color2{ (ARGB)(tjs_int)info.getIntValue(TJS_W("color2"),
                                                              0) };

                tTJSVariant var;
                if(info.checkVariant(TJS_W("point1"), var)) {
                    PointFClass point1 = getPoint(var);
                    info.checkVariant(TJS_W("point2"), var);
                    PointFClass point2 = getPoint(var);
                    lbrush = new LinearGradientBrush{ point1, point2, color1,
                                                      color2 };
                } else if(info.checkVariant(TJS_W("rect"), var)) {
                    RectFClass rect = getRect(var);
                    if(info.HasValue(TJS_W("angle"))) {
                        // アングル指定がある場合
                        lbrush = new LinearGradientBrush{
                            rect, color1, color2,
                            (REAL)info.getRealValue(TJS_W("angle"), 0),
                            static_cast<bool>(
                                info.getIntValue(TJS_W("isAngleScalable"), 0))
                        };
                    } else {
                        // 無い場合はモードを参照
                        lbrush = new LinearGradientBrush{
                            rect, color1, color2,
                            (LinearGradientMode)info.getIntValue(
                                TJS_W("mode"), LinearGradientModeHorizontal)
                        };
                    }
                } else {
                    TVPThrowExceptionMessage(
                        TJS_W("must set point1,2 or rect"));
                }

                // 共通パラメータ
                commonBrushParameter(info, lbrush);

                // SetWrapMode
                if(info.checkVariant(TJS_W("wrapMode"), var)) {
                    lbrush->SetWrapMode((WrapMode)(tjs_int)var);
                }
                brush = lbrush;
            } break;
            default:
                TVPThrowExceptionMessage(TJS_W("invalid brush type"));
                break;
        }
    }
    return brush;
}

/**
 * ブラシの追加
 * @param colorOrBrush ARGB色指定またはブラシ情報（辞書）
 * @param ox 表示オフセットX
 * @param oy 表示オフセットY
 */
void Appearance::addBrush(tTJSVariant colorOrBrush, REAL ox, REAL oy) {
    drawInfos.emplace_back(ox, oy, createBrush(colorOrBrush));
}

/**
 * ペンの追加
 * @param colorOrBrush ARGB色指定またはブラシ情報（辞書）
 * @param widthOrOption ペン幅またはペン情報（辞書）
 * @param ox 表示オフセットX
 * @param oy 表示オフセットY
 */
void Appearance::addPen(tTJSVariant colorOrBrush, tTJSVariant widthOrOption,
                        REAL ox, REAL oy) {
    Pen *pen{};
    REAL width = 1.0;
    if(colorOrBrush.Type() == tvtObject) {
        BrushBase *brush = createBrush(colorOrBrush);
        pen = new Pen{ brush, width };
        delete brush;
    } else {
        pen = new Pen(Color{ (ARGB)(tjs_int)colorOrBrush }, width);
    }
    if(widthOrOption.Type() != tvtObject) {
        pen->SetWidth((REAL)(tjs_real)widthOrOption);
    } else {
        ncbPropAccessor info(widthOrOption);
        REAL penWidth = 1.0;
        tTJSVariant var;

        // SetWidth
        if(info.checkVariant(TJS_W("width"), var)) {
            penWidth = (REAL)(tjs_real)var;
        }
        pen->SetWidth(penWidth);

        // SetAlignment
        if(info.checkVariant(TJS_W("alignment"), var)) {
            pen->SetAlignment((PenAlignment)(tjs_int)var);
        }
        // SetCompoundArray
        if(info.checkVariant(TJS_W("compoundArray"), var)) {
            std::vector<REAL> reals;
            getReals(var, reals);
            pen->SetCompoundArray(&reals[0], (int)reals.size());
        }

        // SetDashCap
        if(info.checkVariant(TJS_W("dashCap"), var)) {
            pen->SetDashCap((GpDashCap)(tjs_int)var);
        }
        // SetDashOffset
        if(info.checkVariant(TJS_W("dashOffset"), var)) {
            pen->SetDashOffset((REAL)(tjs_real)var);
        }

        // SetDashStyle
        // SetDashPattern
        if(info.checkVariant(TJS_W("dashStyle"), var)) {
            if(IsArray(var)) {
                std::vector<REAL> reals;
                getReals(var, reals);
                pen->SetDashStyle(DashStyleCustom);
                pen->SetDashPattern(&reals[0], (int)reals.size());
            } else {
                pen->SetDashStyle((GpDashStyle)(tjs_int)var);
            }
        }

        // SetStartCap
        // SetCustomStartCap
        if(info.checkVariant(TJS_W("startCap"), var)) {
            GpLineCap cap = LineCapFlat;
            CustomLineCap *custom = nullptr;
            if(getLineCap(var, cap, custom, penWidth)) {
                if(custom != nullptr)
                    pen->SetCustomStartCap(custom);
                else
                    pen->SetStartCap(cap);
            }
        }

        // SetEndCap
        // SetCustomEndCap
        if(info.checkVariant(TJS_W("endCap"), var)) {
            GpLineCap cap = LineCapFlat;
            CustomLineCap *custom = nullptr;
            if(getLineCap(var, cap, custom, penWidth)) {
                if(custom != nullptr)
                    pen->SetCustomEndCap(custom);
                else
                    pen->SetEndCap(cap);
            }
        }

        // SetLineJoin
        if(info.checkVariant(TJS_W("lineJoin"), var)) {
            pen->SetLineJoin((GpLineJoin)(tjs_int)var);
        }

        // SetMiterLimit
        if(info.checkVariant(TJS_W("miterLimit"), var)) {
            pen->SetMiterLimit((REAL)(tjs_real)var);
        }
    }
    drawInfos.emplace_back(ox, oy, pen);
}

bool Appearance::getLineCap(tTJSVariant &in, GpLineCap &cap,
                            CustomLineCap *&custom, REAL pw) {
    switch(in.Type()) {
        case tvtVoid:
        case tvtInteger:
            cap = (GpLineCap)(tjs_int)in;
            break;
        case tvtObject: {
            ncbPropAccessor info(in);
            REAL width = pw, height = pw;
            tTJSVariant var;
            if(info.checkVariant(TJS_W("width"), var))
                width = (REAL)(tjs_real)var;
            if(info.checkVariant(TJS_W("height"), var))
                height = (REAL)(tjs_real)var;
            bool filled = info.getIntValue(TJS_W("filled"), 1);
            GpAdjustableArrowCap *arrow{};
            GdipCreateAdjustableArrowCap(height, width, filled, &arrow);
            if(info.checkVariant(TJS_W("middleInset"), var))
                GdipSetAdjustableArrowCapMiddleInset(arrow,
                                                     (REAL)(tjs_real)var);
            customLineCaps.push_back(
                (custom = reinterpret_cast<CustomLineCap *>(arrow)));
        } break;
        default:
            return false;
    }
    return true;
}

// --------------------------------------------------------
// フォント描画系
// --------------------------------------------------------

void LayerExDraw::updateRect(RectFClass &rect) {
    if(updateWhenDraw) {
        tTVPRect rc((tjs_int)rect.X, (tjs_int)rect.Y,
                    (tjs_int)(rect.X + rect.Width), (tjs_int)(rect.Y + rect.Height));
        _this->Update(rc);
    }
}

/**
 * コンストラクタ
 */
LayerExDraw::LayerExDraw(DispatchT obj) :
    layerExBase(obj), width(-1), height(-1), pitch(0), bitmap(nullptr),
    graphics(nullptr), clipLeft(-1), clipTop(-1), clipWidth(-1), clipHeight(-1),
    smoothingMode(SmoothingModeAntiAlias),
    textRenderingHint(TextRenderingHintAntiAlias), metafile(nullptr),
    /*metaGraphics(nullptr),*/ recordBitmap(nullptr), recordGraphics(nullptr),
    recordWidth(0), recordHeight(0), recordOriginX(0), recordOriginY(0),
    recordEnabled(false), updateWhenDraw(true) {}

/**
 * デストラクタ
 */
LayerExDraw::~LayerExDraw() {
    destroyRecord();
    GdipDeleteGraphics(this->graphics);
    GdipDisposeImage(this->bitmap);
}

void LayerExDraw::reset() {
    layerExBase::reset();
    // 変更されている場合はつくりなおし
    if(!(graphics && width == _width && height == _height && pitch == _pitch &&
         buffer == _buffer)) {
        GdipDeleteGraphics(this->graphics);
        GdipDisposeImage((GpImage *)this->bitmap);
        width = _width;
        height = _height;
        pitch = _pitch;
        buffer = _buffer;
        GdipCreateBitmapFromScan0(width, height, pitch, PixelFormat32bppARGB,
                                  (unsigned char *)buffer, &bitmap);
        GdipGetImageGraphicsContext(this->bitmap, &this->graphics);
        GdipSetCompositingMode(this->graphics, CompositingModeSourceOver);
        GdipSetWorldTransform(this->graphics,
                              static_cast<GpMatrix *>(calcTransform));
        if(recordGraphics) {
            setRecordGraphicsTransform(recordGraphics);
        }
        clipWidth = clipHeight = -1;
    }
    // クリッピング領域変更の場合は設定しなおし
    if(_clipLeft != clipLeft || _clipTop != clipTop ||
       _clipWidth != clipWidth || _clipHeight != clipHeight) {
        clipLeft = _clipLeft;
        clipTop = _clipTop;
        clipWidth = _clipWidth;
        clipHeight = _clipHeight;
        GpRegion *clip{};
        GpRect r{ clipLeft, clipTop, clipWidth, clipHeight };
        GdipCreateRegionRectI(&r, &clip);
        GdipSetClipRegion(this->graphics, clip, CombineModeReplace);
        GdipDeleteRegion(clip);
    }
}

void LayerExDraw::updateViewTransform() {
    calcTransform.Reset();
    calcTransform.Multiply(&transform, MatrixOrderAppend);
    calcTransform.Multiply(&viewTransform, MatrixOrderAppend);
    GdipSetWorldTransform(this->graphics,
                          static_cast<GpMatrix *>(calcTransform));
    if(recordGraphics) {
        setRecordGraphicsTransform(recordGraphics);
    }
    redrawRecord();
}

/**
 * 表示トランスフォームの指定
 * @param matrix トランスフォームマトリックス
 */
void LayerExDraw::setViewTransform(/* const */ MatrixClass *trans) {
    if(!viewTransform.Equals(trans)) {
        viewTransform.Reset();
        viewTransform.Multiply(trans);
        updateViewTransform();
    }
}

void LayerExDraw::resetViewTransform() {
    viewTransform.Reset();
    updateViewTransform();
}

void LayerExDraw::rotateViewTransform(REAL angle) {
    viewTransform.Rotate(angle, MatrixOrderAppend);
    updateViewTransform();
}

void LayerExDraw::scaleViewTransform(REAL sx, REAL sy) {
    viewTransform.Scale(sx, sy, MatrixOrderAppend);
    updateViewTransform();
}

void LayerExDraw::translateViewTransform(REAL dx, REAL dy) {
    viewTransform.Translate(dx, dy, MatrixOrderAppend);
    updateViewTransform();
}

void LayerExDraw::updateTransform() {
    calcTransform.Reset();
    calcTransform.Multiply(&transform, MatrixOrderAppend);
    calcTransform.Multiply(&viewTransform, MatrixOrderAppend);
    GdipSetWorldTransform(this->graphics,
                          static_cast<GpMatrix *>(calcTransform));
    if(recordGraphics) {
        setRecordGraphicsTransform(recordGraphics);
    }
    //    if (metaGraphics) {
    //        GdipSetWorldTransform(this->metaGraphics,
    //                              static_cast<GpMatrix
    //                              *>(transform));
    //    }
}

/**
 * トランスフォームの指定
 * @param matrix トランスフォームマトリックス
 */
void LayerExDraw::setTransform(/* const */ MatrixClass *trans) {
    if(!transform.Equals(trans)) {
        transform.Reset();
        transform.Multiply(trans);
        updateTransform();
    }
}

void LayerExDraw::resetTransform() {
    transform.Reset();
    updateTransform();
}

void LayerExDraw::rotateTransform(REAL angle) {
    transform.Rotate(angle, MatrixOrderAppend);
    updateTransform();
}

void LayerExDraw::scaleTransform(REAL sx, REAL sy) {
    transform.Scale(sx, sy, MatrixOrderAppend);
    updateTransform();
}

void LayerExDraw::translateTransform(REAL dx, REAL dy) {
    transform.Translate(dx, dy, MatrixOrderAppend);
    updateTransform();
}

/**
 * 画面の消去
 * @param argb 消去色
 */
void LayerExDraw::clear(ARGB argb) {
    if(recordEnabled) {
        if(ensureRecordSurface(width, height)) {
            GdipGraphicsClear(this->recordGraphics, argb);
        }
        return;
    }
    GdipGraphicsClear(this->graphics, argb);
    //    if (metaGraphics) {
    //        createRecord();
    //        GdipGraphicsClear(this->metaGraphics, argb);
    //    }
    layerExBase::redraw();
}

void LayerExDraw::setRecordGraphicsTransform(GpGraphics *target) {
    if(!target) {
        return;
    }
    MatrixClass recordTransform{ calcTransform };
    recordTransform.Translate(-recordOriginX, -recordOriginY,
                              MatrixOrderAppend);
    GdipSetWorldTransform(target, static_cast<GpMatrix *>(recordTransform));
}

bool LayerExDraw::ensureRecordBounds(REAL left, REAL top, REAL right,
                                     REAL bottom) {
    if(right < left) {
        std::swap(left, right);
    }
    if(bottom < top) {
        std::swap(top, bottom);
    }

    const REAL requiredLeft = recordMinReal(left, 0.0f);
    const REAL requiredTop = recordMinReal(top, 0.0f);
    const REAL requiredRight =
        recordMaxReal(right, static_cast<REAL>(recordMaxGeometry(width, 1)));
    const REAL requiredBottom =
        recordMaxReal(bottom, static_cast<REAL>(recordMaxGeometry(height, 1)));
    const REAL currentRight = recordOriginX + static_cast<REAL>(recordWidth);
    const REAL currentBottom = recordOriginY + static_cast<REAL>(recordHeight);
    if(recordBitmap && recordGraphics && recordOriginX <= requiredLeft &&
       recordOriginY <= requiredTop && currentRight >= requiredRight &&
       currentBottom >= requiredBottom) {
        return true;
    }

    auto grow = [](GeometryT current, GeometryT required) {
        GeometryT value = recordMaxGeometry(current, 1);
        while(value < required && value < 32768) {
            value *= 2;
        }
        return recordMaxGeometry(value, required);
    };

    const REAL newOriginX = recordBitmap ? recordMinReal(recordOriginX, requiredLeft)
                                         : requiredLeft;
    const REAL newOriginY = recordBitmap ? recordMinReal(recordOriginY, requiredTop)
                                         : requiredTop;
    const GeometryT requiredWidth = recordMaxGeometry(
        static_cast<GeometryT>(std::ceil(requiredRight - newOriginX)), 1);
    const GeometryT requiredHeight = recordMaxGeometry(
        static_cast<GeometryT>(std::ceil(requiredBottom - newOriginY)), 1);
    const GeometryT baseWidth = recordMaxGeometry(
        recordWidth > 0 ? recordWidth : width, requiredWidth);
    const GeometryT baseHeight = recordMaxGeometry(
        recordHeight > 0 ? recordHeight : height, requiredHeight);
    const GeometryT newWidth =
        grow(recordWidth > 0 ? recordWidth : baseWidth, requiredWidth);
    const GeometryT newHeight =
        grow(recordHeight > 0 ? recordHeight : baseHeight, requiredHeight);

    GpBitmap *newBitmap = nullptr;
    if(GdipCreateBitmapFromScan0(newWidth, newHeight, 0, PixelFormat32bppARGB,
                                 nullptr, &newBitmap) != Ok ||
       !newBitmap) {
        return false;
    }

    GpGraphics *newGraphics = nullptr;
    if(GdipGetImageGraphicsContext(newBitmap, &newGraphics) != Ok ||
       !newGraphics) {
        GdipDisposeImage(reinterpret_cast<GpImage *>(newBitmap));
        return false;
    }

    GdipSetCompositingMode(newGraphics, CompositingModeSourceOver);
    GdipGraphicsClear(newGraphics, 0x00000000);
    if(recordBitmap) {
        const REAL copyX = recordOriginX - newOriginX;
        const REAL copyY = recordOriginY - newOriginY;
        GdipDrawImageRect(newGraphics, reinterpret_cast<GpImage *>(recordBitmap),
                          copyX, copyY, recordWidth, recordHeight);
    }

    GdipDeleteGraphics(recordGraphics);
    GdipDisposeImage(reinterpret_cast<GpImage *>(recordBitmap));
    recordBitmap = newBitmap;
    recordGraphics = newGraphics;
    recordWidth = newWidth;
    recordHeight = newHeight;
    recordOriginX = newOriginX;
    recordOriginY = newOriginY;
    setRecordGraphicsTransform(recordGraphics);
    return true;
}

bool LayerExDraw::ensureRecordSurface(GeometryT minWidth, GeometryT minHeight) {
    return ensureRecordBounds(0, 0, minWidth, minHeight);
}

/**
 * パスの領域情報を取得
 * @param app 表示表現
 * @param path 描画するパス
 */
RectFClass LayerExDraw::getPathExtents(const Appearance *app,
                                       /* const */ GpPath *path) {
    // 領域記録用
    RectFClass rect{};

    // 描画情報を使って次々描画
    bool first = true;
    auto i = app->drawInfos.begin();
    while(i != app->drawInfos.end()) {
        if(i->info) {
            MatrixClass matrix{ 1, 0, 0, 1, i->ox, i->oy };
            matrix.Multiply(&calcTransform, MatrixOrderAppend);
            switch(i->type) {
                case 0: {
                    Pen *pen = (Pen *)i->info;
                    if(first) {
                        GdipGetPathWorldBounds(path, &rect,
                                               static_cast<GpMatrix *>(matrix),
                                               static_cast<GpPen *>(*pen));
                        first = false;
                    } else {
                        RectFClass r{};
                        GdipGetPathWorldBounds(path, &r,
                                               static_cast<GpMatrix *>(matrix),
                                               static_cast<GpPen *>(*pen));
                        RectFClass::Union(rect, rect, r);
                    }
                } break;
                case 1:
                    if(first) {
                        GdipGetPathWorldBounds(path, &rect,
                                               static_cast<GpMatrix *>(matrix),
                                               nullptr);
                        first = false;
                    } else {
                        RectFClass r;
                        GdipGetPathWorldBounds(
                            path, &r, static_cast<GpMatrix *>(matrix), nullptr);
                        RectFClass::Union(rect, rect, r);
                    }
                    break;
            }
        }
        i++;
    }
    return rect;
}

void LayerExDraw::draw(GpGraphics *graphics, const Pen *pen,
                       const MatrixClass *matrix, const GpPath *path) {
    GraphicsContainer container{};
    GdipBeginContainer2(graphics, &container);
    GdipMultiplyWorldTransform(graphics, static_cast<GpMatrix *>(*matrix),
                               MatrixOrderPrepend);
    GdipSetSmoothingMode(graphics, smoothingMode);
    auto *mutablePath = const_cast<GpPath *>(path);
    GpPen *drawPen = penForPathDraw(pen, pathHasClosedSubpath(mutablePath));
    GdipDrawPath(graphics, drawPen, mutablePath);
    releasePathDrawPen(pen, drawPen);
    GdipEndContainer(graphics, container);
}

void LayerExDraw::fill(GpGraphics *graphics, const BrushBase *brush,
                       const MatrixClass *matrix, const GpPath *path) {
    GraphicsContainer container{};
    GdipBeginContainer2(graphics, &container);
    GdipMultiplyWorldTransform(graphics, static_cast<GpMatrix *>(*matrix),
                               MatrixOrderPrepend);
    GdipSetSmoothingMode(graphics, smoothingMode);
    GdipFillPath(graphics, static_cast<GpBrush *>(*brush),
                 const_cast<GpPath *>(path));
    GdipEndContainer(graphics, container);
}

/**
 * パスを描画する
 * @param app 表示表現
 * @param path 描画するパス
 * @return 更新領域情報
 */
RectFClass LayerExDraw::_drawPath(const Appearance *app, GpPath *path) {
    // 領域記録用
    RectFClass rect{};

    // 描画情報を使って次々描画
    bool first = true;
    auto i = app->drawInfos.begin();
    while(i != app->drawInfos.end()) {
        if(i->info) {
            MatrixClass matrix{ 1, 0, 0, 1, i->ox, i->oy };
            switch(i->type) {
                case 0: {
                    auto *pen = (Pen *)i->info;
                    MatrixClass boundsMatrix = matrix;
                    boundsMatrix.Multiply(&calcTransform, MatrixOrderAppend);
                    RectFClass drawBounds{};
                    GdipGetPathWorldBounds(path, &drawBounds,
                                           static_cast<GpMatrix *>(boundsMatrix),
                                           static_cast<GpPen *>(*pen));
                    GpGraphics *targetGraphics = graphics;
                    if(recordEnabled) {
                        const auto needWidth = static_cast<GeometryT>(std::ceil(
                            recordMaxReal(drawBounds.X + drawBounds.Width,
                                          static_cast<REAL>(width))));
                        const auto needHeight = static_cast<GeometryT>(std::ceil(
                            recordMaxReal(drawBounds.Y + drawBounds.Height,
                                          static_cast<REAL>(height))));
                        if(!ensureRecordBounds(drawBounds.X - 16,
                                               drawBounds.Y - 16,
                                               needWidth + 16,
                                               needHeight + 16)) {
                            return rect;
                        }
                        targetGraphics = recordGraphics;
                    }
                    draw(targetGraphics, pen, &matrix, path);
                    //                if (metaGraphics) {
                    //                    draw(metaGraphics, pen,
                    //                    &matrix, path);
                    //                }
                    if(first) {
                        rect = drawBounds;
                        first = false;
                    } else {
                        RectFClass::Union(rect, rect, drawBounds);
                    }
                } break;
                case 1: {
                    MatrixClass boundsMatrix = matrix;
                    boundsMatrix.Multiply(&calcTransform, MatrixOrderAppend);
                    RectFClass drawBounds{};
                    GdipGetPathWorldBounds(path, &drawBounds,
                                           static_cast<GpMatrix *>(boundsMatrix),
                                           nullptr);
                    GpGraphics *targetGraphics = graphics;
                    if(recordEnabled) {
                        const auto needWidth = static_cast<GeometryT>(std::ceil(
                            recordMaxReal(drawBounds.X + drawBounds.Width,
                                          static_cast<REAL>(width))));
                        const auto needHeight = static_cast<GeometryT>(std::ceil(
                            recordMaxReal(drawBounds.Y + drawBounds.Height,
                                          static_cast<REAL>(height))));
                        if(!ensureRecordBounds(drawBounds.X - 16,
                                               drawBounds.Y - 16,
                                               needWidth + 16,
                                               needHeight + 16)) {
                            return rect;
                        }
                        targetGraphics = recordGraphics;
                    }
                    fill(targetGraphics, (BrushBase *)i->info, &matrix, path);
                    //                if (metaGraphics) {
                    //                    fill(metaGraphics,
                    //                    (BrushBase *)i->info,
                    //                    &matrix, path);
                    //                }
                    if(first) {
                        rect = drawBounds;
                        first = false;
                    } else {
                        RectFClass::Union(rect, rect, drawBounds);
                    }
                } break;
            }
        }
        i++;
    }
    if(!recordEnabled) {
        updateRect(rect);
    }
    return rect;
}

RectFClass LayerExDraw::_drawRectangles(const Appearance *app,
                                        const RectFClass *rects, int count) {
    RectFClass rect{};
    if(!app || !rects || count <= 0) {
        return rect;
    }

    bool first = true;
    for(const auto &drawInfo : app->drawInfos) {
        if(!drawInfo.info) {
            continue;
        }

        MatrixClass matrix{ 1, 0, 0, 1, drawInfo.ox, drawInfo.oy };
        MatrixClass boundsMatrix = matrix;
        boundsMatrix.Multiply(&calcTransform, MatrixOrderAppend);

        RectFClass drawBounds{};
        for(int n = 0; n < count; ++n) {
            PointFClass points[4] = {
                { rects[n].X, rects[n].Y },
                { rects[n].X + rects[n].Width, rects[n].Y },
                { rects[n].X, rects[n].Y + rects[n].Height },
                { rects[n].X + rects[n].Width,
                  rects[n].Y + rects[n].Height },
            };
            boundsMatrix.TransformPoints(points, 4);
            REAL minx = points[0].X;
            REAL maxx = points[0].X;
            REAL miny = points[0].Y;
            REAL maxy = points[0].Y;
            for(int p = 1; p < 4; ++p) {
                if(points[p].X < minx) minx = points[p].X;
                if(points[p].X > maxx) maxx = points[p].X;
                if(points[p].Y < miny) miny = points[p].Y;
                if(points[p].Y > maxy) maxy = points[p].Y;
            }
            RectFClass transformed{ minx, miny, maxx - minx, maxy - miny };
            if(n == 0) {
                drawBounds = transformed;
            } else {
                RectFClass::Union(drawBounds, drawBounds, transformed);
            }
        }

        if(drawInfo.type == 0) {
            REAL penWidth = 1.0f;
            auto *pen = static_cast<Pen *>(drawInfo.info);
            GdipGetPenWidth(static_cast<GpPen *>(*pen), &penWidth);
            const REAL pad = std::ceil(penWidth * 2.0f) + 4.0f;
            drawBounds.X -= pad;
            drawBounds.Y -= pad;
            drawBounds.Width += pad * 2.0f;
            drawBounds.Height += pad * 2.0f;
        }

        GpGraphics *targetGraphics = graphics;
        if(recordEnabled) {
            const auto needWidth = static_cast<GeometryT>(std::ceil(
                recordMaxReal(drawBounds.X + drawBounds.Width,
                              static_cast<REAL>(width))));
            const auto needHeight = static_cast<GeometryT>(std::ceil(
                recordMaxReal(drawBounds.Y + drawBounds.Height,
                              static_cast<REAL>(height))));
            if(!ensureRecordBounds(drawBounds.X - 16, drawBounds.Y - 16,
                                   needWidth + 16, needHeight + 16)) {
                return rect;
            }
            targetGraphics = recordGraphics;
        }

        GraphicsContainer container{};
        GdipBeginContainer2(targetGraphics, &container);
        GdipMultiplyWorldTransform(targetGraphics,
                                   static_cast<GpMatrix *>(matrix),
                                   MatrixOrderPrepend);
        GdipSetSmoothingMode(targetGraphics, smoothingMode);
        if(drawInfo.type == 0) {
            auto *pen = static_cast<Pen *>(drawInfo.info);
            GdipDrawRectangles(targetGraphics, static_cast<GpPen *>(*pen),
                               rects, count);
        } else {
            auto *brush = static_cast<BrushBase *>(drawInfo.info);
            GdipFillRectangles(targetGraphics, static_cast<GpBrush *>(*brush),
                               rects, count);
        }
        GdipEndContainer(targetGraphics, container);

        if(first) {
            rect = drawBounds;
            first = false;
        } else {
            RectFClass::Union(rect, rect, drawBounds);
        }
    }

    if(!recordEnabled) {
        updateRect(rect);
    }
    return rect;
}

/**
 * パスの描画
 * @param app アピアランス
 * @param path パス
 */
RectFClass LayerExDraw::drawPath(const Appearance *app, const DrawPath *path) {
    return _drawPath(app, path->path);
}

/**
 * 円弧の描画
 * @param x 左上座標
 * @param y 左上座標
 * @param width 横幅
 * @param height 縦幅
 * @param startAngle 時計方向円弧開始位置
 * @param sweepAngle 描画角度
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawArc(const Appearance *app, REAL x, REAL y,
                                REAL width, REAL height, REAL startAngle,
                                REAL sweepAngle) {
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathArc(path, x, y, width, height, startAngle, sweepAngle);
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

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
RectFClass LayerExDraw::drawBezier(const Appearance *app, REAL x1, REAL y1,
                                   REAL x2, REAL y2, REAL x3, REAL y3, REAL x4,
                                   REAL y4) {
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathBezier(path, x1, y1, x2, y2, x3, y3, x4, y4);
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * 連続ベジェ曲線の描画
 * @param app アピアランス
 * @param points 点の配列
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawBeziers(const Appearance *app, tTJSVariant points) {
    std::vector<PointFClass> ps;
    getPoints(points, ps);
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathBeziers(path, &ps[0], (int)ps.size());
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * Closed cardinal spline の描画
 * @param app アピアランス
 * @param points 点の配列
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawClosedCurve(const Appearance *app,
                                        tTJSVariant points) {
    std::vector<PointFClass> ps;
    getPoints(points, ps);
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathClosedCurve(path, &ps[0], (int)ps.size());
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * Closed cardinal spline の描画
 * @param app アピアランス
 * @param points 点の配列
 * @pram tension tension
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawClosedCurve2(const Appearance *app,
                                         tTJSVariant points, REAL tension) {
    std::vector<PointFClass> ps;
    getPoints(points, ps);
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathClosedCurve2(path, &ps[0], (int)ps.size(), tension);
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * cardinal spline の描画
 * @param app アピアランス
 * @param points 点の配列
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawCurve(const Appearance *app, tTJSVariant points) {
    std::vector<PointFClass> ps;
    getPoints(points, ps);
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathCurve(path, &ps[0], (int)ps.size());
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * cardinal spline の描画
 * @param app アピアランス
 * @param points 点の配列
 * @parma tension tension
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawCurve2(const Appearance *app, tTJSVariant points,
                                   REAL tension) {
    std::vector<PointFClass> ps;
    getPoints(points, ps);
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathCurve2(path, &ps[0], (int)ps.size(), tension);
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * cardinal spline の描画
 * @param app アピアランス
 * @param points 点の配列
 * @param offset
 * @param numberOfSegments
 * @param tension tension
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawCurve3(const Appearance *app, tTJSVariant points,
                                   int offset, int numberOfSegments,
                                   REAL tension) {
    std::vector<PointFClass> ps;
    getPoints(points, ps);
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathCurve3(path, &ps[0], (int)ps.size(), offset, numberOfSegments,
                      tension);
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * 円錐の描画
 * @param x 左上座標
 * @param y 左上座標
 * @param width 横幅
 * @param height 縦幅
 * @param startAngle 時計方向円弧開始位置
 * @param sweepAngle 描画角度
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawPie(const Appearance *app, REAL x, REAL y,
                                REAL width, REAL height, REAL startAngle,
                                REAL sweepAngle) {
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathPie(path, x, y, width, height, startAngle, sweepAngle);
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * 楕円の描画
 * @param app アピアランス
 * @param x
 * @param y
 * @param width
 * @param height
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawEllipse(const Appearance *app, REAL x, REAL y,
                                    REAL width, REAL height) {
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathEllipse(path, x, y, width, height);
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * 線分の描画
 * @param app アピアランス
 * @param x1 始点X座標
 * @param y1 始点Y座標
 * @param x2 終点X座標
 * @param y2 終点Y座標
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawLine(const Appearance *app, REAL x1, REAL y1,
                                 REAL x2, REAL y2) {
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathLine(path, x1, y1, x2, y2);
    RectFClass r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * 連続線分の描画
 * @param app アピアランス
 * @param points 点の配列
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawLines(const Appearance *app, tTJSVariant points) {
    std::vector<PointFClass> ps;
    getPoints(points, ps);
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathLine2(path, &ps[0], (int)ps.size());
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * 多角形の描画
 * @param app アピアランス
 * @param points 点の配列
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawPolygon(const Appearance *app, tTJSVariant points) {
    std::vector<PointFClass> ps;
    getPoints(points, ps);
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathPolygon(path, &ps[0], (int)ps.size());
    auto r = _drawPath(app, path);
    GdipDeletePath(path);
    return r;
}

/**
 * 矩形の描画
 * @param app アピアランス
 * @param x
 * @param y
 * @param width
 * @param height
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawRectangle(const Appearance *app, REAL x, REAL y,
                                      REAL width, REAL height) {
    RectFClass rect{ x, y, width, height };
    return _drawRectangles(app, &rect, 1);
}

/**
 * 複数矩形の描画
 * @param app アピアランス
 * @param rects 矩形情報の配列
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawRectangles(const Appearance *app,
                                       tTJSVariant rects) {
    std::vector<RectFClass> rs{};
    getRects(rects, rs);
    if(rs.empty()) {
        return {};
    }
    return _drawRectangles(app, &rs[0], (int)rs.size());
}

/**
 * 文字列のパスベースでの描画
 * @param font フォント
 * @param app アピアランス
 * @param x 描画位置X
 * @param y 描画位置Y
 * @param text 描画テキスト
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawPathString(const FontInfo *font,
                                       const Appearance *app, REAL x, REAL y,
                                       const tjs_char *text) {
    if(font->getSelfPathDraw())
        return drawPathString2(font, app, x, y, text);

    spdlog::get("plugin")->error("gdi+ draw path string current no implement");
    return {};
    // 文字列のパスを準備
    //    GpPath *path{};
    //    GdipCreatePath(FillModeAlternate, &path);
    //    GpStringFormat *sf{};
    //    GdipCreateStringFormat(0, 0, &sf);
    //    GdipStringFormatGetGenericDefault(&sf);
    //    const auto *n = reinterpret_cast<const WCHAR *>(text);
    //    const RectF rect{x, y};
    //    // FIXME: libgdiplus 6.x.x: length = -1 works fine.
    //    // FIXME: libgdiplus 5.x.x: length = -1 fails
    //    // FIXME: 5.x.x font backend: Pango uses length, but Cairo
    //    ignore. GdipAddPathString(path, n, 1, font->fontFamily,
    //    font->style, font->emSize,
    //                      &rect, sf);
    //    auto r = _drawPath(app, path);
    //    GdipDeleteStringFormat(sf);
    //    GdipDeletePath(path);
    //    return r;
}

static void transformRect(MatrixClass &calcTransform, RectFClass &rect) {
    PointFClass points[4]; // 元座標値
    points[0].X = rect.X;
    points[0].Y = rect.Y;
    points[1].X = rect.X + rect.Width;
    points[1].Y = rect.Y;
    points[2].X = rect.X;
    points[2].Y = rect.Y + rect.Height;
    points[3].X = rect.X + rect.Width;
    points[3].Y = rect.Y + rect.Height;
    // 描画領域を再計算
    calcTransform.TransformPoints(points, 4);
    REAL minx = points[0].X;
    REAL maxx = points[0].X;
    REAL miny = points[0].Y;
    REAL maxy = points[0].Y;
    for(int i = 1; i < 4; i++) {
        if(points[i].X < minx) {
            minx = points[i].X;
        }
        if(points[i].X > maxx) {
            maxx = points[i].X;
        }
        if(points[i].Y < miny) {
            miny = points[i].Y;
        }
        if(points[i].Y > maxy) {
            maxy = points[i].Y;
        }
    }
    rect.X = minx;
    rect.Y = miny;
    rect.Width = maxx - minx;
    rect.Height = maxy - miny;
}

/**
 * 文字列の描画
 * @param font フォント
 * @param app アピアランス（ブラシのみ参照されます）
 * @param x 描画位置X
 * @param y 描画位置Y
 * @param text 描画テキスト
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawString(const FontInfo *font, const Appearance *app,
                                   REAL x, REAL y, const tjs_char *text) {
    if(font->getSelfPathDraw())
        return drawPathString2(font, app, x, y, text);

    spdlog::get("plugin")->error("gdi+ draw string current no implement");
    return {};
    //    GdipSetTextRenderingHint(this->graphics, textRenderingHint);
    //    //    if (metaGraphics) {
    //    //        GdipSetTextRenderingHint(this->metaGraphics,
    //    textRenderingHint);
    //    //    }
    //
    //    // 領域記録用
    //    RectFClass rect{};
    //    // 描画フォント
    //    GpFont *f{};
    //    GdipCreateFont(font->fontFamily, font->emSize, font->style,
    //    UnitPixel, &f); GpStringFormat *sf{};
    //    GdipCreateStringFormat(0, 0, &sf);
    //    GdipStringFormatGetGenericDefault(&sf);
    //    // 描画情報を使って次々描画
    //    //    bool first = true;
    //    auto i = app->drawInfos.begin();
    //    while (i != app->drawInfos.end()) {
    //        if (i->info) {
    //            if (i->type == 1) { // ブラシのみ
    //
    //                const auto *n = reinterpret_cast<const WCHAR
    //                *>(text);
    //
    //                auto *brush = (BrushBase *)i->info;
    //                RectFClass rectF{x + i->ox, y + i->oy, 0, 0};
    //                GdipDrawString(this->graphics, n, 1, f, &rectF,
    //                sf,
    //                               static_cast<GpBrush *>(*brush));
    //                //                if (metaGraphics) {
    //                // GdipDrawString(this->metaGraphics, n, -1,
    //                //                    f, &rectF, sf,
    //                // static_cast<GpBrush
    //                // *>(*brush));
    //                //                }
    //                // 更新領域計算
    //                //                if (first) {
    //                int codepointsFitted{};
    //                int linesFilled{};
    //                GdipMeasureString(this->graphics, n, 1, f,
    //                &rectF, sf, &rect,
    //                                  &codepointsFitted,
    //                                  &linesFilled);
    //                transformRect(calcTransform, rect);
    //                //                    first = false;
    //                //                } else {
    //                //                    RectFClass r;
    //                //                    int codepointsFitted{};
    //                //                    int linesFilled{};
    //                //                    GdipMeasureString(
    //                //                            this->graphics, n,
    //                -1, f, &rectF,
    //                //                            sf, &r,
    //                &codepointsFitted,
    //                //                            &linesFilled
    //                //                    );
    //                // transformRect(calcTransform, r);
    //                //                    RectFClass::Union(rect,
    //                rect, r);
    //                //                }
    //                break;
    //            }
    //        }
    //        i++;
    //    }
    //    updateRect(rect);
    //    GdipDeleteFont(f);
    //    GdipDeleteStringFormat(sf);
    //    return rect;
}

/**
 * 文字列の描画領域情報の取得
 * @param font フォント
 * @param text 描画テキスト
 * @return 描画領域情報
 */
RectFClass LayerExDraw::measureString(const FontInfo *font,
                                      const tjs_char *text) {
    if(font->getSelfPathDraw())
        return measureString2(font, text);

    spdlog::get("plugin")->error("gdi+ measure string current no implement");
    return {};
    //    const auto *n = reinterpret_cast<const WCHAR *>(text);
    //
    //    GdipSetTextRenderingHint(this->graphics, textRenderingHint);
    //
    //    GpFont *f{};
    //    GdipCreateFont(font->fontFamily, font->emSize, font->style,
    //    UnitPixel, &f);
    //
    //    GpStringFormat *sf{};
    //    GdipCreateStringFormat(0, 0, &sf);
    //    GdipStringFormatGetGenericDefault(&sf);
    //
    //    RectFClass r{};
    //    int codepointsFitted{};
    //    int linesFilled{};
    //    GpRectF layout{};
    //    GdipMeasureString(this->graphics, n, 1, f, &layout, sf, &r,
    //                      &codepointsFitted, &linesFilled);
    //    GdipDeleteFont(f);
    //    GdipDeleteStringFormat(sf);
    //    return r;
}

/**
 * 文字列に外接する領域情報の取得
 * @param font フォント
 * @param text 描画テキスト
 * @return 領域情報の辞書 left, top, width, height
 */
RectFClass LayerExDraw::measureStringInternal(const FontInfo *font,
                                              const tjs_char *text) {
    if(font->getSelfPathDraw())
        return measureStringInternal2(font, text);

    spdlog::get("plugin")->error(
        "gdi+ measure string internal current no implement");
    return {};
    //    const auto *n = reinterpret_cast<const WCHAR *>(text);
    //
    //    GdipSetTextRenderingHint(this->graphics, textRenderingHint);
    //
    //    GpFont *f{};
    //    GdipCreateFont(font->fontFamily, font->emSize, font->style,
    //    UnitPixel, &f);
    //
    //    GpStringFormat *sf{};
    //    GdipCreateStringFormat(0, 0, &sf);
    //    GdipStringFormatGetGenericDefault(&sf);
    //
    //    RectFClass r{};
    //    int codepointsFitted{};
    //    int linesFilled{};
    //    GpRectF layout{};
    //    GdipMeasureString(this->graphics, n, 1, f, &layout, sf, &r,
    //                      &codepointsFitted, &linesFilled);
    //    CharacterRange charRange{0, tTJSString{text}.length()};
    //    GdipSetStringFormatMeasurableCharacterRanges(sf, 1,
    //    &charRange); GpRegion *region{};
    //    GdipMeasureCharacterRanges(this->graphics, n, 1, f, &r, sf,
    //    1, &region); RectFClass regionBounds{};
    //    GdipGetRegionBounds(region, this->graphics, &regionBounds);
    //
    //    GdipDeleteFont(f);
    //    GdipDeleteStringFormat(sf);
    //    return regionBounds;
}

/**
 * 画像の描画。コピー先は元画像の Bounds を配慮した位置、サイズは
 * Pixel 指定になります。
 * @param x コピー先原点
 * @param y  コピー先原点
 * @param src コピー元画像
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawImage(REAL x, REAL y, ImageClass *src) {
    RectFClass rect;
    if(src) {
        RectFClass *bounds = getBounds(src);
        rect = drawImageRect(x + bounds->X, y + bounds->Y, src, bounds->X,
                             bounds->Y,
                             bounds->Width, bounds->Height);
        delete bounds;
        updateRect(rect);
    }
    return rect;
}

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
RectFClass LayerExDraw::drawImageRect(REAL dleft, REAL dtop, ImageClass *src,
                                      REAL sleft, REAL stop, REAL swidth,
                                      REAL sheight) {
    return drawImageAffine(src, sleft, stop, swidth, sheight, true, 1, 0, 0, 1,
                           dleft, dtop);
}

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
RectFClass LayerExDraw::drawImageStretch(REAL dleft, REAL dtop, REAL dwidth,
                                         REAL dheight, ImageClass *src,
                                         REAL sleft, REAL stop, REAL swidth,
                                         REAL sheight) {
    return drawImageAffine(src, sleft, stop, swidth, sheight, true,
                           dwidth / swidth, 0, 0, dheight / sheight, dleft,
                           dtop);
}

/**
 * 画像のアフィン変換コピー
 * @param sleft 元矩形の左端
 * @param stop  元矩形の上端
 * @param swidth 元矩形の横幅
 * @param sheight  元矩形の縦幅
 * @param affine アフィンパラメータの種類(true:変換行列,
 * false:座標指定),
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawImageAffine(ImageClass *src, REAL sleft, REAL stop,
                                        REAL swidth, REAL sheight, bool affine,
                                        REAL A, REAL B, REAL C, REAL D, REAL E,
                                        REAL F) {
    RectFClass rect;
    if(src) {
        traceLayerEx("drawImageAffine", src,
                     fmt::format("target={}x{} affine={} srcRect=({},{} {}x{}) args=({},{}; {},{}; {},{})",
                                 width, height, affine ? 1 : 0, sleft, stop,
                                 swidth, sheight, A, B, C, D, E, F)
                         .c_str());
        if(swidth == 0 || sheight == 0) {
            return rect;
        }
        RectFClass *bounds = getBounds(src);
        const REAL srcLeft = bounds->X;
        const REAL srcTop = bounds->Y;
        const REAL srcRight = bounds->X + bounds->Width;
        const REAL srcBottom = bounds->Y + bounds->Height;
        const bool virtualSolid = src->IsVirtualSolid();
        delete bounds;

        const REAL reqLeft = sleft;
        const REAL reqTop = stop;
        const REAL reqRight = sleft + swidth;
        const REAL reqBottom = stop + sheight;
        // A virtual solid is represented by a one-pixel bitmap.  Its source
        // rectangle is a colour sample, not a finite image extent: clipping
        // it to 1x1 would reduce a full-screen affine fill to a single pixel.
        const REAL clipLeft = virtualSolid ? reqLeft :
            (reqLeft < srcLeft ? srcLeft : reqLeft);
        const REAL clipTop = virtualSolid ? reqTop :
            (reqTop < srcTop ? srcTop : reqTop);
        const REAL clipRight = virtualSolid ? reqRight :
            (reqRight > srcRight ? srcRight : reqRight);
        const REAL clipBottom = virtualSolid ? reqBottom :
            (reqBottom > srcBottom ? srcBottom : reqBottom);
        if(clipRight <= clipLeft || clipBottom <= clipTop) {
            return rect;
        }

        const REAL clipWidth = clipRight - clipLeft;
        const REAL clipHeight = clipBottom - clipTop;

        PointFClass points[4]; // 元座標値
        if(affine) {
#define AFFINEX(x, y) (A * (x) + C * (y) + E)
#define AFFINEY(x, y) (B * (x) + D * (y) + F)
            points[0].X = AFFINEX(0, 0);
            points[0].Y = AFFINEY(0, 0);
            points[1].X = AFFINEX(swidth, 0);
            points[1].Y = AFFINEY(swidth, 0);
            points[2].X = AFFINEX(0, sheight);
            points[2].Y = AFFINEY(0, sheight);
            points[3].X = AFFINEX(swidth, sheight);
            points[3].Y = AFFINEY(swidth, sheight);
#undef AFFINEX
#undef AFFINEY
        } else {
            points[0].X = A;
            points[0].Y = B;
            points[1].X = C;
            points[1].Y = D;
            points[2].X = E;
            points[2].Y = F;
            points[3].X = C - A + E;
            points[3].Y = D - B + F;
        }
        const REAL safeSourceWidth = swidth != 0 ? swidth : 1;
        const REAL safeSourceHeight = sheight != 0 ? sheight : 1;
        const REAL clipOffsetX = clipLeft - reqLeft;
        const REAL clipOffsetY = clipTop - reqTop;
        const PointFClass requestedTopLeft = points[0];
        const REAL ux = (points[1].X - requestedTopLeft.X) / safeSourceWidth;
        const REAL uy = (points[1].Y - requestedTopLeft.Y) / safeSourceWidth;
        const REAL vx = (points[2].X - requestedTopLeft.X) / safeSourceHeight;
        const REAL vy = (points[2].Y - requestedTopLeft.Y) / safeSourceHeight;
        points[0].X = requestedTopLeft.X + ux * clipOffsetX + vx * clipOffsetY;
        points[0].Y = requestedTopLeft.Y + uy * clipOffsetX + vy * clipOffsetY;
        points[1].X = points[0].X + ux * clipWidth;
        points[1].Y = points[0].Y + uy * clipWidth;
        points[2].X = points[0].X + vx * clipHeight;
        points[2].Y = points[0].Y + vy * clipHeight;
        points[3].X = points[1].X + vx * clipHeight;
        points[3].Y = points[1].Y + vy * clipHeight;
        const REAL bitmapSourceLeft = virtualSolid ? 0.0f :
            (clipLeft - srcLeft);
        const REAL bitmapSourceTop = virtualSolid ? 0.0f :
            (clipTop - srcTop);
        const REAL bitmapSourceWidth = virtualSolid ? 1.0f : clipWidth;
        const REAL bitmapSourceHeight = virtualSolid ? 1.0f : clipHeight;
        if(virtualSolid) {
            // A 1x1 bitmap passed through GdipDrawImagePointsRect is not
            // equivalent to a uniform vector fill on libgdiplus: the Cairo
            // backend may split the destination quad into triangles and
            // interpolate transparent samples at the diagonal.  The Windows
            // metafile source is semantically a solid brush, so fill the
            // affine quadrilateral directly and preserve its ARGB value.
            GpSolidFill *solidFill = nullptr;
            if(GdipCreateSolidFill(src->GetVirtualSolidColor(), &solidFill) ==
                   Ok &&
               solidFill) {
                GpBrush *solidBrush = reinterpret_cast<GpBrush *>(solidFill);
                const GpPointF polygon[4] = {
                    points[0], points[1], points[3], points[2] };
                GdipFillPolygon(this->graphics, solidBrush, polygon, 4,
                                FillModeAlternate);
                GdipDeleteBrush(solidBrush);
            }
        } else {
            GdipDrawImagePointsRect(
                this->graphics, static_cast<GpImage *>(*src), points, 3,
                bitmapSourceLeft, bitmapSourceTop, bitmapSourceWidth,
                bitmapSourceHeight, UnitPixel, nullptr, nullptr, nullptr);
        }
        //        if (metaGraphics) {
        //
        //            GdipDrawImagePointsRect(this->metaGraphics,
        //                                    static_cast<GpImage
        //                                    *>(*src), points, 3,
        //                                    sleft, stop, swidth,
        //                                    sheight, UnitPixel,
        //                                    nullptr, nullptr,
        //                                    nullptr);
        //        }

        // 描画領域を取得
        calcTransform.TransformPoints(points, 4);
        REAL minx = points[0].X;
        REAL maxx = points[0].X;
        REAL miny = points[0].Y;
        REAL maxy = points[0].Y;
        for(int i = 1; i < 4; i++) {
            if(points[i].X < minx) {
                minx = points[i].X;
            }
            if(points[i].X > maxx) {
                maxx = points[i].X;
            }
            if(points[i].Y < miny) {
                miny = points[i].Y;
            }
            if(points[i].Y > maxy) {
                maxy = points[i].Y;
            }
        }
        rect.X = minx;
        rect.Y = miny;
        rect.Width = maxx - minx;
        rect.Height = maxy - miny;

        updateRect(rect);
    }
    return rect;
}

void LayerExDraw::createRecord() {
    // FIXME: implement
    // destroyRecord();
    // if ((metaBuffer = ::GlobalAlloc(GMEM_MOVEABLE, 0))){
    //     if (::CreateStreamOnHGlobal(metaBuffer, FALSE, &metaStream)
    //     == S_OK)
    //     {
    //         metafile = new Metafile(metaStream, metaHDC,
    //         EmfTypeEmfPlusOnly); metaGraphics = new
    //         Graphics(metafile);
    //         metaGraphics->SetCompositingMode(CompositingModeSourceOver);
    //         metaGraphics->SetTransform(&transform);
    //     }
    // }
    reset();
    destroyRecord();
    recordEnabled = true;
    ensureRecordSurface(width, height);
    //    GpMetafile *emfMetafile{};
    //    GdipCreateMetafileFromFile((WCHAR
    //    *)TJS_W("krkr2_layerexdraw_emf.metafile"),
    //                               &emfMetafile);
    //    GdipCreateMetafileFromEmf(emfMetafile, false, &metafile);
    //    GdipGetImageGraphicsContext((GpImage *)&metafile,
    //    &metaGraphics); GdipSetCompositingMode(metaGraphics,
    //    CompositingModeSourceOver);
    //    GdipSetWorldTransform(metaGraphics, static_cast<GpMatrix
    //    *>(transform));
}

/**
 * 記録情報の破棄
 */
void LayerExDraw::destroyRecord() {
    //    GdipDeleteGraphics(this->metaGraphics);
    //    metaGraphics = nullptr;
    if(recordGraphics) {
        GdipDeleteGraphics(recordGraphics);
    }
    recordGraphics = nullptr;
    if(recordBitmap) {
        GdipDisposeImage(reinterpret_cast<GpImage *>(recordBitmap));
    }
    recordBitmap = nullptr;
    recordWidth = 0;
    recordHeight = 0;
    recordOriginX = 0;
    recordOriginY = 0;
    recordEnabled = false;
    if(metafile) {
        GdipDisposeImage(reinterpret_cast<GpImage *>(metafile));
    }
    metafile = nullptr;
}

/**
 * @param record 描画内容を記録するかどうか
 */
void LayerExDraw::setRecord(bool record) {
    if(record) {
        if(!recordEnabled) {
            createRecord();
        }
    } else {
        if(recordEnabled) {
            destroyRecord();
        }
    }
}

bool LayerExDraw::redraw(ImageClass *image) {
    if(image) {
        RectFClass *bounds = getBounds(image);
        //        if (metaGraphics) {
        //            GdipGraphicsClear(this->metaGraphics, 0);
        //            GdipResetWorldTransform(this->metaGraphics);
        //            GdipDrawImageRect(this->metaGraphics,
        //                              static_cast<GpImage
        //                              *>(*image), bounds->X,
        //                              bounds->Y, bounds->Width,
        //                              bounds->Height);
        //            GpMatrix *tmp{};
        //            GdipSetWorldTransform(this->metaGraphics, tmp);
        //            transform = MatrixClass{tmp};
        //        }
        //        GdipGraphicsClear(this->metaGraphics, 0);
        //        GdipSetWorldTransform(this->metaGraphics,
        //                              static_cast<GpMatrix
        //                              *>(viewTransform));

        GdipDrawImageRect(this->graphics, static_cast<GpImage *>(*image),
                          bounds->X, bounds->Y, bounds->Width, bounds->Height);
        if(recordEnabled && ensureRecordSurface(bounds->X + bounds->Width,
                                                bounds->Y + bounds->Height)) {
            GdipGraphicsClear(this->recordGraphics, 0);
            GdipDrawImageRect(this->recordGraphics, static_cast<GpImage *>(*image),
                              bounds->X, bounds->Y, bounds->Width,
                              bounds->Height);
        }
        GdipSetWorldTransform(this->graphics,
                              static_cast<GpMatrix *>(calcTransform));
        delete bounds;
        layerExBase::redraw();
        return true;
    }

    return false;
}

/**
 * 記録内容を ImageClass として取得
 * @return 成功したら true
 */
ImageClass *LayerExDraw::getRecordImage() {
    reset();
    if(recordEnabled) {
        ensureRecordSurface(width, height);
    }
    if(!recordBitmap) {
        traceLayerEx("getRecordImage-empty", nullptr);
        return nullptr;
    }
    GpImage *cloned = nullptr;
    if(GdipCloneImage(reinterpret_cast<GpImage *>(recordBitmap), &cloned) != Ok ||
       !cloned) {
        return nullptr;
    }
    ImageClass *image = new ImageClass{ cloned, recordOriginX, recordOriginY };
    traceLayerEx("getRecordImage", image,
                 fmt::format("record={}x{} origin=({}, {})", recordWidth,
                             recordHeight, recordOriginX, recordOriginY)
                     .c_str());
    //    if (metafile) {
    // メタ情報を取得するには一度閉じる必要がある
    //        if (metaGraphics) {
    //            GdipDisposeImage((GpImage *)this->metaGraphics);
    //            metaGraphics = nullptr;
    //        }

    // 閉じたあと継続するための再描画先を別途構築
    //         HGLOBAL oldBuffer = metaBuffer;
    //         metaBuffer = nullptr;
    //         createRecord();
    //
    //         // 再描画
    //         if (oldBuffer) {
    //             IStream* pStream = nullptr;
    //             if(::CreateStreamOnHGlobal(oldBuffer, FALSE,
    //             &pStream) == S_OK) 	{
    //                 image = Image::FromStream(pStream,false);
    //                 if (image) {
    //                     redraw(image);
    //                 }
    //                 pStream->Release();
    //             }
    //             ::GlobalFree(oldBuffer);
    //         }
    //     }
    return image;
}

/**
 * 記録内容の現在の解像度での再描画
 */
bool LayerExDraw::redrawRecord() {
    // 再描画処理
    ImageClass *image = getRecordImage();
    const bool hasImage = image != nullptr;
    if(image) {
        redraw(image);
        delete image;
    }
    return hasImage;
}

/**
 * 記録内容の保存
 * @param filename 保存ファイル名
 * @return 成功したら true
 */
bool LayerExDraw::saveRecord(const tjs_char *filename) {
    bool ret = false;
    //    if (metafile) {
    //        // メタ情報を取得するには一度閉じる必要がある
    //        GdipDisposeImage((GpImage *)this->metaGraphics);
    //        this->metaGraphics = nullptr;
    //        // ファイルに書き出す
    //        if (metafile) {
    //            GdipSaveImageToFile((GpImage *)&metafile, (const
    //            WCHAR
    //            *)filename,
    //                                &emfEncoderClsid, nullptr);
    //        }
    //
    //        // 再描画処理
    //        ImageClass *image = getRecordImage();
    //        delete image;
    //    }
    return ret;
}

/**
 * 記録内容の読み込み
 * @param filename 読み込みファイル名
 * @return 成功したら true
 */
bool LayerExDraw::loadRecord(const tjs_char *filename) {
    ImageClass *image;
    if(filename && (image = loadImage(filename))) {
        createRecord();
        redraw(image);
        delete image;
    }
    return false;
}

/**
 * グリフアウトラインの取得
 * @param font フォント
 * @param offset オフセット
 * @param path グリフを書き出すパス
 * @param glyph 描画するグリフ
 */
void LayerExDraw::getGlyphOutline(const FontInfo *fontInfo, PointFClass &offset,
                                  GpPath *path, UINT charcode) {
    // 加载字形
    FT_UInt glyphIndex = FT_Get_Char_Index(fontInfo->ftFace, charcode);
    if(glyphIndex == 0) {
        // 不支持此字符
        spdlog::get("plugin")->error(
            "not find Unicode >> {} << in FontFamily",
            ttstr{ (tjs_char)charcode }.AsNarrowStdString());
    }

    FT_Int32 flags = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP;
    if(FT_Load_Glyph(fontInfo->ftFace, glyphIndex, flags) != 0) {
        // 字形加载失败
        spdlog::get("plugin")->error("FT Load Glyph Failed!");
        return;
    }

    // 获取字形度量
    FT_GlyphSlot glyph = fontInfo->ftFace->glyph;

    // 字形格式检查
    if(glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
        // 非矢量字形，无法处理
        spdlog::get("plugin")->error("Not Vector Fonts Can't resolve!");
        return;
    }

    static constexpr float scaleFactor =
        1 / 64.0f + 0.009f; // 0.009f修正大小, 游戏字体一般偏小过小

    PointFClass glyphOffset{ offset.X,
                             offset.Y + fontInfo->getAscent() +
                                 fabs(fontInfo->getDescent()) +
                                 fabs(fontInfo->getDescentLeading()) +
                                 fontInfo->getAscentLeading() };

    FT_Outline outline = glyph->outline;

    const auto getPoint = [&](int index) -> PointFClass {
        return { outline.points[index].x * scaleFactor + glyphOffset.X,
                 -outline.points[index].y * scaleFactor + glyphOffset.Y };
    };

    std::function getConicEndPoint = [&](int contourStart, int index,
                                         int contourEnd) -> PointFClass {
        int nextIndex = index == contourEnd ? contourStart : index + 1;
        FT_Byte nextTag = FT_CURVE_TAG(outline.tags[nextIndex]);

        if(nextTag == FT_CURVE_TAG_ON) {
            return getPoint(nextIndex);
        }

        return (getPoint(index) + getPoint(nextIndex)) / 2.0f;
    };

    const auto addPathConicBezier =
        [&path](PointFClass startP, PointFClass controlP, PointFClass endP) {
            float t = 2.0f / 3.0f;
            PointFClass cubicP1 = startP + (controlP - startP) * t;
            PointFClass cubicP2 = endP + (controlP - endP) * t;

            GdipAddPathBezier(path, startP.X, startP.Y, cubicP1.X, cubicP1.Y,
                              cubicP2.X, cubicP2.Y, endP.X, endP.Y);
        };

    // Full Definition:
    // http://freetype.org/freetype2/docs/glyphs/glyphs-6.html
    for(int i = 0; i < outline.n_contours; ++i) {
        int contourStart = (i == 0) ? 0 : outline.contours[i - 1] + 1;
        int contourEnd = outline.contours[i];

        if(contourStart == contourEnd)
            continue;

        FT_Byte contourStartTag = FT_CURVE_TAG(outline.tags[contourStart]);
        PointFClass contourStartP = getPoint(contourStart);

        FT_Byte prevTag{}, currentTag{};
        PointFClass prevP{}, currentP{};

        for(int j = contourStart + 1; j <= contourEnd; ++j) {
            prevTag = FT_CURVE_TAG(outline.tags[j - 1]);
            currentTag = FT_CURVE_TAG(outline.tags[j]);

            prevP = getPoint(j - 1);
            currentP = getPoint(j);

            if(prevTag == FT_CURVE_TAG_CUBIC && currentTag == FT_CURVE_TAG_ON) {
                g_assert(FT_CURVE_TAG(outline.tags[j - 3]) == FT_CURVE_TAG_ON);
                g_assert(FT_CURVE_TAG(outline.tags[j - 2]) ==
                         FT_CURVE_TAG_CUBIC);

                PointFClass startP = getPoint(j - 3);
                PointFClass control = getPoint(j - 2);
                GdipAddPathBezier(path, startP.X, startP.Y, control.X,
                                  control.Y, prevP.X, prevP.Y, currentP.X,
                                  currentP.Y);
                continue;
            }

            if(prevTag == FT_CURVE_TAG_ON && currentTag == FT_CURVE_TAG_ON) {
                GdipAddPathLine(path, prevP.X, prevP.Y, currentP.X, currentP.Y);
                continue;
            }

            if(prevTag == FT_CURVE_TAG_ON && currentTag == FT_CURVE_TAG_CONIC) {
                addPathConicBezier(
                    prevP, currentP,
                    getConicEndPoint(contourStart, j, contourEnd));
                continue;
            }

            if(prevTag == FT_CURVE_TAG_CONIC &&
               currentTag == FT_CURVE_TAG_CONIC) {
                addPathConicBezier(
                    (prevP + currentP) / 2.0f, currentP,
                    getConicEndPoint(contourStart, j, contourEnd));
            }
        }

        // outline close
        if(currentTag == FT_CURVE_TAG_ON &&
           contourStartTag == FT_CURVE_TAG_ON) {
            GdipAddPathLine(path, currentP.X, currentP.Y, contourStartP.X,
                            contourStartP.Y);
        }

        if(currentTag == FT_CURVE_TAG_ON &&
           contourStartTag == FT_CURVE_TAG_CONIC) {
            addPathConicBezier(
                currentP, contourStartP,
                getConicEndPoint(contourStart, contourStart, contourEnd));
        }

        if(currentTag == FT_CURVE_TAG_CONIC &&
           contourStartTag == FT_CURVE_TAG_ON) {
            addPathConicBezier(
                prevTag == FT_CURVE_TAG_ON ? prevP : (prevP + currentP) / 2.0f,
                currentP, contourStartP);
        }

        if(currentTag == FT_CURVE_TAG_CUBIC) {

            PointFClass startP = getPoint(contourEnd - 2);
            GdipAddPathBezier(path, startP.X, startP.Y, prevP.X, prevP.Y,
                              currentP.X, currentP.Y, contourStartP.X,
                              contourStartP.Y);
        }

        GdipClosePathFigure(path);
    }

    offset.X += glyph->advance.x * scaleFactor;
}

/*
 * テキストアウトラインの取得
 * @param font フォント
 * @param offset オフセット
 * @param path グリフを書き出すパス
 * @param text 描画するテキスト
 */
void LayerExDraw::getTextOutline(const FontInfo *fontInfo, PointFClass &offset,
                                 GpPath *path, const ttstr &text) {
    if(text.IsEmpty())
        return;

    for(tjs_int i = 0; i < text.GetLen(); i++) {
        this->getGlyphOutline(fontInfo, offset, path, text[i]);
    }
}

/**
 * 文字列の描画更新領域情報の取得(OpenTypeフォント対応)
 * @param font フォント
 * @param text 描画テキスト
 * @return 更新領域情報の辞書 left, top, width, height
 */
RectFClass LayerExDraw::measureString2(const FontInfo *font,
                                       const tjs_char *text) {
    // 文字列のパスを準備
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    PointFClass offset{};
    this->getTextOutline(font, offset, path, text);
    RectFClass result;
    GdipGetPathWorldBounds(path, &result, nullptr, nullptr);
    result.X = 0;
    result.Y = 0;
    result.Width += REAL(0.167 * font->emSize * 2);
    result.Height = REAL(font->getLineSpacing() * 1.124);
    GdipDeletePath(path);
    return result;
}

/**
 * 文字列に外接する領域情報の取得(OpenTypeのPostScriptフォント対応)
 * @param font フォント
 * @param text 描画テキスト
 * @return 更新領域情報の辞書 left, top, width, height
 */
RectFClass LayerExDraw::measureStringInternal2(const FontInfo *font,
                                               const tjs_char *text) {
    // 文字列のパスを準備
    GpPath *path{};
    GdipCreatePath(FillModeAlternate, &path);
    PointFClass offset{};
    this->getTextOutline(font, offset, path, text);
    RectFClass result;
    GdipGetPathWorldBounds(path, &result, nullptr, nullptr);
    result.X = REAL(LONG(0.167 * font->emSize));
    result.Y = 0;
    result.Height = font->getLineSpacing();
    GdipDeletePath(path);
    return result;
}

/**
 * 文字列の描画(OpenTypeフォント対応)
 * @param font フォント
 * @param app アピアランス
 * @param x 描画位置X
 * @param y 描画位置Y
 * @param text 描画テキスト
 * @return 更新領域情報
 */
RectFClass LayerExDraw::drawPathString2(const FontInfo *font,
                                        const Appearance *app, REAL x, REAL y,
                                        const tjs_char *text) {
    // 文字列のパスを準備
    GpPath *path{};
    GdipCreatePath(FillModeWinding, &path);
    PointFClass offset{ x + REAL(0.167 * font->emSize) - 0.5f, y - 0.5f };
    this->getTextOutline(font, offset, path, text);
    RectFClass result = _drawPath(app, path);
    result.X = x;
    result.Y = y;
    result.Width += REAL(0.167 * font->emSize * 2);
    result.Height = REAL(font->getLineSpacing() * 1.124);
    GdipDeletePath(path);
    return result;
}

static bool getEncoder(const tjs_char *mimeType, CLSID *pClsid) {
    UINT num = 0, size = 0;
    GdipGetImageEncodersSize(&num, &size);
    if(size > 0) {
        auto *pImageCodecInfo = (ImageCodecInfo *)malloc(size);
        if(pImageCodecInfo) {
            GdipGetImageEncoders(num, size, pImageCodecInfo);
            for(UINT j = 0; j < num; ++j) {
                if(tTJSString{ reinterpret_cast<const char16_t *>(
                       pImageCodecInfo[j].MimeType) } != mimeType) {
                    *pClsid = pImageCodecInfo[j].Clsid;
                    free(pImageCodecInfo);
                    return true;
                }
            }
            free(pImageCodecInfo);
        }
    }
    return false;
}

/**
 * エンコードパラメータ情報の参照用
 */
class EncoderParameterGetter : public tTJSDispatch /** EnumMembers 用 */
{
public:
    struct EncoderInfo {
        const char *name{};
        GUID guid{};
        long value{};

        EncoderInfo(const char *name, GUID guid, long value) :
            name(name), guid(guid), value(value) {};

        EncoderInfo() = default;
    } infos[7];

    EncoderParameters *params;

    EncoderParameterGetter() {
        infos[0] = EncoderInfo("compression", GdipEncoderCompression, -1);
        infos[1] = EncoderInfo("scanmethod", GdipEncoderScanMethod, -1);
        infos[2] = EncoderInfo("version", GdipEncoderVersion, -1);
        infos[3] = EncoderInfo("render", GdipEncoderRenderMethod, -1);
        infos[4] = EncoderInfo("tansform", GdipEncoderTransformation, -1);
        infos[5] = EncoderInfo("quality", GdipEncoderQuality, -1);
        infos[6] = EncoderInfo("depth", GdipEncoderColorDepth, 24);
        params = (EncoderParameters *)malloc(sizeof(EncoderParameters) +
                                             6 * sizeof(EncoderParameter));
    };

    ~EncoderParameterGetter() override { delete params; }

    void checkResult() {
        int n = 0;
        for(auto &info : infos) {
            if(info.value >= 0) {
                params->Parameter[n].Guid = info.guid;
                params->Parameter[n].Type = EncoderParameterValueTypeLong;
                params->Parameter[n].NumberOfValues = 1;
                params->Parameter[n].Value = &info.value;
                n++;
            }
        }
        params->Count = n;
    }

    tjs_error FuncCall( // function invocation
        tjs_uint32 flag, // calling flag
        const tjs_char *membername, // member name ( nullptr for a
                                    // default member )
        tjs_uint32 *hint, // hint for the member name (in/out)
        tTJSVariant *result, // result
        tjs_int numparams, // number of parameters
        tTJSVariant **param, // parameters
        iTJSDispatch2 *objthis // object as "this"
        ) override {
        if(numparams > 1) {
            tTVInteger flag = param[1]->AsInteger();
            if(!(flag & TJS_HIDDENMEMBER)) {
                ttstr name = *param[0];
                for(auto &info : infos) {
                    if(name == info.name) {
                        info.value = (tjs_int)*param[1];
                        break;
                    }
                }
            }
        }
        if(result) {
            *result = true;
        }
        return TJS_S_OK;
    }
};

/**
 * 画像の保存
 */
tjs_error LayerExDraw::saveImage(tTJSVariant *result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *objthis) {
    // rawcallback だと hook がきいてない模様
    LayerExDraw *self =
        ncbInstanceAdaptor<LayerExDraw>::GetNativeInstance(objthis);
    if(!self) {
        self = new LayerExDraw(objthis);
        ncbInstanceAdaptor<LayerExDraw>::SetNativeInstance(objthis, self);
    }
    self->reset();

    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    ttstr filename = TVPNormalizeStorageName(param[0]->AsStringNoAddRef());
    TVPGetLocalName(filename);
    ttstr type;
    if(numparams > 1) {
        type = *param[1];
    } else {
        type = TJS_W("image/bmp");
    }
    CLSID clsid;
    if(!getEncoder(type.c_str(), &clsid)) {
        TVPThrowExceptionMessage(TJS_W("unknown format:%1"), type);
    }

    auto *caller = new EncoderParameterGetter();
    // パラメータ辞書がある
    if(numparams > 2 && param[2]->Type() == tvtObject) {
        tTJSVariantClosure closure(caller);
        param[2]->AsObjectClosureNoAddRef().EnumMembers(TJS_IGNOREPROP,
                                                        &closure, nullptr);
    }
    caller->checkResult();
    const auto *n = reinterpret_cast<const WCHAR *>(filename.c_str());
    GpStatus ret = GdipSaveImageToFile(self->bitmap, n, &clsid, caller->params);
    caller->Release();

    if(result) {
        *result = ret == 0;
    }
    return TJS_S_OK;
}

static ARGB getColor(GpBitmap *bitmap, int x, int y) {
    ARGB c;
    GdipBitmapGetPixel(bitmap, x, y, &c);
    return c;
}

tTJSVariant LayerExDraw::getColorRegionRects(ARGB color) {
    iTJSDispatch2 *array = TJSCreateArrayObject();
    if(bitmap) {
        UINT width{};
        UINT height{};
        GdipGetImageWidth(this->bitmap, &width);
        GdipGetImageHeight(this->bitmap, &height);
        GpRegion *region{};
        GdipCreateRegion(&region);
        for(int j = 0; j < height; j++) {
            for(int i = 0; i < width; i++) {
                if(getColor(bitmap, i, j) == color) {
                    int x0 = i++;
                    while(i < width && getColor(bitmap, i, j) == color)
                        i++;
                    GpRect r{ x0, j, i - x0, 1 };
                    GdipCombineRegionRectI(region, &r, CombineModeReplace);
                }
            }
        }

        // 矩形一覧取得
        GpMatrix matrix;
        int count{};
        GdipGetRegionScansCount(region, &count, &matrix);
        if(count > 0) {
            auto *rects = new RectF[count];
            GdipGetRegionScans(region, rects, &count, &matrix);
            for(int i = 0; i < count; i++) {
                RectF *rect = &rects[i];
                tTJSVariant x(rect->X);
                tTJSVariant y(rect->Y);
                tTJSVariant w(rect->Width);
                tTJSVariant h(rect->Height);
                tTJSVariant *points[4] = { &x, &y, &w, &h };
                static tjs_uint32 pushHint;
                iTJSDispatch2 *rarray = TJSCreateArrayObject();
                rarray->FuncCall(0, TJS_W("push"), &pushHint, nullptr, 4,
                                 points, rarray);
                tTJSVariant var(rarray, rarray), *param = &var;
                rarray->Release();
                array->FuncCall(0, TJS_W("push"), &pushHint, nullptr, 1, &param,
                                array);
            }
            delete[] rects;
        }
        GdipDeleteRegion(region);
    }
    tTJSVariant ret(array, array);
    array->Release();
    return ret;
}
