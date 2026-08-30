#include "image_decoder.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <format>
#include <limits>
#include <new>
#include <utility>

namespace panning_wallpaper {
namespace {

void SetHresultError(
    std::wstring_view operation,
    HRESULT result,
    std::wstring& error) {
    error = std::format(
        L"{} failed with HRESULT 0x{:08X}.",
        operation,
        static_cast<unsigned long>(result));
}

}  // namespace

bool DecodeImageFile(
    std::wstring_view path,
    DecodedImage& image,
    std::wstring& error) {
    image = {};

    if (path.empty()) {
        error = L"The image path is empty.";
        return false;
    }

    const std::wstring filePath(path);
    const DWORD attributes = GetFileAttributesW(filePath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        error = std::format(
            L"The image file could not be accessed (Win32 error {}).",
            GetLastError());
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        error = L"The image path refers to a directory, not a file.";
        return false;
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory2,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        SetHresultError(L"WIC factory creation", result, error);
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(
        filePath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
    if (FAILED(result)) {
        SetHresultError(L"WIC image decoder creation", result, error);
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, &frame);
    if (FAILED(result)) {
        SetHresultError(L"WIC first-frame decoding", result, error);
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    result = frame->GetSize(&width, &height);
    if (FAILED(result)) {
        SetHresultError(L"WIC image-size query", result, error);
        return false;
    }
    if (width == 0 || height == 0) {
        error = L"The image has invalid zero dimensions.";
        return false;
    }
    if (width > std::numeric_limits<UINT>::max() / 4U) {
        error = L"The image is too wide to decode as 32-bit pixels.";
        return false;
    }

    const UINT rowPitch = width * 4U;
    const std::uint64_t bufferSize64 =
        static_cast<std::uint64_t>(rowPitch) * height;
    if (bufferSize64 > std::numeric_limits<UINT>::max()) {
        error = L"The decoded 32-bit image is too large for WIC.";
        return false;
    }
    const UINT bufferSize = static_cast<UINT>(bufferSize64);

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(&converter);
    if (FAILED(result)) {
        SetHresultError(L"WIC pixel-format converter creation", result, error);
        return false;
    }

    result = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(result)) {
        SetHresultError(L"WIC conversion to 32-bit BGRA", result, error);
        return false;
    }

    std::vector<std::uint8_t> pixels;
    try {
        pixels.resize(bufferSize);
    } catch (const std::bad_alloc&) {
        error = L"Memory allocation for the decoded image failed.";
        return false;
    }

    result = converter->CopyPixels(
        nullptr, rowPitch, bufferSize, pixels.data());
    if (FAILED(result)) {
        SetHresultError(L"WIC pixel decoding", result, error);
        return false;
    }

    image.width = width;
    image.height = height;
    image.rowPitch = rowPitch;
    image.pixels = std::move(pixels);
    return true;
}

}  // namespace panning_wallpaper
