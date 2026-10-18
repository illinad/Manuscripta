// Settings.cpp — реализация settings::*
#include "Settings.h"

namespace {
    const wchar_t* const ROOT = L"Software\\Manuscripta";
    const wchar_t* const POSITIONS = L"Software\\Manuscripta\\Positions";

    DWORD readDword(const wchar_t* key, const wchar_t* name, DWORD def)
    {
        DWORD value = 0, size = sizeof(value);
        LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, key, name,
            RRF_RT_REG_DWORD, nullptr, &value, &size);
        return st == ERROR_SUCCESS ? value : def;
    }

    void writeDword(const wchar_t* key, const wchar_t* name, DWORD value)
    {
        RegSetKeyValueW(HKEY_CURRENT_USER, key, name, REG_DWORD, &value, sizeof(value));
    }
}

namespace settings {

    DWORD getDword(const wchar_t* name, DWORD def) { return readDword(ROOT, name, def); }
    void  setDword(const wchar_t* name, DWORD value) { writeDword(ROOT, name, value); }

    std::wstring getString(const wchar_t* name, const std::wstring& def)
    {
        DWORD size = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, ROOT, name, RRF_RT_REG_SZ,
                nullptr, nullptr, &size) != ERROR_SUCCESS || size < sizeof(wchar_t))
            return def;

        std::wstring value(size / sizeof(wchar_t), L'\0');
        if (RegGetValueW(HKEY_CURRENT_USER, ROOT, name, RRF_RT_REG_SZ,
                nullptr, value.data(), &size) != ERROR_SUCCESS)
            return def;

        value.resize(wcslen(value.c_str()));
        return value.empty() ? def : value;
    }

    size_t getPosition(const std::wstring& bookPath)
    {
        return readDword(POSITIONS, bookPath.c_str(), 0);
    }

    void setPosition(const std::wstring& bookPath, size_t frame)
    {
        writeDword(POSITIONS, bookPath.c_str(), static_cast<DWORD>(frame));
    }

} // namespace settings
