#include "app/app.h"

#include "app/resource.h"
#include "platform/clipboard.h"
#include "platform/dpi.h"
#include "ui/render.h"

#include "color/parse.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <cstring>
#include <cwchar>
#include <windowsx.h>

using namespace Gdiplus;

namespace {

constexpr wchar_t kWindowClass[] = L"PaletteWindowClass";
constexpr UINT_PTR kToastTimerId = 1;
constexpr int kMinWidthLogical = 980;
constexpr int kMinHeightLogical = 750;
constexpr int kDefaultWidthLogical = 1080;
constexpr int kDefaultHeightLogical = 780;

// Identifies the subclass installed on the inline value editor.
constexpr UINT_PTR kEditorSubclassId = 0x50414454;

// Posted to the editor once the click that opened it has been released, so
// that "select all" is not undone by the mouse-up landing in the control.
constexpr UINT kEditorSelectAllMessage = WM_USER + 1;

Color ToGpColor(const color::Rgb& rgb, uint8_t alpha = 255) {
    return Color(alpha, rgb.r, rgb.g, rgb.b);
}

COLORREF ToColorRef(const Color& color) {
    return RGB(color.GetRed(), color.GetGreen(), color.GetBlue());
}

int HueDegrees(double hue) {
    double wrapped = std::fmod(hue, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return static_cast<int>(std::lround(wrapped)) % 360;
}

std::wstring RgbLine(const color::Rgb& rgb) {
    wchar_t buffer[64];
    std::swprintf(buffer, 64, L"RGB %u, %u, %u",
                  static_cast<unsigned int>(rgb.r),
                  static_cast<unsigned int>(rgb.g),
                  static_cast<unsigned int>(rgb.b));
    return buffer;
}

std::wstring HsvLine(const color::Hsv& hsv) {
    wchar_t buffer[64];
    std::swprintf(buffer, 64, L"H %d\u00B0   S %d%%   V %d%%",
                  HueDegrees(hsv.h),
                  static_cast<int>(std::lround(hsv.s * 100.0)),
                  static_cast<int>(std::lround(hsv.v * 100.0)));
    return buffer;
}

// 三个通道各自的标识色：标签、滑条填充、旋钮都用它。
// 不用纯 R/G/B —— 纯红在深色卡片上偏暗，纯绿偏荧光，都不好认。
Color ChannelColor(int index) {
    switch (index) {
        case 0: return Color(255, 0xFF, 0x5A, 0x5A);
        case 1: return Color(255, 0x4C, 0xD9, 0x7A);
        default: return Color(255, 0x5A, 0xA9, 0xFF);
    }
}

const wchar_t* ChannelLabel(int index) {
    static const wchar_t* labels[3] = {L"R", L"G", L"B"};
    return (index >= 0 && index < 3) ? labels[index] : L"";
}

}  // namespace

namespace app {

bool Application::Create(HINSTANCE instance, int showCommand) {
    instance_ = instance;
    dpi_ = platform::GetSystemDpi();
    dpiScale_ = platform::ScaleFactor(dpi_);
    theme_.Rebuild(dpiScale_);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &Application::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // 应用图标来自 exe 资源（app.rc → assets\palette.ico）。
    // 同一个 .ico 里带了 16~256 共 10 个尺寸，LoadIcon 会按系统要的尺寸挑，
    // 所以大图标和小图标都用同一份资源就行。
    windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.hIconSm = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    RECT rect{0, 0,
              static_cast<LONG>(std::lround(kDefaultWidthLogical * dpiScale_)),
              static_cast<LONG>(std::lround(kDefaultHeightLogical * dpiScale_))};
    platform::AdjustWindowRectForDpi(&rect, WS_OVERLAPPEDWINDOW, 0, dpi_);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int x = workArea.left + ((workArea.right - workArea.left) - width) / 2;
    const int y = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;

    hwnd_ = CreateWindowExW(0, kWindowClass, L"Palette \u2014 \u914D\u8272\u5DE5\u4F5C\u53F0",
                            WS_OVERLAPPEDWINDOW, x, y, width, height,
                            nullptr, nullptr, instance_, this);
    if (!hwnd_) {
        return false;
    }

    // 标题栏会自己去取类图标来画，但任务栏按钮和 Alt+Tab 走的是 WM_GETICON，
    // 那是「窗口自己的图标」—— 不显式设一次的话它们拿到的会是 NULL。
    // 用 LoadImage 指定尺寸，.ico 里带了 16~256 共 10 档，LoadImage 会挑最接近的；
    // 尺寸按系统度量取，DPI 放大时不会糊。
    const int bigSize = GetSystemMetrics(SM_CXICON);
    const int smallSize = GetSystemMetrics(SM_CXSMICON);
    const HICON bigIcon = static_cast<HICON>(LoadImageW(
        instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        bigSize > 0 ? bigSize : 32, bigSize > 0 ? bigSize : 32, LR_DEFAULTCOLOR));
    const HICON smallIcon = static_cast<HICON>(LoadImageW(
        instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        smallSize > 0 ? smallSize : 16, smallSize > 0 ? smallSize : 16, LR_DEFAULTCOLOR));
    if (bigIcon) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
    }
    if (smallIcon) {
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }

    // The window may have been created on a monitor whose DPI differs from the
    // system one; re-derive the scale and fix the size accordingly.
    const uint32_t windowDpi = platform::GetWindowDpi(hwnd_);
    if (windowDpi > 0 && windowDpi != dpi_) {
        dpi_ = windowDpi;
        dpiScale_ = platform::ScaleFactor(dpi_);
        theme_.Rebuild(dpiScale_);

        RECT desired{0, 0,
                     static_cast<LONG>(std::lround(kDefaultWidthLogical * dpiScale_)),
                     static_cast<LONG>(std::lround(kDefaultHeightLogical * dpiScale_))};
        platform::AdjustWindowRectForDpi(&desired, WS_OVERLAPPEDWINDOW, 0, dpi_);
        RECT current{};
        GetWindowRect(hwnd_, &current);
        SetWindowPos(hwnd_, nullptr, current.left, current.top,
                     desired.right - desired.left, desired.bottom - desired.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    RebuildLayout();
    ApplyDarkTitleBar();
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
}

int Application::Run() {
    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result == -1) {
        return 1;
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK Application::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self && message != WM_NCCREATE) {
        return self->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK Application::EditSubclassProc(HWND hwnd, UINT message, WPARAM wParam,
                                               LPARAM lParam, UINT_PTR id, DWORD_PTR refData) {
    auto* self = reinterpret_cast<Application*>(refData);
    (void)id;

    if (message == WM_GETDLGCODE) {
        // Take over Enter / Escape / Tab instead of letting the dialog manager
        // of a non-dialog window swallow them.
        return DLGC_WANTALLKEYS;
    }

    if (message == kEditorSelectAllMessage) {
        SendMessageW(hwnd, EM_SETSEL, 0, -1);
        return 0;
    }

    if (message == WM_KEYDOWN && self) {
        if (wParam == VK_RETURN) {
            self->CommitEditor();
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            self->CancelEditor();
            return 0;
        }
        if (wParam == VK_TAB) {
            const bool backwards = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            // The next row has to be computed before the current one is
            // committed, otherwise the row being edited is already gone.
            const int next = self->NextEditableRow(backwards);
            self->CommitEditor();
            self->ShowEditorFor(next);
            return 0;
        }
    }

    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT Application::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            CreateValueEditor();
            return 0;

        case WM_PAINT:
            OnPaint();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_COMMAND:
            if (editor_ && lParam && reinterpret_cast<HWND>(lParam) == editor_) {
                if (HIWORD(wParam) == EN_KILLFOCUS) {
                    CommitEditor();
                    return 0;
                }
            }
            break;

        case WM_CTLCOLOREDIT:
            // The control handle arrives in lParam; wParam carries the HDC.
            if (editor_ && reinterpret_cast<HWND>(lParam) == editor_) {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, ToColorRef(theme_.colors().text));
                SetBkColor(dc, ToColorRef(theme_.colors().row));
                return reinterpret_cast<LRESULT>(editorBrush_);
            }
            break;

        case WM_SIZE:
            RebuildLayout();
            SyncEditorRect();
            Invalidate();
            return 0;

        case WM_DPICHANGED: {
            uint32_t newDpi = LOWORD(wParam);
            if (newDpi == 0) {
                newDpi = platform::GetWindowDpi(hwnd_);
            }
            dpi_ = newDpi;
            dpiScale_ = platform::ScaleFactor(dpi_);
            theme_.Rebuild(dpiScale_);
            RefreshEditorFont();
            RefreshEditorBrush();

            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RebuildLayout();
            SyncEditorRect();
            Invalidate();
            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            RECT rect{0, 0,
                      static_cast<LONG>(std::lround(kMinWidthLogical * dpiScale_)),
                      static_cast<LONG>(std::lround(kMinHeightLogical * dpiScale_))};
            platform::AdjustWindowRectForDpi(&rect, WS_OVERLAPPEDWINDOW, 0, dpi_);
            info->ptMinTrackSize.x = rect.right - rect.left;
            info->ptMinTrackSize.y = rect.bottom - rect.top;
            return 0;
        }

        case WM_MOUSEMOVE:
            OnMouseMove(lParam);
            return 0;

        case WM_MOUSELEAVE:
            mouseTracking_ = false;
            hover_ = Hit{};
            Invalidate();
            return 0;

        case WM_LBUTTONDOWN:
            OnLeftButtonDown(lParam);
            return 0;

        case WM_LBUTTONUP:
            OnLeftButtonUp();
            return 0;

        case WM_CAPTURECHANGED:
            if (drag_ != DragMode::None) {
                // The pointer was released outside the window: the color the
                // user was dragging towards still has to be recorded.
                drag_ = DragMode::None;
                dragRgbIndex_ = -1;
                CommitCurrentColor();
            }
            return 0;

        case WM_MOUSEWHEEL:
            OnMouseWheel(wParam, lParam);
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(hwnd_, &point);
                Hit hit = HitTest(static_cast<float>(point.x), static_cast<float>(point.y));
                LPCWSTR cursor = IDC_ARROW;
                if (hit.kind == HitKind::Sv || hit.kind == HitKind::Hue) {
                    cursor = IDC_CROSS;
                } else if (hit.kind != HitKind::None) {
                    cursor = IDC_HAND;
                }
                SetCursor(LoadCursorW(nullptr, cursor));
                return TRUE;
            }
            return DefWindowProcW(hwnd_, message, wParam, lParam);

        case WM_TIMER:
            if (wParam == kToastTimerId) {
                toastVisible_ = false;
                if (toastTimer_) {
                    KillTimer(hwnd_, toastTimer_);
                    toastTimer_ = 0;
                }
                Invalidate();
            }
            return 0;

        case WM_KEYDOWN:
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && wParam == 'C') {
                CopyValueRow(0);
                return 0;
            }
            break;

        case WM_DESTROY:
            // Hide the editor before the window goes away: otherwise its
            // destruction fires EN_KILLFOCUS and the teardown path tries to
            // commit a value into a dying window.
            if (editor_) {
                editingRow_ = -1;
                ShowWindow(editor_, SW_HIDE);
            }
            if (toastTimer_) {
                KillTimer(hwnd_, toastTimer_);
                toastTimer_ = 0;
            }
            if (editorBrush_) {
                DeleteObject(editorBrush_);
                editorBrush_ = nullptr;
            }
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void Application::RebuildLayout() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    layout_ = ui::BuildLayout(static_cast<float>(client.right),
                              static_cast<float>(client.bottom),
                              dpiScale_);
}

void Application::Invalidate() {
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Application::OnPaint() {
    PAINTSTRUCT paint{};
    HDC device = BeginPaint(hwnd_, &paint);

    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right;
    const int height = client.bottom;

    if (width > 0 && height > 0) {
        HDC memory = CreateCompatibleDC(device);
        HBITMAP bitmap = memory ? CreateCompatibleBitmap(device, width, height) : nullptr;
        if (memory && bitmap) {
            HBITMAP previous = static_cast<HBITMAP>(SelectObject(memory, bitmap));
            {
                Graphics graphics(memory);
                graphics.SetSmoothingMode(SmoothingModeAntiAlias);
                graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
                graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
                Paint(graphics, static_cast<float>(width), static_cast<float>(height));
            }
            BitBlt(device, 0, 0, width, height, memory, 0, 0, SRCCOPY);
            SelectObject(memory, previous);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (memory) {
            DeleteDC(memory);
        }
    }

    EndPaint(hwnd_, &paint);
}

void Application::Paint(Graphics& graphics, float width, float height) {
    Color top = theme_.colors().background;
    Color bottom(0xFF0F1115);
    LinearGradientBrush background(RectF(0.0f, 0.0f, width, height), top, bottom,
                                   LinearGradientModeVertical);
    graphics.FillRectangle(&background, RectF(0.0f, 0.0f, width, height));

    if (!layout_.valid) {
        return;
    }

    DrawHeader(graphics);
    DrawPickerCard(graphics);
    DrawValuesCard(graphics);
    DrawRgbCard(graphics);
    DrawHarmonyCard(graphics);
    DrawToast(graphics);
}

void Application::DrawHeader(Graphics& graphics) {
    const auto& fonts = theme_.fonts();
    const auto& colors = theme_.colors();
    const RectF& header = layout_.header;

    ui::DrawText(graphics, L"Palette",
                 RectF(header.X, header.Y, header.Width, S(30.0f)),
                 *fonts.title, colors.text, StringAlignmentNear);
    ui::DrawText(graphics,
                 L"\u53D6\u8272 \u00B7 \u8C03\u8272 \u00B7 \u914D\u8272 \u2014\u2014 "
                 L"\u62D6\u52A8\u65B9\u5757\u3001\u8272\u76F8\u6761\u6216 RGB \u6ED1\u6761\uFF0C"
                 L"\u70B9\u53F3\u4FA7\u6570\u503C\u4E5F\u80FD\u76F4\u63A5\u6539\u8272",
                 RectF(header.X, header.Y + S(29.0f), header.Width, S(19.0f)),
                 *fonts.subtitle, colors.muted, StringAlignmentNear);
    ui::DrawText(graphics, L"Ctrl+C \u590D\u5236 HEX",
                 header, *fonts.small, colors.faint, StringAlignmentFar);
}

void Application::DrawCard(Graphics& graphics, const RectF& rect) {
    const auto& colors = theme_.colors();
    ui::FillRounded(graphics, RectF(rect.X, rect.Y + S(3.0f), rect.Width, rect.Height),
                    S(14.0f), Color(0x26000000));
    ui::FillRounded(graphics, rect, S(14.0f), colors.card);
    ui::StrokeRounded(graphics, rect, S(14.0f), S(1.0f), colors.cardBorder);
}

void Application::DrawSectionHeader(Graphics& graphics, const RectF& card,
                                    const std::wstring& title, const std::wstring& hint) {
    const float pad = S(16.0f);
    RectF rect(card.X + pad, card.Y + S(14.0f), card.Width - pad * 2.0f, S(20.0f));
    ui::DrawText(graphics, title, rect, *theme_.fonts().section, theme_.colors().text,
                 StringAlignmentNear);
    ui::DrawText(graphics, hint, rect, *theme_.fonts().small, theme_.colors().faint,
                 StringAlignmentFar);
}

void Application::DrawPickerCard(Graphics& graphics) {
    const auto& colors = theme_.colors();
    const RectF& card = layout_.pickerCard;

    DrawCard(graphics, card);
    DrawSectionHeader(graphics, card, L"\u62FE\u8272\u5668", L"\u62D6\u52A8\u53D6\u8272");

    // --- Saturation / value square -----------------------------------------
    const RectF& sv = layout_.svSquare;
    color::Rgb hueRgb = color::RgbFromHsv(color::Hsv{hsv_.h, 1.0, 1.0});
    Color horizontalColors[2] = {Color(255, 255, 255), ToGpColor(hueRgb)};
    REAL horizontalPositions[2] = {0.0f, 1.0f};
    ui::FillGradientRounded(graphics, sv, S(10.0f), horizontalColors, horizontalPositions, 2,
                            LinearGradientModeHorizontal);

    Color verticalColors[2] = {Color(0, 0, 0, 0), Color(255, 0, 0, 0)};
    REAL verticalPositions[2] = {0.0f, 1.0f};
    ui::FillGradientRounded(graphics, sv, S(10.0f), verticalColors, verticalPositions, 2,
                            LinearGradientModeVertical);
    ui::StrokeRounded(graphics, sv, S(10.0f), S(1.0f), colors.cardBorder);

    const float markerX = sv.X + static_cast<float>(hsv_.s) * sv.Width;
    const float markerY = sv.Y + static_cast<float>(1.0 - hsv_.v) * sv.Height;
    const float markerRadius = S(6.0f);
    SolidBrush markerBrush(Color(255, 255, 255));
    Pen markerPen(Color(0xFF20242C), S(1.5f));
    graphics.FillEllipse(&markerBrush, markerX - markerRadius, markerY - markerRadius,
                         markerRadius * 2.0f, markerRadius * 2.0f);
    graphics.DrawEllipse(&markerPen, markerX - markerRadius, markerY - markerRadius,
                         markerRadius * 2.0f, markerRadius * 2.0f);

    // --- Hue strip ----------------------------------------------------------
    const RectF& hueBar = layout_.hueBar;
    Color hueStops[7] = {
        Color(255, 0, 0),     Color(255, 255, 0), Color(0, 255, 0), Color(0, 255, 255),
        Color(0, 0, 255),     Color(255, 0, 255), Color(255, 0, 0),
    };
    REAL huePositions[7] = {0.0f, 1.0f / 6.0f, 2.0f / 6.0f, 3.0f / 6.0f,
                            4.0f / 6.0f, 5.0f / 6.0f, 1.0f};
    ui::FillGradientRounded(graphics, hueBar, S(6.0f), hueStops, huePositions, 7,
                            LinearGradientModeVertical);
    ui::StrokeRounded(graphics, hueBar, S(6.0f), S(1.0f), colors.cardBorder);

    const float hueY = hueBar.Y + static_cast<float>(hsv_.h / 360.0) * hueBar.Height;
    const float hueMarkerHeight = S(7.0f);
    RectF hueMarker(hueBar.X - S(3.0f), hueY - hueMarkerHeight * 0.5f,
                    hueBar.Width + S(6.0f), hueMarkerHeight);
    ui::FillRounded(graphics, hueMarker, hueMarkerHeight * 0.5f,
                    ToGpColor(color::RgbFromHsv(color::Hsv{hsv_.h, 1.0, 1.0})));
    ui::StrokeRounded(graphics, hueMarker, hueMarkerHeight * 0.5f, S(2.0f), Color(255, 255, 255));

    DrawPreview(graphics);
}

void Application::DrawPreview(Graphics& graphics) {
    const auto& colors = theme_.colors();
    const auto& fonts = theme_.fonts();
    const RectF swatch = layout_.previewSwatch;
    const color::Rgb rgb = CurrentRgb();

    ui::FillRounded(graphics, swatch, S(12.0f), ToGpColor(rgb));
    ui::StrokeRounded(graphics, swatch, S(12.0f), S(1.0f), Color(0x33FFFFFF));

    const RectF& card = layout_.pickerCard;
    const float textX = swatch.X + swatch.Width + S(16.0f);
    const float textWidth = card.X + card.Width - S(16.0f) - textX;
    if (textWidth <= 0.0f) {
        return;
    }

    ui::DrawText(graphics, color::ToHex(rgb),
                 RectF(textX, swatch.Y - S(2.0f), textWidth, S(30.0f)),
                 *fonts.hexLarge, colors.text, StringAlignmentNear);
    ui::DrawText(graphics, RgbLine(rgb),
                 RectF(textX, swatch.Y + S(30.0f), textWidth, S(18.0f)),
                 *fonts.body, colors.muted, StringAlignmentNear);
    ui::DrawText(graphics, HsvLine(hsv_),
                 RectF(textX, swatch.Y + S(50.0f), textWidth, S(18.0f)),
                 *fonts.small, colors.faint, StringAlignmentNear);
}

void Application::DrawValuesCard(Graphics& graphics) {
    const auto& colors = theme_.colors();
    const auto& fonts = theme_.fonts();

    DrawCard(graphics, layout_.valuesCard);
    DrawSectionHeader(graphics, layout_.valuesCard, L"\u989C\u8272\u6570\u503C",
                      L"\u70B9\u6570\u503C\u53EF\u7F16\u8F91 \u00B7 \u70B9\u5176\u4F59\u5904\u590D\u5236");

    const auto rows = BuildValueRows();
    for (int i = 0; i < ui::kValueRowCount; ++i) {
        const RectF row = layout_.valueRows[i];
        const bool hovered = (hover_.kind == HitKind::ValueField ||
                              hover_.kind == HitKind::ValueCopy) && hover_.index == i;
        const bool editing = editingRow_ == i;

        // 正在编辑的那一行始终用 row 底色：原生编辑框的背景刷也是 row，
        // 如果这里画成 rowHover，编辑框周围会露出一圈色差。
        ui::FillRounded(graphics, row, S(8.0f), editing ? colors.row
                                          : hovered   ? colors.rowHover
                                                      : colors.row);
        if (hovered && !editing) {
            ui::StrokeRounded(graphics, row, S(8.0f), S(1.0f), Color(0x33FFFFFF));
        }

        ui::DrawText(graphics, rows[i].label,
                     RectF(row.X + S(ui::kValueLabelOffset), row.Y, S(ui::kValueLabelWidth),
                           row.Height),
                     *fonts.small, colors.muted, StringAlignmentNear);

        // The editable field of the row that is currently being edited is
        // covered by the native edit control, so the text is skipped there.
        if (!editing) {
            ui::DrawText(graphics, rows[i].value, layout_.valueFields[i],
                         *fonts.value, colors.text, StringAlignmentNear);
        } else {
            ui::StrokeRounded(graphics, layout_.valueFields[i], S(6.0f), S(1.0f),
                              colors.selection);
        }

        ui::DrawText(graphics, L"\u590D\u5236", layout_.valueCopyZones[i],
                     *fonts.small, hovered ? colors.muted : colors.faint, StringAlignmentFar);
    }
}

void Application::DrawRgbCard(Graphics& graphics) {
    if (layout_.rgbCard.Width <= 0.0f || layout_.rgbCard.Height <= 0.0f) {
        return;
    }

    const auto& colors = theme_.colors();
    const auto& fonts = theme_.fonts();
    const color::Rgb rgb = CurrentRgb();

    DrawCard(graphics, layout_.rgbCard);
    DrawSectionHeader(graphics, layout_.rgbCard, L"RGB \u8C03\u8282",
                      L"\u62D6\u52A8\u6ED1\u6761\u76F4\u63A5\u6539\u8272");

    const int channels[3] = {rgb.r, rgb.g, rgb.b};
    for (int i = 0; i < ui::kRgbSliderCount; ++i) {
        const RectF& track = layout_.rgbTracks[i];
        if (track.Width <= 0.0f) {
            continue;
        }
        const bool active = (drag_ == DragMode::Rgb && dragRgbIndex_ == i) ||
                            (hover_.kind == HitKind::RgbSlider && hover_.index == i);
        const float ratio = static_cast<float>(channels[i]) / 255.0f;
        const Color channelColor = ChannelColor(i);

        // 轨道底
        ui::FillRounded(graphics, track, track.Height * 0.5f, colors.row);

        // 已选中的部分：通道纯色
        if (ratio > 0.0f) {
            const float width = std::max(track.Height, track.Width * ratio);
            ui::FillRounded(graphics, RectF(track.X, track.Y, width, track.Height),
                            track.Height * 0.5f, channelColor);
        }

        // 旋钮：白心 + 深色描边（和拾色器上的取色点一个路子）
        const float knobX = track.X + track.Width * ratio;
        const float knobY = track.Y + track.Height * 0.5f;
        const float knobRadius = S(7.0f);
        SolidBrush knobBrush(Color(255, 255, 255));
        Pen knobPen(Color(0xFF20242C), S(1.5f));
        graphics.FillEllipse(&knobBrush, knobX - knobRadius, knobY - knobRadius,
                             knobRadius * 2.0f, knobRadius * 2.0f);
        graphics.DrawEllipse(&knobPen, knobX - knobRadius, knobY - knobRadius,
                             knobRadius * 2.0f, knobRadius * 2.0f);
        if (active) {
            Pen haloPen(channelColor, S(2.0f));
            graphics.DrawEllipse(&haloPen, knobX - knobRadius - S(1.5f), knobY - knobRadius - S(1.5f),
                                 (knobRadius + S(1.5f)) * 2.0f, (knobRadius + S(1.5f)) * 2.0f);
        }

        // 通道名和数值
        ui::DrawText(graphics, ChannelLabel(i), layout_.rgbLabels[i], *fonts.body, channelColor,
                     StringAlignmentNear);

        wchar_t buffer[8];
        std::swprintf(buffer, 8, L"%u", static_cast<unsigned int>(channels[i]));
        ui::DrawText(graphics, buffer, layout_.rgbValues[i], *fonts.value, colors.text,
                     StringAlignmentFar);
    }
}

void Application::DrawHarmonyCard(Graphics& graphics) {
    const auto& colors = theme_.colors();
    const auto& fonts = theme_.fonts();
    const RectF& card = layout_.harmonyCard;
    const color::Rgb current = CurrentRgb();

    DrawCard(graphics, card);
    DrawSectionHeader(graphics, card, L"\u914D\u8272\u65B9\u6848",
                      L"\u70B9\u51FB\u8272\u5757\u5E94\u7528");

    // --- Harmony type chips -------------------------------------------------
    for (int i = 0; i < color::kHarmonyTypeCount; ++i) {
        const RectF chip = layout_.chips[i];
        const bool selected = static_cast<int>(harmony_) == i;
        const bool hovered = hover_.kind == HitKind::Chip && hover_.index == i;

        Color fill = hovered ? colors.rowHover : colors.row;
        if (selected) {
            fill = Color(0x50, current.r, current.g, current.b);
        }
        ui::FillRounded(graphics, chip, S(8.0f), fill);
        if (selected) {
            ui::StrokeRounded(graphics, chip, S(8.0f), S(1.5f),
                              Color(0xC0, current.r, current.g, current.b));
        } else if (hovered) {
            ui::StrokeRounded(graphics, chip, S(8.0f), S(1.0f), Color(0x33FFFFFF));
        }

        ui::DrawText(graphics,
                     color::HarmonyName(static_cast<color::HarmonyType>(i)),
                     chip, *fonts.chip, selected ? colors.text : colors.muted,
                     StringAlignmentCenter);
    }

    // --- Generated palette --------------------------------------------------
    const auto palette = color::HarmonyPalette(hsv_, harmony_);
    for (int i = 0; i < ui::kHarmonySwatchCount && i < static_cast<int>(palette.size()); ++i) {
        const RectF swatch = layout_.harmonySwatches[i];
        const bool hovered = hover_.kind == HitKind::Swatch && hover_.index == i;

        ui::FillRounded(graphics, swatch, S(10.0f), ToGpColor(palette[i]));
        if (hovered) {
            ui::StrokeRounded(graphics, swatch, S(10.0f), S(2.0f), Color(255, 255, 255));
        } else {
            ui::StrokeRounded(graphics, swatch, S(10.0f), S(1.0f), colors.cardBorder);
        }

        ui::DrawText(graphics, color::ToHex(palette[i]),
                     RectF(swatch.X, swatch.Y + swatch.Height + S(2.0f), swatch.Width, S(16.0f)),
                     *fonts.small, colors.muted, StringAlignmentCenter);
    }

    // --- Recently used colors ----------------------------------------------
    // 标题在色块那一行的左边（不是另起一行），省下的高度给了上面的色块。
    ui::DrawText(graphics, L"\u6700\u8FD1\u4F7F\u7528", layout_.recentLabel, *fonts.small,
                 colors.muted, StringAlignmentNear);
    if (recents_.empty()) {
        const RectF hint(layout_.recentSwatches[0].X,
                         layout_.recentSwatches[0].Y,
                         layout_.recentSwatches[ui::kRecentCount - 1].X +
                             layout_.recentSwatches[ui::kRecentCount - 1].Width -
                             layout_.recentSwatches[0].X,
                         layout_.recentSwatches[0].Height);
        ui::DrawText(graphics, L"\u53D6\u8272\u540E\u81EA\u52A8\u8BB0\u5F55", hint,
                     *fonts.small, colors.faint, StringAlignmentFar);
    }

    const int count = std::min(static_cast<int>(recents_.size()), ui::kRecentCount);
    for (int i = 0; i < count; ++i) {
        const RectF swatch = layout_.recentSwatches[i];
        const bool hovered = hover_.kind == HitKind::Recent && hover_.index == i;

        ui::FillRounded(graphics, swatch, S(8.0f), ToGpColor(recents_[i]));
        if (hovered) {
            ui::StrokeRounded(graphics, swatch, S(8.0f), S(2.0f), Color(255, 255, 255));
        } else {
            ui::StrokeRounded(graphics, swatch, S(8.0f), S(1.0f), colors.cardBorder);
        }
    }
}

void Application::DrawToast(Graphics& graphics) {
    if (!toastVisible_) {
        return;
    }
    const RectF toast = layout_.toast;
    ui::FillRounded(graphics, toast, toast.Height * 0.5f, theme_.colors().toast);
    ui::StrokeRounded(graphics, toast, toast.Height * 0.5f, S(1.0f), Color(0x33FFFFFF));
    ui::DrawText(graphics, toastText_, toast, *theme_.fonts().toast, Color(255, 255, 255),
                 StringAlignmentCenter);
}

Application::Hit Application::HitTest(float x, float y) const {
    Hit hit;
    if (!layout_.valid) {
        return hit;
    }

    if (ui::Contains(layout_.svSquare, x, y)) {
        hit.kind = HitKind::Sv;
        return hit;
    }
    if (ui::Contains(layout_.hueBar, x, y)) {
        hit.kind = HitKind::Hue;
        return hit;
    }

    for (int i = 0; i < ui::kRgbSliderCount; ++i) {
        if (RgbTrackHit(i, x, y)) {
            hit.kind = HitKind::RgbSlider;
            hit.index = i;
            return hit;
        }
    }

    for (int i = 0; i < ui::kValueRowCount; ++i) {
        if (ui::Contains(layout_.valueFields[i], x, y)) {
            hit.kind = HitKind::ValueField;
            hit.index = i;
            return hit;
        }
        if (ui::Contains(layout_.valueRows[i], x, y)) {
            hit.kind = HitKind::ValueCopy;
            hit.index = i;
            return hit;
        }
    }

    for (int i = 0; i < color::kHarmonyTypeCount; ++i) {
        if (ui::Contains(layout_.chips[i], x, y)) {
            hit.kind = HitKind::Chip;
            hit.index = i;
            return hit;
        }
    }

    for (int i = 0; i < ui::kHarmonySwatchCount; ++i) {
        if (ui::Contains(layout_.harmonySwatches[i], x, y)) {
            hit.kind = HitKind::Swatch;
            hit.index = i;
            return hit;
        }
    }

    const int recentCount = std::min(static_cast<int>(recents_.size()), ui::kRecentCount);
    for (int i = 0; i < recentCount; ++i) {
        if (ui::Contains(layout_.recentSwatches[i], x, y)) {
            hit.kind = HitKind::Recent;
            hit.index = i;
            return hit;
        }
    }

    return hit;
}

bool Application::RgbTrackHit(int index, float x, float y) const {
    if (index < 0 || index >= ui::kRgbSliderCount) {
        return false;
    }
    const RectF& track = layout_.rgbTracks[index];
    if (track.Width <= 0.0f) {
        return false;
    }
    // 轨道只有 6px 高，上下各放宽一点才好按。
    const float tolerance = S(7.0f);
    return x >= track.X && x <= track.X + track.Width &&
           y >= track.Y - tolerance && y <= track.Y + track.Height + tolerance;
}

void Application::OnMouseMove(LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

    if (drag_ == DragMode::Rgb) {
        UpdateColorFromRgbPoint(dragRgbIndex_, point);
    } else if (drag_ != DragMode::None) {
        UpdateColorFromPoint(drag_, point);
    }

    Hit hit = HitTest(static_cast<float>(point.x), static_cast<float>(point.y));
    const bool changed = hit.kind != hover_.kind || hit.index != hover_.index;
    hover_ = hit;
    if (changed) {
        Invalidate();
    }

    if (!mouseTracking_) {
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd_;
        TrackMouseEvent(&track);
        mouseTracking_ = true;
    }
}

void Application::OnLeftButtonDown(LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

    // Clicking outside the field that is being edited commits it: the parent
    // window swallows WM_LBUTTONDOWN, so the focus is never moved away from
    // the edit control by the system.
    if (editingRow_ >= 0 && editingRow_ < ui::kValueRowCount &&
        !ui::Contains(layout_.valueFields[editingRow_], static_cast<float>(point.x),
                      static_cast<float>(point.y))) {
        CommitEditor();
    }

    Hit hit = HitTest(static_cast<float>(point.x), static_cast<float>(point.y));

    switch (hit.kind) {
        case HitKind::Sv:
            drag_ = DragMode::Sv;
            SetCapture(hwnd_);
            UpdateColorFromPoint(drag_, point);
            break;

        case HitKind::Hue:
            drag_ = DragMode::Hue;
            SetCapture(hwnd_);
            UpdateColorFromPoint(drag_, point);
            break;

        case HitKind::RgbSlider:
            if (hit.index >= 0 && hit.index < ui::kRgbSliderCount) {
                drag_ = DragMode::Rgb;
                dragRgbIndex_ = hit.index;
                SetCapture(hwnd_);
                UpdateColorFromRgbPoint(dragRgbIndex_, point);
            }
            break;

        case HitKind::ValueField:
            ShowEditorFor(hit.index);
            break;

        case HitKind::ValueCopy:
            CopyValueRow(hit.index);
            break;

        case HitKind::Chip:
            if (hit.index >= 0 && hit.index < color::kHarmonyTypeCount) {
                harmony_ = static_cast<color::HarmonyType>(hit.index);
                Invalidate();
            }
            break;

        case HitKind::Swatch: {
            auto palette = color::HarmonyPalette(hsv_, harmony_);
            if (hit.index >= 0 && hit.index < static_cast<int>(palette.size())) {
                ApplyRgb(palette[hit.index]);
            }
            break;
        }

        case HitKind::Recent:
            if (hit.index >= 0 && hit.index < static_cast<int>(recents_.size())) {
                ApplyRgb(recents_[hit.index]);
            }
            break;

        default:
            break;
    }
}

void Application::OnLeftButtonUp() {
    if (drag_ == DragMode::None) {
        return;
    }
    drag_ = DragMode::None;
    dragRgbIndex_ = -1;
    if (GetCapture() == hwnd_) {
        ReleaseCapture();
    }
    CommitCurrentColor();
}

void Application::OnMouseWheel(WPARAM wParam, LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(hwnd_, &point);

    // 滚轮不会产生点击，编辑框会留着旧数值；先提交掉，免得回车时把刚才的
    // 调整又覆盖回去。
    if (editingRow_ >= 0) {
        CommitEditor();
    }

    const int delta = GET_WHEEL_DELTA_WPARAM(wParam);

    // 落在 RGB 滑条上：滚轮调那个通道的数值（按住 Ctrl 步长 10）。
    for (int i = 0; i < ui::kRgbSliderCount; ++i) {
        if (!RgbTrackHit(i, static_cast<float>(point.x), static_cast<float>(point.y))) {
            continue;
        }
        const color::Rgb rgb = CurrentRgb();
        const int current = (i == 0) ? rgb.r : (i == 1 ? rgb.g : rgb.b);
        const int step = (GetKeyState(VK_CONTROL) & 0x8000) ? 10 : 1;
        SetRgbChannel(i, current + (delta / WHEEL_DELTA) * step);
        CommitCurrentColor();
        return;
    }

    if (!ui::Contains(layout_.pickerCard, static_cast<float>(point.x), static_cast<float>(point.y))) {
        return;
    }

    hsv_.h += static_cast<double>(delta) / WHEEL_DELTA * 3.0;
    hsv_ = color::NormalizeHsv(hsv_);
    Invalidate();
}

void Application::UpdateColorFromPoint(DragMode mode, POINT point) {
    if (mode == DragMode::Sv) {
        const RectF& sv = layout_.svSquare;
        if (sv.Width <= 0.0f || sv.Height <= 0.0f) {
            return;
        }
        hsv_.s = std::clamp((static_cast<double>(point.x) - sv.X) / sv.Width, 0.0, 1.0);
        hsv_.v = std::clamp(1.0 - (static_cast<double>(point.y) - sv.Y) / sv.Height, 0.0, 1.0);
        hsv_ = color::NormalizeHsv(hsv_);
    } else if (mode == DragMode::Hue) {
        const RectF& hue = layout_.hueBar;
        if (hue.Height <= 0.0f) {
            return;
        }
        double ratio = std::clamp((static_cast<double>(point.y) - hue.Y) / hue.Height, 0.0, 1.0);
        hsv_.h = std::min(359.999, ratio * 360.0);
    }

    Invalidate();
}

void Application::UpdateColorFromRgbPoint(int index, POINT point) {
    if (index < 0 || index >= ui::kRgbSliderCount) {
        return;
    }
    const RectF& track = layout_.rgbTracks[index];
    if (track.Width <= 0.0f) {
        return;
    }
    const double ratio = std::clamp((static_cast<double>(point.x) - track.X) / track.Width, 0.0, 1.0);
    SetRgbChannel(index, static_cast<int>(std::lround(ratio * 255.0)));
}

void Application::SetRgbChannel(int index, int value) {
    if (index < 0 || index >= ui::kRgbSliderCount) {
        return;
    }
    value = std::clamp(value, 0, 255);

    color::Rgb rgb = CurrentRgb();
    switch (index) {
        case 0: rgb.r = static_cast<uint8_t>(value); break;
        case 1: rgb.g = static_cast<uint8_t>(value); break;
        default: rgb.b = static_cast<uint8_t>(value); break;
    }

    // 拖动过程中不记「最近使用」，等松手时统一记一次。
    ApplyRgb(rgb, false);
}

color::Rgb Application::CurrentRgb() const {
    return color::RgbFromHsv(hsv_);
}

std::array<Application::ValueRow, ui::kValueRowCount> Application::BuildValueRows() const {
    std::array<ValueRow, ui::kValueRowCount> rows;
    const color::Rgb rgb = CurrentRgb();

    rows[0] = {L"HEX", color::ToHex(rgb)};
    rows[1] = {L"RGB", color::ToCssRgb(rgb)};
    rows[2] = {L"HSV", color::ToCssHsv(hsv_)};
    rows[3] = {L"HSL", color::ToCssHsl(rgb)};
    rows[4] = {L"CMYK", color::ToCssCmyk(rgb)};
    return rows;
}

void Application::ApplyRgb(const color::Rgb& rgb, bool record) {
    color::Hsv hsv = color::HsvFromRgb(rgb);
    if (rgb.r == rgb.g && rgb.g == rgb.b) {
        // A gray has no hue of its own; keeping the current one stops the hue
        // strip from jumping back to red.
        hsv.h = hsv_.h;
    }
    hsv_ = hsv;
    if (record) {
        AddRecent(rgb);
    }
    Invalidate();
}

void Application::ApplyHsv(const color::Hsv& hsv) {
    hsv_ = color::NormalizeHsv(hsv);
    CommitCurrentColor();
    Invalidate();
}

void Application::CommitCurrentColor() {
    AddRecent(CurrentRgb());
}

void Application::AddRecent(const color::Rgb& rgb) {
    auto existing = std::find_if(recents_.begin(), recents_.end(), [&rgb](const color::Rgb& item) {
        return item.r == rgb.r && item.g == rgb.g && item.b == rgb.b;
    });
    if (existing != recents_.end()) {
        recents_.erase(existing);
    }

    recents_.insert(recents_.begin(), rgb);
    if (static_cast<int>(recents_.size()) > ui::kRecentCount) {
        recents_.resize(ui::kRecentCount);
    }
    Invalidate();
}

void Application::CopyValueRow(int index) {
    if (index < 0 || index >= ui::kValueRowCount) {
        return;
    }
    const auto rows = BuildValueRows();
    if (platform::SetClipboardText(hwnd_, rows[index].value)) {
        ShowToast(L"\u5DF2\u590D\u5236 " + rows[index].label + L"\uFF1A" + rows[index].value);
    } else {
        ShowToast(L"\u590D\u5236\u5931\u8D25");
    }
}

void Application::ShowToast(const std::wstring& text) {
    toastText_ = text;
    toastVisible_ = true;
    if (toastTimer_) {
        KillTimer(hwnd_, toastTimer_);
    }
    toastTimer_ = SetTimer(hwnd_, kToastTimerId, 1600, nullptr);
    Invalidate();
}

void Application::ApplyDarkTitleBar() {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) {
        return;
    }

    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    FARPROC proc = GetProcAddress(dwm, "DwmSetWindowAttribute");
    DwmSetWindowAttributeFn setAttribute = nullptr;
    static_assert(sizeof(setAttribute) == sizeof(proc), "unexpected function pointer size");
    if (proc) {
        std::memcpy(&setAttribute, &proc, sizeof(setAttribute));
    }
    if (setAttribute) {
        BOOL dark = TRUE;
        // 20 on Windows 10 20H1+, 19 on older builds.
        if (FAILED(setAttribute(hwnd_, 20, &dark, sizeof(dark)))) {
            setAttribute(hwnd_, 19, &dark, sizeof(dark));
        }
    }
    FreeLibrary(dwm);
}

// --- inline editing of the numeric values -----------------------------------

void Application::CreateValueEditor() {
    if (editor_) {
        return;
    }

    editor_ = CreateWindowExW(0, L"EDIT", L"",
                              WS_CHILD | ES_AUTOHSCROLL | ES_LEFT | ES_NOHIDESEL,
                              0, 0, 10, 10, hwnd_, nullptr, instance_, nullptr);
    if (!editor_) {
        return;
    }

    SetWindowSubclass(editor_, &Application::EditSubclassProc, kEditorSubclassId,
                      reinterpret_cast<DWORD_PTR>(this));
    RefreshEditorFont();
    RefreshEditorBrush();
}

void Application::RefreshEditorFont() {
    if (!editor_) {
        return;
    }
    if (HFONT font = theme_.valueFont()) {
        SendMessageW(editor_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void Application::RefreshEditorBrush() {
    if (editorBrush_) {
        DeleteObject(editorBrush_);
        editorBrush_ = nullptr;
    }
    editorBrush_ = CreateSolidBrush(ToColorRef(theme_.colors().row));
}

void Application::ShowEditorFor(int row) {
    if (row < 0 || row >= ui::kValueRowCount) {
        return;
    }
    if (!editor_) {
        CreateValueEditor();
    }
    if (!editor_) {
        return;
    }

    if (editingRow_ >= 0 && editingRow_ != row) {
        CommitEditor();
    }

    editingRow_ = row;
    const auto rows = BuildValueRows();
    SetWindowTextW(editor_, rows[row].value.c_str());
    SyncEditorRect();
    ShowWindow(editor_, SW_SHOWNA);
    SetFocus(editor_);
    SendMessageW(editor_, EM_SETSEL, 0, -1);
    // The mouse-up of the click that opened the editor may land inside the
    // control and clear the selection, so select everything once more.
    PostMessageW(editor_, kEditorSelectAllMessage, 0, 0);
    Invalidate();
}

void Application::CommitEditor() {
    if (!editor_ || editingRow_ < 0) {
        return;
    }

    const int row = editingRow_;
    wchar_t buffer[128] = {0};
    GetWindowTextW(editor_, buffer, 128);
    const std::wstring text = buffer;

    editingRow_ = -1;
    ShowWindow(editor_, SW_HIDE);
    SetFocus(hwnd_);

    if (!text.empty()) {
        ApplyEditedValue(row, text);
    }
    Invalidate();
}

void Application::CancelEditor() {
    if (!editor_ || editingRow_ < 0) {
        return;
    }
    editingRow_ = -1;
    ShowWindow(editor_, SW_HIDE);
    SetFocus(hwnd_);
    Invalidate();
}

void Application::SyncEditorRect() {
    if (!editor_ || editingRow_ < 0 || editingRow_ >= ui::kValueRowCount) {
        return;
    }
    if (!layout_.valid) {
        return;
    }

    const RectF& field = layout_.valueFields[editingRow_];
    if (field.Width <= 0.0f || field.Height <= 0.0f) {
        return;
    }
    SetWindowPos(editor_, nullptr, static_cast<int>(std::lround(field.X)),
                 static_cast<int>(std::lround(field.Y)),
                 static_cast<int>(std::lround(field.Width)),
                 static_cast<int>(std::lround(field.Height)),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

int Application::NextEditableRow(bool backwards) const {
    if (editingRow_ < 0) {
        return 0;
    }
    const int count = ui::kValueRowCount;
    int next = editingRow_ + (backwards ? -1 : 1);
    if (next < 0) {
        next = count - 1;
    }
    if (next >= count) {
        next = 0;
    }
    return next;
}

bool Application::ApplyEditedValue(int row, const std::wstring& text) {
    color::Rgb rgb{};

    switch (row) {
        case 0:
            if (!color::ParseHex(text, &rgb)) {
                break;
            }
            ApplyRgb(rgb);
            return true;

        case 1:
            if (!color::ParseRgb(text, &rgb)) {
                break;
            }
            ApplyRgb(rgb);
            return true;

        case 2: {
            color::Hsv hsv{};
            if (!color::ParseHsv(text, &hsv)) {
                break;
            }
            ApplyHsv(hsv);
            return true;
        }

        case 3: {
            color::Hsl hsl{};
            if (!color::ParseHsl(text, &hsl)) {
                break;
            }
            ApplyRgb(color::RgbFromHsl(hsl));
            return true;
        }

        case 4: {
            color::Cmyk cmyk{};
            if (!color::ParseCmyk(text, &cmyk)) {
                break;
            }
            ApplyRgb(color::RgbFromCmyk(cmyk));
            return true;
        }

        default:
            break;
    }

    ShowToast(L"\u6570\u503C\u683C\u5F0F\u65E0\u6CD5\u8BC6\u522B");
    return false;
}

}  // namespace app
