///////////////////////////////////////
// ImageCache.h
///////////////////////////////////////
#pragma once
#include <string>
#include <map>
#include <windows.h>

class ImageCache
{
public:
    // Возвращает HBITMAP или nullptr при ошибке.
    HBITMAP Get(const std::wstring& url);
    HBITMAP Peek(const std::wstring& url) const;
    ~ImageCache();

private:
    std::map<std::wstring, HBITMAP> _cache;

    // helpers
    static void ensureGdiplus();
    static bool _gdiplusStarted;
    static ULONG_PTR _gdiplusToken;

    std::wstring downloadToTemp(const std::wstring& url);
    HBITMAP      loadBitmapFromFile(const std::wstring& file);
};