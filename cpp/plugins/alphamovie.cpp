#include "tjsCommHead.h"
#include "EventIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerIntf.h"
#include "StorageIntf.h"
#include "DebugIntf.h"
#include "ncbind.hpp"
#include "movie/ffmpeg/KRMovieLayer.h"
#include "tvpgl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

#define NCB_MODULE_NAME TJS_W("AlphaMovie.dll")

extern "C"
{
#include "libswscale/swscale.h"
#include <zlib.h>
#define XMD_H
#include <jpeglib.h>
#include <jerror.h>
}

namespace {

static bool AlphaMovieProfileEnabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("AETHERKIRI_AMV_PROFILE");
        return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

static uint64_t AlphaMovieProfileNowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

static void LogAlphaMovieProfile(const char* phase, tjs_int frame,
                                 uint64_t startedUs)
{
    if (!AlphaMovieProfileEnabled())
        return;
    if (const auto logger = spdlog::get("plugin"))
    {
        logger->info("AlphaMovie profile: phase={} frame={} duration_us={}", phase, frame,
                     AlphaMovieProfileNowUs() - startedUs);
    }
}

static tTJSNI_BaseLayer* GetNativeLayer(iTJSDispatch2* object)
{
    if (!object)
        return nullptr;
    tTJSNI_BaseLayer* layer = nullptr;
    tjs_error hr = object->NativeInstanceSupport(
        TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
        reinterpret_cast<iTJSNativeInstance**>(&layer));
    if (TJS_FAILED(hr))
        return nullptr;
    return layer;
}

class AlphaMovieLayer : public KRMovie::VideoPresentLayer
{
public:
    using Callback = std::function<void(KRMovieEvent, void*)>;

    explicit AlphaMovieLayer(Callback callback)
      : callback_(std::move(callback))
    {
    }

    void BuildGraph(IStream* stream, const tjs_char* streamname,
                    const tjs_char* type, uint64_t size)
    {
        m_pPlayer->SetCallback(callback_);
        m_pPlayer->OpenFromStream(stream, streamname, type, size);
    }

    void OnPlayEvent(KRMovieEvent msg, void* p) override
    {
        if (callback_)
            callback_(msg, p);
    }

private:
    Callback callback_;
};

static int ClampPositiveInt64(tjs_int64 value)
{
    if (value <= 0)
        return 0;
    if (value > static_cast<tjs_int64>(0x7fffffff))
        return 0x7fffffff;
    return static_cast<int>(value);
}

} // namespace
struct BufferManager
{
    std::unique_ptr<uint32_t[]> buffer;
    uint32_t bufferLen;

    int32_t bits_available;
    uint32_t bit_buffer;
    uint32_t* current_ptr;
    uint32_t* end_ptr;

    int32_t decodeUVStatus;
    int32_t decodeYStatus;

    BufferManager(tjs_uint8* indata, tjs_uint32 inLen)
    {
        bufferLen = inLen / 4 + 1;

        buffer = std::make_unique<uint32_t[]>(bufferLen);
        memcpy(buffer.get(), indata, inLen);
        ;

        bit_buffer = ((uint32_t)buffer[0] << 24) | ((uint32_t)indata[1] << 16) |
                     ((uint32_t)indata[2] << 8) | (uint32_t)indata[3];
        bits_available = 32;

        current_ptr = buffer.get();
        end_ptr = buffer.get() + bufferLen;

        decodeUVStatus = 0;
        decodeYStatus = 0;
    }
};
const uint32_t BIT_MASKS[33] = {
    0x00000000, 0x00000001, 0x00000003, 0x00000007, // 0-3位
    0x0000000F, 0x0000001F, 0x0000003F, 0x0000007F, // 4-7位
    0x000000FF, 0x000001FF, 0x000003FF, 0x000007FF, // 8-11位
    0x00000FFF, 0x00001FFF, 0x00003FFF, 0x00007FFF, // 12-15位
    0x0000FFFF, 0x0001FFFF, 0x0003FFFF, 0x0007FFFF, // 16-19位
    0x000FFFFF, 0x001FFFFF, 0x003FFFFF, 0x007FFFFF, // 20-23位
    0x00FFFFFF, 0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, // 24-27位
    0x0FFFFFFF, 0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, // 28-31位
    0xFFFFFFFF                                      // 32位
};
struct _ExtHuffmanTable
{
    int32_t extended_limits[20];
    int32_t extended_offsets[20];
    uint8_t extended_symbols[512];
} HuffmanTableEx[4];
struct _HuffmanTable
{
    uint8_t base_symbols[512];
    uint16_t extended_values[512];
    uint8_t run_lengths[512];
    _ExtHuffmanTable* _ext;
} HuffmanTable[4];

// These compact specs are the authoritative GoldenTime-compatible Huffman
// sources. The runtime tables above are rebuilt from them in
// EnsureAlphaMovieTablesInitialized(), matching the original AlphaMovie.dll
// builder chain.
struct _CompactHuffmanSpec
{
    uint8_t bits[16];
    uint8_t values[256];
    size_t value_count;
};

static const _CompactHuffmanSpec kJpegLumaAcSpec = {
    {0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
     0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D},
    {0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
     0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
     0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
     0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
     0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
     0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
     0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
     0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
     0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
     0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
     0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
     0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
     0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
     0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
     0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
     0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
     0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
     0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
     0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
     0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
     0xF9, 0xFA},
    162};

static const _CompactHuffmanSpec kJpegLumaDcSpec = {
    {0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
     0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
     0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B},
    12};

static const _CompactHuffmanSpec kJpegChromaAcSpec = {
    {0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
     0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77},
    {0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
     0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
     0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
     0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
     0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34,
     0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
     0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38,
     0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
     0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
     0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
     0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
     0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
     0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
     0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
     0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
     0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
     0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2,
     0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
     0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
     0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
     0xF9, 0xFA},
    162};

static const _CompactHuffmanSpec kJpegChromaDcSpec = {
    {0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
     0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
     0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B},
    11};

static int16_t SignExtendJpegValue(int value, int bit_count)
{
    if (bit_count == 0)
        return 0;

    int sign_bit = 1 << (bit_count - 1);
    if (value < sign_bit)
        return static_cast<int16_t>(value - ((1 << bit_count) - 1));

    return static_cast<int16_t>(value);
}

static size_t BuildHuffmanCodeLengths(const _CompactHuffmanSpec& spec, uint32_t* code_lengths)
{
    size_t symbol_count = 0;
    for (int bit_length = 1; bit_length <= 16; ++bit_length)
    {
        uint8_t count = spec.bits[bit_length - 1];
        for (uint8_t index = 0; index < count && symbol_count < 256; ++index)
            code_lengths[symbol_count++] = static_cast<uint32_t>(bit_length);
    }
    return symbol_count;
}

static void BuildCanonicalHuffmanCodes(const _CompactHuffmanSpec& spec,
                                       size_t symbol_count,
                                       uint32_t* codes)
{
    uint32_t code = 0;
    size_t value_index = 0;
    for (int bit_length = 1; bit_length <= 16 && value_index < symbol_count; ++bit_length)
    {
        uint8_t count = spec.bits[bit_length - 1];
        for (uint8_t code_index = 0; code_index < count && value_index < symbol_count;
             ++code_index)
        {
            codes[value_index++] = code++;
        }
        code <<= 1;
    }
}

static void BuildExtendedHuffmanTable(const _CompactHuffmanSpec& spec,
                                      size_t symbol_count,
                                      const uint32_t* codes,
                                      _ExtHuffmanTable& table)
{
    for (int index = 0; index < 20; ++index)
    {
        table.extended_limits[index] = static_cast<int32_t>(0xBAADF00D);
        table.extended_offsets[index] = static_cast<int32_t>(0xBAADF00D);
    }

    memset(table.extended_symbols, 0, sizeof(table.extended_symbols));
    if (symbol_count > 0)
        memcpy(table.extended_symbols, spec.values, symbol_count);

    size_t value_index = 0;
    for (int bit_length = 1; bit_length <= 16; ++bit_length)
    {
        int count = spec.bits[bit_length - 1];
        if (count == 0)
        {
            table.extended_limits[bit_length] = static_cast<int32_t>(0xFFFFFFFF);
            continue;
        }

        if (value_index >= symbol_count)
            break;

        table.extended_offsets[bit_length] =
            static_cast<int32_t>(value_index) - static_cast<int32_t>(codes[value_index]);

        size_t next_value_index = value_index + static_cast<size_t>(count);
        if (next_value_index > symbol_count)
            next_value_index = symbol_count;

        table.extended_limits[bit_length] = static_cast<int32_t>(codes[next_value_index - 1]);
        value_index = next_value_index;
    }

    table.extended_limits[17] = 0x000FFFFF;
}

static void BuildFastHuffmanTable(const _CompactHuffmanSpec& spec,
                                  size_t symbol_count,
                                  const uint32_t* code_lengths,
                                  const uint32_t* codes,
                                  _HuffmanTable& table,
                                  _ExtHuffmanTable& ext_table)
{
    memset(table.base_symbols, 0, sizeof(table.base_symbols));
    memset(table.extended_values, 0, sizeof(table.extended_values));
    memset(table.run_lengths, 0, sizeof(table.run_lengths));

    BuildExtendedHuffmanTable(spec, symbol_count, codes, ext_table);
    table._ext = &ext_table;

    for (size_t value_index = 0; value_index < symbol_count; ++value_index)
    {
        int bit_length = static_cast<int>(code_lengths[value_index]);
        if (bit_length < 1 || bit_length > 8)
            continue;

        uint32_t symbol = spec.values[value_index];
        uint32_t prefix = codes[value_index] << (9 - bit_length);
        uint32_t prefix_end = prefix + (1u << (9 - bit_length));
        int extra_bits = static_cast<int>(symbol & 0x0F);
        int filler_bits = 9 - bit_length - extra_bits;

        while (prefix < prefix_end && prefix < 512)
        {
            uint32_t fill_count = 1;
            int16_t base_value = 0;

            if (extra_bits != 0)
            {
                uint32_t amplitude_mask = (1u << extra_bits) - 1u;
                uint32_t amplitude_bits = 0;
                if (filler_bits >= 0)
                {
                    amplitude_bits = (prefix >> filler_bits) & amplitude_mask;
                    fill_count = 1u << filler_bits;
                }
                else
                {
                    amplitude_bits = (prefix << (-filler_bits)) & amplitude_mask;
                }

                base_value = SignExtendJpegValue(static_cast<int>(amplitude_bits), extra_bits);
            }

            uint8_t total_bits = static_cast<uint8_t>(bit_length + extra_bits);
            uint16_t stored_value = static_cast<uint16_t>(base_value);
            for (uint32_t fill_index = 0; fill_index < fill_count && prefix < 512;
                 ++fill_index, ++prefix)
            {
                table.base_symbols[prefix] = total_bits;
                table.extended_values[prefix] = stored_value;
                table.run_lengths[prefix] = static_cast<uint8_t>(symbol);
            }
        }
    }
}

static void InitializeHuffmanTable(const _CompactHuffmanSpec& spec,
                                   _HuffmanTable& table,
                                   _ExtHuffmanTable& ext_table)
{
    uint32_t code_lengths[256] = {};
    uint32_t codes[256] = {};

    size_t symbol_count = BuildHuffmanCodeLengths(spec, code_lengths);
    if (symbol_count > spec.value_count)
        symbol_count = spec.value_count;

    BuildCanonicalHuffmanCodes(spec, symbol_count, codes);
    BuildFastHuffmanTable(spec, symbol_count, code_lengths, codes, table, ext_table);
}

static void EnsureAlphaMovieTablesInitialized()
{
    static bool initialized = false;
    if (initialized)
        return;

    InitializeHuffmanTable(kJpegChromaDcSpec, HuffmanTable[0], HuffmanTableEx[0]);
    InitializeHuffmanTable(kJpegChromaAcSpec, HuffmanTable[1], HuffmanTableEx[1]);
    InitializeHuffmanTable(kJpegLumaDcSpec, HuffmanTable[2], HuffmanTableEx[2]);
    InitializeHuffmanTable(kJpegLumaAcSpec, HuffmanTable[3], HuffmanTableEx[3]);
    initialized = true;
}
const int jpeg_natural_order[DCTSIZE2 + 16] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33,
    40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54,
    47, 55, 62, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};
uint8_t decode_extended_huffman(_HuffmanTable* tbl, BufferManager* stream)
{
    uint32_t code = 0;                           // uVar7
    int bits_to_read = 9;                        // iVar8
    uint32_t* current_ptr = stream->current_ptr; // puVar6

    // 第一阶段：读取基础编码
    while (current_ptr < stream->end_ptr)
    {
        int available_bits = stream->bits_available;        // iVar1
        int bits_remaining = available_bits - bits_to_read; // uVar10

        if (bits_remaining >= 0)
        {
            // 有足够比特直接读取
            uint32_t mask = BIT_MASKS[bits_to_read]; // DAT_015bd470
            code = mask & (stream->bit_buffer >> (bits_remaining & 0x1f)) | code;
            stream->bits_available = bits_remaining;

            if (bits_remaining == 0)
            {
                // 缓冲区耗尽，重新填充
                stream->current_ptr = current_ptr + 1;
                stream->bits_available = 32;
                uint32_t new_word = current_ptr[1];
                new_word = ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
                new_word = (new_word >> 16) | (new_word << 16);
                stream->bit_buffer = new_word;
                current_ptr = stream->current_ptr;
            }
            break;
        }

        // 需要读取新数据
        uint32_t mask = BIT_MASKS[available_bits];
        uint32_t current_bits = mask & stream->bit_buffer;
        stream->current_ptr = current_ptr + 1;
        stream->bits_available = 32;
        uint32_t new_word = current_ptr[1];
        new_word = ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
        new_word = (new_word >> 16) | (new_word << 16);
        stream->bit_buffer = new_word;
        bits_to_read -= available_bits;
        code = current_bits << ((-bits_remaining) & 0x1f) | code;
        current_ptr = stream->current_ptr;

        if (bits_to_read < 1)
            break;
    }

    // 第二阶段：扩展解码
    int total_bits = 9;
    if ((int32_t)code > tbl->_ext->extended_limits[9])
    {
        do
        {
            uint32_t extra_bit = 0; // uVar10
            int bits_needed = 1;    // iVar8

            // 读取单个扩展比特
            while (current_ptr < stream->end_ptr)
            {
                int available_bits = stream->bits_available;       // iVar1
                int bits_remaining = available_bits - bits_needed; // uVar2

                if (bits_remaining >= 0)
                {
                    uint32_t mask = BIT_MASKS[bits_needed];
                    extra_bit = mask & (stream->bit_buffer >> (bits_remaining & 0x1f));
                    stream->bits_available = bits_remaining;

                    if (bits_remaining == 0)
                    {
                        stream->current_ptr = current_ptr + 1;
                        stream->bits_available = 32;
                        uint32_t new_word = current_ptr[1];
                        new_word = ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
                        new_word = (new_word >> 16) | (new_word << 16);
                        stream->bit_buffer = new_word;
                        current_ptr = stream->current_ptr;
                    }
                    break;
                }

                uint32_t mask = BIT_MASKS[available_bits];
                extra_bit = (mask & stream->bit_buffer) << ((-bits_remaining) & 0x1f);
                stream->current_ptr = current_ptr + 1;
                stream->bits_available = 32;
                uint32_t new_word = current_ptr[1];
                new_word = ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
                new_word = (new_word >> 16) | (new_word << 16);
                stream->bit_buffer = new_word;
                bits_needed -= available_bits;
                current_ptr = stream->current_ptr;

                if (bits_needed < 1)
                    break;
            }

            total_bits++;
            code = (code << 1) | extra_bit;

        } while ((int32_t)code > tbl->_ext->extended_limits[total_bits]); // param_1 + 0x808
    }
    else
    {
        total_bits = 9; // lVar9 = 9
    }
    // 返回解码符号
    return tbl->_ext->extended_symbols[tbl->_ext->extended_offsets[total_bits] + // param_1 + 0x850
                                       code                                      // uVar7
    ];
}
void decode_dc_run_length(_HuffmanTable* tbl,
                          BufferManager* stream,
                          int16_t* block,
                          int32_t& lastStatus)
{
    int32_t available_bits = stream->bits_available; // iVar11
    uint32_t bit_buffer = stream->bit_buffer;        // uVar6
    uint32_t* current_ptr = stream->current_ptr;     // puVar8

    uint32_t code = 0;                  // uVar5
    int32_t code_length = 9;            // iVar10
    uint32_t* current = current_ptr;    // puVar7
    uint32_t buffer = bit_buffer;       // uVar9
    int32_t available = available_bits; // iVar12

    // Huffman解码循环
    do
    {
        if (current >= stream->end_ptr)
            break;

        int32_t bits_remaining = available - code_length; // uVar3
        if (bits_remaining >= 0)
        {
            // 直接读取编码
            uint32_t mask = BIT_MASKS[code_length];
            code = mask & (buffer >> (bits_remaining & 0x1f)) | code;
            break;
        }

        // 读取新数据
        current++;
        code_length -= available;
        uint32_t mask = BIT_MASKS[available];
        uint32_t current_bits = mask & buffer;
        uint32_t new_word = *current;
        new_word = ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
        new_word = (new_word >> 16) | (new_word << 16);
        buffer = new_word;
        code = current_bits << ((-bits_remaining) & 0x1f) | code;
        available = 32;

    } while (code_length > 0);

    // 查找符号
    uint8_t symbol = tbl->base_symbols[code]; // bVar4
    int16_t value = 0;
    if (symbol == 0)
    {
        // 扩展编码
        symbol = decode_extended_huffman(tbl, stream);
        int extra_bits = symbol & 0x0F;
        if (extra_bits != 0)
        {
            int bits_remaining = extra_bits;         // uVar6
            uint32_t* current = stream->current_ptr; // puVar8

            while (current < stream->end_ptr)
            {
                int available_bits = stream->bits_available;     // iVar11
                int remaining = available_bits - bits_remaining; // uVar9

                if (remaining >= 0)
                {
                    uint32_t mask = BIT_MASKS[bits_remaining];
                    value = mask & (stream->bit_buffer >> (remaining & 0x1f)) | value;
                    stream->bits_available = remaining;

                    if (remaining == 0)
                    {
                        stream->current_ptr = current + 1;
                        stream->bits_available = 32;
                        uint32_t new_word = current[1];
                        new_word = ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
                        new_word = (new_word >> 16) | (new_word << 16);
                        stream->bit_buffer = new_word;
                    }
                    break;
                }

                uint32_t mask = BIT_MASKS[available_bits];
                value = (mask & stream->bit_buffer) << ((-remaining) & 0x1f) | value;
                stream->current_ptr = current + 1;
                stream->bits_available = 32;
                uint32_t new_word = current[1];
                new_word = ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
                new_word = (new_word >> 16) | (new_word << 16);
                stream->bit_buffer = new_word;
                bits_remaining -= available_bits;
                current = stream->current_ptr;

                if (bits_remaining < 1)
                    break;
            }
        }

        // 符号扩展
        if ((int)value < (1 << (extra_bits - 1)))
        {
            value = ((-1 << extra_bits) + value + 1);
        }
    }
    else
    {
        // 直接编码
        value = tbl->extended_values[code]; // uVar5

        if (symbol < 10)
        {
            // 短编码
            stream->bits_available -= (symbol & 0x1F);
            stream->current_ptr += (symbol >> 3) & 0x1C;
            if (stream->bits_available <= 0)
            {
                stream->current_ptr++;
                stream->bits_available += 32;
                uint32_t new_word = *(uint32_t*)(stream->current_ptr);
                new_word = ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
                new_word = (new_word >> 16) | (new_word << 16);
                stream->bit_buffer = new_word;
            }
        }
        else
        {
            // 长编码
            int extra_bits = symbol - 9; // iVar11
            if (current != current_ptr)
            {
                stream->current_ptr = current;
                stream->bits_available = available - code_length;
                stream->bit_buffer = *stream->current_ptr;
                stream->bit_buffer = ((stream->bit_buffer & 0xFF00FF00) >> 8) |
                                     ((stream->bit_buffer & 0x00FF00FF) << 8);
                stream->bit_buffer = (stream->bit_buffer >> 16) | (stream->bit_buffer << 16);
            }
            else
                stream->bits_available = available - 9;
            int extra_value = 0; // uVar9

            do
            {
                if (stream->current_ptr >= stream->end_ptr)
                    break;

                int available_bits = stream->bits_available;      // iVar10
                int bits_remaining = available_bits - extra_bits; // uVar3

                if (bits_remaining >= 0)
                {
                    uint32_t mask = BIT_MASKS[extra_bits];
                    extra_value =
                        mask & (stream->bit_buffer >> (bits_remaining & 0x1f)) | extra_value;
                    stream->bits_available = bits_remaining;

                    if (bits_remaining == 0)
                    {
                        stream->current_ptr++;
                        stream->bits_available = 32;
                        stream->bit_buffer = *stream->current_ptr;
                        stream->bit_buffer = ((stream->bit_buffer & 0xFF00FF00) >> 8) |
                                             ((stream->bit_buffer & 0x00FF00FF) << 8);
                        stream->bit_buffer =
                            (stream->bit_buffer >> 16) | (stream->bit_buffer << 16);
                    }
                    break;
                }

                uint32_t mask = BIT_MASKS[available_bits];
                extra_value =
                    (mask & stream->bit_buffer) << ((-bits_remaining) & 0x1f) | extra_value;
                stream->current_ptr++;
                stream->bits_available = 32;
                stream->bit_buffer = *stream->current_ptr;
                stream->bit_buffer = ((stream->bit_buffer & 0xFF00FF00) >> 8) |
                                     ((stream->bit_buffer & 0x00FF00FF) << 8);
                stream->bit_buffer = (stream->bit_buffer >> 16) | (stream->bit_buffer << 16);
                extra_bits -= available_bits;

            } while (extra_bits > 0);

            value += extra_value;
        }
    }

    // 赋值
    lastStatus += value;
    block[0] = lastStatus;
}
int decode_ac_run_length(_HuffmanTable* tbl, BufferManager* stream, int16_t* block)
{
    int coeff_index = 1; // iVar16 - 当前系数索引

    do
    {
        int32_t available_bits = stream->bits_available; // iVar14
        uint32_t bit_buffer = stream->bit_buffer;        // uVar10
        uint32_t* current_ptr = stream->current_ptr;     // puVar12

        uint32_t code = 0;                  // uVar9
        int32_t code_length = 9;            // iVar8
        uint32_t* current = current_ptr;    // puVar11
        uint32_t buffer = bit_buffer;       // uVar6
        int32_t available = available_bits; // iVar15
        do
        {
            if (current >= stream->end_ptr)
                break;

            int32_t bits_remaining = available - code_length; // uVar13
            if (bits_remaining >= 0)
            {
                uint32_t mask = BIT_MASKS[code_length];
                code = mask & (buffer >> (bits_remaining & 0x1f)) | code;
                break;
            }
            current++;
            code_length -= available;
            uint32_t mask = BIT_MASKS[available];
            uint32_t current_bits = mask & buffer;
            uint32_t new_word = *current;
            new_word = ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
            new_word = (new_word >> 16) | (new_word << 16);
            buffer = new_word;
            code = current_bits << ((-bits_remaining) & 0x1f) | code;
            available = 32;
        } while (code_length > 0);

        // 查找符号
        uint32_t lookup_addr = (uint32_t)code;           // uVar7
        uint8_t symbol = tbl->base_symbols[lookup_addr]; // bVar5
        int run_length = 0;

        if (symbol == 0)
        {
            // 扩展编码
            symbol = decode_extended_huffman(tbl, stream);
            int extra_bits = symbol & 0x0F; // uVar10
            run_length = symbol >> 4;       // uVar6
            int coefficient = 0;            // uVar9

            if (extra_bits != 0)
            {
                int bits_remaining = extra_bits;         // uVar13
                uint32_t* current = stream->current_ptr; // puVar12

                while (current < stream->end_ptr)
                {
                    int available_bits = stream->bits_available;     // iVar14
                    int remaining = available_bits - bits_remaining; // uVar2

                    if (remaining >= 0)
                    {
                        uint32_t mask = BIT_MASKS[bits_remaining];
                        coefficient =
                            mask & (stream->bit_buffer >> (remaining & 0x1f)) | coefficient;
                        stream->bits_available = remaining;

                        if (remaining == 0)
                        {
                            stream->current_ptr = current + 1;
                            stream->bits_available = 32;
                            uint32_t new_word = current[1];
                            new_word =
                                ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
                            new_word = (new_word >> 16) | (new_word << 16);
                            stream->bit_buffer = new_word;
                        }
                        break;
                    }

                    uint32_t mask = BIT_MASKS[available_bits];
                    coefficient =
                        (mask & stream->bit_buffer) << ((-remaining) & 0x1f) | coefficient;
                    stream->current_ptr = current + 1;
                    stream->bits_available = 32;
                    uint32_t new_word = current[1];
                    new_word = ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
                    new_word = (new_word >> 16) | (new_word << 16);
                    stream->bit_buffer = new_word;
                    bits_remaining -= available_bits;
                    current = stream->current_ptr;

                    if (bits_remaining < 1)
                        break;
                }
            }

            // 符号扩展
            if ((int)coefficient < (1 << (extra_bits - 1)))
            {
                coefficient = ((-1 << extra_bits) + coefficient + 1);
            }

            // 写入系数
            if (coefficient != 0)
            {
                int zigzag_index = run_length + coeff_index; // iVar14
                coeff_index = zigzag_index;
                block[jpeg_natural_order[zigzag_index]] = (int16_t)coefficient;
            }
            else
            {
                // 检查运行长度
                if (run_length != 0x0F)
                {
                    return coeff_index;
                }
                coeff_index += 0x0F;
            }
        }
        else
        {
            // 直接编码
            uint16_t value = tbl->extended_values[lookup_addr];  // uVar9
            int run_length = tbl->run_lengths[lookup_addr] >> 4; // uVar6

            if (symbol < 10)
            {
                // 短编码
                stream->bits_available -= (symbol & 0x1F);
                stream->current_ptr += (symbol >> 3) & 0x1C;
                if (stream->bits_available <= 0)
                {
                    stream->current_ptr += 1;
                    stream->bits_available += 32;
                    uint32_t new_word = *(uint32_t*)(stream->current_ptr);
                    new_word = ((new_word & 0xFF00FF00) >> 8) | ((new_word & 0x00FF00FF) << 8);
                    new_word = (new_word >> 16) | (new_word << 16);
                    stream->bit_buffer = new_word;
                }
            }
            else
            {
                // 长编码
                int extra_bits = symbol - 9; // iVar14
                if (current != current_ptr)
                {
                    stream->current_ptr = current;
                    stream->bits_available = available - code_length;
                    stream->bit_buffer = *stream->current_ptr;
                    stream->bit_buffer = ((stream->bit_buffer & 0xFF00FF00) >> 8) |
                                         ((stream->bit_buffer & 0x00FF00FF) << 8);
                    stream->bit_buffer = (stream->bit_buffer >> 16) | (stream->bit_buffer << 16);
                }
                else
                    stream->bits_available = available - 9;
                int extra_value = 0; // uVar13

                do
                {
                    if (stream->current_ptr >= stream->end_ptr)
                        break;

                    int available_bits = stream->bits_available;      // iVar8
                    int bits_remaining = available_bits - extra_bits; // uVar2

                    if (bits_remaining >= 0)
                    {
                        uint32_t mask = BIT_MASKS[extra_bits];
                        extra_value =
                            mask & (stream->bit_buffer >> (bits_remaining & 0x1f)) | extra_value;
                        stream->bits_available = bits_remaining;

                        if (bits_remaining == 0)
                        {
                            stream->current_ptr++;
                            stream->bits_available = 32;
                            stream->bit_buffer = *stream->current_ptr;
                            stream->bit_buffer = ((stream->bit_buffer & 0xFF00FF00) >> 8) |
                                                 ((stream->bit_buffer & 0x00FF00FF) << 8);
                            stream->bit_buffer =
                                (stream->bit_buffer >> 16) | (stream->bit_buffer << 16);
                        }
                        break;
                    }

                    uint32_t mask = BIT_MASKS[available_bits];
                    extra_value =
                        (mask & stream->bit_buffer) << ((-bits_remaining) & 0x1f) | extra_value;
                    stream->current_ptr++;
                    stream->bits_available = 32;
                    stream->bit_buffer = *stream->current_ptr;
                    stream->bit_buffer = ((stream->bit_buffer & 0xFF00FF00) >> 8) |
                                         ((stream->bit_buffer & 0x00FF00FF) << 8);
                    stream->bit_buffer = (stream->bit_buffer >> 16) | (stream->bit_buffer << 16);
                    extra_bits -= available_bits;

                } while (extra_bits > 0);

                value += extra_value;
            }

            // 写入系数
            if (value != 0)
            {
                int zigzag_index = run_length + coeff_index;
                coeff_index = zigzag_index;
                block[jpeg_natural_order[zigzag_index]] = (int16_t)value;
            }
            else
            {
                // 检查运行长度
                if (run_length != 0x0F)
                {
                    return coeff_index;
                }
                coeff_index += 0x0F;
            }
        }

        coeff_index++;

        // 边界检查
        if (coeff_index > 0x3F)
        {
            return coeff_index;
        }
    } while (true);
}
typedef long JLONG;
#define LEFT_SHIFT(a, b) ((JLONG)((unsigned long)(a) << (b)))
#define RIGHT_SHIFT(x, shft) ((x) >> (shft))
#define DEQUANTIZE(coef, quantval) (((int)(coef)) * (quantval))
#define PASS1_BITS 1
#define CONST_BITS 13
#define MULTIPLY(var, const) ((var) * (const))
#define ONE ((JLONG)1)
#define FIX_0_298631336 ((JLONG)2446)  /* FIX(0.298631336) */
#define FIX_0_390180644 ((JLONG)3196)  /* FIX(0.390180644) */
#define FIX_0_541196100 ((JLONG)4433)  /* FIX(0.541196100) */
#define FIX_0_765366865 ((JLONG)6270)  /* FIX(0.765366865) */
#define FIX_0_899976223 ((JLONG)7373)  /* FIX(0.899976223) */
#define FIX_1_175875602 ((JLONG)9633)  /* FIX(1.175875602) */
#define FIX_1_501321110 ((JLONG)12299) /* FIX(1.501321110) */
#define FIX_1_847759065 ((JLONG)15137) /* FIX(1.847759065) */
#define FIX_1_961570560 ((JLONG)16069) /* FIX(1.961570560) */
#define FIX_2_053119869 ((JLONG)16819) /* FIX(2.053119869) */
#define FIX_2_562915447 ((JLONG)20995) /* FIX(2.562915447) */
#define FIX_3_072711026 ((JLONG)25172) /* FIX(3.072711026) */
#define DESCALE(x, n) RIGHT_SHIFT((x) + (ONE << ((n)-1)), n)
#define MAXJSAMPLE 255
#define CENTERJSAMPLE 128
#define RANGE_MASK (MAXJSAMPLE * 4 + 3)
static JSAMPLE* sample_range_limit = nullptr;
void jpeg_idct_data(int16_t* block, uint8_t* qtbl, uint8_t* retBlock, uint32_t retBlockPitch)
{
    JLONG tmp0, tmp1, tmp2, tmp3;
    JLONG tmp10, tmp11, tmp12, tmp13;
    JLONG z1, z2, z3, z4, z5;
    JCOEFPTR inptr;
    int* quantptr;
    int* wsptr;
    JSAMPROW outptr;
    if (sample_range_limit == nullptr)
    {
        JSAMPLE* tmp_tbl = new JSAMPLE[5 * (MAXJSAMPLE + 1) + CENTERJSAMPLE];
        tmp_tbl += (MAXJSAMPLE + 1);
        sample_range_limit = tmp_tbl;
        memset(tmp_tbl - (MAXJSAMPLE + 1), 0, (MAXJSAMPLE + 1));
        for (int i = 0; i < 256; i++)
            tmp_tbl[i] = (JSAMPLE)i;
        tmp_tbl += CENTERJSAMPLE;
        for (int i = 128; i < 512; i++)
            tmp_tbl[i] = MAXJSAMPLE;
        memset(tmp_tbl + (2 * (MAXJSAMPLE + 1)), 0, 2 * (MAXJSAMPLE + 1) - CENTERJSAMPLE);
        memcpy(tmp_tbl + (4 * (MAXJSAMPLE + 1) - CENTERJSAMPLE), sample_range_limit,
               CENTERJSAMPLE * sizeof(JSAMPLE));
    }
    JSAMPLE* range_limit = ((JSAMPLE*)sample_range_limit + CENTERJSAMPLE);
    int ctr;
    int workspace[DCTSIZE2]; /* buffers data between passes */

    /* Pass 1: process columns from input, store into work array. */
    /* Note results are scaled up by sqrt(8) compared to a true IDCT; */
    /* furthermore, we scale the results by 2**PASS1_BITS. */

    inptr = block;
    int _quantptrData[64];
    for (size_t i = 0; i < 64; i++)
        _quantptrData[i] = (int)qtbl[i];
    quantptr = _quantptrData;
    wsptr = workspace;
    for (ctr = DCTSIZE; ctr > 0; ctr--)
    {
        /* Due to quantization, we will usually find that many of the input
         * coefficients are zero, especially the AC terms.  We can exploit this
         * by short-circuiting the IDCT calculation for any column in which all
         * the AC terms are zero.  In that case each output is equal to the
         * DC coefficient (with scale factor as needed).
         * With typical images and quantization tables, half or more of the
         * column DCT calculations can be simplified this way.
         */

        if (inptr[DCTSIZE * 1] == 0 && inptr[DCTSIZE * 2] == 0 && inptr[DCTSIZE * 3] == 0 &&
            inptr[DCTSIZE * 4] == 0 && inptr[DCTSIZE * 5] == 0 && inptr[DCTSIZE * 6] == 0 &&
            inptr[DCTSIZE * 7] == 0)
        {
            /* AC terms all zero */
            int dcval =
                LEFT_SHIFT(DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]), PASS1_BITS);

            wsptr[DCTSIZE * 0] = dcval;
            wsptr[DCTSIZE * 1] = dcval;
            wsptr[DCTSIZE * 2] = dcval;
            wsptr[DCTSIZE * 3] = dcval;
            wsptr[DCTSIZE * 4] = dcval;
            wsptr[DCTSIZE * 5] = dcval;
            wsptr[DCTSIZE * 6] = dcval;
            wsptr[DCTSIZE * 7] = dcval;

            inptr++; /* advance pointers to next column */
            quantptr++;
            wsptr++;
            continue;
        }

        /* Even part: reverse the even part of the forward DCT. */
        /* The rotator is sqrt(2)*c(-6). */

        z2 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
        z3 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);

        z1 = MULTIPLY(z2 + z3, FIX_0_541196100);
        tmp2 = z1 + MULTIPLY(z3, -FIX_1_847759065);
        tmp3 = z1 + MULTIPLY(z2, FIX_0_765366865);

        z2 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
        z3 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);

        tmp0 = LEFT_SHIFT(z2 + z3, CONST_BITS);
        tmp1 = LEFT_SHIFT(z2 - z3, CONST_BITS);

        tmp10 = tmp0 + tmp3;
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;

        /* Odd part per figure 8; the matrix is unitary and hence its
         * transpose is its inverse.  i0..i3 are y7,y5,y3,y1 respectively.
         */

        tmp0 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
        tmp1 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
        tmp2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
        tmp3 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);

        z1 = tmp0 + tmp3;
        z2 = tmp1 + tmp2;
        z3 = tmp0 + tmp2;
        z4 = tmp1 + tmp3;
        z5 = MULTIPLY(z3 + z4, FIX_1_175875602); /* sqrt(2) * c3 */

        tmp0 = MULTIPLY(tmp0, FIX_0_298631336); /* sqrt(2) * (-c1+c3+c5-c7) */
        tmp1 = MULTIPLY(tmp1, FIX_2_053119869); /* sqrt(2) * ( c1+c3-c5+c7) */
        tmp2 = MULTIPLY(tmp2, FIX_3_072711026); /* sqrt(2) * ( c1+c3+c5-c7) */
        tmp3 = MULTIPLY(tmp3, FIX_1_501321110); /* sqrt(2) * ( c1+c3-c5-c7) */
        z1 = MULTIPLY(z1, -FIX_0_899976223);    /* sqrt(2) * ( c7-c3) */
        z2 = MULTIPLY(z2, -FIX_2_562915447);    /* sqrt(2) * (-c1-c3) */
        z3 = MULTIPLY(z3, -FIX_1_961570560);    /* sqrt(2) * (-c3-c5) */
        z4 = MULTIPLY(z4, -FIX_0_390180644);    /* sqrt(2) * ( c5-c3) */

        z3 += z5;
        z4 += z5;

        tmp0 += z1 + z3;
        tmp1 += z2 + z4;
        tmp2 += z2 + z3;
        tmp3 += z1 + z4;

        /* Final output stage: inputs are tmp10..tmp13, tmp0..tmp3 */

        wsptr[DCTSIZE * 0] = (int)DESCALE(tmp10 + tmp3, CONST_BITS - PASS1_BITS);
        wsptr[DCTSIZE * 7] = (int)DESCALE(tmp10 - tmp3, CONST_BITS - PASS1_BITS);
        wsptr[DCTSIZE * 1] = (int)DESCALE(tmp11 + tmp2, CONST_BITS - PASS1_BITS);
        wsptr[DCTSIZE * 6] = (int)DESCALE(tmp11 - tmp2, CONST_BITS - PASS1_BITS);
        wsptr[DCTSIZE * 2] = (int)DESCALE(tmp12 + tmp1, CONST_BITS - PASS1_BITS);
        wsptr[DCTSIZE * 5] = (int)DESCALE(tmp12 - tmp1, CONST_BITS - PASS1_BITS);
        wsptr[DCTSIZE * 3] = (int)DESCALE(tmp13 + tmp0, CONST_BITS - PASS1_BITS);
        wsptr[DCTSIZE * 4] = (int)DESCALE(tmp13 - tmp0, CONST_BITS - PASS1_BITS);

        inptr++; /* advance pointers to next column */
        quantptr++;
        wsptr++;
    }

    /* Pass 2: process rows from work array, store into output array. */
    /* Note that we must descale the results by a factor of 8 == 2**3, */
    /* and also undo the PASS1_BITS scaling. */

    wsptr = workspace;
    for (ctr = 0; ctr < DCTSIZE; ctr++)
    {
        outptr = retBlock + retBlockPitch * ctr;
        /* Rows of zeroes can be exploited in the same way as we did with columns.
         * However, the column calculation has created many nonzero AC terms, so
         * the simplification applies less often (typically 5% to 10% of the time).
         * On machines with very fast multiplication, it's possible that the
         * test takes more time than it's worth.  In that case this section
         * may be commented out.
         */

        if (wsptr[1] == 0 && wsptr[2] == 0 && wsptr[3] == 0 && wsptr[4] == 0 && wsptr[5] == 0 &&
            wsptr[6] == 0 && wsptr[7] == 0)
        {
            /* AC terms all zero */
            JSAMPLE dcval = range_limit[(int)DESCALE((JLONG)wsptr[0], PASS1_BITS + 3) & RANGE_MASK];

            outptr[0] = dcval;
            outptr[1] = dcval;
            outptr[2] = dcval;
            outptr[3] = dcval;
            outptr[4] = dcval;
            outptr[5] = dcval;
            outptr[6] = dcval;
            outptr[7] = dcval;

            wsptr += DCTSIZE; /* advance pointer to next row */
            continue;
        }

        /* Even part: reverse the even part of the forward DCT. */
        /* The rotator is sqrt(2)*c(-6). */

        z2 = (JLONG)wsptr[2];
        z3 = (JLONG)wsptr[6];

        z1 = MULTIPLY(z2 + z3, FIX_0_541196100);
        tmp2 = z1 + MULTIPLY(z3, -FIX_1_847759065);
        tmp3 = z1 + MULTIPLY(z2, FIX_0_765366865);

        tmp0 = LEFT_SHIFT((JLONG)wsptr[0] + (JLONG)wsptr[4], CONST_BITS);
        tmp1 = LEFT_SHIFT((JLONG)wsptr[0] - (JLONG)wsptr[4], CONST_BITS);

        tmp10 = tmp0 + tmp3;
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;

        /* Odd part per figure 8; the matrix is unitary and hence its
         * transpose is its inverse.  i0..i3 are y7,y5,y3,y1 respectively.
         */

        tmp0 = (JLONG)wsptr[7];
        tmp1 = (JLONG)wsptr[5];
        tmp2 = (JLONG)wsptr[3];
        tmp3 = (JLONG)wsptr[1];

        z1 = tmp0 + tmp3;
        z2 = tmp1 + tmp2;
        z3 = tmp0 + tmp2;
        z4 = tmp1 + tmp3;
        z5 = MULTIPLY(z3 + z4, FIX_1_175875602); /* sqrt(2) * c3 */

        tmp0 = MULTIPLY(tmp0, FIX_0_298631336); /* sqrt(2) * (-c1+c3+c5-c7) */
        tmp1 = MULTIPLY(tmp1, FIX_2_053119869); /* sqrt(2) * ( c1+c3-c5+c7) */
        tmp2 = MULTIPLY(tmp2, FIX_3_072711026); /* sqrt(2) * ( c1+c3+c5-c7) */
        tmp3 = MULTIPLY(tmp3, FIX_1_501321110); /* sqrt(2) * ( c1+c3-c5-c7) */
        z1 = MULTIPLY(z1, -FIX_0_899976223);    /* sqrt(2) * ( c7-c3) */
        z2 = MULTIPLY(z2, -FIX_2_562915447);    /* sqrt(2) * (-c1-c3) */
        z3 = MULTIPLY(z3, -FIX_1_961570560);    /* sqrt(2) * (-c3-c5) */
        z4 = MULTIPLY(z4, -FIX_0_390180644);    /* sqrt(2) * ( c5-c3) */

        z3 += z5;
        z4 += z5;

        tmp0 += z1 + z3;
        tmp1 += z2 + z4;
        tmp2 += z2 + z3;
        tmp3 += z1 + z4;

        /* Final output stage: inputs are tmp10..tmp13, tmp0..tmp3 */

        outptr[0] =
            range_limit[(int)DESCALE(tmp10 + tmp3, CONST_BITS + PASS1_BITS + 3) & RANGE_MASK];
        outptr[7] =
            range_limit[(int)DESCALE(tmp10 - tmp3, CONST_BITS + PASS1_BITS + 3) & RANGE_MASK];
        outptr[1] =
            range_limit[(int)DESCALE(tmp11 + tmp2, CONST_BITS + PASS1_BITS + 3) & RANGE_MASK];
        outptr[6] =
            range_limit[(int)DESCALE(tmp11 - tmp2, CONST_BITS + PASS1_BITS + 3) & RANGE_MASK];
        outptr[2] =
            range_limit[(int)DESCALE(tmp12 + tmp1, CONST_BITS + PASS1_BITS + 3) & RANGE_MASK];
        outptr[5] =
            range_limit[(int)DESCALE(tmp12 - tmp1, CONST_BITS + PASS1_BITS + 3) & RANGE_MASK];
        outptr[3] =
            range_limit[(int)DESCALE(tmp13 + tmp0, CONST_BITS + PASS1_BITS + 3) & RANGE_MASK];
        outptr[4] =
            range_limit[(int)DESCALE(tmp13 - tmp0, CONST_BITS + PASS1_BITS + 3) & RANGE_MASK];

        wsptr += DCTSIZE; /* advance pointer to next row */
    }
}

#define NCB_MODULE_NAME TJS_W("AlphaMovie.dll")

struct AlphaMovieHeader
{
    char magic[4];
    tjs_uint32 size_of_file;
    tjs_uint32 revision;
    tjs_uint32 quantaization_table_size_plus_hdr_size;
    tjs_uint32 unk;
    tjs_uint32 frame_cnt;
    tjs_uint32 unk2;
    tjs_uint32 frame_rate;
    tjs_uint16 width;
    tjs_uint16 height;
    tjs_uint32 alpha_decode_attr;

    bool parseAMVHeader(tTJSBinaryStream* stream)
    {
        stream->ReadBuffer(magic, 4);
        stream->ReadBuffer(&size_of_file, 4);
        stream->ReadBuffer(&revision, 4);
        stream->ReadBuffer(&quantaization_table_size_plus_hdr_size, 4);
        stream->ReadBuffer(&unk, 4);
        stream->ReadBuffer(&frame_cnt, 4);
        stream->ReadBuffer(&unk2, 4);
        stream->ReadBuffer(&frame_rate, 4);
        stream->ReadBuffer(&width, 2);
        stream->ReadBuffer(&height, 2);
        stream->ReadBuffer(&alpha_decode_attr, 4);
        return memcmp(magic, "AJPM", 4) == 0;
    }
};

struct AlphaMovieFrame
{
    // Common Frame Header
    char magic[4];
    tjs_uint32 size_of_frame;
    tjs_uint32 index_of_cur_frame;
    tjs_uint16 alpha_channel_width;
    tjs_uint16 alpha_channel_height;
    tjs_uint16 frame_width;
    tjs_uint16 frame_height;

    // ZLIB Ext Data
    tjs_uint32 zlib_buffer_size;

    // Frame pixels are decoded only when they are presented.  Retaining the
    // decoded RGBA pixels for every frame makes even a single full-HD AMV
    // consume several gigabytes on mobile devices.
    tjs_uint8* ptrData = nullptr;
    // Absolute stream position of the encoded payload.  The frame index is
    // built once at open() time so lazy presentation can seek directly to a
    // frame instead of reparsing every preceding frame header.
    tjs_uint64 data_offset = 0;

    bool parseAMVFrame(tTJSBinaryStream* stream, bool isZlib)
    {
        // ptrPose = stream->GetPosition() + (isZlib ? 24 : 20);
        stream->ReadBuffer(magic, 4);
        stream->ReadBuffer(&size_of_frame, 4);
        size_of_frame -= (isZlib ? 16 : 12);
        stream->ReadBuffer(&index_of_cur_frame, 4);
        stream->ReadBuffer(&alpha_channel_width, 2);
        stream->ReadBuffer(&alpha_channel_height, 2);
        stream->ReadBuffer(&frame_width, 2);
        stream->ReadBuffer(&frame_height, 2);
        if (isZlib)
            stream->ReadBuffer(&zlib_buffer_size, 4);
        return true;
    }
};

struct AlphaMovieReadyFrame
{
    tjs_uint8* ptrData = nullptr;
    tjs_uint16 width = 0;
    tjs_uint16 height = 0;
};

class tTJSNI_AlphaMovie : public tTJSNativeInstance,
                          public tTVPContinuousEventCallbackIntf
{
public:
    tTJSNI_AlphaMovie();
    ~tTJSNI_AlphaMovie();

    void OnContinuousCallback(tjs_uint64 tick) override;

    void open(tTJSString fileName);
    void clear();
    tjs_int showNextImage(tTJSVariant layer);
    bool isPlaying();
    void play();
    void stop();
    void setPosition(tjs_int x, tjs_int y);
    void setNextMovieFile(tTJSString fileName);

    tjs_int GetNumOfFrame() { return getNumberOfFrames(); }
    void SetNumOfFrame(tjs_int numOfFrame) { _numOfFrame = numOfFrame; }
    tjs_int GetFrame() { return getFrame(); }
    void SetFrame(tjs_int frame) { setFrame(frame); }
    bool GetLoop() { return _loop; }
    void SetLoop(bool loop) { _loop = loop; }
    bool GetNextLoop() { return _nextLoop; }
    void SetNextLoop(bool nextLoop) { _nextLoop = nextLoop; }
    tjs_int GetPreloadSamples() { return _preloadSamples; }
    void SetPreloadSamples(tjs_int preloadSamples) { _preloadSamples = preloadSamples; }
    tjs_int GetLeft() { return _left; }
    void SetLeft(tjs_int left) { _left = left; }
    tjs_int GetTop() { return _top; }
    void SetTop(tjs_int top) { _top = top; }
    tjs_int GetScreenWidth() { return _screenWidth; }
    void SetScreenWidth(tjs_int screenWidth) { _screenWidth = screenWidth; }
    tjs_int GetScreenHeight() { return _screenHeight; }
    void SetScreenHeight(tjs_int screenHeight) { _screenHeight = screenHeight; }
    tjs_real GetFPSScale() { return _FPSScale; }
    void SetFPSScale(tjs_real fpsScale) { _FPSScale = fpsScale; }
    tjs_real GetFPSRate() { return getFPSRate(); }
    void SetFPSRate(tjs_real fpsRate) { _FPSRate = fpsRate; }

private:
    enum class Backend
    {
        None,
        Amv,
        Ffmpeg,
    };

    void openFfmpeg(tTJSString fileName, tTJSBinaryStream* stream);
    void closeFfmpeg();
    void queueFfmpegEvent(KRMovieEvent msg, void* payload);
    void processFfmpegEvents();
    tTVPBaseTexture* takeFfmpegFrameForPresentation();
    tjs_int showNextFfmpegImage(tTJSVariant layer);
    void startAmvOpenAsync(tTJSString fileName);
    bool finishAmvOpenAsync();
    void cancelAmvOpenAsync();
    void removeAmvOpenCallback();
    void startAmvDecodeWorker();
    void stopAmvDecodeWorker();
    void enqueueAmvDecode(tjs_int frameIndex);
    void pruneAmvDecodeFrames(tjs_int currentFrameIndex, tjs_int nextFrameIndex);
    tjs_uint8* takeAmvReadyFrame(tjs_int frameIndex, tjs_uint16* width,
                                  tjs_uint16* height);
    void amvDecodeWorkerMain();
    bool indexAmvFramesThrough(tjs_int frameIndex);
    void decodeAmvFrame(tjs_int frameIndex);
    tjs_int getFrame();
    void setFrame(tjs_int frame);
    tjs_int getNumberOfFrames();
    tjs_real getFPSRate();

    Backend backend_ = Backend::None;

    tTJSBinaryStream* filePtr = nullptr;
    AlphaMovieHeader _header = {0};
    std::vector<AlphaMovieFrame> frameInfoList;
    ttstr amvFileName_;
    tjs_uint64 amvFirstFrameOffset_ = 0;
    size_t amvIndexedFrames_ = 0;
    std::thread amvOpenThread_;
    std::mutex amvOpenMutex_;
    tTJSBinaryStream* amvOpenStream_ = nullptr;
    AlphaMovieHeader amvOpenHeader_ = {0};
    uint8_t amvOpenQtbl_[3][64] = {0};
    bool amvOpenInFlight_ = false;
    bool amvOpenReady_ = false;
    bool amvOpenFailed_ = false;
    bool amvOpenCallbackRegistered_ = false;
    tTJSVariant pendingAmvLayer_;
    bool amvDecodeRequested_ = false;
    tjs_int amvFrameToDecode_ = -1;
    std::thread amvDecodeThread_;
    std::mutex amvDecodeMutex_;
    std::condition_variable amvDecodeCv_;
    std::deque<tjs_int> amvDecodeQueue_;
    std::vector<AlphaMovieReadyFrame> amvReadyFrames_;
    bool amvDecodeStop_ = false;
    // The AMV decoder is single-threaded per movie. Reuse its transient
    // compressed/YUV/alpha work buffers instead of allocating several large
    // blocks for every frame; the decoded RGBA frame remains separately owned
    // until the render thread presents it.
    std::vector<tjs_uint8> amvScratchCache_;
    std::vector<tjs_uint8> amvScratchAlpha_;
    std::vector<tjs_uint8> amvScratchYuva_;
    std::vector<int16_t> amvScratchIdc_;
    SwsContext* amvImageConvertContext_ = nullptr;
    uint8_t qtbl[3][64] = {0};
    tTVPBaseTexture* m_BmpBits = nullptr;

    AlphaMovieLayer* ffmpegMovie_ = nullptr;
    tTJSBinaryStream* ffmpegStream_ = nullptr;
    tTVPBaseTexture* ffmpegBuffers_[2]{};
    long ffmpegSourceWidth_ = 0;
    long ffmpegSourceHeight_ = 0;
    bool ffmpegPlayRequested_ = false;
    std::mutex ffmpegEventMutex_;
    std::vector<KRMovieEvent> ffmpegPendingEvents_;

    bool _isPlaying = false;
    tjs_int _numOfFrame = 0;
    tjs_int _frame = 0;
    bool _loop = false;
    bool _nextLoop = false;
    tjs_int _preloadSamples = 0;
    tjs_int _left = 0;
    tjs_int _top = 0;
    tjs_int _screenWidth = 0;
    tjs_int _screenHeight = 0;
    tjs_real _FPSScale = 1.0;
    tjs_real _FPSRate = 1.0;
};

tTJSNI_AlphaMovie::tTJSNI_AlphaMovie()
{
}

tTJSNI_AlphaMovie::~tTJSNI_AlphaMovie()
{
    clear();
}

void tTJSNI_AlphaMovie::removeAmvOpenCallback()
{
    if (!amvOpenCallbackRegistered_)
        return;
    TVPRemoveContinuousEventHook(this);
    amvOpenCallbackRegistered_ = false;
}

void tTJSNI_AlphaMovie::OnContinuousCallback(tjs_uint64)
{
    if (!amvOpenCallbackRegistered_ || backend_ != Backend::Amv ||
        pendingAmvLayer_.Type() == tvtVoid)
    {
        removeAmvOpenCallback();
        pendingAmvLayer_.Clear();
        return;
    }

    if (!finishAmvOpenAsync())
    {
        bool inFlight = false;
        {
            std::lock_guard<std::mutex> lock(amvOpenMutex_);
            inFlight = amvOpenInFlight_;
        }
        if (!inFlight)
        {
            removeAmvOpenCallback();
            pendingAmvLayer_.Clear();
        }
        return;
    }

    // Take a strong reference to the layer while dispatching.  The pending
    // variant is cleared before calling back into showNextImage() so a
    // re-entrant clear/open cannot leave a stale closure behind.
    tTJSVariant layer = pendingAmvLayer_;
    pendingAmvLayer_.Clear();
    removeAmvOpenCallback();
    showNextImage(layer);
}

void tTJSNI_AlphaMovie::startAmvOpenAsync(tTJSString fileName)
{
    cancelAmvOpenAsync();
    {
        std::lock_guard<std::mutex> lock(amvOpenMutex_);
        amvOpenInFlight_ = true;
        amvOpenReady_ = false;
        amvOpenFailed_ = false;
    }

    amvOpenThread_ = std::thread([this, fileName] {
        const uint64_t profileStartedUs =
            AlphaMovieProfileEnabled() ? AlphaMovieProfileNowUs() : 0;
        tTJSBinaryStream* stream = nullptr;
        AlphaMovieHeader header = {0};
        uint8_t tables[3][64] = {0};
        bool succeeded = false;
        try
        {
            stream = TVPCreateStream(ttstr(fileName), TJS_BS_READ);
            if (stream != nullptr && stream->GetSize() >= 40)
            {
                char magic[4] = {};
                stream->SetPosition(0);
                stream->ReadBuffer(magic, sizeof(magic));
                stream->SetPosition(0);
                if (memcmp(magic, "AJPM", sizeof(magic)) == 0 &&
                    header.parseAMVHeader(stream))
                {
                    const size_t tableCount = header.alpha_decode_attr == 2 ? 2 : 3;
                    const size_t tableBytes = tableCount * 64;
                    if (header.quantaization_table_size_plus_hdr_size >= 40 &&
                        header.quantaization_table_size_plus_hdr_size - 40 == tableBytes)
                    {
                        for (size_t i = 0; i < tableCount; ++i)
                            stream->ReadBuffer(tables[i], 64);
                        succeeded = true;
                    }
                }
            }
        }
        catch (...)
        {
            succeeded = false;
        }

        {
            std::lock_guard<std::mutex> lock(amvOpenMutex_);
            if (succeeded)
            {
                amvOpenStream_ = stream;
                stream = nullptr;
                amvOpenHeader_ = header;
                memcpy(amvOpenQtbl_, tables, sizeof(tables));
                amvOpenReady_ = true;
            }
            else
            {
                amvOpenFailed_ = true;
            }
            amvOpenInFlight_ = false;
        }
        if (stream != nullptr)
            delete stream;
        LogAlphaMovieProfile("async_open", -1, profileStartedUs);
    });
}

bool tTJSNI_AlphaMovie::finishAmvOpenAsync()
{
    tTJSBinaryStream* stream = nullptr;
    AlphaMovieHeader header = {0};
    uint8_t tables[3][64] = {0};
    bool failed = false;
    {
        std::lock_guard<std::mutex> lock(amvOpenMutex_);
        if (amvOpenInFlight_)
            return false;
        if (!amvOpenReady_)
        {
            failed = amvOpenFailed_;
            if (!failed)
                return filePtr != nullptr && !frameInfoList.empty();
        }
        else
        {
            stream = amvOpenStream_;
            amvOpenStream_ = nullptr;
            header = amvOpenHeader_;
            memcpy(tables, amvOpenQtbl_, sizeof(tables));
            amvOpenReady_ = false;
            amvOpenFailed_ = false;
        }
    }

    // The loader has already published (or rejected) its stream under
    // amvOpenMutex_, so presentation can adopt it without synchronizing on
    // thread termination.  Joining here can still block a render tick while
    // the worker unwinds archive-local state; clear() and the next open()
    // perform that join outside the active playback handoff.
    if (failed || stream == nullptr)
    {
        backend_ = Backend::None;
        return false;
    }

    filePtr = stream;
    _header = header;
    memcpy(qtbl, tables, sizeof(qtbl));
    _numOfFrame = _header.frame_cnt;
    _FPSRate = _header.frame_rate;
    _screenWidth = _header.width;
    _screenHeight = _header.height;
    frameInfoList.assign(static_cast<size_t>(_numOfFrame), AlphaMovieFrame{});
    amvReadyFrames_.assign(static_cast<size_t>(_numOfFrame), AlphaMovieReadyFrame{});
    amvFirstFrameOffset_ = _header.quantaization_table_size_plus_hdr_size;
    amvIndexedFrames_ = 0;
    if (m_BmpBits == nullptr)
        m_BmpBits = new tTVPBaseTexture(_header.width, _header.height, 32);
    _frame = 0;
    startAmvDecodeWorker();
    return true;
}

void tTJSNI_AlphaMovie::cancelAmvOpenAsync()
{
    if (amvOpenThread_.joinable())
        amvOpenThread_.join();

    std::lock_guard<std::mutex> lock(amvOpenMutex_);
    if (amvOpenStream_ != nullptr)
    {
        delete amvOpenStream_;
        amvOpenStream_ = nullptr;
    }
    amvOpenInFlight_ = false;
    amvOpenReady_ = false;
    amvOpenFailed_ = false;
}

void tTJSNI_AlphaMovie::startAmvDecodeWorker()
{
    stopAmvDecodeWorker();
    {
        std::lock_guard<std::mutex> lock(amvDecodeMutex_);
        amvDecodeStop_ = false;
        amvDecodeQueue_.clear();
    }
    amvDecodeThread_ = std::thread(&tTJSNI_AlphaMovie::amvDecodeWorkerMain, this);
}

void tTJSNI_AlphaMovie::stopAmvDecodeWorker()
{
    {
        std::lock_guard<std::mutex> lock(amvDecodeMutex_);
        amvDecodeStop_ = true;
        amvDecodeQueue_.clear();
    }
    amvDecodeCv_.notify_all();
    if (amvDecodeThread_.joinable())
        amvDecodeThread_.join();

    std::lock_guard<std::mutex> lock(amvDecodeMutex_);
    for (auto& ready : amvReadyFrames_)
    {
        delete[] ready.ptrData;
        ready = AlphaMovieReadyFrame{};
    }
    amvDecodeQueue_.clear();
}

void tTJSNI_AlphaMovie::enqueueAmvDecode(tjs_int frameIndex)
{
    if (frameIndex < 0 || frameIndex >= _numOfFrame)
        return;
    std::lock_guard<std::mutex> lock(amvDecodeMutex_);
    if (amvDecodeStop_ || static_cast<size_t>(frameIndex) >= amvReadyFrames_.size())
        return;
    if (amvReadyFrames_[static_cast<size_t>(frameIndex)].ptrData != nullptr)
        return;
    if (std::find(amvDecodeQueue_.begin(), amvDecodeQueue_.end(), frameIndex) !=
        amvDecodeQueue_.end())
        return;

    // Keep only a very small look-ahead.  A full AMV frame is commonly several
    // megabytes; allowing the render loop to enqueue every future frame makes
    // the decoder compete with the main thread and recreates the old memory
    // spike under a slow device.  The current frame is always given priority
    // by dropping the farthest queued request when the two-entry window is
    // already full.
    constexpr size_t kAmvDecodeQueueLimit = 2;
    if (amvDecodeQueue_.size() >= kAmvDecodeQueueLimit)
        amvDecodeQueue_.pop_back();
    amvDecodeQueue_.push_back(frameIndex);
    amvDecodeCv_.notify_one();
}

void tTJSNI_AlphaMovie::pruneAmvDecodeFrames(tjs_int currentFrameIndex,
                                             tjs_int nextFrameIndex)
{
    std::lock_guard<std::mutex> lock(amvDecodeMutex_);
    if (amvDecodeStop_)
        return;

    // Playback timers are allowed to skip frames.  Without pruning, each
    // skipped, fully decoded RGBA buffer remains in amvReadyFrames_ until the
    // movie loops back to it, which recreates the mobile memory growth that
    // this asynchronous path is intended to avoid.  The current presentation
    // frame and a single look-ahead are sufficient for smooth playback.
    for (size_t index = 0; index < amvReadyFrames_.size(); ++index)
    {
        if (static_cast<tjs_int>(index) == currentFrameIndex ||
            static_cast<tjs_int>(index) == nextFrameIndex)
            continue;
        AlphaMovieReadyFrame& ready = amvReadyFrames_[index];
        delete[] ready.ptrData;
        ready = AlphaMovieReadyFrame{};
    }

    amvDecodeQueue_.erase(
        std::remove_if(amvDecodeQueue_.begin(), amvDecodeQueue_.end(),
                       [&](tjs_int frameIndex) {
                           return frameIndex != currentFrameIndex &&
                                  frameIndex != nextFrameIndex;
                       }),
        amvDecodeQueue_.end());
}

tjs_uint8* tTJSNI_AlphaMovie::takeAmvReadyFrame(tjs_int frameIndex,
                                                 tjs_uint16* width,
                                                 tjs_uint16* height)
{
    std::lock_guard<std::mutex> lock(amvDecodeMutex_);
    if (frameIndex < 0 || static_cast<size_t>(frameIndex) >= amvReadyFrames_.size())
        return nullptr;
    AlphaMovieReadyFrame& ready = amvReadyFrames_[static_cast<size_t>(frameIndex)];
    tjs_uint8* result = ready.ptrData;
    if (result != nullptr)
    {
        if (width != nullptr)
            *width = ready.width;
        if (height != nullptr)
            *height = ready.height;
        ready = AlphaMovieReadyFrame{};
    }
    return result;
}

void tTJSNI_AlphaMovie::amvDecodeWorkerMain()
{
    for (;;)
    {
        tjs_int frameIndex = -1;
        {
            std::unique_lock<std::mutex> lock(amvDecodeMutex_);
            amvDecodeCv_.wait(lock, [&] {
                return amvDecodeStop_ || !amvDecodeQueue_.empty();
            });
            if (amvDecodeStop_)
                return;
            frameIndex = amvDecodeQueue_.front();
            amvDecodeQueue_.pop_front();
            if (frameIndex < 0 || static_cast<size_t>(frameIndex) >= amvReadyFrames_.size() ||
                amvReadyFrames_[static_cast<size_t>(frameIndex)].ptrData != nullptr)
                continue;
        }

        // Only the worker touches the AMV input stream and frame index while
        // decoding.  The render thread consumes completed buffers from the
        // ready-frame queue, so it never waits for JPEG/zlib work.
        decodeAmvFrame(frameIndex);

        tjs_uint8* decoded = nullptr;
        tjs_uint16 width = 0;
        tjs_uint16 height = 0;
        if (frameIndex >= 0 && static_cast<size_t>(frameIndex) < frameInfoList.size())
        {
            AlphaMovieFrame& frame = frameInfoList[static_cast<size_t>(frameIndex)];
            decoded = frame.ptrData;
            width = frame.frame_width;
            height = frame.frame_height;
            frame.ptrData = nullptr;
        }

        std::lock_guard<std::mutex> lock(amvDecodeMutex_);
        if (amvDecodeStop_ || frameIndex < 0 ||
            static_cast<size_t>(frameIndex) >= amvReadyFrames_.size())
        {
            delete[] decoded;
            return;
        }
        AlphaMovieReadyFrame& ready = amvReadyFrames_[static_cast<size_t>(frameIndex)];
        if (ready.ptrData != nullptr)
            delete[] decoded;
        else
        {
            ready.ptrData = decoded;
            ready.width = width;
            ready.height = height;
        }
        amvDecodeCv_.notify_all();
    }
}

void tTJSNI_AlphaMovie::open(tTJSString fileName)
{
    EnsureAlphaMovieTablesInitialized();
    const tjs_int frameToDecode = amvDecodeRequested_ ? amvFrameToDecode_ : -1;
    const bool decodeOnly = frameToDecode >= 0;
    const uint64_t profileStartedUs =
        AlphaMovieProfileEnabled() ? AlphaMovieProfileNowUs() : 0;
    struct ProfileGuard
    {
        const char* phase;
        tjs_int frame;
        uint64_t startedUs;
        ~ProfileGuard() { LogAlphaMovieProfile(phase, frame, startedUs); }
    } profileGuard{decodeOnly ? "decode" : "open", frameToDecode, profileStartedUs};

    if (!decodeOnly) {
        clear();
        amvFileName_ = fileName;

        // A first read from a compressed XP3 AMV can inflate several hundred
        // megabytes of source data.  The game calls open() from its click
        // handler immediately before showNextImage(), so perform only the
        // storage/header work on the loader thread and keep the input tick
        // responsive.  The main thread adopts the completed stream before it
        // presents the first decoded frame.
        ttstr extension = TVPExtractStorageExt(fileName);
        extension.ToLowerCase();
        if (extension == TJS_W(".amv"))
        {
            backend_ = Backend::Amv;
            // Keep script-side frame timers valid while the header is being
            // prepared.  The game computes its flip interval immediately
            // after open(); exposing zero here would cause a division by
            // zero before the worker can publish the real frame count/rate.
            _numOfFrame = 1;
            _FPSRate = 1.0;
            startAmvOpenAsync(fileName);
            return;
        }
    }
    // Reuse the indexed AMV stream for lazy frames.  Reopening an archive
    // storage on every frame adds lookup, decompression, and locking overhead
    // on the render thread even when the encoded frame itself is seekable.
    tTJSBinaryStream* stream =
        (decodeOnly && filePtr != nullptr)
            ? filePtr
            : TVPCreateStream(ttstr(fileName), TJS_BS_READ);
    if (!stream)
        return;

    const size_t readSize = stream->GetSize();
    char magic[4] = {};
    if (readSize >= sizeof(magic))
    {
        stream->SetPosition(0);
        stream->ReadBuffer(magic, sizeof(magic));
        stream->SetPosition(0);
    }
    if (memcmp(magic, "AJPM", 4) != 0)
    {
        if (!decodeOnly)
            TVPAddLog(ttstr(TJS_W("AlphaMovie.open: backend=ffmpeg storage=")) + ttstr(fileName));
        openFfmpeg(fileName, stream);
        return;
    }

    backend_ = Backend::Amv;
    if (!decodeOnly)
        TVPAddLog(ttstr(TJS_W("AlphaMovie.open: backend=amv storage=")) + ttstr(fileName));
    filePtr = stream;
    if (readSize < 40)
    {
        clear();
        return;
    }

    // header
    filePtr->SetPosition(0);
    _header.parseAMVHeader(filePtr);
    _numOfFrame = _header.frame_cnt;
    _FPSRate = _header.frame_rate;
    _screenWidth = _header.width;
    _screenHeight = _header.height;

    // quantaization_table
    if (_header.alpha_decode_attr == 2) // qtbl = 64 * 2
    {
        if (_header.quantaization_table_size_plus_hdr_size - 40 != 64 * 2)
        {
            clear();
            return;
        }
        for (size_t i = 0; i < 2; i++)
        {
            filePtr->ReadBuffer(qtbl[i], 64);
        }
    }
    else // qtbl = 64 * 3
    {
        if (_header.quantaization_table_size_plus_hdr_size - 40 != 64 * 3)
        {
            clear();
            return;
        }
        for (size_t i = 0; i < 3; i++)
        {
            filePtr->ReadBuffer(qtbl[i], 64);
        }
    }

    // AMV entries in XP3 archives are commonly split into independently
    // compressed chunks.  Walking every frame header here forces every chunk
    // to be inflated before the movie is shown, which turns a simple click
    // into a several-hundred-millisecond stall.  Keep slots for the headers
    // and fill them incrementally as playback reaches each frame.
    bool isZlib = _header.alpha_decode_attr == 2;
    const tjs_uint64 frameHeaderSize = isZlib ? 24 : 20;
    if (!decodeOnly)
    {
        frameInfoList.assign(static_cast<size_t>(_numOfFrame), AlphaMovieFrame{});
        amvFirstFrameOffset_ = _header.quantaization_table_size_plus_hdr_size;
        amvIndexedFrames_ = 0;
        if (m_BmpBits == nullptr)
            m_BmpBits = new tTVPBaseTexture(_header.width, _header.height, 32);
        _frame = 0;
        return;
    }

    size_t firstFrame = 0;
    size_t lastFrame = _numOfFrame;
    if (decodeOnly &&
        static_cast<size_t>(frameToDecode) < frameInfoList.size()) {
        firstFrame = static_cast<size_t>(frameToDecode);
        lastFrame = firstFrame + 1;
        const auto payloadOffset = frameInfoList[firstFrame].data_offset;
        if (payloadOffset < frameHeaderSize) {
            clear();
            return;
        }
        filePtr->SetPosition(payloadOffset - frameHeaderSize);
    } else {
        filePtr->SetPosition(_header.quantaization_table_size_plus_hdr_size);
    }
    for (size_t currF = firstFrame; currF < lastFrame; currF++)
    {
        // frame
        AlphaMovieFrame _a;
        _a.parseAMVFrame(filePtr, isZlib);
        _a.data_offset = filePtr->GetPosition();

        // The AMV frame payload follows the parsed header.  We only decode a
        // requested frame; all other payloads remain in the source stream.
        if (static_cast<tjs_int>(currF) != frameToDecode)
        {
            if (_a.frame_width != 0 && _a.frame_height != 0)
                filePtr->SetPosition(filePtr->GetPosition() + _a.size_of_frame);
            if (decodeOnly) {
                frameInfoList[currF] = _a;
            } else
                frameInfoList.push_back(_a);
            continue;
        }

        // filter
        if (_a.frame_width == 0 || _a.frame_height == 0)
        {
            if (decodeOnly) {
                frameInfoList[currF] = _a;
            } else
                frameInfoList.push_back(_a);
            continue;
        }

        // get All Data
        if (isZlib) // zlib解压缩
        {
            // alpha
            tjs_uint32 cacheLen = _a.zlib_buffer_size;
            amvScratchCache_.resize(cacheLen);
            tjs_uint8* cache = amvScratchCache_.data();
            filePtr->ReadBuffer(cache, cacheLen);
            uLongf alphaSize = _a.frame_width * _a.frame_height;
            amvScratchAlpha_.resize(alphaSize);
            tjs_uint8* alpha_buffer = amvScratchAlpha_.data();
            if (uncompress(alpha_buffer, &alphaSize, cache, cacheLen) != Z_OK)
            {
                return;
            }
            // yuv
            cacheLen = _a.size_of_frame - _a.zlib_buffer_size;
            amvScratchCache_.resize(cacheLen);
            cache = amvScratchCache_.data();
            filePtr->ReadBuffer(cache, cacheLen);
            uLongf yuvSize = _a.frame_width * _a.frame_height * 3 / 2;
            amvScratchYuva_.resize(yuvSize);
            tjs_uint8* yuv_buffer = amvScratchYuva_.data();
            tjs_uint8* uStart = yuv_buffer + _a.frame_width * _a.frame_height;
            tjs_uint8* vStart = uStart + _a.frame_width * _a.frame_height / 4;

            // jpeg decode
            auto streamOwner = std::make_unique<BufferManager>(cache, cacheLen);
            BufferManager* stream = streamOwner.get();
            amvScratchIdc_.resize(64);
            int16_t* idcBuff = amvScratchIdc_.data();
            int decoded = 0;
            // 8*8分块处理, U/V单独算一个小块，2*2Y算一个小块
            for (size_t i = 0; i < _a.frame_height / 16; i++)
            {
                for (size_t j = 0; j < _a.frame_width / 16; j++)
                {
                    // Pose
                    tjs_uint8* currentY = yuv_buffer + _a.frame_width * 16 * i + 16 * j;
                    tjs_uint8* currentU = uStart + _a.frame_width * 4 * i + 8 * j;
                    tjs_uint8* currentV = vStart + _a.frame_width * 4 * i + 8 * j;

                    // U
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[0], stream, idcBuff, stream->decodeUVStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[1], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[1][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)currentU) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 2)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 3)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 4)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 5)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 6)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 7)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[1], currentU, _a.frame_width / 2);
                    }

                    // V
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[0], stream, idcBuff, stream->decodeUVStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[1], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[1][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)currentV) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 2)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 3)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 4)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 5)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 6)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 7)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[1], currentV, _a.frame_width / 2);
                    }

                    // 4*Y
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)currentY) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 2)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 3)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 4)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 5)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 6)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 7)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentY, _a.frame_width);
                    }
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)(currentY + 8)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 2)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 3)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 4)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 5)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 6)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 7)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentY + 8, _a.frame_width);
                    }
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)(currentY + _a.frame_width * 8)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 9)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 10)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 11)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 12)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 13)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 14)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 15)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentY + _a.frame_width * 8,
                                       _a.frame_width);
                    }
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 8)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 9)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 10)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 11)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 12)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 13)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 14)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 15)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentY + 8 + _a.frame_width * 8,
                                       _a.frame_width);
                    }
                }
            }
            // clean
            // yuv420p to rgba
            uLongf rgbaSize = _a.frame_width * _a.frame_height * 4;
            auto rgbaBuffer = std::make_unique<tjs_uint8[]>(rgbaSize);
            tjs_uint8* rgba_buffer = rgbaBuffer.get();
            amvImageConvertContext_ =
                sws_getCachedContext(amvImageConvertContext_, _a.frame_width, _a.frame_height,
                                     AV_PIX_FMT_YUV420P, _a.frame_width, _a.frame_height,
                                     AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
            if (amvImageConvertContext_ == nullptr)
                return;
            const uint8_t* src_slice[4] = {
                yuv_buffer, yuv_buffer + _a.frame_width * _a.frame_height,
                yuv_buffer + _a.frame_width * _a.frame_height * 5 / 4, NULL};
            int src_stride[4] = {_a.frame_width,     // Y分量步长
                                 _a.frame_width / 2, // U分量步长（宽度减半）
                                 _a.frame_width / 2, // V分量步长（宽度减半）
                                 0};
            uint8_t* dst[4] = {rgba_buffer, // RGBA数据
                               NULL, NULL, NULL};
            int dst_stride[4] = {_a.frame_width * 4, // RGBA步长
                                 0, 0, 0};
            if (sws_scale(amvImageConvertContext_, src_slice, src_stride, 0,
                          _a.frame_height, dst, dst_stride) <= 0)
                return;
            // clean

            // blend alpha to rgba
            TVPBindMaskToMain((tjs_uint32*)rgba_buffer, alpha_buffer, alphaSize);

            // set value
            _a.ptrData = rgbaBuffer.release();
        }
        else // jpeg解码
        {
            // prepare
            tjs_uint32 cacheLen = _a.size_of_frame;
            amvScratchCache_.resize(cacheLen);
            tjs_uint8* cache = amvScratchCache_.data();
            filePtr->ReadBuffer(cache, cacheLen);
            tjs_uint32 yuvaSize = _a.frame_width * _a.frame_height * 5 / 2;
            amvScratchYuva_.resize(yuvaSize);
            tjs_uint8* yuva_buffer = amvScratchYuva_.data();
            tjs_uint8* uStart = yuva_buffer + _a.frame_width * _a.frame_height;
            tjs_uint8* vStart = uStart + _a.frame_width * _a.frame_height / 4;
            tjs_uint8* aStart = vStart + _a.frame_width * _a.frame_height / 4;

            // decode
            auto streamOwner = std::make_unique<BufferManager>(cache, cacheLen);
            BufferManager* stream = streamOwner.get();
            amvScratchIdc_.resize(64);
            int16_t* idcBuff = amvScratchIdc_.data();
            int decoded = 0;
            // 8*8分块处理, U/V单独算一个小块，2*2Y/A算一个小块
            for (size_t i = 0; i < _a.frame_height / 16; i++)
            {
                for (size_t j = 0; j < _a.frame_width / 16; j++)
                {
                    // Pose
                    tjs_uint8* currentY = yuva_buffer + _a.frame_width * 16 * i + 16 * j;
                    tjs_uint8* currentU = uStart + _a.frame_width * 4 * i + 8 * j;
                    tjs_uint8* currentV = vStart + _a.frame_width * 4 * i + 8 * j;
                    tjs_uint8* currentA = aStart + _a.frame_width * 16 * i + 16 * j;

                    // U
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[0], stream, idcBuff, stream->decodeUVStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[1], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[1][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)currentU) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 2)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 3)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 4)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 5)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 6)) = pattern;
                        *((uint64_t*)(currentU + _a.frame_width / 2 * 7)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[1], currentU, _a.frame_width / 2);
                    }

                    // V
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[0], stream, idcBuff, stream->decodeUVStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[1], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[1][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)currentV) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 2)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 3)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 4)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 5)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 6)) = pattern;
                        *((uint64_t*)(currentV + _a.frame_width / 2 * 7)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[1], currentV, _a.frame_width / 2);
                    }

                    // 4*Y
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)currentY) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 2)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 3)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 4)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 5)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 6)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 7)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentY, _a.frame_width);
                    }
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)(currentY + 8)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 2)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 3)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 4)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 5)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 6)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 7)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentY + 8, _a.frame_width);
                    }
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)(currentY + _a.frame_width * 8)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 9)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 10)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 11)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 12)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 13)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 14)) = pattern;
                        *((uint64_t*)(currentY + _a.frame_width * 15)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentY + _a.frame_width * 8,
                                       _a.frame_width);
                    }
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 8)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 9)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 10)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 11)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 12)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 13)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 14)) = pattern;
                        *((uint64_t*)(currentY + 8 + _a.frame_width * 15)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentY + 8 + _a.frame_width * 8,
                                       _a.frame_width);
                    }

                    // 4*A
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)currentA) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 2)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 3)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 4)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 5)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 6)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 7)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentA, _a.frame_width);
                    }
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)(currentA + 8)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 2)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 3)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 4)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 5)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 6)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 7)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentA + 8, _a.frame_width);
                    }
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)(currentA + _a.frame_width * 8)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 9)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 10)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 11)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 12)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 13)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 14)) = pattern;
                        *((uint64_t*)(currentA + _a.frame_width * 15)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentA + _a.frame_width * 8,
                                       _a.frame_width);
                    }
                    memset(idcBuff, 0, 128);
                    decode_dc_run_length(&HuffmanTable[2], stream, idcBuff, stream->decodeYStatus);
                    decoded = decode_ac_run_length(&HuffmanTable[3], stream, idcBuff);
                    if (decoded == 1)
                    {
                        int dequantized = idcBuff[0] * qtbl[0][0];
                        int rounded = (dequantized < 0) ? dequantized + 7 : dequantized;
                        int pixel = (rounded >> 3) + 128;
                        pixel = (pixel < 0) ? 0 : (pixel > 255) ? 255 : pixel;

                        uint64_t pattern = (uint64_t)(uint8_t)pixel * 0x0101010101010101;

                        // 填充8x8块 - 7次内存写入覆盖整个块
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 8)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 9)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 10)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 11)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 12)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 13)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 14)) = pattern;
                        *((uint64_t*)(currentA + 8 + _a.frame_width * 15)) = pattern;
                    }
                    else
                    {
                        jpeg_idct_data(idcBuff, qtbl[0], currentA + 8 + _a.frame_width * 8,
                                       _a.frame_width);
                    }
                }
            }
            // clean
            // yuva420p to rgba
            uLongf rgbaSize = _a.frame_width * _a.frame_height * 4;
            auto rgbaBuffer = std::make_unique<tjs_uint8[]>(rgbaSize);
            tjs_uint8* rgba_buffer = rgbaBuffer.get();
            amvImageConvertContext_ =
                sws_getCachedContext(amvImageConvertContext_, _a.frame_width, _a.frame_height,
                                     AV_PIX_FMT_YUVA420P, _a.frame_width, _a.frame_height,
                                     AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
            if (amvImageConvertContext_ == nullptr)
                return;
            const uint8_t* src_slice[5] = {
                yuva_buffer, yuva_buffer + _a.frame_width * _a.frame_height,
                yuva_buffer + _a.frame_width * _a.frame_height * 5 / 4,
                yuva_buffer + _a.frame_width * _a.frame_height * 3 / 2, NULL};
            int src_stride[5] = {_a.frame_width,     // Y分量步长
                                 _a.frame_width / 2, // U分量步长（宽度减半）
                                 _a.frame_width / 2, // V分量步长（宽度减半）
                                 _a.frame_width, 0};
            uint8_t* dst[4] = {rgba_buffer, // RGBA数据
                               NULL, NULL, NULL};
            int dst_stride[4] = {_a.frame_width * 4, // RGBA步长
                                 0, 0, 0};
            if (sws_scale(amvImageConvertContext_, src_slice, src_stride, 0,
                          _a.frame_height, dst, dst_stride) <= 0)
                return;
            // clean

            // set value
            _a.ptrData = rgbaBuffer.release();
        }
        // push
        if (decodeOnly) {
            frameInfoList[currF] = _a;
        } else
            frameInfoList.push_back(_a);
    }
    if (m_BmpBits == nullptr)
        m_BmpBits = new tTVPBaseTexture(_header.width, _header.height, 32);
}

void tTJSNI_AlphaMovie::openFfmpeg(tTJSString fileName, tTJSBinaryStream* stream)
{
    backend_ = Backend::Ffmpeg;
    ffmpegStream_ = stream;

    ttstr filename(fileName);
    ttstr ext = TVPExtractStorageExt(filename);
    ext.ToLowerCase();

    auto* movie = new AlphaMovieLayer(
        [this](KRMovieEvent msg, void* payload) { queueFfmpegEvent(msg, payload); });
    movie->BuildGraph(TVPCreateIStream(ffmpegStream_), filename.c_str(),
                      ext.c_str(), ffmpegStream_->GetSize());
    movie->GetVideoSize(&ffmpegSourceWidth_, &ffmpegSourceHeight_);

    if (ffmpegSourceWidth_ <= 0 || ffmpegSourceHeight_ <= 0)
    {
        TVPAddLog(ttstr(TJS_W("AlphaMovie.open: invalid video size for ")) + filename);
        movie->Release();
        ffmpegMovie_ = nullptr;
        closeFfmpeg();
        backend_ = Backend::None;
        return;
    }

    ffmpegBuffers_[0] = new tTVPBaseTexture(ffmpegSourceWidth_, ffmpegSourceHeight_, 32);
    ffmpegBuffers_[1] = new tTVPBaseTexture(ffmpegSourceWidth_, ffmpegSourceHeight_, 32);
    movie->SetVideoBuffer(ffmpegBuffers_[0], ffmpegBuffers_[1],
                          ffmpegSourceWidth_ * ffmpegSourceHeight_ * 4);

    ffmpegMovie_ = movie;
    _screenWidth = static_cast<tjs_int>(ffmpegSourceWidth_);
    _screenHeight = static_cast<tjs_int>(ffmpegSourceHeight_);
    _numOfFrame = getNumberOfFrames();
    _FPSScale = 1000.0;
    _FPSRate = getFPSRate();
    _isPlaying = false;
    ffmpegPlayRequested_ = false;
}

void tTJSNI_AlphaMovie::closeFfmpeg()
{
    if (ffmpegMovie_)
    {
        ffmpegMovie_->Stop();
        ffmpegMovie_->Release();
        ffmpegMovie_ = nullptr;
    }
    if (ffmpegStream_)
    {
        delete ffmpegStream_;
        ffmpegStream_ = nullptr;
    }
    for (auto*& buffer : ffmpegBuffers_)
    {
        delete buffer;
        buffer = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(ffmpegEventMutex_);
        ffmpegPendingEvents_.clear();
    }
    ffmpegSourceWidth_ = 0;
    ffmpegSourceHeight_ = 0;
    ffmpegPlayRequested_ = false;
}

void tTJSNI_AlphaMovie::queueFfmpegEvent(KRMovieEvent msg, void*)
{
    std::lock_guard<std::mutex> lock(ffmpegEventMutex_);
    ffmpegPendingEvents_.push_back(msg);
}

void tTJSNI_AlphaMovie::processFfmpegEvents()
{
    std::vector<KRMovieEvent> events;
    {
        std::lock_guard<std::mutex> lock(ffmpegEventMutex_);
        events.swap(ffmpegPendingEvents_);
    }
    for (KRMovieEvent msg : events)
    {
        if (msg != KRMovieEvent::Ended)
            continue;
        if (_loop && ffmpegMovie_)
        {
            ffmpegMovie_->Rewind();
            ffmpegMovie_->Play();
            ffmpegPlayRequested_ = true;
            _isPlaying = true;
        }
        else
        {
            ffmpegPlayRequested_ = false;
            _isPlaying = false;
        }
    }
}

tTVPBaseTexture* tTJSNI_AlphaMovie::takeFfmpegFrameForPresentation()
{
    bool autoStarted = false;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        processFfmpegEvents();
        ffmpegMovie_->FrameMove();
        if (tTVPBaseTexture* front = ffmpegMovie_->GetFrontBuffer())
        {
            if (autoStarted && !ffmpegPlayRequested_)
                ffmpegMovie_->Pause();
            return front;
        }

        tTVPVideoStatus status = vsStopped;
        ffmpegMovie_->GetStatus(&status);
        if (status != vsPlaying)
        {
            ffmpegMovie_->Play();
            autoStarted = true;
        }

        if (attempt < 7)
            std::this_thread::sleep_for(std::chrono::milliseconds(attempt < 3 ? 2 : 4));
    }

    if (autoStarted && !ffmpegPlayRequested_)
        ffmpegMovie_->Pause();
    return nullptr;
}

tjs_int tTJSNI_AlphaMovie::showNextFfmpegImage(tTJSVariant layer)
{
    if (!ffmpegMovie_)
        return 0;

    processFfmpegEvents();

    tTVPBaseTexture* front = takeFfmpegFrameForPresentation();
    if (!front)
        return 0;

    tTJSVariantClosure clo = layer.AsObjectClosureNoAddRef();
    tTJSNI_BaseLayer* targetLayer = GetNativeLayer(clo.Object);
    if (!targetLayer || !targetLayer->GetMainImage())
        return 0;

    tTVPBaseTexture* target = targetLayer->GetMainImage();
    const int targetWidth = static_cast<int>(target->GetWidth());
    const int targetHeight = static_cast<int>(target->GetHeight());
    if (targetWidth <= 0 || targetHeight <= 0)
        return 0;

    const int sourceWidth = ffmpegSourceWidth_ > 0 ? static_cast<int>(ffmpegSourceWidth_)
                                                   : static_cast<int>(front->GetWidth());
    const int sourceHeight = ffmpegSourceHeight_ > 0 ? static_cast<int>(ffmpegSourceHeight_)
                                                     : static_cast<int>(front->GetHeight());
    if (sourceWidth <= 0 || sourceHeight <= 0)
        return 0;

    tTVPRect clip(0, 0, targetWidth, targetHeight);
    tTVPRect dest(0, 0, targetWidth, targetHeight);
    tTVPRect src(0, 0, sourceWidth, sourceHeight);
    target->StretchBlt(clip, dest, front, src, bmCopy, 255, false, stLinear);
    targetLayer->Update(dest);
    return 1;
}

tjs_int tTJSNI_AlphaMovie::getFrame()
{
    if (backend_ == Backend::Ffmpeg && ffmpegMovie_)
    {
        int frame = 0;
        ffmpegMovie_->GetFrame(&frame);
        return std::max(0, frame);
    }
    return _frame;
}

void tTJSNI_AlphaMovie::setFrame(tjs_int frame)
{
    if (backend_ == Backend::Ffmpeg && ffmpegMovie_)
    {
        ffmpegMovie_->Flush();
        ffmpegMovie_->SetFrame(std::max<tjs_int>(0, frame));
        return;
    }
    _frame = frame;
}

tjs_int tTJSNI_AlphaMovie::getNumberOfFrames()
{
    if (backend_ == Backend::Ffmpeg && ffmpegMovie_)
    {
        int frames = 0;
        ffmpegMovie_->GetNumberOfFrame(&frames);
        return std::max(0, frames);
    }
    return _numOfFrame;
}

tjs_real tTJSNI_AlphaMovie::getFPSRate()
{
    if (backend_ == Backend::Ffmpeg && ffmpegMovie_)
    {
        double fps = 0.0;
        ffmpegMovie_->GetFPS(&fps);
        if (std::isfinite(fps) && fps > 0.0)
            return fps * 1000.0;
    }
    return _FPSRate;
}

void tTJSNI_AlphaMovie::clear()
{
    removeAmvOpenCallback();
    pendingAmvLayer_.Clear();
    stopAmvDecodeWorker();
    cancelAmvOpenAsync();
    closeFfmpeg();
    for (size_t i = 0; i < frameInfoList.size(); i++)
    {
        if (frameInfoList.at(i).ptrData != nullptr)
            delete[] frameInfoList.at(i).ptrData;
    }
    // clear() is also the lifecycle boundary after a long fast-forward.  A
    // vector clear retains its peak capacity, so a single oversized AMV frame
    // would keep its compressed/YUV/RGBA work buffers resident until the
    // plugin object is destroyed.  Swap with empty vectors to return that
    // transient storage immediately.
    std::vector<AlphaMovieFrame>().swap(frameInfoList);
    std::vector<AlphaMovieReadyFrame>().swap(amvReadyFrames_);
    std::vector<tjs_uint8>().swap(amvScratchCache_);
    std::vector<tjs_uint8>().swap(amvScratchAlpha_);
    std::vector<tjs_uint8>().swap(amvScratchYuva_);
    std::vector<int16_t>().swap(amvScratchIdc_);
    sws_freeContext(amvImageConvertContext_);
    amvImageConvertContext_ = nullptr;
    _numOfFrame = 0;
    if (filePtr != nullptr)
    {
        delete filePtr;
        filePtr = nullptr;
    }
    if (m_BmpBits != nullptr)
    {
        delete m_BmpBits;
        m_BmpBits = nullptr;
    }
    backend_ = Backend::None;
    _isPlaying = false;
    _numOfFrame = 0;
    _frame = 0;
    amvFirstFrameOffset_ = 0;
    amvIndexedFrames_ = 0;
}

tjs_int tTJSNI_AlphaMovie::showNextImage(tTJSVariant layer)
{
    const uint64_t profileStartedUs =
        AlphaMovieProfileEnabled() ? AlphaMovieProfileNowUs() : 0;
    struct ProfileGuard
    {
        tjs_int frame;
        uint64_t startedUs;
        ~ProfileGuard() { LogAlphaMovieProfile("present", frame, startedUs); }
    } profileGuard{_frame, profileStartedUs};

    if (backend_ == Backend::Ffmpeg)
        return showNextFfmpegImage(layer);
    if (backend_ == Backend::Amv && !finishAmvOpenAsync())
    {
        // Do not join an in-flight archive open from the render thread.  The
        // first AMV access can spend hundreds of milliseconds inflating an
        // XP3 entry; waiting here turns one click into a visible frame hitch.
        // Keep the layer alive and retry from the main-thread continuous-event
        // hook once the loader publishes the stream, otherwise a one-shot
        // showNextImage() call would never submit the first frame.
        pendingAmvLayer_ = layer;
        if (!amvOpenCallbackRegistered_)
        {
            TVPAddContinuousEventHook(this);
            amvOpenCallbackRegistered_ = true;
        }
        return _frame;
    }

    tTJSNI_BaseLayer* src = NULL;
    tTJSVariantClosure clo = layer.AsObjectClosureNoAddRef();
    if (clo.Object)
    {
        if (TJS_FAILED(clo.Object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                                         (iTJSNativeInstance**)&src)))
            TVPThrowExceptionMessage(TVPSpecifyLayer);

        if (!src)
            TVPThrowExceptionMessage(TVPSpecifyLayer);
    }
    if (!src)
        return 0;
    if ((long)src->GetImageWidth() != _screenWidth || (long)src->GetImageHeight() != _screenHeight)
        src->SetImageSize(_screenWidth, _screenHeight);
    if ((long)src->GetWidth() != _screenWidth || (long)src->GetHeight() != _screenHeight)
        src->SetSize(_screenWidth, _screenHeight);

    if (m_BmpBits != nullptr && !frameInfoList.empty())
    {
        _frame++;
        if (_frame > _numOfFrame)
            _frame = 1;
        const tjs_int frameIndex = _frame - 1;
        const tjs_int nextFrameIndex = _frame % _numOfFrame;
        pruneAmvDecodeFrames(frameIndex, nextFrameIndex);
        enqueueAmvDecode(frameIndex);
        // Keep one frame ahead so the decoder can work while the current
        // frame is being composited.  The bounded ready-frame queue prevents
        // the old all-frames RGBA cache from returning on mobile devices.
        enqueueAmvDecode(nextFrameIndex);

        tjs_uint16 frameWidth = 0;
        tjs_uint16 frameHeight = 0;
        tjs_uint8* frameData = takeAmvReadyFrame(frameIndex, &frameWidth, &frameHeight);
        if (frameData != nullptr && frameWidth > 0 && frameHeight > 0)
        {
            m_BmpBits->Update(frameData, frameWidth * 4, _left, _top,
                              frameWidth, frameHeight);
            // Update() copies into the reusable presentation texture.  Do not
            // retain a decoded RGBA image per AMV frame: a 112-frame 1080p
            // feather animation alone is almost a gigabyte.
            delete[] frameData;
            // Swap the frame into the display layer rather than sharing its
            // texture. The layer's previous image becomes our next decode
            // target, so the following Update() does not copy-on-write a
            // full-size texture that is still being presented upstream.
            if (!src->ExchangeMainImage(m_BmpBits))
            {
                // Keep the historical assignment behavior for unusual layer
                // states (for example a size change while playback starts).
                src->AssignMainImage(m_BmpBits);
                src->Update();
            }
        }
    }
    return _frame;
}

bool tTJSNI_AlphaMovie::indexAmvFramesThrough(tjs_int frameIndex)
{
    const uint64_t profileStartedUs =
        AlphaMovieProfileEnabled() ? AlphaMovieProfileNowUs() : 0;
    struct ProfileGuard
    {
        tjs_int frame;
        uint64_t startedUs;
        ~ProfileGuard() { LogAlphaMovieProfile("index", frame, startedUs); }
    } profileGuard{frameIndex, profileStartedUs};

    if (backend_ != Backend::Amv || filePtr == nullptr || frameIndex < 0 ||
        frameIndex >= _numOfFrame || frameInfoList.empty())
        return false;

    const size_t target = static_cast<size_t>(frameIndex);
    if (target < amvIndexedFrames_)
        return true;

    const bool isZlib = _header.alpha_decode_attr == 2;
    if (amvIndexedFrames_ == 0)
    {
        filePtr->SetPosition(amvFirstFrameOffset_);
    }
    else
    {
        const AlphaMovieFrame& previous = frameInfoList.at(amvIndexedFrames_ - 1);
        filePtr->SetPosition(previous.data_offset + previous.size_of_frame);
    }

    for (size_t current = amvIndexedFrames_; current <= target; ++current)
    {
        AlphaMovieFrame frame{};
        frame.parseAMVFrame(filePtr, isZlib);
        frame.data_offset = filePtr->GetPosition();
        frameInfoList.at(current) = frame;
        if (frame.frame_width != 0 && frame.frame_height != 0)
            filePtr->SetPosition(frame.data_offset + frame.size_of_frame);
        amvIndexedFrames_ = current + 1;
    }
    return true;
}

void tTJSNI_AlphaMovie::decodeAmvFrame(tjs_int frameIndex)
{
    if (backend_ != Backend::Amv || amvFileName_.IsEmpty() || frameIndex < 0 ||
        frameIndex >= _numOfFrame)
        return;

    if (!indexAmvFramesThrough(frameIndex))
        return;

    struct DecodeRequestGuard
    {
        bool &requested;
        tjs_int &frame;
        ~DecodeRequestGuard()
        {
            frame = -1;
            requested = false;
        }
    } guard{amvDecodeRequested_, amvFrameToDecode_};
    amvDecodeRequested_ = true;
    amvFrameToDecode_ = frameIndex;
    open(amvFileName_);
}

bool tTJSNI_AlphaMovie::isPlaying()
{
    if (backend_ == Backend::Ffmpeg && ffmpegMovie_)
    {
        tTVPVideoStatus status = vsStopped;
        ffmpegMovie_->GetStatus(&status);
        return status == vsPlaying;
    }
    return _isPlaying;
}

void tTJSNI_AlphaMovie::play()
{
    if (backend_ == Backend::Ffmpeg && ffmpegMovie_)
    {
        ffmpegMovie_->Play();
        ffmpegPlayRequested_ = true;
        _isPlaying = true;
        return;
    }
    _isPlaying = true;
}

void tTJSNI_AlphaMovie::stop()
{
    if (backend_ == Backend::Ffmpeg && ffmpegMovie_)
    {
        ffmpegMovie_->Stop();
        ffmpegPlayRequested_ = false;
        _isPlaying = false;
        return;
    }
    clear();
    _isPlaying = false;
    _frame = 0;
}

void tTJSNI_AlphaMovie::setPosition(tjs_int x, tjs_int y)
{
    _left = x;
    _top = y;
}

void tTJSNI_AlphaMovie::setNextMovieFile(tTJSString fileName)
{
    // nothing to do
}

//---------------------------------------------------------------------------
// tTJSNC_AlphaMovie
//---------------------------------------------------------------------------
class tTJSNC_AlphaMovie : public tTJSNativeClass
{
    typedef tTJSNativeClass inherited;

public:
    tTJSNC_AlphaMovie();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance* CreateNativeInstance()
    {
        return new tTJSNI_AlphaMovie();
    }
};
tjs_uint32 tTJSNC_AlphaMovie::ClassID = (tjs_uint32)-1;
tTJSNC_AlphaMovie::tTJSNC_AlphaMovie()
  : tTJSNativeClass(TJS_W("AlphaMovie")){
        // register native methods/properties

        TJS_BEGIN_NATIVE_MEMBERS(AlphaMovie) TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------
            // constructor/methods
            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/ _this,
                                              /*var.type*/ tTJSNI_AlphaMovie,
                                              /*TJS class name*/ AlphaMovie){return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ AlphaMovie)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ open)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    _this->open(param[0]->AsStringNoAddRef());
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ open)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ clear)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->clear();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ clear)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ showNextImage)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    tjs_int num = _this->showNextImage(*param[0]);
    if (result)
        *result = num;
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ showNextImage)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ isPlaying)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    if (result)
        *result = _this->isPlaying();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ isPlaying)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ play)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->play();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ play)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ stop)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->stop();
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ stop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setPosition)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);

    if (numparams < 2)
        return TJS_E_BADPARAMCOUNT;
    _this->setPosition(*param[0], *param[1]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setPosition)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ setNextMovieFile)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    _this->setNextMovieFile(param[0]->AsStringNoAddRef());
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ setNextMovieFile)
//----------------------------------------------------------------------

//---------------------------------------------------------------------------

//----------------------------------------------------------------------
// properties
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(numOfFrame){TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(
    /*var. name*/ _this, /*var. type*/ tTJSNI_AlphaMovie);
*result = _this->GetNumOfFrame();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->SetNumOfFrame(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(numOfFrame)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(frame){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_AlphaMovie);
*result = _this->GetFrame();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->SetFrame(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(frame)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(loop){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_AlphaMovie);
*result = _this->GetLoop();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->SetLoop(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(loop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(nextLoop){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_AlphaMovie);
*result = _this->GetNextLoop();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->SetNextLoop(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(nextLoop)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(preloadSamples){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_AlphaMovie);
*result = _this->GetPreloadSamples();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->SetPreloadSamples(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(preloadSamples)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(left){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_AlphaMovie);
*result = _this->GetLeft();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->SetLeft(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(left)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(top){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_AlphaMovie);
*result = _this->GetTop();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->SetTop(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(top)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(screenWidth){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_AlphaMovie);
*result = _this->GetScreenWidth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->SetScreenWidth(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(screenWidth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(screenHeight){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_AlphaMovie);
*result = _this->GetScreenHeight();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->SetScreenHeight(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(screenHeight)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(FPSScale){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_AlphaMovie);
*result = _this->GetFPSScale();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->SetFPSScale(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(FPSScale)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(FPSRate){
    TJS_BEGIN_NATIVE_PROP_GETTER{TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                                                         /*var. type*/ tTJSNI_AlphaMovie);
*result = _this->GetFPSRate();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this,
                            /*var. type*/ tTJSNI_AlphaMovie);
    _this->SetFPSRate(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(FPSRate)
//----------------------------------------------------------------------
TJS_END_NATIVE_MEMBERS
}
tTJSNativeClass* TVPCreateNativeClass_AlphaMovie()
{
    return new tTJSNC_AlphaMovie();
}

void InitPlugin_AlphaMovie()
{
    iTJSDispatch2* global = TVPGetScriptDispatch();
    if (global)
    {
        tTJSVariant val;
        iTJSDispatch2* tjsclass = TVPCreateNativeClass_AlphaMovie();
        val = tTJSVariant(tjsclass);
        tjsclass->Release();
        global->PropSet(TJS_MEMBERENSURE, TJS_W("AlphaMovie"), NULL, &val, global);
        global->Release();
    }
}
NCB_PRE_REGIST_CALLBACK(InitPlugin_AlphaMovie);
