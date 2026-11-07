#pragma once

#include "ui/gdiplus_include.h"

namespace ui {

constexpr int kValueRowCount = 5;
constexpr int kHarmonySwatchCount = 5;
constexpr int kRecentCount = 10;
constexpr int kRgbSliderCount = 3;

// Inner geometry of a value row (logical pixels, before the DPI scale).
// The row is split into a label, an editable value field and a copy button.
constexpr float kValueLabelOffset = 14.0f;
constexpr float kValueLabelWidth = 58.0f;
constexpr float kValueFieldOffset = 74.0f;
constexpr float kValueFieldTrailing = 66.0f;   // room kept for the copy button
constexpr float kValueCopyWidth = 46.0f;
constexpr float kValueCopyRight = 12.0f;

// Inner geometry of an RGB slider row (logical pixels).
//   [R] ━━━━━━━●━━━━━━ [128]
//   label + track + numeric readout
constexpr float kRgbRowHeight = 26.0f;
constexpr float kRgbRowGap = 8.0f;
constexpr float kRgbLabelWidth = 18.0f;
constexpr float kRgbLabelGap = 10.0f;
constexpr float kRgbValueWidth = 38.0f;
constexpr float kRgbValueGap = 10.0f;
constexpr float kRgbTrackHeight = 6.0f;

struct Layout {
    Gdiplus::RectF header;
    Gdiplus::RectF pickerCard;
    Gdiplus::RectF valuesCard;
    Gdiplus::RectF rgbCard;
    Gdiplus::RectF harmonyCard;
    Gdiplus::RectF svSquare;
    Gdiplus::RectF hueBar;
    Gdiplus::RectF previewSwatch;
    Gdiplus::RectF valueRows[kValueRowCount];
    Gdiplus::RectF valueFields[kValueRowCount];    // editable area
    Gdiplus::RectF valueCopyZones[kValueRowCount]; // "copy" button
    Gdiplus::RectF rgbTracks[kRgbSliderCount];     // slider track
    Gdiplus::RectF rgbLabels[kRgbSliderCount];     // "R" / "G" / "B"
    Gdiplus::RectF rgbValues[kRgbSliderCount];     // numeric readout
    Gdiplus::RectF chips[6];
    Gdiplus::RectF harmonySwatches[kHarmonySwatchCount];
    Gdiplus::RectF recentSwatches[kRecentCount];
    Gdiplus::RectF recentLabel;                   // 「最近使用」标题
    Gdiplus::RectF toast;
    float scale = 1.0f;
    bool valid = false;
};

// All returned rectangles are already expressed in physical pixels. The
// layout is computed from logical sizes so it stays consistent at any DPI.
Layout BuildLayout(float clientWidth, float clientHeight, float scale);

}  // namespace ui
