#pragma once

#include <string>

#include "color/color.h"

namespace color {

// Parses a hexadecimal color. Accepted forms: "#RGB", "RGB", "#RRGGBB",
// "RRGGBB" and the same with an "0x" prefix. The leading '#' is optional.
bool ParseHex(const std::wstring& text, Rgb* out);

// Accepts "rgb(58, 131, 214)", "58, 131, 214" and "58 131 214".
// Components are clamped into [0, 255]; "100%" is understood as a ratio.
bool ParseRgb(const std::wstring& text, Rgb* out);

// Accepts "hsv(212, 73%, 84%)" and "212, 73, 84". Saturation and value are
// read as percentages unless they are written as a plain fraction (0..1).
bool ParseHsv(const std::wstring& text, Hsv* out);

// Accepts "hsl(212, 66%, 53%)" and "212, 66, 53".
bool ParseHsl(const std::wstring& text, Hsl* out);

// Accepts "cmyk(73%, 39%, 0%, 16%)" and "73, 39, 0, 16".
bool ParseCmyk(const std::wstring& text, Cmyk* out);

}  // namespace color
