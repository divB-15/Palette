#include "ui/gdiplus_include.h"

#include "app/app.h"
#include "platform/dpi.h"

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    // Per-monitor V2 awareness must be enabled before any window is created.
    platform::EnableDpiAwareness();

    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"GDI+ \u521D\u59CB\u5316\u5931\u8D25\u3002",
                    L"Palette", MB_ICONERROR | MB_OK);
        return 1;
    }

    int exitCode = 0;
    {
        // Keep the application (and its fonts) alive only while GDI+ is up.
        app::Application application;
        if (application.Create(instance, showCommand)) {
            exitCode = application.Run();
        } else {
            MessageBoxW(nullptr, L"\u65E0\u6CD5\u521B\u5EFA\u7A0B\u5E8F\u7A97\u53E3\u3002",
                        L"Palette", MB_ICONERROR | MB_OK);
            exitCode = 1;
        }
    }

    Gdiplus::GdiplusShutdown(token);
    return exitCode;
}
