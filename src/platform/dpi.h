#pragma once

#include <windows.h>
#include <cstdint>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef WM_DPICHANGED_BEFOREPARENT
#define WM_DPICHANGED_BEFOREPARENT 0x02E2
#endif

namespace platform {

// Must be called before creating any window.
void EnableDpiAwareness();

uint32_t GetSystemDpi();

// Per-window DPI with a safe fallback for older Windows versions.
uint32_t GetWindowDpi(HWND hwnd);

float ScaleFactor(uint32_t dpi);

// DPI-aware AdjustWindowRectEx, falling back to the legacy API.
bool AdjustWindowRectForDpi(RECT* rect, DWORD style, DWORD exStyle, uint32_t dpi);

}  // namespace platform
