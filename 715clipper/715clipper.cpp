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
        std::cerr << "cliphotkey registry: fail\n";
        return 1;
    }
    std::cout << "cliphotkey registry: success\n";

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
        std::cerr << "screen dc: fail\n";
        return;
    }

    std::cout << "screen dc: success\n";

    HDC memoryDc = CreateCompatibleDC(screenDc);

    if (memoryDc == nullptr) {
        std::cerr << "memory dc: fail\n";
        ReleaseDC(nullptr, screenDc);
        return;
    }

    std::cout << "memory dc: success\n";

    //temporarily gets screen dimensions of primary screen
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    HBITMAP screenBitmap = CreateCompatibleBitmap(
        screenDc,
        screenWidth,
        screenHeight
    );

    if (screenBitmap == nullptr) {
        std::cerr << "screen bitmap: fail\n";

        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }

    std::cout << "screen bitmap: success\n";

    HGDIOBJ previousSelectedObject = SelectObject(memoryDc, screenBitmap); 

    if (previousSelectedObject == nullptr) {
        std::cerr << "bitmap -> memory dc: fail\n";

        DeleteObject(screenBitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }

    std::cout << "bitmap -> memory dc: success\n";

    if (!BitBlt(memoryDc, 0, 0, screenWidth, screenHeight, screenDc, 0, 0, SRCCOPY))
    {
        std::cerr << "bit block transfer: faii\n";

        SelectObject(memoryDc, previousSelectedObject);
        DeleteObject(screenBitmap);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }

    std::cout << "bit block transfer: success\n";

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
        std::cout << "pixel extraction: success\n";
        SaveBitmap(bitmapInfo.bmiHeader, pixelBytes);
    }
    else {
        std::cerr << "pixel extraction: fail\n";
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
        std::cerr << "bitmap file: fail\n";
        return;
    }

    std::cout << "bitmap file: success\n";

    BITMAPFILEHEADER fileHeader{};

    fileHeader.bfType = 0x4D42; //win bitmap
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + static_cast<DWORD>(pixelBytes.size());

    const char* fileHeaderBytePtr = reinterpret_cast<const char*>(&fileHeader);
    const char* bitmapHeaderBytePtr = reinterpret_cast<const char*>(&bitmapHeader);
    const char* pixelBytesPtr = reinterpret_cast<const char*>(pixelBytes.data());

    std::streamsize pixelBytesStreamSize = static_cast<std::streamsize>(pixelBytes.size());

    file.write(fileHeaderBytePtr, sizeof(fileHeader));
    file.write(bitmapHeaderBytePtr, sizeof(bitmapHeader));
    file.write(pixelBytesPtr, pixelBytesStreamSize);

    if (!file) {
        std::cerr << "bitmap write: fail\n";
        return;
    }

    std::cout << "bitmap write: success\n";
}