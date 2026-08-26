#include <Windows.h>
#include "715clipper.h"

#include <iostream>
#include <climits>
#include <vector>
#include <fstream>

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
    constexpr int bitsPerPixel = 32;

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

    //temporarily gets screen dimensions of primary screen
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
    bitmapInfo.bmiHeader.biBitCount = bitsPerPixel;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    std::vector<unsigned char> pixelBytes(screenWidth * screenHeight * bitsPerPixel / CHAR_BIT); 

    SelectObject(memoryDc, previousSelectedObject); //GetDIBits expects the bitmap to be unselected in a dc

    int copiedScanLines = GetDIBits(screenDc, screenBitmap, 0, screenHeight, pixelBytes.data(), &bitmapInfo, DIB_RGB_COLORS); 

    if (copiedScanLines == screenHeight) {
        std::cout << "extraction succeeded\n";
        SaveBitmap(bitmapInfo.bmiHeader, pixelBytes);
    }
    else {
        std::cerr << "extraction failed\n";
        DeleteObject(screenBitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return;
        }

    DeleteObject(screenBitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
}
void SaveBitmap(const BITMAPINFOHEADER& bitmapHeader, const std::vector<unsigned char>& pixelBytes) { 
    std::ofstream file("capture.bmp", std::ios::binary);

    if (!file) {
        std::cerr << "failed to create bitmap file\n";
        return;
    }

    BITMAPFILEHEADER fileHeader{};

    fileHeader.bfType = 0x4D42; //win bitmap
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + static_cast<DWORD>(pixelBytes.size());
}