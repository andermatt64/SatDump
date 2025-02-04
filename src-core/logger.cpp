#define SATDUMP_DLL_EXPORT 1
#include "logger.h"
#include <iostream>
#include <ctime>
#include <algorithm>
#ifdef __ANDROID__
#include <android/log.h>
static char ag_LogTag[] = "SatDump";
#endif
#if defined(_WIN32)
#include <windows.h>
#include <wincon.h>
#endif

// Logger and sinks. We got a console sink and file sink
#ifdef __ANDROID__
std::shared_ptr<slog::AndroidSink> console_sink;
#elif defined(_WIN32)
std::shared_ptr<slog::WinOutSink> console_sink;
#else
std::shared_ptr<slog::StdOutSink> console_sink;
#endif
std::shared_ptr<slog::FileSink> file_sink;
SATDUMP_DLL std::shared_ptr<slog::Logger> logger;

namespace slog
{
    const std::string log_schar[] = {"T", "D", "I", "W", "E", "C"};
    const std::string colors[] = {"\033[37m",
                                  "\033[36m",
                                  "\033[32m",
                                  "\033[33m\033[1m",
                                  "\033[31m\033[1m",
                                  "\033[1m\033[41m"};

    std::string LoggerSink::format_log(LogMsg m, bool color, int *cpos)
    {
        time_t ct = time(0);
        std::tm *tmr = gmtime(&ct);

        std::string timestamp =
            (tmr->tm_hour < 10 ? "0" : "") + std::to_string(tmr->tm_hour) + ":" + // Hour
            (tmr->tm_min < 10 ? "0" : "") + std::to_string(tmr->tm_min) + ":" +   // Min
            (tmr->tm_sec < 10 ? "0" : "") + std::to_string(tmr->tm_sec) + " - " + // Sec
            (tmr->tm_mday < 10 ? "0" : "") + std::to_string(tmr->tm_mday) + "/" + // Day
            (tmr->tm_mon < 10 ? "0" : "") + std::to_string(tmr->tm_mon) + "/" +   // Mon
            (tmr->tm_year < 10 ? "0" : "") + std::to_string(tmr->tm_year + 1900); // Year

        if (cpos != nullptr)
            *cpos = timestamp.size() + 3;

        if (color)
            return fmt::format("[{:s}] {:s}({:s}) {:s}\033[m\n",
                               timestamp.c_str(),
                               colors[m.lvl].c_str(), log_schar[m.lvl].c_str(), m.str.c_str());
        else
            return fmt::format("[{:s}] ({:s}) {:s}\n",
                               timestamp.c_str(),
                               log_schar[m.lvl].c_str(), m.str.c_str());
    }

    void StdOutSink::receive(LogMsg log)
    {
        if (log.lvl >= sink_lvl)
        {
            std::string s = format_log(log, true);
            fwrite(s.c_str(), sizeof(char), s.size(), stderr);
        }
    }

#if defined(_WIN32)
    const int colors_win[] = {FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
                          FOREGROUND_GREEN | FOREGROUND_BLUE,
                          FOREGROUND_GREEN,
                          FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
                          FOREGROUND_RED | FOREGROUND_INTENSITY,
                          BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY};

    int win_set_foreground_color(HANDLE &out_handle_,int attribs)
    {
        CONSOLE_SCREEN_BUFFER_INFO orig_buffer_info;
        if (!::GetConsoleScreenBufferInfo(out_handle_, &orig_buffer_info))
        {
            // just return white if failed getting console info
            return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        }

        // change only the foreground bits (lowest 4 bits)
        auto new_attribs = static_cast<int>(attribs) | (orig_buffer_info.wAttributes & 0xfff0);
        auto ignored = ::SetConsoleTextAttribute(out_handle_, static_cast<WORD>(new_attribs));
        (void)(ignored);
        return orig_buffer_info.wAttributes; // return orig attribs
    }

    void WinOutSink::receive(LogMsg log)
    {
        if (log.lvl >= sink_lvl)
        {
            int color_pos = 0;
            std::string s = format_log(log, false, &color_pos);
            std::string s1 = s.substr(0, color_pos);
            std::string s2 = s.substr(color_pos, s.size() - color_pos - 1);
            std::string s3 = s.substr(s.size() - 1, 1);
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            fwrite(s1.c_str(), sizeof(char), s1.size(), stderr);
            int rst = win_set_foreground_color(hConsole, colors_win[log.lvl]);
            fwrite(s2.c_str(), sizeof(char), s2.size(), stderr);
            SetConsoleTextAttribute(hConsole, rst);
            fwrite(s3.c_str(), sizeof(char), s3.size(), stderr);
        }
    }
#endif

    FileSink::FileSink(std::string path)
    {
        outf = std::ofstream(path);
    }

    FileSink::~FileSink()
    {
        outf.close();
    }

    void FileSink::receive(LogMsg log)
    {
        if (log.lvl >= sink_lvl)
        {
            std::string s = format_log(log, false);
            outf.write(s.c_str(), s.size());
            outf.flush(); // Too annoying when there's a segault not letting it flush
        }
    }

    void Logger::log(LogLevel lvl, std::string v)
    {
        LogMsg m;
        m.str = v;
        m.lvl = lvl;
        if (m.lvl >= logger_lvl)
            for (auto &l : sinks)
                l->receive(m);
    }

#ifdef __ANDROID__
    const android_LogPriority log_lvls_a[] = {
        ANDROID_LOG_VERBOSE,
        ANDROID_LOG_DEBUG,
        ANDROID_LOG_INFO,
        ANDROID_LOG_WARN,
        ANDROID_LOG_ERROR,
        ANDROID_LOG_FATAL,
    };

    void AndroidSink::receive(LogMsg log)
    {
        if (log.lvl >= sink_lvl)
        {
            std::string s = format_log(log, false);
            __android_log_print(log_lvls_a[log.lvl], ag_LogTag, "%s", log.str.c_str());
        }
    }
#endif

    void Logger::set_level(LogLevel lvl)
    {
        logger_lvl = lvl;
    }

    void Logger::add_sink(std::shared_ptr<LoggerSink> sink)
    {
        sinks.push_back(sink);
    }

    void Logger::del_sink(std::shared_ptr<LoggerSink> sink)
    {
        auto it = std::find_if(sinks.rbegin(), sinks.rend(), [&sink](std::shared_ptr<LoggerSink> &c)
                               { return sink.get() == c.get(); })
                      .base();
        if (it != sinks.end())
            sinks.erase(it);
    }
}

void initLogger()
{
    try
    {
// Initialize everything
#ifdef __ANDROID__
        console_sink = std::make_shared<slog::AndroidSink>();
#elif defined(_WIN32)
        console_sink = std::make_shared<slog::WinOutSink>();
#else
        console_sink = std::make_shared<slog::StdOutSink>();
#endif

        // logger = std::shared_ptr<spdlog::logger>(new spdlog::logger("SatDump", {console_sink}));
        logger = std::make_shared<slog::Logger>();
        logger->add_sink(console_sink);

        // Use a custom, nicer log pattern. No color in the file
        // console_sink->set_pattern("[%D - %T] %^(%L) %v%$");

        // Default log level
        console_sink->set_level(slog::LOG_TRACE);
        logger->set_level(slog::LOG_TRACE);
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
        exit(1);
    }
}

void initFileSink()
{
    try
    {
        file_sink = std::make_shared<slog::FileSink>("satdump.logs");
        logger->add_sink(file_sink);
        // file_sink->set_pattern("[%D - %T] (%L) %v");
        file_sink->set_level(slog::LOG_TRACE);
    }
    catch (std::exception &e)
    {
        std::cout << e.what() << std::endl;
        exit(1);
    }
}

void setConsoleLevel(slog::LogLevel level)
{
    // Just change our log level
    console_sink->set_level(level);
}