///////////////////////////////////////
// ImageLoader.h
///////////////////////////////////////
#pragma once
#include <string>
#include <windows.h>

// Скачивает картинку по URL и декодирует её в HBITMAP (nullptr при ошибке).
// Общего состояния нет, поэтому можно вызывать из фоновых потоков.
// Нужен запущенный GDI+ (см. GdiplusSession). HBITMAP принадлежит вызывающему.
HBITMAP downloadBitmap(const std::wstring& url);

// GDI+ на всё время жизни приложения: создаётся один раз в wWinMain.
class GdiplusSession
{
public:
    GdiplusSession();
    ~GdiplusSession();
    GdiplusSession(const GdiplusSession&) = delete;
    GdiplusSession& operator=(const GdiplusSession&) = delete;

private:
    ULONG_PTR _token = 0;
};
