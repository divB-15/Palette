#include "platform/dpi.h"

#include <cstdint>
#include <cstring>

namespace {

using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(void*);
using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(int);
using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
using GetDpiForSystemFn = UINT(WINAPI*)();
using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, DWORD, UINT, UINT);

// GetProcAddress returns a generic function pointer; going through memcpy
// keeps the compiler from warning about the incompatible cast.
template <typename Fn>
Fn Resolve(HMODULE module, const char* name) {
    FARPROC proc = module ? GetProcAddress(module, name) : nullptr;
    Fn fn = nullptr;
    static_assert(sizeof(fn) == sizeof(proc), "unexpected function pointer size");
    if (proc) {
        std::memcpy(&fn, &proc, sizeof(fn));
    }
    return fn;
}

struct Api {
    SetProcessDpiAwarenessContextFn setContext = nullptr;
    GetDpiForWindowFn getDpiForWindow = nullptr;
    GetDpiForSystemFn getDpiForSystem = nullptr;
    AdjustWindowRectExForDpiFn adjustWindowRectExForDpi = nullptr;

    Api() {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            setContext = Resolve<SetProcessDpiAwarenessContextFn>(
                user32, "SetProcessDpiAwarenessContext");
            getDpiForWindow = Resolve<GetDpiForWindowFn>(user32, "GetDpiForWindow");
            getDpiForSystem = Resolve<GetDpiForSystemFn>(user32, "GetDpiForSystem");
            adjustWindowRectExForDpi = Resolve<AdjustWindowRectExForDpiFn>(
                user32, "AdjustWindowRectExForDpi");
        }
    }
};

Api& ApiInstance() {
    static Api api;
    return api;
}

uint32_t DpiFromDc(HDC dc) {
    if (!dc) {
        return 96;
    }
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    return dpi > 0 ? static_cast<uint32_t>(dpi) : 96;
}

}  // namespace

namespace platform {

void EnableDpiAwareness() {
    Api& api = ApiInstance();

    if (api.setContext) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (void*)-4
        void* context = reinterpret_cast<void*>(static_cast<std::intptr_t>(-4));
        if (api.setContext(context)) {
            return;
        }
    }

    // PROCESS_PER_MONITOR_DPI_AWARE == 2
    HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    if (shcore) {
        auto setAwareness = Resolve<SetProcessDpiAwarenessFn>(shcore, "SetProcessDpiAwareness");
        if (setAwareness) {
            if (SUCCEEDED(setAwareness(2))) {
                FreeLibrary(shcore);
                return;
            }
        }
        FreeLibrary(shcore);
    }

    SetProcessDPIAware();
}

uint32_t GetSystemDpi() {
    Api& api = ApiInstance();
    if (api.getDpiForSystem) {
        UINT dpi = api.getDpiForSystem();
        if (dpi > 0) {
            return dpi;
        }
    }

    HDC dc = GetDC(nullptr);
    uint32_t dpi = DpiFromDc(dc);
    if (dc) {
        ReleaseDC(nullptr, dc);
    }
    return dpi;
}

uint32_t GetWindowDpi(HWND hwnd) {
    Api& api = ApiInstance();
    if (api.getDpiForWindow && hwnd) {
        UINT dpi = api.getDpiForWindow(hwnd);
        if (dpi > 0) {
            return dpi;
        }
    }

    HDC dc = hwnd ? GetDC(hwnd) : GetDC(nullptr);
    uint32_t dpi = DpiFromDc(dc);
    if (dc) {
        ReleaseDC(hwnd, dc);
    }
    return dpi;
}

float ScaleFactor(uint32_t dpi) {
    return static_cast<float>(dpi) / 96.0f;
}

bool AdjustWindowRectForDpi(RECT* rect, DWORD style, DWORD exStyle, uint32_t dpi) {
    Api& api = ApiInstance();
    if (api.adjustWindowRectExForDpi && rect) {
        return api.adjustWindowRectExForDpi(rect, style, FALSE, exStyle, dpi) != FALSE;
    }
    return AdjustWindowRectEx(rect, style, FALSE, exStyle) != FALSE;
}

}  // namespace platform
