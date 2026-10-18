///////////////////////////////////////
// ImageLoader.cpp
///////////////////////////////////////
#include "ImageLoader.h"
#include <urlmon.h>
#include <gdiplus.h>
#include <atomic>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "gdiplus.lib")

// Фоновые потоки декодируют картинки, пока приложение закрывается:
// GDI+ гасим только после того, как текущие декодирования закончились.
static std::atomic<bool> g_shuttingDown{ false };
static std::atomic<int>  g_decoding{ 0 };

GdiplusSession::GdiplusSession()
{
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&_token, &input, nullptr);
}

GdiplusSession::~GdiplusSession()
{
    g_shuttingDown = true;
    for (int i = 0; i < 200 && g_decoding > 0; ++i)   // не дольше 2 секунд
        Sleep(10);
    if (_token && g_decoding == 0) Gdiplus::GdiplusShutdown(_token);
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

    HBITMAP bmp = nullptr;
    ++g_decoding;
    if (!g_shuttingDown)
        bmp = loadBitmapFromFile(file);
    --g_decoding;
    DeleteFileW(file.c_str());
    return bmp;
}
