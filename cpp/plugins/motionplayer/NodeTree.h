//
// Build persistent node tree from PSB layer hierarchy.
// Aligned to libkrkr2.so sub_6B4A6C (0x6B4A6C): recursive tree walk
// that appends into the Player-owned node deque with parentIndex.
//
#pragma once

#include <string>

namespace motion {
    class Player;
    class ResourceManager;
}

namespace motion::detail {

    struct MotionSnapshot;
    struct MotionNode;
    struct PlayerRuntime;

    // Walk the PSB layer tree for the given clip (or root layers if clipLabel
    // is empty/not found) and append nodes after the persistent root node.
    // Index 0 is the constructor-created root; each real PSB layer points to
    // its parent node index, with top-level layers using parentIndex=0.
    void buildNodeTree(
        PlayerRuntime &runtime,
        const MotionSnapshot &snapshot,
        const std::string &clipLabel,
        motion::ResourceManager *resourceManager = nullptr,
        motion::Player *ownerPlayer = nullptr,
        int parentCompletionType = 0);

} // namespace motion::detail
