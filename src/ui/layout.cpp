#include "ui/layout.h"

#include <algorithm>

namespace {

using Gdiplus::RectF;

RectF MakeRect(float x, float y, float w, float h) {
    return RectF(x, y, std::max(0.0f, w), std::max(0.0f, h));
}

}  // namespace

namespace ui {

Layout BuildLayout(float clientWidth, float clientHeight, float scale) {
    Layout layout;
    layout.scale = scale;

    if (clientWidth <= 0.0f || clientHeight <= 0.0f || scale <= 0.0f) {
        return layout;
    }

    const float s = scale;
    auto S = [s](float value) { return value * s; };

    const float margin = S(20.0f);
    const float headerTop = S(14.0f);
    const float headerHeight = S(52.0f);
    const float gap = S(14.0f);

    layout.header = MakeRect(margin, headerTop, clientWidth - margin * 2.0f, headerHeight);

    const float contentTop = headerTop + headerHeight + S(6.0f);
    const float contentHeight = clientHeight - contentTop - margin;
    if (contentHeight < S(120.0f)) {
        return layout;
    }

    float availableWidth = clientWidth - margin * 2.0f - gap;
    float leftWidth = std::clamp(clientWidth * 0.40f, S(330.0f), S(420.0f));
    if (availableWidth - leftWidth < S(280.0f)) {
        leftWidth = std::max(S(280.0f), availableWidth - S(280.0f));
    }
    const float rightX = margin + leftWidth + gap;
    const float rightWidth = clientWidth - rightX - margin;

    layout.pickerCard = MakeRect(margin, contentTop, leftWidth, contentHeight);

    // --- Left card: saturation/value square + vertical hue strip ------------
    const float pickerPad = S(16.0f);
    const float sectionHeight = S(20.0f);
    const float titleGap = S(12.0f);
    const float hueWidth = S(26.0f);
    const float hueGap = S(14.0f);
    const float previewGap = S(16.0f);
    const float previewMinHeight = S(92.0f);
    const float innerWidth = leftWidth - pickerPad * 2.0f;

    float squareSize = innerWidth - hueWidth - hueGap;
    squareSize = std::min(
        squareSize,
        contentHeight - (pickerPad + sectionHeight + titleGap) - previewGap - previewMinHeight - pickerPad);
    squareSize = std::max(squareSize, S(120.0f));

    const float svTop = contentTop + pickerPad + sectionHeight + titleGap;
    layout.svSquare = MakeRect(margin + pickerPad, svTop, squareSize, squareSize);
    layout.hueBar = MakeRect(layout.svSquare.X + squareSize + hueGap, svTop, hueWidth, squareSize);

    const float previewTop = layout.svSquare.Y + squareSize + previewGap;
    const float previewHeight = layout.pickerCard.Y + layout.pickerCard.Height - pickerPad - previewTop;
    // 窗口变高时下面会空出一块，预览色块跟着长一点把它填上。
    const float swatchSize = std::min(S(112.0f), std::max(S(48.0f), previewHeight - S(8.0f)));
    layout.previewSwatch = MakeRect(
        margin + pickerPad, previewTop + (previewHeight - swatchSize) * 0.5f, swatchSize, swatchSize);

    // --- Right column: 颜色数值 / RGB 调节 / 配色方案 -------------------------
    const float valuesPad = S(16.0f);
    const float rowGapMax = S(4.0f);

    auto valuesHeightFor = [&](float rowHeight, float rowGap) {
        return S(44.0f) + kValueRowCount * rowHeight + (kValueRowCount - 1) * rowGap + S(14.0f);
    };

    float rowHeight = S(30.0f);
    float rowGap = rowGapMax;
    float valuesHeight = valuesHeightFor(rowHeight, rowGap);

    const float rgbHeight = S(44.0f) + kRgbSliderCount * S(kRgbRowHeight) +
                            (kRgbSliderCount - 1) * S(kRgbRowGap) + S(14.0f);
    // 配色方案卡的最小高度：内容全部放得下（色块 40px）。
    const float harmonyMinHeight = S(200.0f);

    // 竖向预算不够时的降级顺序：先撤掉 RGB 调节卡，再压缩数值行。
    float rgbCardHeight = rgbHeight;
    float harmonyHeight = contentHeight - valuesHeight - rgbCardHeight - gap * 2.0f;
    if (harmonyHeight < harmonyMinHeight) {
        rgbCardHeight = 0.0f;
        harmonyHeight = contentHeight - valuesHeight - gap;
    }
    if (harmonyHeight < harmonyMinHeight) {
        const float available = contentHeight - gap - harmonyMinHeight;
        const float per = (available - S(44.0f) - S(14.0f) -
                           (kValueRowCount - 1) * rowGapMax) / kValueRowCount;
        rowHeight = std::max(S(20.0f), std::min(S(30.0f), per));
        rowGap = rowHeight >= S(26.0f) ? rowGapMax : S(2.0f);
        valuesHeight = valuesHeightFor(rowHeight, rowGap);
        harmonyHeight = contentHeight - valuesHeight - gap;
    }

    layout.valuesCard = MakeRect(rightX, contentTop, rightWidth, valuesHeight);

    const float rowsTop = contentTop + S(44.0f);
    const float rowX = rightX + valuesPad;
    const float rowWidth = rightWidth - valuesPad * 2.0f;
    for (int i = 0; i < kValueRowCount; ++i) {
        const float rowTop = rowsTop + i * (rowHeight + rowGap);
        layout.valueRows[i] = MakeRect(rowX, rowTop, rowWidth, rowHeight);
        layout.valueFields[i] = MakeRect(rowX + S(kValueFieldOffset), rowTop,
                                         rowWidth - S(kValueFieldOffset + kValueFieldTrailing),
                                         rowHeight);
        layout.valueCopyZones[i] = MakeRect(rowX + rowWidth - S(kValueCopyRight + kValueCopyWidth),
                                            rowTop, S(kValueCopyWidth), rowHeight);
    }

    // --- RGB 调节卡：三条滑条（空间不够时整张卡撤掉） ------------------------
    if (rgbCardHeight > 0.0f) {
        const float rgbTop = contentTop + valuesHeight + gap;
        layout.rgbCard = MakeRect(rightX, rgbTop, rightWidth, rgbCardHeight);

        const float sliderTop = rgbTop + S(44.0f);
        const float sliderX = rightX + valuesPad;
        const float sliderWidth = rightWidth - valuesPad * 2.0f;
        for (int i = 0; i < kRgbSliderCount; ++i) {
            const float rowTop = sliderTop + i * (S(kRgbRowHeight) + S(kRgbRowGap));
            const float trackX = sliderX + S(kRgbLabelWidth + kRgbLabelGap);
            const float trackWidth = sliderWidth - S(kRgbLabelWidth + kRgbLabelGap +
                                                     kRgbValueWidth + kRgbValueGap);
            const float trackY = rowTop + (S(kRgbRowHeight) - S(kRgbTrackHeight)) * 0.5f;
            layout.rgbLabels[i] = MakeRect(sliderX, rowTop, S(kRgbLabelWidth), S(kRgbRowHeight));
            layout.rgbTracks[i] = MakeRect(trackX, trackY, trackWidth, S(kRgbTrackHeight));
            layout.rgbValues[i] = MakeRect(sliderX + sliderWidth - S(kRgbValueWidth), rowTop,
                                           S(kRgbValueWidth), S(kRgbRowHeight));
        }
    }

    // --- Right bottom card: harmony chips, swatches and recent colors -------
    const float harmonyTop =
        contentTop + valuesHeight + (rgbCardHeight > 0.0f ? rgbCardHeight + gap : 0.0f) + gap;
    harmonyHeight = contentHeight - (harmonyTop - contentTop);
    layout.harmonyCard = MakeRect(rightX, harmonyTop, rightWidth, harmonyHeight);

    const float harmonyPad = S(16.0f);
    const float contentX = rightX + harmonyPad;
    const float contentWidth = rightWidth - harmonyPad * 2.0f;

    const float chipsTop = harmonyTop + harmonyPad + sectionHeight + S(10.0f);
    const float chipHeight = S(32.0f);
    const float chipGap = S(8.0f);
    const float chipWidth = std::min(S(76.0f), (contentWidth - chipGap * 5.0f) / 6.0f);
    for (int i = 0; i < 6; ++i) {
        layout.chips[i] = MakeRect(contentX + i * (chipWidth + chipGap), chipsTop, chipWidth, chipHeight);
    }

    const float swatchTop = chipsTop + chipHeight + S(12.0f);
    const float swatchGap = S(10.0f);
    const float swatchWidth = (contentWidth - swatchGap * 4.0f) / kHarmonySwatchCount;
    const float swatchHeight = std::clamp(harmonyHeight - S(160.0f), S(40.0f), S(110.0f));
    for (int i = 0; i < kHarmonySwatchCount; ++i) {
        layout.harmonySwatches[i] =
            MakeRect(contentX + i * (swatchWidth + swatchGap), swatchTop, swatchWidth, swatchHeight);
    }

    // 最近使用：标签放在色块那一行的左边，省下一整行的高度。
    const float recentTop = swatchTop + swatchHeight + S(22.0f);
    const float recentGap = S(8.0f);
    const float recentSize = std::min(S(36.0f), (contentWidth - recentGap * 9.0f) / kRecentCount);
    const float recentX = contentX + S(84.0f);
    for (int i = 0; i < kRecentCount; ++i) {
        layout.recentSwatches[i] =
            MakeRect(recentX + i * (recentSize + recentGap), recentTop, recentSize, recentSize);
    }
    layout.recentLabel = MakeRect(contentX, recentTop, S(76.0f), recentSize);

    const float toastWidth = std::min(S(320.0f), clientWidth - margin * 2.0f);
    const float toastHeight = S(42.0f);
    layout.toast = MakeRect((clientWidth - toastWidth) * 0.5f,
                            clientHeight - margin - toastHeight,
                            toastWidth, toastHeight);

    layout.valid = true;
    return layout;
}

}  // namespace ui
