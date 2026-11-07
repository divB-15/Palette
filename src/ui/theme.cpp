#include "ui/theme.h"

#include <cmath>
#include <vector>

namespace {

std::wstring PickFamily(const std::vector<std::wstring>& candidates) {
    for (const auto& name : candidates) {
        Gdiplus::FontFamily family(name.c_str());
        if (!family.IsAvailable()) {
            continue;
        }
        // Some systems report a family as available while GDI+ still cannot
        // instantiate it, so probe with a real font before committing.
        Gdiplus::Font probe(name.c_str(), 12.0f, Gdiplus::FontStyleRegular,
                            Gdiplus::UnitPixel);
        if (probe.GetLastStatus() == Gdiplus::Ok) {
            return name;
        }
    }
    return candidates.empty() ? std::wstring(L"Arial") : candidates.back();
}

std::unique_ptr<Gdiplus::Font> MakeFont(const std::wstring& family, float pixelSize, INT style) {
    auto attempt = [pixelSize, style](const std::wstring& name) -> std::unique_ptr<Gdiplus::Font> {
        auto font = std::make_unique<Gdiplus::Font>(name.c_str(), pixelSize, style,
                                                    Gdiplus::UnitPixel);
        if (font->GetLastStatus() != Gdiplus::Ok) {
            return nullptr;
        }
        return font;
    };

    if (auto font = attempt(family)) {
        return font;
    }
    if (auto font = attempt(L"Arial")) {
        return font;
    }

    // Last resort: the generic sans-serif family always exists in GDI+.
    if (const Gdiplus::FontFamily* generic = Gdiplus::FontFamily::GenericSansSerif()) {
        auto font = std::make_unique<Gdiplus::Font>(generic, pixelSize, style,
                                                    Gdiplus::UnitPixel);
        if (font->GetLastStatus() == Gdiplus::Ok) {
            return font;
        }
    }

    // Never hand back a null font: the drawing code dereferences these
    // pointers unconditionally and a crash is far worse than a wrong glyph.
    return std::make_unique<Gdiplus::Font>(Gdiplus::FontFamily::GenericSansSerif(), pixelSize,
                                           style, Gdiplus::UnitPixel);
}

// Creates the GDI font that mirrors the GDI+ monospace font, so the native
// edit control renders text at the same size as the painted rows.
HFONT MakeValueFont(const std::wstring& family, float pixelSize) {
    const int height = -static_cast<int>(std::lround(pixelSize));
    return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, family.c_str());
}

}  // namespace

namespace ui {

Theme::Theme() {
    // Chinese UI text needs a CJK-capable family; fall back gracefully when the
    // system is running in another language.
    uiFamily_ = PickFamily({L"Microsoft YaHei UI", L"Microsoft YaHei", L"Noto Sans CJK SC",
                            L"WenQuanYi Micro Hei", L"PingFang SC", L"Segoe UI", L"Arial"});
    monoFamily_ = PickFamily({L"Consolas", L"Cascadia Mono", L"Liberation Mono",
                              L"DejaVu Sans Mono", L"Courier New", L"Segoe UI"});

    colors_.background = Gdiplus::Color(0xFF14161A);
    colors_.card = Gdiplus::Color(0xFF1E2128);
    colors_.cardBorder = Gdiplus::Color(0xFF2B2F38);
    colors_.row = Gdiplus::Color(0xFF23262E);
    colors_.rowHover = Gdiplus::Color(0xFF2B303A);
    colors_.text = Gdiplus::Color(0xFFF1F3F5);
    colors_.muted = Gdiplus::Color(0xFF9AA2B1);
    colors_.faint = Gdiplus::Color(0xFF626B7A);
    colors_.selection = Gdiplus::Color(0xFF5B8DEF);
    colors_.toast = Gdiplus::Color(0xF22A2E36);

    Rebuild(1.0f);
}

void Theme::Rebuild(float scale) {
    scale_ = scale;

    const float u = scale;
    fonts_.title = MakeFont(uiFamily_, 26.0f * u, Gdiplus::FontStyleBold);
    fonts_.subtitle = MakeFont(uiFamily_, 12.0f * u, Gdiplus::FontStyleRegular);
    fonts_.section = MakeFont(uiFamily_, 14.0f * u, Gdiplus::FontStyleBold);
    fonts_.body = MakeFont(uiFamily_, 13.0f * u, Gdiplus::FontStyleRegular);
    fonts_.value = MakeFont(monoFamily_, 14.0f * u, Gdiplus::FontStyleRegular);
    fonts_.small = MakeFont(uiFamily_, 11.0f * u, Gdiplus::FontStyleRegular);
    fonts_.hexLarge = MakeFont(monoFamily_, 20.0f * u, Gdiplus::FontStyleBold);
    fonts_.chip = MakeFont(uiFamily_, 12.0f * u, Gdiplus::FontStyleRegular);
    fonts_.toast = MakeFont(uiFamily_, 13.0f * u, Gdiplus::FontStyleRegular);

    if (valueFont_) {
        DeleteObject(valueFont_);
        valueFont_ = nullptr;
    }
    valueFont_ = MakeValueFont(monoFamily_, 14.0f * u);
}

Theme::~Theme() {
    if (valueFont_) {
        DeleteObject(valueFont_);
        valueFont_ = nullptr;
    }
}

}  // namespace ui
