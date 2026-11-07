#include "color/harmony.h"

#include <algorithm>

namespace {

color::Hsv Shifted(color::Hsv hsv, double deltaHue) {
    hsv.h += deltaHue;
    return color::NormalizeHsv(hsv);
}

color::Hsv Lighter(color::Hsv hsv) {
    hsv.v += (1.0 - hsv.v) * 0.35;
    hsv.s *= 0.85;
    return color::NormalizeHsv(hsv);
}

color::Hsv Darker(color::Hsv hsv) {
    hsv.v *= 0.55;
    hsv.s = std::min(1.0, hsv.s * 1.05);
    return color::NormalizeHsv(hsv);
}

}  // namespace

namespace color {

const wchar_t* HarmonyName(HarmonyType type) {
    switch (type) {
        case HarmonyType::Complementary: return L"互补色";
        case HarmonyType::Analogous: return L"邻近色";
        case HarmonyType::Triadic: return L"三角色";
        case HarmonyType::SplitComplementary: return L"分裂互补";
        case HarmonyType::Tetradic: return L"四方色";
        case HarmonyType::Monochromatic: return L"单色调";
    }
    return L"配色";
}

std::vector<Rgb> HarmonyPalette(const Hsv& base, HarmonyType type) {
    Hsv normalized = NormalizeHsv(base);
    std::vector<Hsv> colors;

    switch (type) {
        case HarmonyType::Complementary: {
            Hsv complement = Shifted(normalized, 180.0);
            colors = {normalized, complement, Lighter(normalized), Lighter(complement), Darker(normalized)};
            break;
        }
        case HarmonyType::Analogous: {
            colors = {
                Shifted(normalized, -40.0),
                Shifted(normalized, -20.0),
                normalized,
                Shifted(normalized, 20.0),
                Shifted(normalized, 40.0),
            };
            break;
        }
        case HarmonyType::Triadic: {
            colors = {
                normalized,
                Shifted(normalized, 120.0),
                Shifted(normalized, 240.0),
                Lighter(normalized),
                Darker(normalized),
            };
            break;
        }
        case HarmonyType::SplitComplementary: {
            colors = {
                normalized,
                Shifted(normalized, 150.0),
                Shifted(normalized, 210.0),
                Lighter(normalized),
                Darker(Shifted(normalized, 180.0)),
            };
            break;
        }
        case HarmonyType::Tetradic: {
            colors = {
                normalized,
                Shifted(normalized, 90.0),
                Shifted(normalized, 180.0),
                Shifted(normalized, 270.0),
                Lighter(normalized),
            };
            break;
        }
        case HarmonyType::Monochromatic: {
            Hsv tone = normalized;
            for (double value : {0.92, 0.76, 0.60, 0.44, 0.28}) {
                tone.v = value;
                colors.push_back(tone);
            }
            break;
        }
    }

    std::vector<Rgb> result;
    result.reserve(colors.size());
    for (const auto& item : colors) {
        result.push_back(RgbFromHsv(item));
    }
    return result;
}

}  // namespace color
