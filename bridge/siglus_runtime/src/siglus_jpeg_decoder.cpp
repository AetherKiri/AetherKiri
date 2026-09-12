#include "siglus_jpeg_decoder.h"
#include <climits>
#include <limits>
#include <turbojpeg.h>

namespace aetherkiri::siglus {
    int32_t DecodeJpeg(const uint8_t *source, size_t size, uint32_t width,
        uint32_t height, uint8_t *output, size_t output_size) noexcept {
        if(!source || !output || size == 0 || size > ULONG_MAX || width == 0 ||
            height == 0 || width > INT_MAX / 4 || height > INT_MAX ||
            size_t(width) > std::numeric_limits<size_t>::max() / 4 / height ||
            output_size != size_t(width) * height * 4) return -1;

        tjhandle decoder = tjInitDecompress();
        if(!decoder) return -1;
        int actual_width = 0, actual_height = 0, subsampling = 0, colorspace = 0;
        int result = tjDecompressHeader3(decoder, source, static_cast<unsigned long>(size),
            &actual_width, &actual_height, &subsampling, &colorspace);
        if(result == 0 && actual_width == static_cast<int>(width) &&
            actual_height == static_cast<int>(height)) {
            // Accurate DCT/default chroma upsampling: no lower-quality fast path.
            result = tjDecompress2(decoder, source, static_cast<unsigned long>(size),
                output, actual_width, actual_width * 4, actual_height, TJPF_RGBA,
                TJFLAG_ACCURATEDCT | TJFLAG_STOPONWARNING);
        } else {
            result = -1;
        }
        tjDestroy(decoder);
        return result == 0 ? 0 : -1;
    }
}
