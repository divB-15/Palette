#include "color/color.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {

double WrapHue(double hue) {
    hue = std::fmod(hue, 360.0);
    if (hue < 0.0) {
        hue += 360.0;
    }
    return hue;
}

double Clamp01(double value) {
    return std::min(1.0, std::max(0.0, value));
}

int Percent(double value) {
    return static_cast<int>(std::lround(Clamp01(value) * 100.0));
}

int Degrees(double hue) {
    int value = static_cast<int>(std::lround(WrapHue(hue)));
    return value % 360;
}

// Shared by HSV and HSL: computes the hue and the chroma of an RGB color.
double HueFromRgb(double r, double g, double b, double maximum, double delta) {
    if (delta <= 0.0) {
        return 0.0;
    }
    double hue;
    if (maximum == r) {
        hue = 60.0 * (g - b) / delta;
    } else if (maximum == g) {
        hue = 60.0 * ((b - r) / delta + 2.0);
    } else {
        hue = 60.0 * ((r - g) / delta + 4.0);
    }
    return WrapHue(hue);
}

// Common sector computation used by HSV -> RGB and HSL -> RGB.
void ChromaToRgb(double hue, double chroma, double x, double offset, double* r, double* g, double* b) {
    int sector = static_cast<int>(std::floor(hue / 60.0)) % 6;
    double rr = 0.0, gg = 0.0, bb = 0.0;
    switch (sector) {
        case 0: rr = chroma; gg = x;      bb = 0.0;    break;
        case 1: rr = x;      gg = chroma; bb = 0.0;    break;
        case 2: rr = 0.0;    gg = chroma; bb = x;      break;
        case 3: rr = 0.0;    gg = x;      bb = chroma; break;
        case 4: rr = x;      gg = 0.0;    bb = chroma; break;
        default: rr = chroma; gg = 0.0;   bb = x;      break;
    }
    *r = rr + offset;
    *g = gg + offset;
    *b = bb + offset;
}

}  // namespace

namespace color {

uint8_t ToByte(double value) {
    return static_cast<uint8_t>(std::lround(std::min(255.0, std::max(0.0, value))));
}

Hsv NormalizeHsv(Hsv hsv) {
    hsv.h = WrapHue(hsv.h);
    hsv.s = Clamp01(hsv.s);
    hsv.v = Clamp01(hsv.v);
    return hsv;
}

Rgb RgbFromHsv(const Hsv& hsv) {
    Hsv normalized = NormalizeHsv(hsv);

    double chroma = normalized.v * normalized.s;
    double huePrime = normalized.h / 60.0;
    double x = chroma * (1.0 - std::abs(std::fmod(huePrime, 2.0) - 1.0));
    double offset = normalized.v - chroma;

    double r = 0.0, g = 0.0, b = 0.0;
    ChromaToRgb(normalized.h, chroma, x, offset, &r, &g, &b);

    Rgb rgb;
    rgb.r = ToByte(r * 255.0);
    rgb.g = ToByte(g * 255.0);
    rgb.b = ToByte(b * 255.0);
    return rgb;
}

Hsv HsvFromRgb(const Rgb& rgb) {
    double r = static_cast<double>(rgb.r) / 255.0;
    double g = static_cast<double>(rgb.g) / 255.0;
    double b = static_cast<double>(rgb.b) / 255.0;

    double maximum = std::max(r, std::max(g, b));
    double minimum = std::min(r, std::min(g, b));
    double delta = maximum - minimum;

    Hsv hsv;
    hsv.h = HueFromRgb(r, g, b, maximum, delta);
    hsv.s = maximum <= 0.0 ? 0.0 : delta / maximum;
    hsv.v = maximum;
    return hsv;
}

Hsl HslFromRgb(const Rgb& rgb) {
    double r = static_cast<double>(rgb.r) / 255.0;
    double g = static_cast<double>(rgb.g) / 255.0;
    double b = static_cast<double>(rgb.b) / 255.0;

    double maximum = std::max(r, std::max(g, b));
    double minimum = std::min(r, std::min(g, b));
    double delta = maximum - minimum;
    double lightness = (maximum + minimum) * 0.5;

    Hsl hsl;
    hsl.h = HueFromRgb(r, g, b, maximum, delta);
    hsl.l = lightness;

    double denominator = 1.0 - std::abs(2.0 * lightness - 1.0);
    hsl.s = denominator <= 0.0 ? 0.0 : delta / denominator;
    return hsl;
}

Rgb RgbFromHsl(const Hsl& hsl) {
    double hue = WrapHue(hsl.h);
    double saturation = Clamp01(hsl.s);
    double lightness = Clamp01(hsl.l);

    double chroma = (1.0 - std::abs(2.0 * lightness - 1.0)) * saturation;
    double huePrime = hue / 60.0;
    double x = chroma * (1.0 - std::abs(std::fmod(huePrime, 2.0) - 1.0));
    double offset = lightness - chroma * 0.5;

    double r = 0.0, g = 0.0, b = 0.0;
    ChromaToRgb(hue, chroma, x, offset, &r, &g, &b);

    Rgb rgb;
    rgb.r = ToByte(r * 255.0);
    rgb.g = ToByte(g * 255.0);
    rgb.b = ToByte(b * 255.0);
    return rgb;
}

Cmyk CmykFromRgb(const Rgb& rgb) {
    double r = static_cast<double>(rgb.r) / 255.0;
    double g = static_cast<double>(rgb.g) / 255.0;
    double b = static_cast<double>(rgb.b) / 255.0;

    double k = 1.0 - std::max(r, std::max(g, b));
    Cmyk cmyk;
    cmyk.k = k;
    if (k >= 1.0) {
        return cmyk;
    }
    double denominator = 1.0 - k;
    cmyk.c = (1.0 - r - k) / denominator;
    cmyk.m = (1.0 - g - k) / denominator;
    cmyk.y = (1.0 - b - k) / denominator;
    return cmyk;
}

Rgb RgbFromCmyk(const Cmyk& cmyk) {
    double k = Clamp01(cmyk.k);
    double scale = 1.0 - k;
    double r = (1.0 - Clamp01(cmyk.c)) * scale;
    double g = (1.0 - Clamp01(cmyk.m)) * scale;
    double b = (1.0 - Clamp01(cmyk.y)) * scale;

    Rgb rgb;
    rgb.r = ToByte(r * 255.0);
    rgb.g = ToByte(g * 255.0);
    rgb.b = ToByte(b * 255.0);
    return rgb;
}

uint32_t Argb(const Rgb& rgb, uint8_t alpha) {
    return (static_cast<uint32_t>(alpha) << 24) |
           (static_cast<uint32_t>(rgb.r) << 16) |
           (static_cast<uint32_t>(rgb.g) << 8) |
           static_cast<uint32_t>(rgb.b);
}

std::wstring ToHex(const Rgb& rgb) {
    wchar_t buffer[16];
    std::swprintf(buffer, 16, L"#%02X%02X%02X",
                  static_cast<unsigned int>(rgb.r),
                  static_cast<unsigned int>(rgb.g),
                  static_cast<unsigned int>(rgb.b));
    return buffer;
}

std::wstring ToCssRgb(const Rgb& rgb) {
    wchar_t buffer[64];
    std::swprintf(buffer, 64, L"rgb(%u, %u, %u)",
                  static_cast<unsigned int>(rgb.r),
                  static_cast<unsigned int>(rgb.g),
                  static_cast<unsigned int>(rgb.b));
    return buffer;
}

std::wstring ToCssHsv(const Hsv& hsv) {
    Hsv normalized = NormalizeHsv(hsv);
    wchar_t buffer[64];
    std::swprintf(buffer, 64, L"hsv(%d, %d%%, %d%%)",
                  Degrees(normalized.h),
                  Percent(normalized.s),
                  Percent(normalized.v));
    return buffer;
}

std::wstring ToCssHsl(const Rgb& rgb) {
    Hsl hsl = HslFromRgb(rgb);
    wchar_t buffer[64];
    std::swprintf(buffer, 64, L"hsl(%d, %d%%, %d%%)",
                  Degrees(hsl.h),
                  Percent(hsl.s),
                  Percent(hsl.l));
    return buffer;
}

std::wstring ToCssCmyk(const Rgb& rgb) {
    Cmyk cmyk = CmykFromRgb(rgb);
    wchar_t buffer[64];
    std::swprintf(buffer, 64, L"cmyk(%d%%, %d%%, %d%%, %d%%)",
                  Percent(cmyk.c),
                  Percent(cmyk.m),
                  Percent(cmyk.y),
                  Percent(cmyk.k));
    return buffer;
}

}  // namespace color
