#include <catch2/catch_test_macros.hpp>

#include "tjs.h"
#include "visual/transhandler.h"

TEST_CASE("divisible transitions use stable page snapshots") {
    CHECK(TVPTransitionUsesStaticSnapshots(tutDivisibleFade));
    CHECK(TVPTransitionUsesStaticSnapshots(tutDivisible));
    CHECK_FALSE(TVPTransitionUsesStaticSnapshots(tutGiveUpdate));
}
