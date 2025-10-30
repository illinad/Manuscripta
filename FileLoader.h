#pragma once
// FileLoader.h — tiny utility to read an entire text file into memory
// Throws std::runtime_error on failure.
#include <string>
#include <vector>
#include <windows.h>

std::wstring selectTxtFile(HWND owner = nullptr);

namespace manuscripta {

	// Reads a text file (UTF‑8 or ANSI) into std::string.
	// If you need wide char, use loadTextFileW below.
	std::string loadTextFileA(const std::wstring& filePath);

	// Reads a UTF‑16LE text file into std::wstring. If file is UTF‑8/ANSI it will
	// be converted using MultiByteToWideChar with CP_UTF8 / CP_ACP fallback.
	std::wstring loadTextFileW(const std::wstring& filePath);

} // namespace manuscripta
