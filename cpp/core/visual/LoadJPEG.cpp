
#include "tjsCommHead.h"

#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"
#include "StorageIntf.h"
#include "MsgIntf.h"
#include "tvpgl.h"

#include "tjsDictionary.h"
#include "ScriptMgnIntf.h"

bool TVPAcceptSaveAsJPG(void *formatdata, const ttstr &type,
                        class iTJSDispatch2 **dic) {
    bool result = false;
    if(type.StartsWith(TJS_W("jpg")))
        result = true;
    else if(type == TJS_W(".jpg"))
        result = true;
    else if(type == TJS_W(".jpeg"))
        result = true;
    else if(type == TJS_W(".jif"))
        result = true;
    // quality 1 - 100
    // subsampling : select 0 : 4:2:0, 1 : 4:2:2, 2 : 4:4:4
    // dct : select 0 : islow, 1 : ifast, 2 : float
    // progressive : boolean
    if(result && dic) {
        tTJSVariant result;
        TVPExecuteExpression(
            TJS_W("(const)%[") TJS_W("\"quality\"=>(const)%[\"type\"="
                                     ">\"range\",\"min\"=>1,"
                                     "\"max\"=>100,\"desc\"=>\"100 "
                                     "is high quality, 1 is low "
                                     "quality\",\"default\"=>90],")
                TJS_W("\"subsampling\"=>(const)%[\"type\"=>\"select\","
                      "\"items\"=>(const)[\"4:2:0\",\"4:2:2\",\"4:4:"
                      "4\"],"
                      "\"desc\"=>\"subsampling\",\"default\"=>0],")
                    TJS_W("\"dct\"=>(const)%[\"type\"=>\"select\","
                          "\"items\"=>("
                          "const)[\"islow\",\"ifast\",\"float\"],"
                          "\"desc\"=>"
                          "\"DCT method\",\"default\"=>0],")
                        TJS_W("\"progressive\"=>(const)%[\"type\"=>"
                              "\"boolean\","
                              "\"desc\"=>\"100 is high quality, 1 is "
                              "low "
                              "quality\",\"default\"=>true]") TJS_W("]"),
            nullptr, &result);
        if(result.Type() == tvtObject) {
            *dic = result.AsObject();
        }
    }
    return result;
}

extern "C" {
#define XMD_H
#include <jpeglib.h>
#include <jerror.h>
}

#include "TVPDecodeArena.h"

//---------------------------------------------------------------------------
// Arena-aware JPEG memory manager hooks
//---------------------------------------------------------------------------
#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)

struct TVPJpegMemHooks {
    decltype(jpeg_memory_mgr::alloc_small) orig_alloc_small;
    decltype(jpeg_memory_mgr::alloc_large) orig_alloc_large;
    decltype(jpeg_memory_mgr::free_pool) orig_free_pool;
    decltype(jpeg_memory_mgr::self_destruct) orig_self_destruct;
};

static thread_local TVPJpegMemHooks sJpegHooks;

static void *JPEG_arena_alloc_small(j_common_ptr cinfo, int pool_id, size_t size) {
    if (TVPDecodeArenaActive()) {
        void *p = TVPDecodeArenaAlloc(size);
        if (p) return p;
    }
    return sJpegHooks.orig_alloc_small(cinfo, pool_id, size);
}

static void *JPEG_arena_alloc_large(j_common_ptr cinfo, int pool_id, size_t size) {
    if (TVPDecodeArenaActive()) {
        void *p = TVPDecodeArenaAlloc(size);
        if (p) return p;
    }
    return sJpegHooks.orig_alloc_large(cinfo, pool_id, size);
}

static void JPEG_arena_free_pool(j_common_ptr cinfo, int pool_id) {
    // Arena allocations bypass libjpeg's pool lists.  The original manager
    // therefore sees only allocations that fell back after the arena cap and
    // can release those safely while the arena is active.
    sJpegHooks.orig_free_pool(cinfo, pool_id);
}

static void JPEG_arena_self_destruct(j_common_ptr cinfo) {
    if (TVPDecodeArenaActive()) {
        // Restore the original vtable before destruction. Its pool lists
        // contain only fallback allocations; arena-backed pointers were
        // returned directly by the hooks and are released by arena End().
        jpeg_memory_mgr *mem = cinfo->mem;
        if (!mem) return;
        mem->alloc_small = sJpegHooks.orig_alloc_small;
        mem->alloc_large = sJpegHooks.orig_alloc_large;
        mem->free_pool = sJpegHooks.orig_free_pool;
        mem->self_destruct = sJpegHooks.orig_self_destruct;
        sJpegHooks.orig_self_destruct(cinfo);
        return;
    }
    sJpegHooks.orig_self_destruct(cinfo);
}

static void TVPInstallJpegArenaHooks(j_common_ptr cinfo) {
    jpeg_memory_mgr *mem = cinfo->mem;
    if (!mem) return;
    sJpegHooks.orig_alloc_small = mem->alloc_small;
    sJpegHooks.orig_alloc_large = mem->alloc_large;
    sJpegHooks.orig_free_pool = mem->free_pool;
    sJpegHooks.orig_self_destruct = mem->self_destruct;
    mem->alloc_small = JPEG_arena_alloc_small;
    mem->alloc_large = JPEG_arena_alloc_large;
    mem->free_pool = JPEG_arena_free_pool;
    mem->self_destruct = JPEG_arena_self_destruct;
}

#else
static void TVPInstallJpegArenaHooks(j_common_ptr) {}
#endif

//---------------------------------------------------------------------------
// JPEG loading handler
//---------------------------------------------------------------------------
tTVPJPEGLoadPrecision TVPJPEGLoadPrecision = jlpMedium;
//---------------------------------------------------------------------------
struct my_error_mgr {
    struct jpeg_error_mgr pub;
};
//---------------------------------------------------------------------------
static void my_error_exit(j_common_ptr cinfo) {
    TVPThrowExceptionMessage(TVPJPEGLoadError,
                             ttstr(TVPErrorCode) + ttstr(cinfo->err->msg_code));
}
//---------------------------------------------------------------------------
static void my_emit_message(j_common_ptr c, int n) {}
//---------------------------------------------------------------------------
static void my_output_message(j_common_ptr c) {}
//---------------------------------------------------------------------------
static void my_format_message(j_common_ptr c, char *m) {}
//---------------------------------------------------------------------------
static void my_reset_error_mgr(j_common_ptr c) {
    c->err->num_warnings = 0;
    c->err->msg_code = 0;
}
//---------------------------------------------------------------------------
#define BUFFER_SIZE 8192
struct my_source_mgr {
    jpeg_source_mgr pub;
    JOCTET *buffer;
    tTJSBinaryStream *stream;
    boolean start_of_file;
};
//---------------------------------------------------------------------------
static void init_source(j_decompress_ptr cinfo) {
    // ??
    my_source_mgr *src = (my_source_mgr *)cinfo->src;

    src->start_of_file = TRUE;
}
//---------------------------------------------------------------------------
METHODDEF(boolean)
fill_input_buffer(j_decompress_ptr cinfo) {
    my_source_mgr *src = (my_source_mgr *)cinfo->src;

    int nbytes = src->stream->Read(src->buffer, BUFFER_SIZE);

    if(nbytes <= 0) {
        if(src->start_of_file)
            ERREXIT(cinfo, JERR_INPUT_EMPTY);
        WARNMS(cinfo, JWRN_JPEG_EOF);

        src->buffer[0] = (JOCTET)0xFF;
        src->buffer[1] = (JOCTET)JPEG_EOI;
        nbytes = 2;
    }

    src->pub.next_input_byte = src->buffer;
    src->pub.bytes_in_buffer = nbytes;

    src->start_of_file = FALSE;

    return TRUE;
}
//---------------------------------------------------------------------------
static void skip_input_data(j_decompress_ptr cinfo, long num_bytes) {
    my_source_mgr *src = (my_source_mgr *)cinfo->src;

    if(num_bytes > 0) {
        while(num_bytes > (long)src->pub.bytes_in_buffer) {
            num_bytes -= (long)src->pub.bytes_in_buffer;
            fill_input_buffer(cinfo);
            /* note that we assume that fill_input_buffer will never
             * return FALSE, so suspension need not be handled.
             */
        }
        src->pub.next_input_byte += (size_t)num_bytes;
        src->pub.bytes_in_buffer -= (size_t)num_bytes;
    }
}
//---------------------------------------------------------------------------
static void term_source(j_decompress_ptr cinfo) { /* no work necessary here */ }
//---------------------------------------------------------------------------
void jpeg_TStream_src(j_decompress_ptr cinfo, tTJSBinaryStream *infile) {
    my_source_mgr *src;

    if(cinfo->src == nullptr) { /* first time for this JPEG object? */
        cinfo->src = (struct jpeg_source_mgr *)(*cinfo->mem->alloc_small)(
            (j_common_ptr)cinfo, JPOOL_PERMANENT, sizeof(my_source_mgr));
        src = (my_source_mgr *)cinfo->src;
        src->buffer = (JOCTET *)(*cinfo->mem->alloc_small)(
            (j_common_ptr)cinfo, JPOOL_PERMANENT, BUFFER_SIZE * sizeof(JOCTET));
    }

    src = (my_source_mgr *)cinfo->src;
    src->pub.init_source = init_source;
    src->pub.fill_input_buffer = fill_input_buffer;
    src->pub.skip_input_data = skip_input_data;
    src->pub.resync_to_restart =
        jpeg_resync_to_restart; /* use default method */
    src->pub.term_source = term_source;
    src->stream = infile;
    src->pub.bytes_in_buffer = 0; /* forces fill_input_buffer on first read */
    src->pub.next_input_byte = nullptr; /* until buffer loaded */
}
//---------------------------------------------------------------------------
void TVPLoadJPEG(void *formatdata, void *callbackdata,
                 tTVPGraphicSizeCallback sizecallback,
                 tTVPGraphicScanLineCallback scanlinecallback,
                 tTVPMetaInfoPushCallback metainfopushcallback,
                 tTJSBinaryStream *src, tjs_int keyidx,
                 tTVPGraphicLoadMode mode) {
    if(mode == glmPalettized)
        TVPThrowExceptionMessage(TVPJPEGLoadError,
                                 ttstr(TVPUnsupportedJpegPalette));

    jpeg_decompress_struct cinfo;
    my_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    jerr.pub.emit_message = my_emit_message;
    jerr.pub.output_message = my_output_message;
    jerr.pub.format_message = my_format_message;
    jerr.pub.reset_error_mgr = my_reset_error_mgr;

    jpeg_create_decompress(&cinfo);
    TVPInstallJpegArenaHooks((j_common_ptr)&cinfo);
    jpeg_TStream_src(&cinfo, src);
    jpeg_read_header(&cinfo, TRUE);

    switch(TVPJPEGLoadPrecision) {
        case jlpLow:
            cinfo.dct_method = JDCT_IFAST;
            cinfo.do_fancy_upsampling = FALSE;
            break;
        case jlpMedium:
            cinfo.dct_method = JDCT_ISLOW;
            cinfo.do_fancy_upsampling = TRUE;
            break;
        case jlpHigh:
            cinfo.dct_method = JDCT_FLOAT;
            cinfo.do_fancy_upsampling = TRUE;
            break;
    }

    if(mode == glmGrayscale) {
        cinfo.out_color_space = JCS_GRAYSCALE;
    } else {
        cinfo.out_color_space = JCS_EXT_RGBA;
    }

    jpeg_start_decompress(&cinfo);

    try {
        sizecallback(callbackdata, cinfo.output_width, cinfo.output_height,
                     gpfRGB);

        while(cinfo.output_scanline < cinfo.output_height) {
            tjs_int y = cinfo.output_scanline;
            void *scanline = scanlinecallback(callbackdata, y);
            if(!scanline)
                break;
            JSAMPROW row = (JSAMPROW)scanline;
            jpeg_read_scanlines(&cinfo, &row, 1);
            scanlinecallback(callbackdata, -1);
        }
    } catch(...) {
        jpeg_destroy_decompress(&cinfo);
        throw;
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
}
//---------------------------------------------------------------------------
struct stream_destination_mgr {
    struct jpeg_destination_mgr pub; /* public fields */
    tTJSBinaryStream *stream;
    JOCTET *buffer; /* buffer start address */
    int bufsize; /* size of buffer */
    int bufsizeinit; /* size of buffer */
    size_t datasize; /* final size of compressed data */
    int *outsize; /* user pointer to datasize */
};
typedef stream_destination_mgr *stream_dest_ptr;

static void JPEG_write_init_destination(j_compress_ptr cinfo) {
    stream_dest_ptr dest = (stream_dest_ptr)cinfo->dest;
    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = dest->bufsize;
    dest->datasize = 0; /* reset output size */
}

static boolean JPEG_write_empty_output_buffer(j_compress_ptr cinfo) {
    stream_dest_ptr dest = (stream_dest_ptr)cinfo->dest;

    // 足りなくなったら途中書き込み
    size_t wrotelen = dest->bufsizeinit - dest->pub.free_in_buffer;
    dest->stream->WriteBuffer(dest->buffer, (tjs_uint)wrotelen);

    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = dest->bufsize;
    return TRUE;
}

static void JPEG_write_term_destination(j_compress_ptr cinfo) {
    stream_dest_ptr dest = (stream_dest_ptr)cinfo->dest;
    dest->datasize = dest->bufsize - dest->pub.free_in_buffer;
    if(dest->outsize)
        *dest->outsize += (int)dest->datasize;
}
static void JPEG_write_stream(j_compress_ptr cinfo, JOCTET *buffer, int bufsize,
                              int *outsize, tTJSBinaryStream *stream) {
    stream_dest_ptr dest;

    /* first call for this instance - need to setup */
    if(cinfo->dest == 0) {
        cinfo->dest = (struct jpeg_destination_mgr *)(*cinfo->mem->alloc_small)(
            (j_common_ptr)cinfo, JPOOL_PERMANENT,
            sizeof(stream_destination_mgr));
    }

    dest = (stream_dest_ptr)cinfo->dest;
    dest->stream = stream;
    dest->bufsize = bufsize;
    dest->bufsizeinit = bufsize;
    dest->buffer = buffer;
    dest->outsize = outsize;
    /* set method callbacks */
    dest->pub.init_destination = JPEG_write_init_destination;
    dest->pub.empty_output_buffer = JPEG_write_empty_output_buffer;
    dest->pub.term_destination = JPEG_write_term_destination;
}
struct tTVPJPGOption {
    tjs_uint quality;
    J_DCT_METHOD dct_method;
    int subsampling;
    bool progressive;
};
//---------------------------------------------------------------------------
/**
 * JPG書き込み
 * フルカラーでの書き込みのみ対応
 * @param storagename : 出力ファイル名
 * @param mode : モード "jpg" と必要なら圧縮率を数値で後に付け足す
 * @param image : 書き出しイメージデータ
 */
void TVPSaveAsJPG(void *formatdata, tTJSBinaryStream *dst,
                  const iTVPBaseBitmap *image, const ttstr &mode,
                  iTJSDispatch2 *meta) {
    tTVPJPGOption opt = { 90, JDCT_ISLOW, 0, true };
    if(mode.StartsWith(TJS_W("jpg")) && mode.length() > 3) {
        opt.quality = 0;
        for(tjs_int len = 3; len < mode.length(); len++) {
            tjs_char c = mode[len];
            if(c >= TJS_W('0') && c <= TJS_W('9')) {
                opt.quality *= 10;
                opt.quality += c - TJS_W('0');
            }
        }
        if(opt.quality <= 0)
            opt.quality = 10;
        if(opt.quality > 100)
            opt.quality = 100;
    }

    {
        // EnumCallback を使ってプロパティを設定する
        struct MetaDictionaryEnumCallback : public tTJSDispatch {
            tTVPJPGOption *opt_;
            MetaDictionaryEnumCallback(tTVPJPGOption *opt) : opt_(opt) {}
            tjs_error FuncCall(tjs_uint32 flag, const tjs_char *membername,
                               tjs_uint32 *hint, tTJSVariant *result,
                               tjs_int numparams, tTJSVariant **param,
                               iTJSDispatch2 *objthis) override { // called from
                // tTJSCustomObject::EnumMembers
                if(numparams < 3)
                    return TJS_E_BADPARAMCOUNT;
                tjs_uint32 flags = (tjs_int)*param[1];
                if(flags & TJS_HIDDENMEMBER) { // hidden members are
                                               // not processed
                    if(result)
                        *result = (tjs_int)1;
                    return TJS_S_OK;
                }
                // push items
                ttstr value = *param[0];
                if(value == TJS_W("quality")) {
                    tjs_int64 v = param[2]->AsInteger();
                    v = v < 1 ? 1 : v > 100 ? 100 : v;
                    opt_->quality = (tjs_uint)v;
                } else if(value == TJS_W("subsampling")) {
                    // 0 : 4:2:0, 1 : 4:2:2, 2 : 4:4:4
                    tjs_int64 v = param[2]->AsInteger();
                    v = v < 0 ? 0 : v > 2 ? 2 : v;
                    opt_->subsampling = (int)v;
                } else if(value == TJS_W("dct")) {
                    tjs_int64 v = param[2]->AsInteger();
                    v = v < 0 ? 0 : v > 2 ? 2 : v;
                    switch(v) {
                        case 0:
                            opt_->dct_method = JDCT_ISLOW;
                            break;
                        case 1:
                            opt_->dct_method = JDCT_IFAST;
                            break;
                        case 2:
                            opt_->dct_method = JDCT_FLOAT;
                            break;
                    }
                } else if(value == TJS_W("progressive")) {
                    tjs_int64 v = param[2]->AsInteger();
                    if(v) {
                        opt_->progressive = true;
                    } else {
                        opt_->progressive = false;
                    }
                }
                if(result)
                    *result = (tjs_int)1;
                return TJS_S_OK;
            }
        } callback(&opt);
        tTJSVariantClosure clo(&callback, nullptr);
        meta->EnumMembers(TJS_IGNOREPROP, &clo, meta);
    }

    tjs_uint height = image->GetHeight();
    tjs_uint width = image->GetWidth();
    if(height == 0 || width == 0)
        TVPThrowInternalError;

    // open stream
    tTJSBinaryStream *stream = dst;
    struct jpeg_compress_struct cinfo;

    size_t buff_size = width * height * sizeof(tjs_uint32);
    tjs_uint8 *dest_buf = new tjs_uint8[buff_size];
    try {
        TVPClearGraphicCache();

        struct jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jerr.error_exit = my_error_exit;
        jerr.output_message = my_output_message;
        jerr.reset_error_mgr = my_reset_error_mgr;
        // emit_message, format_message はデフォルトのを使う

        jpeg_create_compress(&cinfo);

        int num_write_bytes = 0; // size of jpeg after compression
        JOCTET *jpgbuff = (JOCTET *)dest_buf; // JOCTET pointer to buffer
        JPEG_write_stream(&cinfo, jpgbuff, (int)buff_size, &num_write_bytes,
                          stream);

        cinfo.image_width = width;
        cinfo.image_height = height;
        cinfo.input_components = 4;
        cinfo.in_color_space = JCS_EXT_RGBX; // JCS_RGB;
        cinfo.dct_method = opt.dct_method; // JDCT_ISLOW;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, opt.quality, TRUE);
        if(opt.subsampling == 2) { // 444
            cinfo.comp_info[0].h_samp_factor = 1;
            cinfo.comp_info[0].v_samp_factor = 1;
            if(cinfo.num_components > 3) {
                cinfo.comp_info[3].h_samp_factor = 1;
                cinfo.comp_info[3].v_samp_factor = 1;
            }
        } else if(opt.subsampling == 1) { // 422
            cinfo.comp_info[0].h_samp_factor = 2;
            cinfo.comp_info[0].v_samp_factor = 1;
            if(cinfo.num_components > 3) {
                cinfo.comp_info[3].h_samp_factor = 2;
                cinfo.comp_info[3].v_samp_factor = 1;
            }
        } else { // 420
            cinfo.comp_info[0].h_samp_factor = 2;
            cinfo.comp_info[0].v_samp_factor = 2;
            if(cinfo.num_components > 3) {
                cinfo.comp_info[3].h_samp_factor = 2;
                cinfo.comp_info[3].v_samp_factor = 2;
            }
        }
        cinfo.comp_info[1].h_samp_factor = 1;
        cinfo.comp_info[2].h_samp_factor = 1;
        cinfo.comp_info[1].v_samp_factor = 1;
        cinfo.comp_info[2].v_samp_factor = 1;
        cinfo.optimize_coding = TRUE;
        if(opt.progressive) {
            jpeg_simple_progression(
                &cinfo); // set progressive, may be more compression
        }
        jpeg_start_compress(&cinfo, TRUE);

        while(cinfo.next_scanline < cinfo.image_height) {
            JSAMPROW row_pointer[1];
            int y = cinfo.next_scanline;
            row_pointer[0] = reinterpret_cast<JSAMPROW>(
                const_cast<void *>(image->GetScanLine(y)));
            jpeg_write_scanlines(&cinfo, row_pointer, 1);
        }
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        stream->WriteBuffer(dest_buf, num_write_bytes);
    } catch(...) {
        jpeg_destroy_compress(&cinfo);
        delete[] dest_buf;
        throw;
    }
    delete[] dest_buf;
}
//---------------------------------------------------------------------------
void TVPLoadHeaderJPG(void *formatdata, tTJSBinaryStream *src,
                      iTJSDispatch2 **dic) {
    jpeg_decompress_struct cinfo;
    my_error_mgr jerr;

    // error handling
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    jerr.pub.emit_message = my_emit_message;
    jerr.pub.output_message = my_output_message;
    jerr.pub.format_message = my_format_message;
    jerr.pub.reset_error_mgr = my_reset_error_mgr;

    jpeg_create_decompress(&cinfo);
    TVPInstallJpegArenaHooks((j_common_ptr)&cinfo);
    jpeg_TStream_src(&cinfo, src);
    jpeg_read_header(&cinfo, TRUE);

    *dic = TJSCreateDictionaryObject();
    tTJSVariant val((tjs_int64)cinfo.image_width);
    (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("width"), 0, &val, (*dic));
    val = tTJSVariant((tjs_int64)cinfo.image_height);
    (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("height"), 0, &val, (*dic));
    val = tTJSVariant((tjs_int64)24);
    (*dic)->PropSet(TJS_MEMBERENSURE, TJS_W("bpp"), 0, &val, (*dic));

    jpeg_destroy_decompress(&cinfo);
}
//---------------------------------------------------------------------------
