#ifndef LayerCompletionCoordinatesH
#define LayerCompletionCoordinatesH

namespace TVPLayerInternal {

struct GpuCompletionCoordinates {
    int destinationX;
    int destinationY;
    int parentLeft;
    int parentTop;
    int parentRight;
    int parentBottom;
};

inline GpuCompletionCoordinates ResolveGpuCompletionCoordinates(
    int updateLeft, int updateTop, int updateRight, int updateBottom,
    int layerLeft, int layerTop, bool localDestination) {
    const int parentLeft = updateLeft + layerLeft;
    const int parentTop = updateTop + layerTop;
    return {localDestination ? updateLeft : parentLeft,
            localDestination ? updateTop : parentTop,
            parentLeft,
            parentTop,
            updateRight + layerLeft,
            updateBottom + layerTop};
}

} // namespace TVPLayerInternal

#endif
