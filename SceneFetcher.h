#pragma once
#include <string>
#include <functional>

struct SceneApiResponse {
    std::wstring imageUrl;
};

SceneApiResponse fetchScene(const std::wstring& text);

void fetchSceneAsync(const std::wstring& frameText, std::function<void(SceneApiResponse)> onDone);