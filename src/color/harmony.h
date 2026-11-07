#pragma once

#include <vector>

#include "color/color.h"

namespace color {

enum class HarmonyType {
    Complementary = 0,
    Analogous,
    Triadic,
    SplitComplementary,
    Tetradic,
    Monochromatic
};

constexpr int kHarmonyTypeCount = 6;

const wchar_t* HarmonyName(HarmonyType type);

// Returns exactly five colors, ordered from left to right in the UI.
std::vector<Rgb> HarmonyPalette(const Hsv& base, HarmonyType type);

}  // namespace color
