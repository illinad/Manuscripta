// Settings.h — настройки и позиции чтения в реестре (HKCU\Software\Manuscripta)
#pragma once
#include <string>
#include <windows.h>

namespace settings {

    DWORD        getDword(const wchar_t* name, DWORD def);
    void         setDword(const wchar_t* name, DWORD value);

    std::wstring getString(const wchar_t* name, const std::wstring& def);

    // Номер кадра, на котором остановились в книге (0 — с начала)
    size_t       getPosition(const std::wstring& bookPath);
    void         setPosition(const std::wstring& bookPath, size_t frame);

} // namespace settings
