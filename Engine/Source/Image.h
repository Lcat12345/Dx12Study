// Image.h : CPU-side image decoding. No D3D12 here on purpose.
#pragma once

#include <Windows.h>
#include <cstdint>
#include <vector>
#include <filesystem>

struct ImageData
{
    std::vector<uint8_t> pixels; // tightly packed RGBA8, row pitch = width * 4
    UINT                 width  = 0;
    UINT                 height = 0;
};

// Decoded with WIC, the imaging component that ships with Windows - no
// third-party library, and it handles PNG/JPG/BMP/TIFF alike. Swapping in
// stb_image later means replacing only this function.
// Requires COM to be initialized on the calling thread.
ImageData LoadImageRGBA(const std::filesystem::path& path);
