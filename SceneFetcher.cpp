#include "SceneFetcher.h"
#include <windows.h>
#include <winhttp.h>
#include "nlohmann_json.hpp"
#include "thread"
#include "config.h"

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

static std::string to_utf8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), strTo.data(), size_needed, nullptr, nullptr);
    return strTo;
}

//  UTF-8  → UTF-16 (std::wstring)
//  --------------------------------
static std::wstring utf8_to_wstr(const std::string& utf8)
{
    if (utf8.empty()) return {};                     // пустая строка → пустой wstring

    int lenW = MultiByteToWideChar(
        CP_UTF8,                // исходная кодировка
        MB_ERR_INVALID_CHARS,   // отсекаем невалидные последовательности
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr, 0);

    if (lenW <= 0) return {};                        // конвертация провалилась

    std::wstring wstr(lenW, L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
        utf8.data(), static_cast<int>(utf8.size()),
        wstr.data(), lenW);

    return wstr;
}


SceneApiResponse fetchScene(const std::wstring& text)
{
    const wchar_t* host = L"vps72250.hyperhost.name";
    const wchar_t* path = L"/api/scene/getScene";

    HINTERNET hSession = WinHttpOpen(L"Manuscripta/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        nullptr, nullptr, 0);
    if (!hSession) return {};               // 🔹 нет WinHTTP

    HINTERNET hConnect = WinHttpConnect(hSession, host,
        INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return {}; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path,
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return {};
    }

    // тело запроса
    std::string body = json{ {"text_chunk", 
//        #ifdef _USE_STYLES 
//        std::string("Make in style of ") + std::string(_USE_STYLES) +
//#endif
        std::string(to_utf8(text))
        

        } }.dump();
    std::wstring hdr = L"Content-Type: application/json";

    BOOL ok = WinHttpSendRequest(hRequest, hdr.c_str(), (DWORD)-1,
        (LPVOID)body.data(), (DWORD)body.size(),
        (DWORD)body.size(), 0);
    if (!ok) {                               // 🔹 сеть не доступна
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};                           // → возвращаем «пусто»
    }

    WinHttpReceiveResponse(hRequest, nullptr);

    // читаем ответ …
    std::string resp; DWORD sz = 0;
    do {
        DWORD n = 0; WinHttpQueryDataAvailable(hRequest, &sz);
        if (!sz) break;
        std::string buf(sz, '\0');
        WinHttpReadData(hRequest, buf.data(), sz, &n);
        resp.append(buf.data(), n);
    } while (sz);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // безопасный parse
    json j;
    try { j = json::parse(resp); }
    catch (...) { return {}; }               // 🔹 битый JSON

    if (j.contains("data") && j["data"].contains("image"))
        return { utf8_to_wstr(j["data"]["image"].get<std::string>()) };

    return {};                               // 🔹 error / пустой
}


void fetchSceneAsync(const std::wstring& frameText, std::function<void(SceneApiResponse)> onDone)
{
    std::thread([frameText, onDone]() {
        SceneApiResponse result = fetchScene(frameText);
        if (onDone) onDone(result);
        }).detach();
}
