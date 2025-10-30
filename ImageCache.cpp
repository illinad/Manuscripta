///////////////////////////////////////
// ImageCache.cpp
///////////////////////////////////////
#include "ImageCache.h"
#include <urlmon.h>
#include <gdiplus.h>
#include <shlwapi.h>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace Gdiplus;

bool       ImageCache::_gdiplusStarted = false;
ULONG_PTR  ImageCache::_gdiplusToken = 0;

void ImageCache::ensureGdiplus()
{
    if (!_gdiplusStarted)
    {
        GdiplusStartupInput gdiSI;
        GdiplusStartup(&_gdiplusToken, &gdiSI, nullptr);
        _gdiplusStarted = true;
    }
}

HBITMAP ImageCache::Get(const std::wstring& url)
{
    auto it = _cache.find(url);
    if (it != _cache.end()) return it->second;

    std::wstring tmpFile = downloadToTemp(url);
    if (tmpFile.empty()) return nullptr;

    ensureGdiplus();
    HBITMAP bmp = loadBitmapFromFile(tmpFile);
    if (bmp) _cache[url] = bmp;
    return bmp;
}

//HBITMAP ImageCache::Peek(const std::wstring& url) const
//{
//    auto it = _cache.find(url);
//    return (it != _cache.end()) ? it->second : nullptr;
//}

std::wstring ImageCache::downloadToTemp(const std::wstring& url)
{
    WCHAR tmpDir[MAX_PATH]{};
    WCHAR tmpName[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmpDir);
    GetTempFileNameW(tmpDir, L"msc", 0, tmpName);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return L"";
    HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), tmpName, 0, nullptr);
    CoUninitialize();
    return SUCCEEDED(hr) ? std::wstring(tmpName) : L"";
}

HBITMAP ImageCache::loadBitmapFromFile(const std::wstring& file)
{
    Bitmap bmp(file.c_str());
    if (bmp.GetLastStatus() != Ok)
        return nullptr;

    HBITMAP hBmp = nullptr;
    bmp.GetHBITMAP(Color::Black, &hBmp);
    return hBmp;
}

ImageCache::~ImageCache()
{
    for (auto& kv : _cache)
        if (kv.second) DeleteObject(kv.second);

    if (_gdiplusStarted)
        GdiplusShutdown(_gdiplusToken);
}
