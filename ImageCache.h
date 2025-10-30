///////////////////////////////////////
// ImageCache.h
///////////////////////////////////////
#pragma once
#include <string>
#include <map>
#include <mutex>
#include <windows.h>

class ImageCache
{
public:
    //  HBITMAP  nullptr  .
    HBITMAP Get(const std::wstring& url);
    HBITMAP Peek(const std::wstring& url) const;
    ~ImageCache();

private:
    std::map<std::wstring, HBITMAP> _cache;
    mutable std::mutex _mutex;

    // helpers
    static void ensureGdiplus();
    static bool _gdiplusStarted;
    static ULONG_PTR _gdiplusToken;

    std::wstring downloadToTemp(const std::wstring& url);
    HBITMAP      loadBitmapFromFile(const std::wstring& file);
};