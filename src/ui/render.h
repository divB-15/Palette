#pragma once

#include "ui/gdiplus_include.h"

#include <string>

namespace ui {

bool Contains(const Gdiplus::RectF& rect, float x, float y);

Gdiplus::RectF Inset(const Gdiplus::RectF& rect, float dx, float dy);

void BuildRoundRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius);

void FillRounded(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius,
                 const Gdiplus::Color& color);

void StrokeRounded(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius,
                   float width, const Gdiplus::Color& color);

// Clipped linear gradient with optional multi-stop color blend.
void FillGradientRounded(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius,
                         Gdiplus::Color* colors, Gdiplus::REAL* positions, int count,
                         Gdiplus::LinearGradientMode mode);

void DrawText(Gdiplus::Graphics& graphics, const std::wstring& text, const Gdiplus::RectF& rect,
              const Gdiplus::Font& font, const Gdiplus::Color& color,
              Gdiplus::StringAlignment alignment, bool ellipsis = true);

}  // namespace ui
