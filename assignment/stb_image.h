#ifndef STB_IMAGE_H
#define STB_IMAGE_H

#include <windows.h>
#include <gdiplus.h>
#include <cstdlib>

static unsigned char* stbi_load_w(const wchar_t* filename, int* x, int* y, int* channels_in_file, int desired_channels)
{
    if (!filename || !x || !y) return nullptr;
    if (desired_channels != 0 && desired_channels != 4) return nullptr;

    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok)
        return nullptr;

    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(filename);
    if (!bitmap)
    {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return nullptr;
    }

    if (bitmap->GetLastStatus() != Gdiplus::Ok)
    {
        delete bitmap;
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return nullptr;
    }

    const int width = (int)bitmap->GetWidth();
    const int height = (int)bitmap->GetHeight();
    const int outChannels = 4;
    unsigned char* out = (unsigned char*)std::malloc(width * height * outChannels);
    if (!out)
    {
        delete bitmap;
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return nullptr;
    }

    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData data;
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
    {
        std::free(out);
        delete bitmap;
        Gdiplus::GdiplusShutdown(gdiplusToken);
        return nullptr;
    }

    for (int row = 0; row < height; ++row)
    {
        const unsigned char* src = (const unsigned char*)data.Scan0 + row * data.Stride;
        unsigned char* dst = out + row * width * outChannels;
        for (int col = 0; col < width; ++col)
        {
            dst[col * 4 + 0] = src[col * 4 + 2];
            dst[col * 4 + 1] = src[col * 4 + 1];
            dst[col * 4 + 2] = src[col * 4 + 0];
            dst[col * 4 + 3] = src[col * 4 + 3];
        }
    }

    bitmap->UnlockBits(&data);
    delete bitmap;
    Gdiplus::GdiplusShutdown(gdiplusToken);

    *x = width;
    *y = height;
    if (channels_in_file) *channels_in_file = 4;
    return out;
}

static unsigned char* stbi_load(const char* filename, int* x, int* y, int* channels_in_file, int desired_channels)
{
    if (!filename) return nullptr;

    int len = MultiByteToWideChar(CP_ACP, 0, filename, -1, nullptr, 0);
    if (len <= 0) return nullptr;

    wchar_t* wideName = (wchar_t*)std::malloc(sizeof(wchar_t) * len);
    if (!wideName) return nullptr;

    MultiByteToWideChar(CP_ACP, 0, filename, -1, wideName, len);
    unsigned char* result = stbi_load_w(wideName, x, y, channels_in_file, desired_channels);
    std::free(wideName);
    return result;
}

static void stbi_image_free(void* retval_from_stbi_load)
{
    std::free(retval_from_stbi_load);
}

#endif
