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

using namespace std;

atomic<bool> captureInProgress{ false };

int main()
{
    constexpr int clipHotkeyId = 1;

    HWND window = nullptr; //temp

    if (!RegisterHotKey(window, clipHotkeyId, MOD_CONTROL | MOD_SHIFT, VK_F7)) {
        return 1;
    }
    cout << "running\n";

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
        return;
    }
    thread capture([window]() {
        capture_multi_frames(window);
        captureInProgress.store(false);
        });
    capture.detach();
}
bool initialize_frame_context(FrameContext& ctx, HWND window) {
    ctx.window = window;

    if (!get_window_dc(ctx.windowDc, ctx.window)) {
        return false;
    }
    if (!get_memory_dc(ctx.memoryDc, ctx.windowDc)) {
        ReleaseDC(ctx.window, ctx.windowDc);
        return false;
    }
    if (!get_window_dimensions(ctx.window, ctx.width, ctx.height)){
        DeleteDC(ctx.memoryDc);
        ReleaseDC(ctx.window, ctx.windowDc);
        return false;
    }
    if (!get_window_bitmap(ctx.windowBitmap, ctx.windowDc, ctx.width, ctx.height))
    {
        DeleteDC(ctx.memoryDc);
        ReleaseDC(ctx.window, ctx.windowDc);
        return false;
    }

    return true;
}
void capture_multi_frames(HWND window)
{
    constexpr int targetFPS = 2;
    constexpr int durationSeconds = 5;
    constexpr int frameCount = targetFPS * durationSeconds;
    constexpr int intervalNs = 1'000'000'000 / targetFPS;

    auto interval = chrono::nanoseconds(intervalNs);    
    auto nextTimePoint = chrono::steady_clock::now();

    FrameContext ctx{};
    if (!initialize_frame_context(ctx, window)) {
        cerr << "frame context initialization: fail\n";
        return;
    }
    vector<Frame> frames;
    optional<Frame> frame;
    frames.reserve(frameCount);
    for (int i = 0; i < frameCount; i++) {
        frame = capture_frame(ctx);

        if (frame.has_value()) {
            frames.push_back(move(frame.value()));
        }

        if (i < frameCount - 1) {
            nextTimePoint += interval;
            this_thread::sleep_until(nextTimePoint);
        }
    }
    cleanup_frame_context(ctx);
    for (int i = 0; i < frames.size(); i++) {
        save_bitmap(frames[i].bitmapHeader, frames[i].pixelBytes, i);
    }
}
bool get_window_dc(HDC& windowDc, HWND window) {
    windowDc = GetDC(window);
    return windowDc != nullptr;
}
bool get_memory_dc(HDC& memoryDc, HDC windowDc) {
    memoryDc = CreateCompatibleDC(windowDc);
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
void cleanup_frame_context(FrameContext& ctx)
{
    if (ctx.windowBitmap != nullptr) {
        DeleteObject(ctx.windowBitmap);
    }
    if (ctx.memoryDc != nullptr) {
        DeleteDC(ctx.memoryDc);
    }
    if (ctx.windowDc != nullptr) {
        ReleaseDC(ctx.window, ctx.windowDc);
    }
}
optional<Frame> capture_frame(FrameContext& ctx)
{
    constexpr int bitsPerPixel = 32;

    HGDIOBJ previousSelectedObject = SelectObject(ctx.memoryDc, ctx.windowBitmap); 

    if (previousSelectedObject == nullptr) {
        return nullopt;
    }
    if (!BitBlt(ctx.memoryDc, 0, 0, ctx.width, ctx.height, ctx.windowDc, 0, 0, SRCCOPY))
    {
        SelectObject(ctx.memoryDc, previousSelectedObject);
        return nullopt;
    }

    BITMAPINFO bitmapInfo = create_bitmap_info(ctx.width, ctx.height, bitsPerPixel);
    SelectObject(ctx.memoryDc, previousSelectedObject); //GetDIBits expects the bitmap to be unselected in a dc

    return extract_frame_from_bitmap(ctx.windowDc, ctx.windowBitmap, ctx.width, ctx.height, bitsPerPixel);

}
void save_bitmap(const BITMAPINFOHEADER& bitmapHeader, const vector<unsigned char>& pixelBytes, int frameNum) {
    string fileName = "_" + to_string(frameNum) + ".bmp";
    ofstream file(fileName, ios::binary);
    if (!file) {
        return;
    }
    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42; //windows bitmap
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + static_cast<DWORD>(pixelBytes.size());

    const char* fileHeaderBytePtr = reinterpret_cast<const char*>(&fileHeader);
    const char* bitmapHeaderBytePtr = reinterpret_cast<const char*>(&bitmapHeader);
    const char* pixelBytesPtr = reinterpret_cast<const char*>(pixelBytes.data());
    streamsize pixelBytesStreamSize = static_cast<streamsize>(pixelBytes.size());

    file.write(fileHeaderBytePtr, sizeof(fileHeader));
    file.write(bitmapHeaderBytePtr, sizeof(bitmapHeader));
    file.write(pixelBytesPtr, pixelBytesStreamSize);

    if (!file) {
        cerr << "bitmap write: fail\n";
        return;
    }
    cout << "bitmap write: success\n";
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
optional<Frame> extract_frame_from_bitmap(HDC screenDc, HBITMAP screenBitmap,int screenWidth, int screenHeight, int bitsPerPixel)
{
    BITMAPINFO bitmapInfo = create_bitmap_info(screenWidth,screenHeight,bitsPerPixel);
    Frame frame{};
    frame.bitmapHeader = bitmapInfo.bmiHeader;
    frame.pixelBytes.resize(screenWidth * screenHeight * bitsPerPixel / CHAR_BIT);
    int copiedScanLines = GetDIBits(screenDc, screenBitmap, 0, screenHeight, frame.pixelBytes.data(), &bitmapInfo, DIB_RGB_COLORS);
    if (copiedScanLines != screenHeight) {
        return nullopt;
    }
    return frame;
}
