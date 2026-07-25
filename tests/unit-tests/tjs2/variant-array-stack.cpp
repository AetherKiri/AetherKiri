#include <catch2/catch_test_macros.hpp>

#include "tjs.h"
#include "tjsInterCodeExec.h"

TEST_CASE("TJS variant stack keeps its current block across compaction") {
    tTJSVariantArrayStack stack;

    tTJSVariant *first = stack.Allocate(1000);
    tTJSVariant *second = stack.Allocate(1000);

    stack.Compact();
    stack.Deallocate(1000, second);

    tTJSVariant *reused = stack.Allocate(1000);
    CHECK(reused == second);

    stack.Deallocate(1000, reused);
    stack.Deallocate(1000, first);
}
