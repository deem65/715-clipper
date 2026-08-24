#include <Windows.h>
#include <iostream>
#include "715clipper.h"

int main()
{
    constexpr int ClipHotkeyId = 1;

    if (!RegisterHotKey(nullptr, ClipHotkeyId, MOD_CONTROL | MOD_SHIFT, VK_F7)) {
        std::cerr << "failed to register hotkey\n";
        return 1;
    }

    std::cout << "running\n";
    MSG message{};

    while (GetMessage(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_HOTKEY && message.wParam == ClipHotkeyId) {
            OnClipHotkeyPressed();
        }
    }

    UnregisterHotKey(nullptr, ClipHotkeyId);

    return 0;
}

void OnClipHotkeyPressed()
{
    HDC ScreenDC = GetDC(nullptr);

    if (ScreenDC == nullptr) {
        std::cerr << "failed to access screen\n";
        return;
    }

    std::cout << "screen accessed\n";

    ReleaseDC(nullptr, ScreenDC);
}
