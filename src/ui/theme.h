#pragma once

#include "ui/gdiplus_include.h"

#include <memory>
#include <string>

namespace ui {

struct Colors {
    Gdiplus::Color background;
    Gdiplus::Color card;
    Gdiplus::Color cardBorder;
    Gdiplus::Color row;
    Gdiplus::Color rowHover;
    Gdiplus::Color text;
    Gdiplus::Color muted;
    Gdiplus::Color faint;
    Gdiplus::Color selection;
    Gdiplus::Color toast;
};

struct Fonts {
    std::unique_ptr<Gdiplus::Font> title;
    std::unique_ptr<Gdiplus::Font> subtitle;
    std::unique_ptr<Gdiplus::Font> section;
    std::unique_ptr<Gdiplus::Font> body;
    std::unique_ptr<Gdiplus::Font> value;
    std::unique_ptr<Gdiplus::Font> small;
    std::unique_ptr<Gdiplus::Font> hexLarge;
    std::unique_ptr<Gdiplus::Font> chip;
    std::unique_ptr<Gdiplus::Font> toast;
};

// Owns the design tokens (colors/fonts) and rebuilds the pixel-sized fonts
// whenever the window moves to a monitor with a different DPI.
class Theme {
public:
    Theme();
    ~Theme();

    Theme(const Theme&) = delete;
    Theme& operator=(const Theme&) = delete;

    void Rebuild(float scale);

    float scale() const { return scale_; }
    const Colors& colors() const { return colors_; }
    const Fonts& fonts() const { return fonts_; }

    // GDI counterpart of fonts().value, used by the native edit control that
    // overlays the value rows. Created together with the GDI+ fonts.
    HFONT valueFont() const { return valueFont_; }

private:
    float scale_ = 1.0f;
    Colors colors_;
    Fonts fonts_;
    std::wstring uiFamily_;
    std::wstring monoFamily_;
    HFONT valueFont_ = nullptr;
};

}  // namespace ui
