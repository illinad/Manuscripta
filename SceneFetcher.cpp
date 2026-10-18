#include "SceneFetcher.h"
#include <windows.h>
#include <winhttp.h>
#include "nlohmann_json.hpp"
#include "thread"
#include "config.h"
#include "Settings.h"

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


// Адрес API: HKCU\Software\Manuscripta, строка "SceneApiUrl".
// http:// или https:// — схема определяет, шифруется ли соединение.
static const wchar_t* const DEFAULT_API_URL = L"http://vps72250.hyperhost.name/api/scene/getScene";

SceneApiResponse fetchScene(const std::wstring& text)
{
    // ── разбор адреса ──
    std::wstring url = settings::getString(L"SceneApiUrl", DEFAULT_API_URL);
    URL_COMPONENTS uc{ sizeof(uc) };
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host;  uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = _countof(path);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return {};
    const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"Manuscripta/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        nullptr, nullptr, 0);
    if (!hSession) return {};               // 🔹 нет WinHTTP

    // генерация картинки небыстрая: ждём ответ до 2 минут
    WinHttpSetTimeouts(hSession, 10000, 10000, 30000, 120000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return {}; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path,
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return {};
    }

    auto closeAll = [&]() {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
    };

    // тело запроса
    std::string body = json{ {"text_chunk", to_utf8(text)} }.dump();
    std::wstring hdr = L"Content-Type: application/json";

    if (!WinHttpSendRequest(hRequest, hdr.c_str(), (DWORD)-1,
            (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        closeAll();                          // 🔹 сеть не доступна / тайм-аут
        return {};
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) { closeAll(); return {}; }   // 🔹 ошибка сервера

    // читаем ответ …
    std::string resp;
    for (;;) {
        DWORD sz = 0, n = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &sz) || sz == 0) break;
        std::string buf(sz, '\0');
        if (!WinHttpReadData(hRequest, buf.data(), sz, &n) || n == 0) break;
        resp.append(buf.data(), n);
    }
    closeAll();

    // безопасный parse
    json j = json::parse(resp, nullptr, false);
    if (j.is_discarded()) return {};         // 🔹 битый JSON

    if (j.contains("data") && j["data"].is_object() &&
        j["data"].contains("image") && j["data"]["image"].is_string())
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
