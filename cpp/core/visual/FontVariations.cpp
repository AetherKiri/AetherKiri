#include "FontVariations.h"

#include "MsgIntf.h"
#include "tvpfontstruc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

bool TVPFontDefaultUseVarStyle = false;

namespace {

    struct ParsedVariation {
        std::string tag;
        float value = 0;
    };

    void ParseVariationSpec(const ttstr &spec,
                            std::vector<ParsedVariation> &out, bool strict) {
        const tjs_char *text = spec.c_str();
        const tjs_int length = spec.GetLen();
        tjs_int start = 0;
        while(start <= length) {
            tjs_int end = start;
            while(end < length && text[end] != TJS_W(','))
                ++end;
            tjs_int begin = start;
            tjs_int trimmed_end = end;
            while(begin < trimmed_end &&
                  (text[begin] == TJS_W(' ') || text[begin] == TJS_W('\t')))
                ++begin;
            while(trimmed_end > begin &&
                  (text[trimmed_end - 1] == TJS_W(' ') ||
                   text[trimmed_end - 1] == TJS_W('\t')))
                --trimmed_end;

            if(begin < trimmed_end) {
                tjs_int equal = begin;
                while(equal < trimmed_end && text[equal] != TJS_W('='))
                    ++equal;
                bool valid = equal > begin && equal < trimmed_end;
                std::string tag;
                float value = 0;
                if(valid) {
                    tjs_int tag_end = equal;
                    while(tag_end > begin &&
                          (text[tag_end - 1] == TJS_W(' ') ||
                           text[tag_end - 1] == TJS_W('\t')))
                        --tag_end;
                    const tjs_int tag_length = tag_end - begin;
                    valid = tag_length >= 1 && tag_length <= 4;
                    for(tjs_int index = begin; valid && index < tag_end;
                        ++index) {
                        tjs_char ch = text[index];
                        if(ch < 0x21 || ch > 0x7e || ch == TJS_W('=') ||
                           ch == TJS_W(',')) {
                            valid = false;
                        } else {
                            if(ch >= TJS_W('A') && ch <= TJS_W('Z'))
                                ch = ch - TJS_W('A') + TJS_W('a');
                            tag.push_back(static_cast<char>(ch));
                        }
                    }
                    std::string number;
                    for(tjs_int index = equal + 1; valid && index < trimmed_end;
                        ++index) {
                        const tjs_char ch = text[index];
                        if(ch == TJS_W(' ') || ch == TJS_W('\t'))
                            continue;
                        if(ch > 0x7e) {
                            valid = false;
                            break;
                        }
                        number.push_back(static_cast<char>(ch));
                    }
                    if(valid && !number.empty()) {
                        char *number_end = nullptr;
                        value = std::strtof(number.c_str(), &number_end);
                        valid = number_end && *number_end == '\0' &&
                            std::isfinite(value);
                    } else {
                        valid = false;
                    }
                }
                if(valid) {
                    out.push_back({ std::move(tag), value });
                } else if(strict) {
                    TVPThrowExceptionMessage(TVPInvalidParam);
                }
            }
            if(end >= length)
                break;
            start = end + 1;
        }
    }

    float QuantizeVariation(const std::string &tag, float value) {
        const float step = tag == "wght" ? 1.0f : 0.5f;
        return std::round(value / step) * step;
    }

    std::string FormatVariationValue(float value) {
        char buffer[48];
        if(value == std::floor(value) && std::fabs(value) < 1e7f)
            std::snprintf(buffer, sizeof(buffer), "%.0f", value);
        else
            std::snprintf(buffer, sizeof(buffer), "%g", value);
        return buffer;
    }

} // namespace

ttstr TVPNormalizeFontVariations(const ttstr &spec) {
    if(spec.IsEmpty())
        return ttstr();
    std::vector<ParsedVariation> values;
    ParseVariationSpec(spec, values, true);
    for(auto &value : values)
        value.value = QuantizeVariation(value.tag, value.value);
    std::stable_sort(values.begin(), values.end(),
                     [](const auto &left, const auto &right) {
                         return left.tag < right.tag;
                     });
    std::string normalized;
    for(size_t index = 0; index < values.size(); ++index) {
        if(index + 1 < values.size() &&
           values[index + 1].tag == values[index].tag)
            continue;
        if(!normalized.empty())
            normalized += ',';
        normalized += values[index].tag;
        normalized += '=';
        normalized += FormatVariationValue(values[index].value);
    }
    return ttstr(normalized);
}

tjs_uint32 TVPFontVarPackTag(const char *tag, size_t len) {
    char chars[4] = { ' ', ' ', ' ', ' ' };
    for(size_t index = 0; index < 4 && index < len; ++index)
        chars[index] = tag[index];
    return (static_cast<tjs_uint32>(static_cast<unsigned char>(chars[0]))
            << 24) |
        (static_cast<tjs_uint32>(static_cast<unsigned char>(chars[1])) << 16) |
        (static_cast<tjs_uint32>(static_cast<unsigned char>(chars[2])) << 8) |
        static_cast<tjs_uint32>(static_cast<unsigned char>(chars[3]));
}

void TVPParseFontVariations(const ttstr &spec,
                            std::vector<tTVPFontAxisCoord> &out) {
    std::vector<ParsedVariation> values;
    ParseVariationSpec(spec, values, false);
    for(const auto &value : values) {
        const tjs_uint32 tag =
            TVPFontVarPackTag(value.tag.c_str(), value.tag.size());
        auto existing =
            std::find_if(out.begin(), out.end(), [tag](const auto &coord) {
                return coord.first == tag;
            });
        if(existing != out.end())
            existing->second = value.value;
        else
            out.emplace_back(tag, value.value);
    }
}

void TVPFontGetEffectiveVarCoords(tjs_int weight, const ttstr &variations,
                                  std::vector<tTVPFontAxisCoord> &out) {
    TVPParseFontVariations(variations, out);
    if(weight < 0)
        return;
    const tjs_uint32 weight_tag = TVPFontVarPackTag("wght", 4);
    if(std::none_of(out.begin(), out.end(), [weight_tag](const auto &coord) {
           return coord.first == weight_tag;
       })) {
        out.emplace_back(weight_tag, static_cast<float>(weight));
    }
}

void TVPFontGetEffectiveVarCoords(const tTVPFont &font,
                                  std::vector<tTVPFontAxisCoord> &out) {
    TVPFontGetEffectiveVarCoords(font.Weight, font.Variations, out);
}
