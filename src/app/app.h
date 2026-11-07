#pragma once

#include "ui/gdiplus_include.h"

#include <array>
#include <string>
#include <vector>

#include "color/color.h"
#include "color/harmony.h"
#include "ui/layout.h"
#include "ui/theme.h"

namespace app {

class Application {
public:
    Application() = default;

    bool Create(HINSTANCE instance, int showCommand);
    int Run();

private:
    enum class DragMode { None, Sv, Hue, Rgb };

    enum class HitKind {
        None,
        Sv,
        Hue,
        RgbSlider,   // 右侧 RGB 调节卡里的滑条
        ValueField,   // the editable value area
        ValueCopy,    // anywhere else on the row -> copies that row
        Chip,
        Swatch,
        Recent
    };

    struct Hit {
        HitKind kind = HitKind::None;
        int index = -1;
    };

    struct ValueRow {
        std::wstring label;
        std::wstring value;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR id, DWORD_PTR refData);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void RebuildLayout();
    void Invalidate();

    void OnPaint();
    void Paint(Gdiplus::Graphics& graphics, float width, float height);
    void DrawHeader(Gdiplus::Graphics& graphics);
    void DrawPickerCard(Gdiplus::Graphics& graphics);
    void DrawPreview(Gdiplus::Graphics& graphics);
    void DrawValuesCard(Gdiplus::Graphics& graphics);
    void DrawRgbCard(Gdiplus::Graphics& graphics);
    void DrawHarmonyCard(Gdiplus::Graphics& graphics);
    void DrawToast(Gdiplus::Graphics& graphics);
    void DrawCard(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect);
    void DrawSectionHeader(Gdiplus::Graphics& graphics, const Gdiplus::RectF& card,
                           const std::wstring& title, const std::wstring& hint);

    Hit HitTest(float x, float y) const;
    void OnMouseMove(LPARAM lParam);
    void OnLeftButtonDown(LPARAM lParam);
    void OnLeftButtonUp();
    void OnMouseWheel(WPARAM wParam, LPARAM lParam);
    void UpdateColorFromPoint(DragMode mode, POINT point);
    void UpdateColorFromRgbPoint(int index, POINT point);
    void SetRgbChannel(int index, int value);
    bool RgbTrackHit(int index, float x, float y) const;
    void ApplyRgb(const color::Rgb& rgb, bool record = true);
    void ApplyHsv(const color::Hsv& hsv);
    void CommitCurrentColor();
    void AddRecent(const color::Rgb& rgb);
    void CopyValueRow(int index);
    void ShowToast(const std::wstring& text);
    void ApplyDarkTitleBar();

    // --- inline editing of the numeric values ------------------------------
    void CreateValueEditor();
    void ShowEditorFor(int row);
    void CommitEditor();
    void CancelEditor();
    void SyncEditorRect();
    void RefreshEditorFont();
    void RefreshEditorBrush();
    bool ApplyEditedValue(int row, const std::wstring& text);
    int NextEditableRow(bool backwards) const;

    color::Rgb CurrentRgb() const;
    std::array<ValueRow, ui::kValueRowCount> BuildValueRows() const;

    // Converts a logical (96 DPI) measurement to physical pixels.
    float S(float value) const { return value * dpiScale_; }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;

    uint32_t dpi_ = 96;
    float dpiScale_ = 1.0f;

    ui::Theme theme_;
    ui::Layout layout_;

    // HSV is the canonical editing state: it keeps the hue alive even when the
    // selected color becomes black, white or gray.
    color::Hsv hsv_{212.0, 0.73, 0.84};
    color::HarmonyType harmony_ = color::HarmonyType::Complementary;

    std::vector<color::Rgb> recents_;

    DragMode drag_ = DragMode::None;
    int dragRgbIndex_ = -1;
    Hit hover_;
    bool mouseTracking_ = false;

    bool toastVisible_ = false;
    std::wstring toastText_;
    UINT_PTR toastTimer_ = 0;

    // Single reusable edit control that overlays the row being edited.
    HWND editor_ = nullptr;
    int editingRow_ = -1;
    HBRUSH editorBrush_ = nullptr;
};

}  // namespace app
