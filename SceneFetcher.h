#pragma once
#include <string>
#include <functional>
#include <windows.h>

struct SceneApiResponse {
    std::wstring imageUrl;
    std::wstring errorMessage;
    DWORD statusCode = 0;
    DWORD win32Error = 0;
    std::wstring requestId;
};

SceneApiResponse fetchScene(const std::wstring& text);

void fetchSceneAsync(const std::wstring& frameText, std::function<void(SceneApiResponse)> onDone);