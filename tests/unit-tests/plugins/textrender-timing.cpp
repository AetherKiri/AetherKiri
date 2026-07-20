#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "textrender_timing.h"

namespace {

int showCount(const std::vector<double> &delays, double elapsed,
              double timeScale = 1.0) {
  return krkr::textrender::CalcShowCount(
      delays.begin(), delays.end(), elapsed, timeScale,
      [](double delay) { return delay; });
}

}  // namespace

TEST_CASE("TextRender reveal count follows cumulative character delays") {
  const std::vector<double> delays{0.0, 25.0, 50.0, 75.0};

  CHECK(showCount(delays, -1.0) == 0);
  CHECK(showCount(delays, 0.0) == 1);
  CHECK(showCount(delays, 24.0) == 1);
  CHECK(showCount(delays, 25.0) == 2);
  CHECK(showCount(delays, 74.0) == 3);
  CHECK(showCount(delays, 75.0) == 4);
}

TEST_CASE("TextRender reveal count applies timeScale") {
  const std::vector<double> delays{0.0, 25.0, 50.0, 75.0};

  CHECK(showCount(delays, 49.0, 2.0) == 1);
  CHECK(showCount(delays, 50.0, 2.0) == 2);
  CHECK(showCount(delays, 149.0, 2.0) == 3);
  CHECK(showCount(delays, 150.0, 2.0) == 4);
}

TEST_CASE("TextRender delay scaling clamps negative timeScale") {
  CHECK(krkr::textrender::ScaleDelay(25.0, -2.0) == 0.0);
}

TEST_CASE("TextRender CharacterInfo delay remains on the unscaled timeline") {
  constexpr double rawDelay = 25.0;

  CHECK(krkr::textrender::CharacterDelayForScript(rawDelay) == rawDelay);
  CHECK(krkr::textrender::CharacterDelayForScript(rawDelay) !=
        krkr::textrender::ScaleDelay(rawDelay, 2.0));
}

TEST_CASE("TextRender plain mode treats only LF as a line break") {
  std::u16string characters;
  int lineFeeds = 0;

  krkr::textrender::ParsePlainText<char16_t>(
      u"%\\[]#$&\r\ntext",
      [&characters](char16_t ch) { characters.push_back(ch); },
      [&lineFeeds]() { ++lineFeeds; });

  CHECK(characters == u"%\\[]#$&\rtext");
  CHECK(lineFeeds == 1);
}

TEST_CASE("TextRender optional timing values distinguish empty and zero") {
  const auto reset = krkr::textrender::ParseOptionalIntegerToken<char16_t>(u"");
  REQUIRE(reset.valid);
  CHECK_FALSE(reset.hasValue);

  const auto zero =
      krkr::textrender::ParseOptionalIntegerToken<char16_t>(u"0");
  REQUIRE(zero.valid);
  REQUIRE(zero.hasValue);
  CHECK(zero.value == 0);

  const auto absolute =
      krkr::textrender::ParseOptionalIntegerToken<char16_t>(u"20");
  REQUIRE(absolute.valid);
  REQUIRE(absolute.hasValue);
  CHECK(absolute.value == 20);
}
