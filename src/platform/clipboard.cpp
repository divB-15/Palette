#include "platform/clipboard.h"

#include <cstring>

namespace platform {

bool SetClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) {
        return false;
    }

    bool success = false;
    if (EmptyClipboard()) {
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory) {
            void* target = GlobalLock(memory);
            if (target) {
                std::memcpy(target, text.c_str(), bytes);
                GlobalUnlock(memory);
                if (SetClipboardData(CF_UNICODETEXT, memory)) {
                    success = true;
                }
            }
            if (!success) {
                GlobalFree(memory);
            }
        }
    }

    CloseClipboard();
    return success;
}

}  // namespace platform
