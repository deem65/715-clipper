#include <Windows.h>
#include "715clipper.h"

#include <iostream>
#include <climits>
#include <vector>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <optional>
#include <utility>

std::atomic<bool> captureInProgress{ false };

int main()
{
    constexpr int clipHotkeyId = 1;

    HWND window = nullptr; //hardcoded for now

    if (!RegisterHotKey(nullptr, clipHotkeyId, MOD_CONTROL | MOD_SHIFT, VK_F7)) {
        std::cerr << "cliphotkey registry: fail\n";
        return 1;
    }

    std::cout << "running\n";

    MSG message{};

    while (GetMessage(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_HOTKEY && message.wParam == clipHotkeyId) {
            on_clip(window);
        }
    }

    UnregisterHotKey(nullptr, clipHotkeyId);

    return 0;
}
void on_clip(HWND window) {
    bool prev = captureInProgress.exchange(true); 

    if (prev) {
        std::cerr << "capture already in progress\n";
        return;
    }

    std::thread capture([window]() {
        capture_multi_frames(window);
        captureInProgress.store(false);
        });

    capture.detach();
}
bool initialize_frame_context(FrameContext& context, HWND window) {
    context.window = window;

    if (!get_window_dc(context.windowDc, context.window)) {
        return false;
    }

    if (!get_memory_dc(context.memoryDc, context.windowDc)) {
        ReleaseDC(context.window, context.windowDc);
        return false;
    }

    if (!get_window_dimensions(context.window, context.width, context.height)){
        DeleteDC(context.memoryDc);
        ReleaseDC(context.window, context.windowDc);
        return false;
    }

    if (!get_window_bitmap(context.windowBitmap, context.windowDc, context.width, context.height))
    {
        DeleteDC(context.memoryDc);
        ReleaseDC(context.window, context.windowDc);
        return false;
    }

    return true;
}

void capture_multi_frames(HWND window)
{
    constexpr int targetFPS = 5;
    constexpr int durationSeconds = 2;
    constexpr int frameCount = targetFPS * durationSeconds;
    constexpr int intervalNs = 1'000'000'000 / targetFPS;

    auto interval = std::chrono::nanoseconds(intervalNs);    
    auto nextTimePoint = std::chrono::steady_clock::now();

    FrameContext context{};

    if (!initialize_frame_context(context, window)) {
        std::cerr << "frame context initialization: fail\n";
        return;
    }

    std::vector<Frame> frames;
    std::optional<Frame> frame;

    frames.reserve(frameCount);

    for (int i = 0; i < frameCount; i++) {
        frame = capture_frame(context);

        if (frame.has_value()) {
            frames.push_back(std::move(frame.value()));
        }

        if (i < frameCount - 1) {
            nextTimePoint += interval;
            std::this_thread::sleep_until(nextTimePoint);
        }
    }

    cleanup_frame_context(context);

    for (int i = 0; i < frames.size(); i++) {
        save_bitmap(frames[i].bitmapHeader, frames[i].pixelBytes, i);
    }
}
bool get_window_dc(HDC& windowDc, HWND window) {
    windowDc = GetDC(window);
    return windowDc != nullptr;
}
bool get_memory_dc(HDC& memoryDc, HDC screenDc) {
    memoryDc = CreateCompatibleDC(screenDc);
    return memoryDc != nullptr;
}
bool get_window_dimensions(HWND window, int& width, int& height) {
    if (window == nullptr) {
        //multi monitor selection will be added later
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
        return true;
    }

    RECT rect{};

    if (!GetClientRect(window, &rect)) {
        return false;
    }

    width = rect.right - rect.left;
    height = rect.bottom - rect.top;

    return width > 0 && height > 0;
}
bool get_window_bitmap(HBITMAP& windowBitmap, HDC windowDc, int width, int height) {
    windowBitmap = CreateCompatibleBitmap(windowDc, width, height);
    return windowBitmap != nullptr;
}
void cleanup_frame_context(FrameContext& context)
{
    if (context.windowBitmap != nullptr) {
        DeleteObject(context.windowBitmap);
    }
    if (context.memoryDc != nullptr) {
        DeleteDC(context.memoryDc);
    }
    if (context.windowDc != nullptr) {
        ReleaseDC(context.window, context.windowDc);
    }
}
std::optional<Frame> capture_frame(FrameContext& context)
{
    constexpr int bitsPerPixel = 32;

    HGDIOBJ previousSelectedObject = SelectObject(context.memoryDc, context.windowBitmap); 

    if (previousSelectedObject == nullptr) {
        return std::nullopt;
    }

    if (!BitBlt(context.memoryDc, 0, 0, context.width, context.height, context.windowDc, 0, 0, SRCCOPY))
    {
        SelectObject(context.memoryDc, previousSelectedObject);
        return std::nullopt;
    }

    BITMAPINFO bitmapInfo = create_bitmap_info(context.width, context.height, bitsPerPixel);

    SelectObject(context.memoryDc, previousSelectedObject); //GetDIBits expects the bitmap to be unselected in a dc

    Frame frame{};

    frame.bitmapHeader = bitmapInfo.bmiHeader;
    frame.pixelBytes.resize(context.width * context.height * bitsPerPixel / CHAR_BIT);

    int copiedScanLines = GetDIBits(context.windowDc, context.windowBitmap, 0, context.height, frame.pixelBytes.data(), &bitmapInfo, DIB_RGB_COLORS);

    if (copiedScanLines != context.height) {
        return std::nullopt;
    }

    return frame;
}
void save_bitmap(const BITMAPINFOHEADER& bitmapHeader, const std::vector<unsigned char>& pixelBytes, int frameNum) {

    std::string fileName = "_" + std::to_string(frameNum) + ".bmp";

    std::ofstream file(fileName, std::ios::binary);

    if (!file) {
        std::cerr << "bitmap file: fail\n";
        return;
    }

    std::cout << "bitmap file: success\n";

    BITMAPFILEHEADER fileHeader{};

    fileHeader.bfType = 0x4D42; //windows bitmap
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
BITMAPINFO create_bitmap_info(int screenWidth, int screenHeight, int bitsPerPixel) {
    BITMAPINFO bitmapInfo{};

    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = screenWidth;
    bitmapInfo.bmiHeader.biHeight = -screenHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = bitsPerPixel;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    return bitmapInfo;
}
std::optional<Frame> extract_frame_from_bitmap(HDC screenDc, HBITMAP screenBitmap,int screenWidth, int screenHeight, int bitsPerPixel)
{
    BITMAPINFO bitmapInfo = create_bitmap_info(screenWidth,screenHeight,bitsPerPixel);

    Frame frame{};
    frame.bitmapHeader = bitmapInfo.bmiHeader;
    frame.pixelBytes.resize(screenWidth * screenHeight * bitsPerPixel / CHAR_BIT);

    int copiedScanLines = GetDIBits(
        screenDc,
        screenBitmap,
        0,
        screenHeight,
        frame.pixelBytes.data(),
        &bitmapInfo,
        DIB_RGB_COLORS
    );

    if (copiedScanLines != screenHeight) {
        return std::nullopt;
    }

    return frame;
}
