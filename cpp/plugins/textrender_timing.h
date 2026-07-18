#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

namespace krkr::textrender {

template <typename RandomAccessIterator, typename DelayAccessor>
int CalcShowCount(RandomAccessIterator first, RandomAccessIterator last,
                  double elapsed, double timeScale,
                  DelayAccessor delayAccessor) {
  const double scale = std::max(timeScale, 0.0);
  for (auto it = last; it != first;) {
    --it;
    if (elapsed >= delayAccessor(*it) * scale) {
      return static_cast<int>((it - first) + 1);
    }
  }
  return 0;
}

inline double ScaleDelay(double delay, double timeScale) {
  return delay * std::max(timeScale, 0.0);
}

// CharacterInfo.delay is part of the script-visible layout data and is stored
// in the renderer's unscaled timeline.  timeScale is only applied by the
// reveal-count and renderDelay accessors.
inline double CharacterDelayForScript(double delay) { return delay; }

template <typename CharT, typename CharacterCallback,
          typename LineFeedCallback>
void ParsePlainText(std::basic_string_view<CharT> text,
                    CharacterCallback &&characterCallback,
                    LineFeedCallback &&lineFeedCallback) {
  for (const CharT ch : text) {
    // The native fast/plain path recognizes LF only.  CR and every markup
    // introducer are ordinary, visible characters in this mode.
    if (ch == static_cast<CharT>('\n')) {
      lineFeedCallback();
    } else {
      characterCallback(ch);
    }
  }
}

struct OptionalInteger {
  bool valid = true;
  bool hasValue = false;
  int value = 0;
};

template <typename CharT>
OptionalInteger ParseOptionalIntegerToken(
    std::basic_string_view<CharT> token) {
  OptionalInteger result;
  bool negative = false;
  for (const CharT ch : token) {
    if (ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9')) {
      result.hasValue = true;
      result.value = result.value * 10 +
                     static_cast<int>(ch - static_cast<CharT>('0'));
    } else if (ch == static_cast<CharT>('-')) {
      negative = !negative;
    } else {
      result.valid = false;
      return result;
    }
  }
  if (negative && result.hasValue) result.value = -result.value;
  return result;
}

}  // namespace krkr::textrender
