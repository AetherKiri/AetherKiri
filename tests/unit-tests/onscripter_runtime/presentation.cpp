#include <catch2/catch_test_macros.hpp>

#include "onscripter_presentation.h"

using aetherkiri::onscripter::PresentationViewport;
using aetherkiri::onscripter::ResolvePresentationViewport;
using aetherkiri::onscripter::ResolvePresentationScroll;

TEST_CASE("ONS widescreen packages expose their top 16:9 viewport") {
    const PresentationViewport viewport =
        ResolvePresentationViewport(800, 600, 16, 9);
    CHECK(viewport.x == 0);
    CHECK(viewport.y == 0);
    CHECK(viewport.width == 800);
    CHECK(viewport.height == 450);
}

TEST_CASE("ONS presentation viewport preserves already matching frames") {
    const PresentationViewport viewport =
        ResolvePresentationViewport(1280, 720, 16, 9);
    CHECK(viewport.x == 0);
    CHECK(viewport.y == 0);
    CHECK(viewport.width == 1280);
    CHECK(viewport.height == 720);
}

TEST_CASE("ONS presentation viewport supports other widescreen canvases") {
    const PresentationViewport viewport =
        ResolvePresentationViewport(1024, 768, 16, 9);
    CHECK(viewport.width == 1024);
    CHECK(viewport.height == 576);
}

TEST_CASE("ONS presentation viewport never implicitly narrows content") {
    const PresentationViewport viewport =
        ResolvePresentationViewport(1280, 720, 4, 3);
    CHECK(viewport.width == 1280);
    CHECK(viewport.height == 720);
}

TEST_CASE("ONS presentation viewport ignores incomplete configuration") {
    const PresentationViewport viewport =
        ResolvePresentationViewport(800, 600, 0, 9);
    CHECK(viewport.width == 800);
    CHECK(viewport.height == 600);
}

TEST_CASE("ONS presentation scroll stays inside cropped source") {
    CHECK(ResolvePresentationScroll(600, 0, 450, 0, 40) == 40);
    CHECK(ResolvePresentationScroll(600, 0, 450, 40, 200) == 150);
    CHECK(ResolvePresentationScroll(600, 0, 450, 150, -35) == 115);
    CHECK(ResolvePresentationScroll(600, 0, 450, 20, -200) == 0);
}

TEST_CASE("ONS presentation scroll is disabled without overflow") {
    CHECK(ResolvePresentationScroll(600, 0, 600, 0, 40) == 0);
    CHECK(ResolvePresentationScroll(600, 200, 450, 0, 40) == 0);
}
