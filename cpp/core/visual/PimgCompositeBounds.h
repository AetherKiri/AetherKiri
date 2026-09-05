#ifndef PimgCompositeBoundsH
#define PimgCompositeBoundsH

#include <algorithm>
#include <limits>
#include <vector>

namespace TVPLayerInternal {

struct PimgLayerRect {
    int left;
    int top;
    int width;
    int height;
};

struct PimgCompositeBounds {
    int left;
    int top;
    int width;
    int height;
};

inline bool ComputePimgCompositeBounds(
    const std::vector<PimgLayerRect> &layers, PimgCompositeBounds &bounds) {
    bool found = false;
    long long minLeft = 0;
    long long minTop = 0;
    long long maxRight = 0;
    long long maxBottom = 0;

    for(const auto &layer : layers) {
        if(layer.width <= 0 || layer.height <= 0) {
            continue;
        }

        const long long left = layer.left;
        const long long top = layer.top;
        const long long right = left + layer.width;
        const long long bottom = top + layer.height;
        if(!found) {
            minLeft = left;
            minTop = top;
            maxRight = right;
            maxBottom = bottom;
            found = true;
        } else {
            minLeft = std::min(minLeft, left);
            minTop = std::min(minTop, top);
            maxRight = std::max(maxRight, right);
            maxBottom = std::max(maxBottom, bottom);
        }
    }

    if(!found) {
        return false;
    }

    const long long width = maxRight - minLeft;
    const long long height = maxBottom - minTop;
    if(width <= 0 || height <= 0 ||
       width > std::numeric_limits<int>::max() ||
       height > std::numeric_limits<int>::max()) {
        return false;
    }

    bounds = {static_cast<int>(minLeft), static_cast<int>(minTop),
              static_cast<int>(width), static_cast<int>(height)};
    return true;
}

} // namespace TVPLayerInternal

#endif
