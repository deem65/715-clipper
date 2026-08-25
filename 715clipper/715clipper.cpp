#include <Windows.h>
#include "715clipper.h"

#include <iostream>
#include <vector>

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
    HDC screenDc = GetDC(nullptr);

    if (screenDc == nullptr) {
        std::cerr << "failed to access screen\n";
        return;
    }

    std::cout << "screen accessed\n";

    HDC memoryDc = CreateCompatibleDC(screenDc);

    if (memoryDc == nullptr) {
        std::cerr << "failed to create memory dc\n";
        ReleaseDC(nullptr, screenDc);
        return;
    }

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    HBITMAP screenBitmap = CreateCompatibleBitmap(
        screenDc,
        screenWidth,
        screenHeight
    );

    if (screenBitmap == nullptr) {
        std::cerr << "failed to create screen bitmap\n";

        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }

    std::cout 
        << "screen size: "
        << screenWidth
        << "x"
        << screenHeight
        << '\n';

    HGDIOBJ previousSelectedObject = SelectObject(memoryDc, screenBitmap); 

    if (previousSelectedObject == nullptr) {
        std::cerr << "failed to select bitmap into memory dc\n";

        DeleteObject(screenBitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }

    if (!BitBlt(memoryDc, 0, 0, screenWidth, screenHeight, screenDc, 0, 0, SRCCOPY))
    {
        std::cerr << "failed to copy screen pixels\n";
    }
    else
    {
        std::cout << "screen captured into memory\n";
    }

    BITMAPINFO bitmapInfo{};

    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = screenWidth;
    bitmapInfo.bmiHeader.biHeight = -screenHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    std::vector<unsigned char> pixelBytes(screenWidth * screenHeight * 4); 

    SelectObject(memoryDc, previousSelectedObject); //GetDIBits() expects the bitmap not to be selected into a dc

    int copiedScanLines = GetDIBits(screenDc, screenBitmap, 0, screenHeight, pixelBytes.data(), &bitmapInfo, DIB_RGB_COLORS); //Win32 C style API which requires pointers

    if (copiedScanLines == screenHeight) {
        std::cout << "extraction succeeded";
    }
    else {
        std::cerr << "extraction failed";
;    }

    DeleteObject(screenBitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
}
