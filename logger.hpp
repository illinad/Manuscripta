#pragma once
#include <iostream>
#include <fstream>
#include <string_view>
#include <chrono>
#include <iomanip>
#ifdef _WIN32
#include <windows.h>     // OutputDebugString
#endif

class Logger
{
public:
    enum class Level { Info, Warn, Error };

    /// Инициализировать логгер (по желанию указать файл-приёмник).
    static void init(std::string_view fileName = {})
    {
        if (!fileName.empty())
            s_file.open(std::string(fileName), std::ios::app);
    }

    /// Записать сообщение c автоматической меткой времени и уровнем.
    static void log(Level lvl, std::string_view msg)
    {
        const auto stamp = timeStamp();
        const char* tag =
            (lvl == Level::Info) ? "[INFO ]" :
            (lvl == Level::Warn) ? "[WARN ]" :
            "[ERROR]";

        std::ostringstream line;
        line << stamp << ' ' << tag << ' ' << msg << '\n';
        const std::string ready = line.str();

        // ---- вывод на консоль ----
        std::cout << ready;
        std::cout.flush();

        // ---- вывод в файл ----
        if (s_file.is_open())
        {
            s_file << ready;
            s_file.flush();
        }

        // ---- вывод в OutputDebugString (Visual Studio) ----
#ifdef _WIN32
        ::OutputDebugStringA(ready.c_str());
#endif
    }

    static void info(std::string_view m) { log(Level::Info, m); }
    static void warn(std::string_view m) { log(Level::Warn, m); }
    static void error(std::string_view m) { log(Level::Error, m); }

private:
    static std::ofstream   s_file;

    static std::string timeStamp()
    {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const auto t = system_clock::to_time_t(now);
        const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%F %T") << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }
};

// статическое поле
inline std::ofstream Logger::s_file;
