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

	// Reads a text file into std::wstring. Understands UTF-8 / UTF-16 LE / UTF-16 BE
	// with a BOM; without one tries strict UTF-8, then the system code page.
	// An empty file gives an empty string.
	std::wstring loadTextFileW(const std::wstring& filePath);

} // namespace manuscripta
