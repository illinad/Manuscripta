///////////////////////////////////////
// ImageLoader.cpp
///////////////////////////////////////
#include "ImageLoader.h"
#include <urlmon.h>
#include <gdiplus.h>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "gdiplus.lib")

GdiplusSession::GdiplusSession()
{
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&_token, &input, nullptr);
}

GdiplusSession::~GdiplusSession()
{
    if (_token) Gdiplus::GdiplusShutdown(_token);
}

static std::wstring downloadToTemp(const std::wstring& url)
{
    WCHAR tmpDir[MAX_PATH]{};
    WCHAR tmpName[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmpDir);
    if (!GetTempFileNameW(tmpDir, L"msc", 0, tmpName)) return L"";

    HRESULT hr = E_FAIL;
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    {
        hr = URLDownloadToFileW(nullptr, url.c_str(), tmpName, 0, nullptr);
        CoUninitialize();
    }
    if (FAILED(hr))
    {
        DeleteFileW(tmpName);   // GetTempFileNameW уже создал пустой файл
        return L"";
    }
    return tmpName;
}

static HBITMAP loadBitmapFromFile(const std::wstring& file)
{
    HBITMAP hBmp = nullptr;
    {
        Gdiplus::Bitmap bmp(file.c_str());
        if (bmp.GetLastStatus() == Gdiplus::Ok)
            bmp.GetHBITMAP(Gdiplus::Color::Black, &hBmp);
    }   // Bitmap держит файл открытым до разрушения
    return hBmp;
}

HBITMAP downloadBitmap(const std::wstring& url)
{
    std::wstring file = downloadToTemp(url);
    if (file.empty()) return nullptr;

    HBITMAP bmp = loadBitmapFromFile(file);
    DeleteFileW(file.c_str());
    return bmp;
}
