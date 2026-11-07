#include "ui/render.h"

#include <algorithm>

namespace ui {

bool Contains(const Gdiplus::RectF& rect, float x, float y) {
    return x >= rect.X && x < rect.X + rect.Width && y >= rect.Y && y < rect.Y + rect.Height;
}

Gdiplus::RectF Inset(const Gdiplus::RectF& rect, float dx, float dy) {
    return Gdiplus::RectF(rect.X + dx, rect.Y + dy, rect.Width - dx * 2.0f, rect.Height - dy * 2.0f);
}

void BuildRoundRectPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    if (rect.Width <= 0.0f || rect.Height <= 0.0f) {
        return;
    }

    float r = std::min(radius, std::min(rect.Width, rect.Height) * 0.5f);
    if (r <= 0.0f) {
        path.AddRectangle(rect);
        return;
    }

    float d = r * 2.0f;
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillRounded(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius,
                 const Gdiplus::Color& color) {
    if (rect.Width <= 0.0f || rect.Height <= 0.0f) {
        return;
    }
    Gdiplus::GraphicsPath path;
    BuildRoundRectPath(path, rect, radius);
    Gdiplus::SolidBrush brush(color);
    graphics.FillPath(&brush, &path);
}

void StrokeRounded(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius,
                   float width, const Gdiplus::Color& color) {
    if (rect.Width <= 0.0f || rect.Height <= 0.0f || width <= 0.0f) {
        return;
    }
    Gdiplus::GraphicsPath path;
    BuildRoundRectPath(path, rect, radius);
    Gdiplus::Pen pen(color, width);
    graphics.DrawPath(&pen, &path);
}

void FillGradientRounded(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float radius,
                         Gdiplus::Color* colors, Gdiplus::REAL* positions, int count,
                         Gdiplus::LinearGradientMode mode) {
    if (rect.Width <= 0.0f || rect.Height <= 0.0f || !colors || !positions || count < 2) {
        return;
    }

    Gdiplus::GraphicsPath path;
    BuildRoundRectPath(path, rect, radius);

    // Keep whatever clip the caller had: ResetClip() would throw it away and
    // let the fill bleed outside of it.
    Gdiplus::GraphicsState saved = graphics.Save();
    graphics.SetClip(&path, Gdiplus::CombineModeIntersect);

    Gdiplus::LinearGradientBrush brush(rect, colors[0], colors[count - 1], mode);
    if (count > 2) {
        brush.SetInterpolationColors(colors, positions, count);
    }
    graphics.FillRectangle(&brush, rect);
    graphics.Restore(saved);
}

void DrawText(Gdiplus::Graphics& graphics, const std::wstring& text, const Gdiplus::RectF& rect,
              const Gdiplus::Font& font, const Gdiplus::Color& color,
              Gdiplus::StringAlignment alignment, bool ellipsis) {
    if (text.empty() || rect.Width <= 0.0f || rect.Height <= 0.0f) {
        return;
    }

    Gdiplus::StringFormat format;
    format.SetAlignment(alignment);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    if (ellipsis) {
        format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    }

    Gdiplus::SolidBrush brush(color);
    graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, rect, &format, &brush);
}

}  // namespace ui
