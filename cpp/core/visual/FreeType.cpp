//---------------------------------------------------------------------------
/*
        Risa [りさ]      alias 吉里吉里3 [kirikiri-3]
         stands for "Risa Is a Stagecraft Architecture"
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
//! @file
//! @brief FreeType フォントドライバ
//---------------------------------------------------------------------------

#include "tjsCommHead.h"
#include "FreeType.h"
#include "FontBaseline.h"
// #include "NativeFreeTypeFace.h"
// #include "uni_cp932.h"
// #include "cp932_uni.h"

#include "BinaryStream.h"
#include "MsgIntf.h"
#include "SysInitIntf.h"
#include "ComplexRect.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4819)
#endif
#include <ft2build.h>
#include FT_TRUETYPE_UNPATENTED_H
#include FT_SYNTHESIS_H
#include FT_BITMAP_H
#include FT_MULTIPLE_MASTERS_H
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "FontImpl.h"
#include "FreeTypeColor.h"
#include "FontVariations.h"

extern bool TVPEncodeUTF8ToUTF16(ttstr &output, const std::string &source);

//---------------------------------------------------------------------------

FT_Library FreeTypeLibrary = nullptr; //!< FreeType ライブラリ
static int FreeTypeRefCount = 0;
void TVPInitializeFont() {
    if(FreeTypeLibrary == nullptr) {
        FT_Error err = FT_Init_FreeType(&FreeTypeLibrary);
    }
    FreeTypeRefCount++;
}
void TVPUninitializeFreeFont() {
    if(FreeTypeRefCount > 0)
        FreeTypeRefCount--;
    if(FreeTypeRefCount <= 0 && FreeTypeLibrary) {
        FT_Done_FreeType(FreeTypeLibrary);
        FreeTypeLibrary = nullptr;
    }
}

//---------------------------------------------------------------------------
/**
 * ファイルシステム経由でのFreeType Face クラス
 */
class tGenericFreeTypeFace : public tBaseFreeTypeFace {
protected:
    FT_Face Face; //!< FreeType face オブジェクト
    tTJSBinaryStream *File; //!< tTJSBinaryStream オブジェクト
    std::vector<ttstr> FaceNames; //!< Face名を列挙した配列

private:
    FT_StreamRec Stream;

public:
    tGenericFreeTypeFace(const ttstr &fontname, tjs_uint32 options);
    ~tGenericFreeTypeFace() override;

    FT_Face GetFTFace() const override;
    void GetFaceNameList(std::vector<ttstr> &dest) const override;
    tjs_char GetDefaultChar() const override { return L' '; }

private:
    void Clear();
    static unsigned long IoFunc(FT_Stream stream, unsigned long offset,
                                unsigned char *buffer, unsigned long count);
    static void CloseFunc(FT_Stream stream);

    bool OpenFaceByIndex(tjs_uint index, FT_Face &face);
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * コンストラクタ
 * @param fontname	フォント名
 * @param options	オプション(TVP_TF_XXXX
 * 定数かTVP_FACE_OPTIONS_XXXX定数の組み合わせ)
 */
tGenericFreeTypeFace::tGenericFreeTypeFace(const ttstr &fontname,
                                           tjs_uint32 options) : File(nullptr) {
    // フィールドの初期化
    Face = nullptr;
    memset(&Stream, 0, sizeof(Stream));

    try {
        if(File) {
            delete File;
            File = nullptr;
        }

        // ファイルを開く。共有フォント登録表は TTC/OTC の face index も
        // 保持しているため、ここで受け取る。通常の Aether フォントと
        // krkrz layerExVector の別名フォントを同じポリシーで解決する。
        tjs_int registeredFaceIndex = 0;
        File = TVPCreateFontStream(fontname, &registeredFaceIndex);
        if(File == nullptr) {
            TVPThrowExceptionMessage(TVPCannotOpenFontFile, fontname);
        }

        // FT_StreamRec の各フィールドを埋める
        FT_StreamRec *fsr = &Stream;
        fsr->base = 0;
        fsr->size = static_cast<unsigned long>(File->GetSize());
        fsr->pos = 0;
        fsr->descriptor.pointer = this;
        fsr->pathname.pointer = nullptr;
        fsr->read = IoFunc;
        fsr->close = CloseFunc;

        // Face をそれぞれ開き、Face名を取得して FaceNames に格納する
        tjs_uint face_num = 1;

        FT_Face face = nullptr;

        for(tjs_uint i = 0; i < face_num; i++) {
            if(!OpenFaceByIndex(i, face)) {
                FaceNames.push_back(ttstr());
            } else {
                const char *name = face->family_name;
                ttstr wname;
                TVPEncodeUTF8ToUTF16(wname, std::string(name));
                FaceNames.push_back(wname);
                face_num = face->num_faces;
            }
        }

        if(face)
            FT_Done_Face(face), face = nullptr;

        // FreeType エンジンでファイルを開こうとしてみる
        tjs_uint index = TVP_GET_FACE_INDEX_FROM_OPTIONS(options);
        // A non-zero option is an explicit legacy face selection.  When the
        // caller only supplied a family name (the normal path), prefer the
        // face selected by the shared registry.  Face 0 remains the
        // backward-compatible default for ordinary single-face files.
        const bool explicitFaceIndex =
            (options & TVP_FACE_OPTIONS_EXPLICIT_FACE_INDEX) != 0;
        if(!explicitFaceIndex && index == 0 && registeredFaceIndex > 0)
            index = static_cast<tjs_uint>(registeredFaceIndex);
        if(!OpenFaceByIndex(index, Face)) {
            // A stale registry entry must not make an otherwise valid font
            // unusable.  Retry face 0 before reporting the original error.
            if(!explicitFaceIndex && index != 0 && OpenFaceByIndex(0, Face)) {
                index = 0;
            } else {
                // フォントを開けなかった
                TVPThrowExceptionMessage(TVPFontCannotBeUsed, fontname);
            }
        }
    } catch(...) {
        throw;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * デストラクタ
 */
tGenericFreeTypeFace::~tGenericFreeTypeFace() {
    if(Face)
        FT_Done_Face(Face), Face = nullptr;
    if(File) {
        delete File;
        File = nullptr;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * FreeType の Face オブジェクトを返す
 */
FT_Face tGenericFreeTypeFace::GetFTFace() const { return Face; }
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * このフォントファイルが持っているフォントを配列として返す
 */
void tGenericFreeTypeFace::GetFaceNameList(std::vector<ttstr> &dest) const {
    dest = FaceNames;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * FreeType 用 ストリーム読み込み関数
 */
unsigned long tGenericFreeTypeFace::IoFunc(FT_Stream stream,
                                           unsigned long offset,
                                           unsigned char *buffer,
                                           unsigned long count) {
    tGenericFreeTypeFace *_this =
        static_cast<tGenericFreeTypeFace *>(stream->descriptor.pointer);

    size_t result;
    if(count == 0) {
        // seek
        result = 0;
        _this->File->SetPosition(offset);
    } else {
        // read
        _this->File->SetPosition(offset);
        _this->File->ReadBuffer(buffer, count);
        result = count;
    }

    return (unsigned long)result;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * FreeType 用 ストリーム削除関数
 */
void tGenericFreeTypeFace::CloseFunc(FT_Stream stream) {}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * 指定インデックスのFaceを開く
 * @param index	開くindex
 * @param face	FT_Face 変数への参照
 * @return	Faceを開ければ true そうでなければ false
 * @note	初めて Face を開く場合は face で指定する変数には nullptr
 * を入れておくこと
 */
bool tGenericFreeTypeFace::OpenFaceByIndex(tjs_uint index, FT_Face &face) {
    if(face)
        FT_Done_Face(face), face = nullptr;

    FT_Open_Args args;
    memset(&args, 0, sizeof(args));
    args.flags = FT_OPEN_STREAM;
    args.stream = &Stream;
    args.driver = 0;

    FT_Error err = FT_Open_Face(FreeTypeLibrary, &args, index, &face);

    return err == 0;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * コンストラクタ
 * @param fontname	フォント名
 * @param options	オプション
 */
tFreeTypeFace::tFreeTypeFace(const ttstr &fontname, tjs_uint32 options) :
    FontName(fontname) {
    TVPInitializeFont();

    // フィールドをクリア
    Face = nullptr;
    GlyphIndexToCharcodeVector = nullptr;
    UnicodeToLocalChar = nullptr;
    LocalCharToUnicode = nullptr;
    Options = options;
    Height = 10;

    // フォントを開く
    // if(options & TVP_FACE_OPTIONS_FILE)
    try {
        // ファイルを開く
        Face = new tGenericFreeTypeFace(fontname, options);
        // 例外がここで発生する可能性があるので注意
    } catch(...) {
        TVPUninitializeFreeFont();
        throw;
    }
    // else
    {
        // ネイティブのフォント名による指定 (プラットフォーム依存)
        // Face = new tNativeFreeTypeFace(fontname, options);
        // 例外がここで発生する可能性があるので注意
    }
    FTFace = Face->GetFTFace();

    // マッピングを確認する
    if(FTFace->charmap == nullptr) {
        // FreeType は自動的に UNICODE マッピングを使用するが、
        // フォントが UNICODE マッピングの情報を含んでいない場合は
        // 自動的な文字マッピングの選択は行われない。
        // とりあえず(日本語環境に限って言えば) SJIS
        // マッピングしかもってない
        // フォントが多いのでSJISを選択させてみる。
#if 0
		FT_Error err = FT_Select_Charmap(FTFace, FT_ENCODING_SJIS);
		if(!err)
		{
			// SJIS への切り替えが成功した
			// 変換関数をセットする
 			UnicodeToLocalChar = UnicodeToSJIS;
 			LocalCharToUnicode = SJISToUnicode;
		}
		else
#else
        FT_Error err;
#endif
        {
            int numcharmap = FTFace->num_charmaps;
            for(int i = 0; i < numcharmap; i++) {
                FT_Encoding enc = FTFace->charmaps[i]->encoding;
                if(enc != FT_ENCODING_NONE && enc != FT_ENCODING_APPLE_ROMAN) {
                    err = FT_Select_Charmap(FTFace, enc);
                    if(!err) {
                        break;
                    }
                }
            }
        }
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * デストラクタ
 */
tFreeTypeFace::~tFreeTypeFace() {
    if(GlyphIndexToCharcodeVector)
        delete GlyphIndexToCharcodeVector;
    if(Face)
        delete Face;
    TVPUninitializeFreeFont();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * このFaceが保持しているglyphの数を得る
 * @return	このFaceが保持しているglyphの数
 */
tjs_uint tFreeTypeFace::GetGlyphCount() {
    if(!FTFace)
        return 0;

    // FreeType
    // が返してくるグリフの数は、実際に文字コードが割り当てられていない
    // グリフをも含んだ数となっている
    // ここで、実際にフォントに含まれているグリフを取得する
    // TODO:スレッド保護されていないので注意！！！！！！
    if(!GlyphIndexToCharcodeVector) {
        // マップが作成されていないので作成する
        GlyphIndexToCharcodeVector = new tGlyphIndexToCharcodeVector;
        FT_ULong charcode;
        FT_UInt gindex;
        charcode = FT_Get_First_Char(FTFace, &gindex);
        while(gindex != 0) {
            FT_ULong code;
            if(LocalCharToUnicode)
                code = LocalCharToUnicode(charcode);
            else
                code = charcode;
            GlyphIndexToCharcodeVector->push_back(code);
            charcode = FT_Get_Next_Char(FTFace, charcode, &gindex);
        }
        std::sort(GlyphIndexToCharcodeVector->begin(),
                  GlyphIndexToCharcodeVector->end()); // 文字コード順で並び替え
    }

    return (tjs_uint)GlyphIndexToCharcodeVector->size();
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * Glyph インデックスから対応する文字コードを得る
 * @param index
 * インデックス(FreeTypeの管理している文字indexとは違うので注意)
 * @return	対応する文字コード(対応するコードが無い場合は 0)
 */
tjs_char tFreeTypeFace::GetCharcodeFromGlyphIndex(tjs_uint index) {
    tjs_uint size = GetGlyphCount(); // グリフ数を得るついでにマップを作成する

    if(!GlyphIndexToCharcodeVector)
        return 0;
    if(index >= size)
        return 0;

    return static_cast<tjs_char>((*GlyphIndexToCharcodeVector)[index]);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * このフォントに含まれるFace名のリストを得る
 * @param dest	格納先配列
 */
void tFreeTypeFace::GetFaceNameList(std::vector<ttstr> &dest) {
    Face->GetFaceNameList(dest);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * フォントの高さを設定する
 * @param height	フォントの高さ(ピクセル単位)
 */
void tFreeTypeFace::SetHeight(int height) {
    Height = std::max(1, height);
    if(!FTFace)
        return;
    FT_Error err = FT_Set_Pixel_Sizes(FTFace, 0, Height);
    if(!err)
        return;

    // CBDT/sbix and other bitmap-strike fonts reject arbitrary pixel sizes.
    // Select the nearest available strike and let GetGlyphFromCharcode scale
    // the copied BGRA bitmap to the requested logical height.
    if(FTFace->num_fixed_sizes <= 0)
        return;
    int best = 0;
    int best_ppem = static_cast<int>(FTFace->available_sizes[0].y_ppem >> 6);
    for(int i = 1; i < FTFace->num_fixed_sizes; ++i) {
        const int ppem = static_cast<int>(FTFace->available_sizes[i].y_ppem >> 6);
        const bool best_above = best_ppem >= Height;
        const bool current_above = ppem >= Height;
        if((current_above && !best_above) ||
           (current_above && best_above && ppem < best_ppem) ||
           (!current_above && !best_above && ppem > best_ppem)) {
            best = i;
            best_ppem = ppem;
        }
    }
    FT_Select_Size(FTFace, best);
}

void tFreeTypeFace::ApplyFontVariations(tjs_int weight,
                                        const ttstr &variations) {
    if(!FTFace || !FreeTypeLibrary)
        return;

    std::vector<tTVPFontAxisCoord> requested;
    TVPFontGetEffectiveVarCoords(weight, variations, requested);
    if(requested.empty())
        return;

    FT_MM_Var *mm = nullptr;
    if(FT_Get_MM_Var(FTFace, &mm) != 0 || !mm)
        return; // static face: the compatibility property is harmless

    std::vector<FT_Fixed> coords(mm->num_axis);
    for(FT_UInt i = 0; i < mm->num_axis; ++i)
        coords[i] = mm->axis[i].def;

    for(const auto &[tag, value] : requested) {
        for(FT_UInt i = 0; i < mm->num_axis; ++i) {
            if(mm->axis[i].tag != tag)
                continue;
            const float minimum = static_cast<float>(mm->axis[i].minimum) /
                                  65536.0f;
            const float maximum = static_cast<float>(mm->axis[i].maximum) /
                                  65536.0f;
            const float clamped = std::max(minimum, std::min(maximum, value));
            const double fixed = static_cast<double>(clamped) * 65536.0;
            coords[i] = static_cast<FT_Fixed>(std::llround(fixed));
            break;
        }
    }

    // FreeType owns the axis metadata allocation.  Applying coordinates is
    // deliberately best-effort: unsupported/invalid axes fall back to the
    // face defaults, preserving the pre-variable-font rendering contract.
    FT_Set_Var_Design_Coordinates(FTFace, mm->num_axis, coords.data());
    FT_Done_MM_Var(FreeTypeLibrary, mm);
}
//---------------------------------------------------------------------------

tjs_int tFreeTypeFace::GetLineBaseline() const {
    if(!FTFace || !FTFace->size)
        return 0;
    if(FTFace->units_per_EM == 0) {
        const tjs_int ppem = FTFace->size->metrics.y_ppem;
        if(ppem <= 0)
            return Height;
        const tjs_int strike_ascent = FT_PosToInt(
            FTFace->size->metrics.ascender);
        return strike_ascent > 0 ? strike_ascent * Height / ppem : Height;
    }
    return krkr::font::ComputeLineBaseline(
        Height, FTFace->ascender, FTFace->descender,
        FTFace->size->metrics.y_ppem, FTFace->units_per_EM);
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * 指定した文字コードに対するグリフビットマップを得る
 * @param code	文字コード
 * @return	新規作成されたグリフビットマップオブジェクトへのポインタ
 *			nullptr の場合は変換に失敗した場合
 */
tTVPCharacterData *tFreeTypeFace::GetGlyphFromCharcode(tjs_uint32 code) {
    // グリフスロットにグリフを読み込み、寸法を取得する
    tGlyphMetrics metrics;
    if(!GetGlyphMetricsFromCharcode(code, metrics))
        return nullptr;

    // 文字をレンダリングする
    FT_Error err;

    if(FTFace->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
        FT_Render_Mode mode;
        if(!(Options & TVP_FACE_OPTIONS_NO_ANTIALIASING))
            mode = FT_RENDER_MODE_NORMAL;
        else
            mode = FT_RENDER_MODE_MONO;
        err = FT_Render_Glyph(FTFace->glyph, mode);
        // note: デフォルトのレンダリングモードは
        // FT_RENDER_MODE_NORMAL (256色グレースケール)
        //       FT_RENDER_MODE_MONO は 1bpp モノクローム
        if(err)
            return nullptr;
    }

    // 一応ビットマップ形式をチェック
    FT_Bitmap *ft_bmp = &(FTFace->glyph->bitmap);
    FT_Bitmap new_bmp;
    bool release_ft_bmp = false;
    tTVPCharacterData *glyph_bmp = nullptr;
    try {
        if(ft_bmp->rows && ft_bmp->width &&
           ft_bmp->pixel_mode != FT_PIXEL_MODE_BGRA) {
            // ビットマップがサイズを持っている場合
            if(ft_bmp->pixel_mode != ft_pixel_mode_grays) {
                // ft_pixel_mode_grays ではないので
                // ft_pixel_mode_grays 形式に変換する
                FT_Bitmap_New(&new_bmp);
                release_ft_bmp = true;
                ft_bmp = &new_bmp;
                err = FT_Bitmap_Convert(FTFace->glyph->library,
                                        &(FTFace->glyph->bitmap), &new_bmp, 1);
                // 結局 tGlyphBitmap
                // 形式に変換する際にアラインメントをし直すので
                // ここで指定する alignment は 1 でよい
                if(err) {
                    if(release_ft_bmp)
                        FT_Bitmap_Done(FTFace->glyph->library, ft_bmp);
                    return nullptr;
                }
            }

            if(ft_bmp->num_grays != 256) {
                // gray レベルが 256 ではない
                // 256 になるように乗算を行う
                tjs_int32 multiply =
                    static_cast<tjs_int32>((static_cast<tjs_int32>(1) << 30) -
                                           1) /
                    (ft_bmp->num_grays - 1);
                for(tjs_int y = ft_bmp->rows - 1; y >= 0; y--) {
                    unsigned char *p = ft_bmp->buffer + y * ft_bmp->pitch;
                    for(tjs_int x = ft_bmp->width - 1; x >= 0; x--) {
                        tjs_int32 v =
                            static_cast<tjs_int32>((*p * multiply) >> 22);
                        *p = static_cast<unsigned char>(v);
                        p++;
                    }
                }
            }
        }
        // 64倍されているものを解除する
        metrics.CellIncX = FT_PosToInt(metrics.CellIncX);
        metrics.CellIncY = FT_PosToInt(metrics.CellIncY);

        // tGlyphBitmap を作成して返す
        // int baseline = (int)(FTFace->height + FTFace->descender) *
        // FTFace->size->metrics.y_ppem / FTFace->units_per_EM;
        // FT_Set_Pixel_Sizes sets the em square, while KAG's font height is
        // the logical line-box height.  Clamp the face baseline once using
        // the face descent.  A glyph-specific descent would move the baseline
        // as punctuation and CJK characters are revealed next to each other.
        const int baseline = GetLineBaseline();

        if(ft_bmp->pixel_mode == FT_PIXEL_MODE_BGRA) {
            // FreeType exposes color glyphs as premultiplied BGRA.  Convert
            // every path (including an unscaled fixed strike) to Aether's
            // straight-alpha RGBA byte layout before handing it to the
            // character-data owner.  This also makes a negative-pitch bitmap
            // obey FreeType's documented row-step contract.
            const tjs_uint sw = ft_bmp->width;
            const tjs_uint sh = ft_bmp->rows;
            if(sw == 0 || sh == 0)
                return nullptr;

            const tjs_int y_ppem = FTFace->size
                ? static_cast<tjs_int>(FTFace->size->metrics.y_ppem)
                : 0;
            const double scale = y_ppem > 0
                ? static_cast<double>(Height) / y_ppem : 1.0;
            if(!std::isfinite(scale) || scale <= 0.0)
                return nullptr;
            // The requested logical height comes from script input.  Reject
            // dimensions that cannot be represented by CharacterData's
            // signed pitch/allocation instead of allowing a floating-point
            // to integer wrap to a tiny buffer (or a size_t multiplication
            // overflow) on malformed fonts or hostile scripts.
            const auto scaled_dimension = [scale](tjs_uint source,
                                                   tjs_uint &destination) {
                const long double scaled =
                    static_cast<long double>(source) * scale;
                const long double max_dimension = static_cast<long double>(
                    std::numeric_limits<tjs_int>::max());
                if(!std::isfinite(scaled) || scaled <= 0.0L ||
                   scaled > max_dimension)
                    return false;
                const long double rounded = std::floor(scaled + 0.5L);
                if(rounded < 1.0L || rounded > max_dimension)
                    return false;
                destination = static_cast<tjs_uint>(rounded);
                return destination != 0;
            };
            tjs_uint dw = 0, dh = 0;
            if(!scaled_dimension(sw, dw) || !scaled_dimension(sh, dh))
                return nullptr;
            const std::size_t max_size =
                std::numeric_limits<std::size_t>::max();
            if(dw > static_cast<tjs_uint>(
                         std::numeric_limits<tjs_int>::max() / 4) ||
               static_cast<std::size_t>(dh) >
                   max_size / (static_cast<std::size_t>(dw) * 4u))
                return nullptr;
            const tjs_int src_pitch = ft_bmp->pitch;
            const std::size_t converted_size =
                static_cast<std::size_t>(dw) * dh * 4;
            std::vector<tjs_uint8> converted(converted_size);
            auto source_row = [&](tjs_uint row) -> const tjs_uint8 * {
                // FT_Bitmap::pitch is the signed byte offset to the next
                // logical row; do not reverse rows a second time when it is
                // negative.
                return ft_bmp->buffer +
                    static_cast<std::ptrdiff_t>(row) * src_pitch;
            };
            for(tjs_uint dy = 0; dy < dh; ++dy) {
                tjs_uint sy0 = static_cast<tjs_uint>(
                    static_cast<tjs_uint64>(dy) * sh / dh);
                tjs_uint sy1 = static_cast<tjs_uint>(
                    static_cast<tjs_uint64>(dy + 1) * sh / dh);
                if(sy1 <= sy0)
                    sy1 = std::min<tjs_uint>(sh, sy0 + 1);
                for(tjs_uint dx = 0; dx < dw; ++dx) {
                    tjs_uint sx0 = static_cast<tjs_uint>(
                        static_cast<tjs_uint64>(dx) * sw / dw);
                    tjs_uint sx1 = static_cast<tjs_uint>(
                        static_cast<tjs_uint64>(dx + 1) * sw / dw);
                    if(sx1 <= sx0)
                        sx1 = std::min<tjs_uint>(sw, sx0 + 1);

                    // Average premultiplied channels before converting to
                    // straight alpha.  This is the same area rule used by
                    // the krkrz color-glyph leaf and avoids dark fringes on
                    // downscaled emoji.
                    tjs_uint64 blue = 0, green = 0, red = 0, alpha = 0;
                    tjs_uint64 count = 0;
                    for(tjs_uint sy = sy0; sy < sy1; ++sy) {
                        const tjs_uint8 *pixel = source_row(sy) +
                            static_cast<std::size_t>(sx0) * 4;
                        for(tjs_uint sx = sx0; sx < sx1; ++sx) {
                            blue += pixel[0];
                            green += pixel[1];
                            red += pixel[2];
                            alpha += pixel[3];
                            ++count;
                            pixel += 4;
                        }
                    }
                    if(count == 0)
                        continue;
                    const tjs_uint8 bgra[4] = {
                        static_cast<tjs_uint8>(blue / count),
                        static_cast<tjs_uint8>(green / count),
                        static_cast<tjs_uint8>(red / count),
                        static_cast<tjs_uint8>(alpha / count)};
                    krkr::font::ConvertFreeTypeBGRAPixel(
                        bgra, &converted[(static_cast<std::size_t>(dy) * dw +
                                          dx) * 4]);
                }
            }

            const tjs_int origin_x = static_cast<tjs_int>(
                FTFace->glyph->bitmap_left * scale +
                (FTFace->glyph->bitmap_left >= 0 ? 0.5 : -0.5));
            const tjs_int origin_top = static_cast<tjs_int>(
                FTFace->glyph->bitmap_top * scale +
                (FTFace->glyph->bitmap_top >= 0 ? 0.5 : -0.5));
            tGlyphMetrics color_metrics = metrics;
            color_metrics.CellIncX = static_cast<tjs_int>(
                color_metrics.CellIncX * scale +
                (color_metrics.CellIncX >= 0 ? 0.5 : -0.5));
            color_metrics.CellIncY = static_cast<tjs_int>(
                color_metrics.CellIncY * scale +
                (color_metrics.CellIncY >= 0 ? 0.5 : -0.5));
            glyph_bmp = new tTVPCharacterData(
                converted.data(), static_cast<tjs_int>(dw), origin_x,
                krkr::font::ComputeGlyphOriginY(baseline, origin_top), dw, dh,
                color_metrics, true);
        } else {
            glyph_bmp = new tTVPCharacterData(
                ft_bmp->buffer, ft_bmp->pitch, FTFace->glyph->bitmap_left,
                krkr::font::ComputeGlyphOriginY(
                    baseline, FTFace->glyph->bitmap_top),
                ft_bmp->width, ft_bmp->rows, metrics);
            glyph_bmp->Gray = 256;

            if(Options & TVP_TF_UNDERLINE) {
                tjs_int pos = -1, thickness = -1;
                GetUnderline(pos, thickness);
                if(pos >= 0 && thickness > 0)
                    glyph_bmp->AddHorizontalLine(pos, thickness, 255);
            }
            if(Options & TVP_TF_STRIKEOUT) {
                tjs_int pos = -1, thickness = -1;
                GetStrikeOut(pos, thickness);
                if(pos >= 0 && thickness > 0)
                    glyph_bmp->AddHorizontalLine(pos, thickness, 255);
            }
        }
    } catch(...) {
        if(release_ft_bmp)
            FT_Bitmap_Done(FTFace->glyph->library, ft_bmp);
        throw;
    }
    if(release_ft_bmp)
        FT_Bitmap_Done(FTFace->glyph->library, ft_bmp);

    return glyph_bmp;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * 指定した文字コードに対する描画領域を得る
 * @param code	文字コード
 * @return	レンダリング領域矩形へのポインタ
 *			nullptr の場合は変換に失敗した場合
 */
bool tFreeTypeFace::GetGlyphRectFromCharcode(tTVPRect &rt, tjs_uint32 code,
                                             tjs_int &advancex,
                                             tjs_int &advancey) {
    advancex = advancey = 0;
    if(!LoadGlyphSlotFromCharcode(code))
        return false;

    const int baseline = GetLineBaseline();
    /*
    FT_Render_Glyph でレンダリングしないと以下の各値は取得できない
    tjs_int t = baseline - FTFace->glyph->bitmap_top;
    tjs_int l = FTFace->glyph->bitmap_left;
    tjs_int w = FTFace->glyph->bitmap.width;
    tjs_int h = FTFace->glyph->bitmap.rows;
    */
    tjs_int t = krkr::font::ComputeGlyphOriginY(
        baseline, FT_PosToInt(FTFace->glyph->metrics.horiBearingY));
    tjs_int l = FT_PosToInt(FTFace->glyph->metrics.horiBearingX);
    tjs_int w = FT_PosToInt(FTFace->glyph->metrics.width);
    tjs_int h = FT_PosToInt(FTFace->glyph->metrics.height);
    advancex = FT_PosToInt(FTFace->glyph->advance.x);
    advancey = FT_PosToInt(FTFace->glyph->advance.y);
    rt = tTVPRect(l, t, l + w, t + h);
    if(Options & TVP_TF_UNDERLINE) {
        tjs_int pos = -1, thickness = -1;
        GetUnderline(pos, thickness);
        if(pos >= 0 && thickness > 0) {
            if(rt.left > 0)
                rt.left = 0;
            if(rt.right < advancex)
                rt.right = advancex;
            if(pos < rt.top)
                rt.top = pos;
            if((pos + thickness) >= rt.bottom)
                rt.bottom = pos + thickness + 1;
        }
    }
    if(Options & TVP_TF_STRIKEOUT) {
        tjs_int pos = -1, thickness = -1;
        GetStrikeOut(pos, thickness);
        if(pos >= 0 && thickness > 0) {
            if(rt.left > 0)
                rt.left = 0;
            if(rt.right < advancex)
                rt.right = advancex;
            if(pos < rt.top)
                rt.top = pos;
            if((pos + thickness) >= rt.bottom)
                rt.bottom = pos + thickness + 1;
        }
    }
    return true;
}

//---------------------------------------------------------------------------
/**
 * 指定した文字コードに対するグリフの寸法を得る(文字を進めるためのサイズ)
 * @param code		文字コード
 * @param metrics	寸法
 * @return	成功の場合真、失敗の場合偽
 */
bool tFreeTypeFace::GetGlyphMetricsFromCharcode(tjs_uint32 code,
                                                tGlyphMetrics &metrics) {
    if(!LoadGlyphSlotFromCharcode(code))
        return false;

    // メトリック構造体を作成
    // CellIncX や CellIncY は ピクセル値が 64 倍された値なので注意
    // これはもともと FreeType の仕様だけれども、Risaでも内部的には
    // この精度で CellIncX や CellIncY を扱う
    metrics.CellIncX = FTFace->glyph->advance.x;
    metrics.CellIncY = FTFace->glyph->advance.y;

    return true;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * 指定した文字コードに対するグリフのサイズを得る(文字の大きさ)
 * @param code		文字コード
 * @param metrics	サイズ
 * @return	成功の場合真、失敗の場合偽
 */
bool tFreeTypeFace::GetGlyphSizeFromCharcode(tjs_uint32 code,
                                             tGlyphMetrics &metrics) {
    if(!LoadGlyphSlotFromCharcode(code))
        return false;

    // メトリック構造体を作成
    metrics.CellIncX = FT_PosToInt(FTFace->glyph->metrics.horiAdvance);
    metrics.CellIncY = FT_PosToInt(FTFace->glyph->metrics.vertAdvance);

    return true;
}

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
/**
 * 指定した文字コードに対するグリフをグリフスロットに設定する
 * @param code	文字コード
 * @return	成功の場合真、失敗の場合偽
 */
bool tFreeTypeFace::LoadGlyphSlotFromCharcode(tjs_uint32 code) {
    if(!FTFace)
        return false;

    // 文字コードを得る
    FT_ULong localcode;
    if(UnicodeToLocalChar == nullptr)
        localcode = code;
    else
        localcode = UnicodeToLocalChar(code);

    // 文字コードから index を得る
    FT_UInt glyph_index = FT_Get_Char_Index(FTFace, localcode);
    if(glyph_index == 0)
        return false;

    // グリフスロットに文字を読み込む
    FT_Int32 load_glyph_flag = 0;
    if(Options & TVP_FACE_OPTIONS_COLOR) {
        // Keep embedded color strikes available.  FT_LOAD_COLOR is harmless
        // for ordinary outline-only faces and lets COLR/CBDT/sbix faces
        // expose BGRA data to GetGlyphFromCharcode.
        load_glyph_flag |= FT_LOAD_COLOR;
    } else if(!FT_IS_SCALABLE(FTFace)) {
        // Bitmap-strike-only fonts cannot satisfy FT_LOAD_NO_BITMAP.
        load_glyph_flag |= FT_LOAD_COLOR;
    } else if(!(Options & TVP_FACE_OPTIONS_NO_ANTIALIASING))
        load_glyph_flag |= FT_LOAD_NO_BITMAP;
    else
        load_glyph_flag |= FT_LOAD_TARGET_MONO;
    // note: ビットマップフォントを読み込みたくない場合は
    // FT_LOAD_NO_BITMAP を指定

    if(Options & TVP_FACE_OPTIONS_NO_HINTING)
        load_glyph_flag |= FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT;
    if(Options & TVP_FACE_OPTIONS_FORCE_AUTO_HINTING)
        load_glyph_flag |= FT_LOAD_FORCE_AUTOHINT;

    FT_Error err;
    err = FT_Load_Glyph(FTFace, glyph_index, load_glyph_flag);

    if(err)
        return false;

    // フォントの変形を行う
    if(Options & TVP_TF_BOLD)
        FT_GlyphSlot_Embolden(FTFace->glyph);
    if(Options & TVP_TF_ITALIC)
        FT_GlyphSlot_Oblique(FTFace->glyph);

    return true;
}
//---------------------------------------------------------------------------

float FT_fixed26p6_to_float(long fixP) {
    const unsigned long fractional_base = 1 << 6;
    const unsigned long fractional_mask = fractional_base - 1;
    return (float)(abs(fixP) & fractional_mask) / fractional_base + (fixP >> 6);
}

const FT_Outline *tFreeTypeFace::GetOulineData(tjs_uint32 code, float &w,
                                               float &h) {
    FT_UInt glyph_index = FT_Get_Char_Index(FTFace, code);
    if(glyph_index == 0)
        return GetOulineData(GetDefaultChar(), w, h);
    if(FT_Load_Glyph(FTFace, glyph_index, FT_LOAD_DEFAULT)) {
        return nullptr;
    }
    w = FT_fixed26p6_to_float(FTFace->glyph->advance.x);
    h = FT_fixed26p6_to_float(FTFace->glyph->advance.y);
    FT_GlyphSlot pGlyphSlot = FTFace->glyph;
    return &pGlyphSlot->outline;
}
