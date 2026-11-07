#pragma once

#include <windows.h>
#include <string>

namespace platform {

bool SetClipboardText(HWND owner, const std::wstring& text);

}  // namespace platform
