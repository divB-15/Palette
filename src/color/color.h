#pragma once

#include <cstdint>
#include <string>

namespace color {

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct Hsv {
    double h = 0.0;  // hue,      [0, 360)
    double s = 0.0;  // saturation, [0, 1]
    double v = 0.0;  // value,      [0, 1]
};

struct Hsl {
    double h = 0.0;  // hue,        [0, 360)
    double s = 0.0;  // saturation, [0, 1]
    double l = 0.0;  // lightness,  [0, 1]
};

struct Cmyk {
    double c = 0.0;
    double m = 0.0;
    double y = 0.0;
    double k = 0.0;
};

Rgb RgbFromHsv(const Hsv& hsv);
Hsv HsvFromRgb(const Rgb& rgb);

Hsl HslFromRgb(const Rgb& rgb);
Rgb RgbFromHsl(const Hsl& hsl);

Cmyk CmykFromRgb(const Rgb& rgb);
Rgb RgbFromCmyk(const Cmyk& cmyk);

// Normalizes hue into [0, 360) and clamps saturation/value into [0, 1].
Hsv NormalizeHsv(Hsv hsv);

// 0xAARRGGBB, convenient for GDI+.
uint32_t Argb(const Rgb& rgb, uint8_t alpha = 255);

std::wstring ToHex(const Rgb& rgb);
std::wstring ToCssRgb(const Rgb& rgb);
std::wstring ToCssHsv(const Hsv& hsv);
std::wstring ToCssHsl(const Rgb& rgb);
std::wstring ToCssCmyk(const Rgb& rgb);

uint8_t ToByte(double value);

}  // namespace color
