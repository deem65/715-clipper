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
    constexpr int clipId = 1;

    HWND window = nullptr; //temp

    if (!RegisterHotKey(window, clipId, MOD_CONTROL | MOD_SHIFT, VK_F7)) {
        return 1;
    }
    cout << "running\n";

    MSG message{};

    while (GetMessage(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_HOTKEY && message.wParam == clipId) {
            clip(window);
        }
    }
    UnregisterHotKey(nullptr, clipId);

    return 0;
}
void clip(HWND window) {
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

    if (
        !get_window_dc(ctx.windowDc, ctx.window) ||
        !get_memory_dc(ctx.memoryDc, ctx.windowDc) ||
        !get_window_dimensions(ctx.window, ctx.width, ctx.height) ||
        !get_window_bitmap(ctx.windowBitmap, ctx.windowDc, ctx.width, ctx.height)
        ) {
        cleanup_frame_context(ctx);
        return false;
    }
    return true;
}
void capture_multi_frames(HWND window)
{
    constexpr int fps = 5;
    constexpr int duration = 2;//s
    constexpr int frameCount = fps * duration;
    constexpr int intervalNs = 1'000'000'000 / fps;
    constexpr int bitsPerPixel = 32;

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
        frame = capture_frame(ctx, bitsPerPixel);

        if (frame.has_value()) {
            frames.push_back(move(frame.value()));
        }
        else {
            cerr << "frame" << i << " failed :(";
            //continues anyway
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

    cout << "requested frames: " << frameCount << '\n';
    cout << "captured frames: " << frames.size() << '\n';
    cout << "failed frames: " << frameCount - frames.size() << '\n';
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
        ctx.windowBitmap = nullptr; //prevents second cleanup
    }
    if (ctx.memoryDc != nullptr) {
        DeleteDC(ctx.memoryDc);
        ctx.memoryDc = nullptr;
    }
    if (ctx.windowDc != nullptr) {
        ReleaseDC(ctx.window, ctx.windowDc);
        ctx.windowDc = nullptr;
    }
}
optional<Frame> capture_frame(FrameContext& ctx, int bits)
{
    HGDIOBJ previousSelectedObject = SelectObject(ctx.memoryDc, ctx.windowBitmap); 

    if (previousSelectedObject == nullptr) {
        return nullopt;
    }
    if (!BitBlt(ctx.memoryDc, 0, 0, ctx.width, ctx.height, ctx.windowDc, 0, 0, SRCCOPY)) {
        SelectObject(ctx.memoryDc, previousSelectedObject);
        return nullopt;
    }
    SelectObject(ctx.memoryDc, previousSelectedObject); //GetDIBits expects the bitmap to be unselected in a dc

    return extract_frame_from_bitmap(ctx.windowDc, ctx.windowBitmap, ctx.width, ctx.height, bits);

}
void save_bitmap(const BITMAPINFOHEADER& bitmapHeader, const vector<unsigned char>& pixelBytes, int frameNum) {
    string fName = "_" + to_string(frameNum) + ".bmp";
    ofstream ofStream(fName, ios::binary);
    if (!ofStream) {
        return;
    }
    BITMAPFILEHEADER fHeader{};
    fHeader.bfType = 0x4D42; //windows bitmap
    fHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fHeader.bfSize = fHeader.bfOffBits + static_cast<DWORD>(pixelBytes.size());

    const char* fileHeaderBytePtr = reinterpret_cast<const char*>(&fHeader);
    const char* bitmapHeaderBytePtr = reinterpret_cast<const char*>(&bitmapHeader);
    const char* pixelBytesPtr = reinterpret_cast<const char*>(pixelBytes.data());
    streamsize pixelBytesStreamSize = static_cast<streamsize>(pixelBytes.size());

    ofStream.write(fileHeaderBytePtr, sizeof(fHeader));
    ofStream.write(bitmapHeaderBytePtr, sizeof(bitmapHeader));
    ofStream.write(pixelBytesPtr, pixelBytesStreamSize);

    if (!ofStream) {
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
