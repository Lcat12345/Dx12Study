#include "Image.h"
#include "Common.h"

#include <wincodec.h>
#include <wrl/client.h>
#include <stdexcept>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

ImageData LoadImageRGBA(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
    {
        throw std::runtime_error("Texture file not found:\n" + path.string());
    }

    ComPtr<IWICImagingFactory> factory;
    ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
                  "WIC factory");

    ComPtr<IWICBitmapDecoder> decoder;
    ThrowIfFailed(factory->CreateDecoderFromFilename(
                      path.c_str(), nullptr, GENERIC_READ,
                      WICDecodeMetadataCacheOnDemand, &decoder),
                  "WIC CreateDecoderFromFilename");

    ComPtr<IWICBitmapFrameDecode> frame;
    ThrowIfFailed(decoder->GetFrame(0, &frame), "WIC GetFrame");

    // Whatever the file's native format is, force it to plain RGBA8 so it
    // matches DXGI_FORMAT_R8G8B8A8_UNORM on the GPU side.
    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed(factory->CreateFormatConverter(&converter), "WIC CreateFormatConverter");
    ThrowIfFailed(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                        WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom),
                  "WIC converter Initialize");

    ImageData image;
    ThrowIfFailed(converter->GetSize(&image.width, &image.height), "WIC GetSize");

    const UINT rowPitch = image.width * 4;
    image.pixels.resize(size_t(rowPitch) * image.height);
    ThrowIfFailed(converter->CopyPixels(nullptr, rowPitch,
                                        UINT(image.pixels.size()),
                                        image.pixels.data()),
                  "WIC CopyPixels");
    return image;
}
