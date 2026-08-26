#pragma once

#include <Windows.h>
#include <vector>

void OnClipHotkeyPressed();

void SaveBitmap(
    const BITMAPINFOHEADER& bitmapHeader,
    const std::vector<unsigned char>& pixelBytes
);