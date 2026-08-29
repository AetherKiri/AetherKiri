#include <catch2/catch_test_macros.hpp>

#include <richtext/Appearance.hpp>
#include <richtext/FontManager.hpp>
#include <richtext/TextStyle.hpp>

TEST_CASE("krkrz richtext library keeps its host-injected defaults") {
    richtext::TextStyle style;
    CHECK(style.fontSize > 0.0f);
    CHECK(style.fontWeight > 0);

    richtext::Appearance appearance;
    CHECK(appearance.isEmpty());
    appearance.setColor(0xff336699u);
    CHECK_FALSE(appearance.isEmpty());
    appearance.clear();
    CHECK(appearance.isEmpty());
}

TEST_CASE("krkrz richtext FontManager is a stable process singleton") {
    auto &first = richtext::FontManager::instance();
    auto &second = richtext::FontManager::instance();
    CHECK(&first == &second);
}
