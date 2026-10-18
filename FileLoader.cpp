// FileLoader.cpp — implementation of manuscripta::loadTextFile*
#define WIN32_LEAN_AND_MEAN
#include "FileLoader.h"
#include <windows.h>
#include <stdexcept>
#include <fstream>
#include <filesystem>
#include <initializer_list>
#include <sstream>
#include <windows.h>
#include <commdlg.h>
#include <string>

std::wstring selectTxtFile(HWND owner)
{
    OPENFILENAMEW ofn{};
    wchar_t fileBuf[MAX_PATH] = L"";

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST |
        OFN_HIDEREADONLY | OFN_EXPLORER;
    ofn.lpstrDefExt = L"txt";

    return GetOpenFileNameW(&ofn) ? std::wstring(fileBuf) : std::wstring();
}
namespace manuscripta {


    static std::string readBinary(const std::wstring& path) {
        std::ifstream fs(std::filesystem::path(path), std::ios::binary);
        if (!fs) throw std::runtime_error("Cannot open file");
        std::ostringstream oss;
        oss << fs.rdbuf();
        return oss.str();
    }

    static std::wstring decode(const char* data, int size, UINT codePage, DWORD flags) {
        if (size <= 0) return {};
        int lenW = MultiByteToWideChar(codePage, flags, data, size, nullptr, 0);
        if (lenW == 0) return {};
        std::wstring w(lenW, L'\0');
        MultiByteToWideChar(codePage, flags, data, size, w.data(), lenW);
        return w;
    }

    static std::wstring decodeUtf16(const std::string& raw, size_t offset, bool bigEndian) {
        std::wstring w;
        w.reserve((raw.size() - offset) / 2);
        for (size_t i = offset; i + 1 < raw.size(); i += 2) {
            unsigned char lo = static_cast<unsigned char>(raw[bigEndian ? i + 1 : i]);
            unsigned char hi = static_cast<unsigned char>(raw[bigEndian ? i : i + 1]);
            w.push_back(static_cast<wchar_t>(lo | (hi << 8)));
        }
        return w;
    }

    std::string loadTextFileA(const std::wstring& filePath) {
        return readBinary(filePath); // raw bytes -> caller decides encoding
    }

    std::wstring loadTextFileW(const std::wstring& filePath) {
        std::string raw = readBinary(filePath);
        if (raw.empty()) return {};

        auto starts = [&raw](std::initializer_list<unsigned char> bom) {
            if (raw.size() < bom.size()) return false;
            size_t i = 0;
            for (unsigned char b : bom)
                if (static_cast<unsigned char>(raw[i++]) != b) return false;
            return true;
        };

        // BOM: UTF-8 / UTF-16 LE / UTF-16 BE
        if (starts({ 0xEF, 0xBB, 0xBF }))
            return decode(raw.data() + 3, static_cast<int>(raw.size() - 3), CP_UTF8, 0);
        if (starts({ 0xFF, 0xFE }))
            return decodeUtf16(raw, 2, false);
        if (starts({ 0xFE, 0xFF }))
            return decodeUtf16(raw, 2, true);

        // Without BOM: strict UTF-8 first, then the system code page
        const int size = static_cast<int>(raw.size());
        std::wstring w = decode(raw.data(), size, CP_UTF8, MB_ERR_INVALID_CHARS);
        if (w.empty())
            w = decode(raw.data(), size, CP_ACP, 0);
        if (w.empty())
            throw std::runtime_error("Encoding conversion failed");
        return w;
    }

} // namespace manuscripta
