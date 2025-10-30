#include "SceneFetcher.h"
#include <windows.h>
#include <winhttp.h>
#include "nlohmann_json.hpp"
#include "thread"
#include "config.h"
#include <algorithm>
#include <sstream>
#include "logger.hpp"

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

static std::wstring utf8_to_wstr(const std::string& utf8)
{
    if (utf8.empty()) return {};

    int lenW = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr, 0);

    if (lenW <= 0) return {};

    std::wstring wstr(lenW, L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
        utf8.data(), static_cast<int>(utf8.size()),
        wstr.data(), lenW);

    return wstr;
}

namespace
{
    struct HttpHandle
    {
        HttpHandle() = default;
        explicit HttpHandle(HINTERNET h) : handle(h) {}
        ~HttpHandle() { reset(); }

        HttpHandle(const HttpHandle&) = delete;
        HttpHandle& operator=(const HttpHandle&) = delete;

        HttpHandle(HttpHandle&& other) noexcept : handle(other.handle)
        {
            other.handle = nullptr;
        }

        HttpHandle& operator=(HttpHandle&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                handle = other.handle;
                other.handle = nullptr;
            }
            return *this;
        }

        void reset()
        {
            if (handle)
            {
                WinHttpCloseHandle(handle);
                handle = nullptr;
            }
        }

        operator HINTERNET() const { return handle; }
        HINTERNET get() const { return handle; }

        HINTERNET handle = nullptr;
    };

    std::wstring systemErrorToString(DWORD err)
    {
        if (!err) return {};

        LPWSTR buffer = nullptr;
        DWORD len = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            err,
            0,
            reinterpret_cast<LPWSTR>(&buffer),
            0,
            nullptr);

        std::wstring message;
        if (len && buffer)
        {
            message.assign(buffer, len);
            while (!message.empty() &&
                (message.back() == L'\r' || message.back() == L'\n'))
            {
                message.pop_back();
            }
        }

        if (buffer)
            LocalFree(buffer);

        return message;
    }

    std::wstring makeTransportMessage(const wchar_t* stage, DWORD err)
    {
        std::wstringstream ss;
        ss << stage;
        if (err)
        {
            ss << L" failed (" << err << L")";
            std::wstring sys = systemErrorToString(err);
            if (!sys.empty())
                ss << L": " << sys;
        }
        return ss.str();
    }

    std::wstring summariseUtf8(const std::string& payload, size_t limit = 160)
    {
        if (payload.empty())
            return L"(empty body)";

        std::string slice = payload.substr(0, std::min(limit, payload.size()));
        std::wstring preview = utf8_to_wstr(slice);
        if (payload.size() > limit)
            preview.append(L"…");
        return preview;
    }

    std::wstring extractMessageFromJson(const json& j)
    {
        const auto pullString = [](const json& node) -> std::wstring
        {
            if (node.is_string())
                return utf8_to_wstr(node.get<std::string>());
            return {};
        };

        if (j.contains("error"))
        {
            if (auto s = pullString(j["error"]); !s.empty())
                return s;
        }

        if (j.contains("message"))
        {
            if (auto s = pullString(j["message"]); !s.empty())
                return s;
        }

        if (j.contains("data"))
        {
            const json& data = j["data"];
            if (data.is_object())
            {
                if (data.contains("error"))
                {
                    if (auto s = pullString(data["error"]); !s.empty())
                        return s;
                }
                if (data.contains("message"))
                {
                    if (auto s = pullString(data["message"]); !s.empty())
                        return s;
                }
            }
        }

        return {};
    }
}

SceneApiResponse fetchScene(const std::wstring& text)
{
    SceneApiResponse result;
    const wchar_t* host = L"vps72250.hyperhost.name";
    const wchar_t* path = L"/api/scene/getScene";

    HttpHandle hSession{ WinHttpOpen(L"Manuscripta/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        nullptr, nullptr, 0) };
    if (!hSession.get())
    {
        DWORD err = GetLastError();
        result.win32Error = err;
        result.errorMessage = makeTransportMessage(L"WinHttpOpen", err);
        Logger::error(to_utf8(result.errorMessage));
        return result;
    }

    HttpHandle hConnect{ WinHttpConnect(hSession.get(), host,
        INTERNET_DEFAULT_HTTP_PORT, 0) };
    if (!hConnect.get())
    {
        DWORD err = GetLastError();
        result.win32Error = err;
        result.errorMessage = makeTransportMessage(L"WinHttpConnect", err);
        Logger::error(to_utf8(result.errorMessage));
        return result;
    }

    HttpHandle hRequest{ WinHttpOpenRequest(hConnect.get(), L"POST", path,
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, 0) };
    if (!hRequest.get())
    {
        DWORD err = GetLastError();
        result.win32Error = err;
        result.errorMessage = makeTransportMessage(L"WinHttpOpenRequest", err);
        Logger::error(to_utf8(result.errorMessage));
        return result;
    }

    std::string chunkUtf8 = to_utf8(text);
#ifdef _USE_STYLES
    chunkUtf8 = std::string("Make in style of ") + std::string(_USE_STYLES) + std::string(": ") + chunkUtf8;
#endif
    json payload;
    payload["text_chunk"] = chunkUtf8;
    std::string body = payload.dump();
    std::wstring hdr = L"Content-Type: application/json";

    BOOL ok = WinHttpSendRequest(hRequest.get(), hdr.c_str(), (DWORD)-1,
        (LPVOID)body.data(), (DWORD)body.size(),
        (DWORD)body.size(), 0);
    if (!ok)
    {
        DWORD err = GetLastError();
        result.win32Error = err;
        result.errorMessage = makeTransportMessage(L"WinHttpSendRequest", err);
        Logger::error(to_utf8(result.errorMessage));
        return result;
    }

    if (!WinHttpReceiveResponse(hRequest.get(), nullptr))
    {
        DWORD err = GetLastError();
        result.win32Error = err;
        result.errorMessage = makeTransportMessage(L"WinHttpReceiveResponse", err);
        Logger::error(to_utf8(result.errorMessage));
        return result;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(hRequest.get(),
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr,
        &status,
        &statusSize,
        nullptr))
    {
        DWORD err = GetLastError();
        result.win32Error = err;
        result.errorMessage = makeTransportMessage(L"WinHttpQueryHeaders", err);
        Logger::error(to_utf8(result.errorMessage));
        return result;
    }
    result.statusCode = status;

    std::string resp;
    while (true)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest.get(), &available))
        {
            DWORD err = GetLastError();
            result.win32Error = err;
            result.errorMessage = makeTransportMessage(L"WinHttpQueryDataAvailable", err);
            Logger::error(to_utf8(result.errorMessage));
            return result;
        }

        if (!available)
            break;

        std::string buf(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(hRequest.get(), buf.data(), available, &read))
        {
            DWORD err = GetLastError();
            result.win32Error = err;
            result.errorMessage = makeTransportMessage(L"WinHttpReadData", err);
            Logger::error(to_utf8(result.errorMessage));
            return result;
        }

        resp.append(buf.data(), read);
    }

    json j;
    try
    {
        if (!resp.empty())
            j = json::parse(resp);
        else
            j = json::object();
    }
    catch (const std::exception& ex)
    {
        result.errorMessage = L"Scene API returned invalid JSON";
        result.errorMessage += L" (" + utf8_to_wstr(ex.what()) + L")";
        if (!resp.empty())
        {
            result.errorMessage += L" — body preview: "";
            result.errorMessage += summariseUtf8(resp);
            result.errorMessage += L""";
        }
        Logger::error(to_utf8(result.errorMessage));
        return result;
    }

    if (status >= 400)
    {
        std::wstring serverMessage = extractMessageFromJson(j);
        std::wstringstream ss;
        ss << L"HTTP " << status;
        if (!serverMessage.empty())
            ss << L": " << serverMessage;
        if (serverMessage.empty() && !resp.empty())
        {
            ss << L" — body: "" << summariseUtf8(resp) << L""";
        }
        result.errorMessage = ss.str();
        Logger::warn(to_utf8(result.errorMessage));
        return result;
    }

    if (j.contains("data") && j["data"].contains("image") && j["data"]["image"].is_string())
    {
        result.imageUrl = utf8_to_wstr(j["data"]["image"].get<std::string>());
        return result;
    }

    std::wstring reason = extractMessageFromJson(j);
    if (reason.empty())
    {
        reason = L"Scene API response did not contain data.image";
        if (!resp.empty())
        {
            reason += L" — body preview: "";
            reason += summariseUtf8(resp);
            reason += L""";
        }
    }
    result.errorMessage = reason;
    Logger::warn(to_utf8(result.errorMessage));
    return result;
}

void fetchSceneAsync(const std::wstring& frameText, std::function<void(SceneApiResponse)> onDone)
{
    std::thread([frameText, onDone]() {
        SceneApiResponse result = fetchScene(frameText);
        if (onDone) onDone(std::move(result));
        }).detach();
}
