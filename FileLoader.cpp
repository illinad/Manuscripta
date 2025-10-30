// FileLoader.cpp — implementation of manuscripta::loadTextFile*
#define WIN32_LEAN_AND_MEAN
#include "FileLoader.h"
#include <windows.h>
#include <stdexcept>
#include <fstream>
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
        std::ifstream fs(path, std::ios::binary);
        if (!fs) throw std::runtime_error("Cannot open file");
        std::ostringstream oss;
        oss << fs.rdbuf();
        return oss.str();
    }

    std::string loadTextFileA(const std::wstring& filePath) {
        return readBinary(filePath); // raw bytes -> caller decides encoding
    }

    std::wstring loadTextFileW(const std::wstring& filePath) {
        std::string raw = readBinary(filePath);

        // Try interpret as UTF‑8 first
        int lenW = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), (int)raw.size(), nullptr, 0);
        if (lenW == 0) {
            // Fallback to system codepage
            lenW = MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), nullptr, 0);
            if (lenW == 0) throw std::runtime_error("Encoding conversion failed");
        }
        std::wstring w(lenW, L'\0');
        MultiByteToWideChar(lenW == 0 ? CP_ACP : CP_UTF8, 0, raw.data(), (int)raw.size(), w.data(), lenW);
        return w;
    }

} // namespace manuscripta
