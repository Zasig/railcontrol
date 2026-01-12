/*
MyTrainControl - Einfacher Logger
*/

#pragma once

#include <iostream>
#include <iomanip>
#include <string>
#include <ctime>

namespace Logger
{
    enum LogLevel
    {
        LogLevelDebug,
        LogLevelInfo,
        LogLevelWarning,
        LogLevelError
    };

    class Logger
    {
    public:
        Logger(const std::string& name, LogLevel level = LogLevelInfo)
            : name(name), level(level)
        {
        }

        void SetLevel(LogLevel newLevel)
        {
            level = newLevel;
        }

        void Debug(const std::string& message)
        {
            if (level <= LogLevelDebug)
            {
                Log("DEBUG", message);
            }
        }

        void Info(const std::string& message)
        {
            if (level <= LogLevelInfo)
            {
                Log("INFO", message);
            }
        }

        void Warning(const std::string& message)
        {
            if (level <= LogLevelWarning)
            {
                Log("WARN", message);
            }
        }

        void Error(const std::string& message)
        {
            if (level <= LogLevelError)
            {
                Log("ERROR", message);
            }
        }

        void HexOut(const unsigned char* data, size_t length)
        {
            if (level <= LogLevelDebug)
            {
                std::cout << "[" << GetTimeStamp() << "] [" << name << "] [DEBUG] TX: ";
                for (size_t i = 0; i < length; ++i)
                {
                    std::cout << std::hex << std::setw(2) << std::setfill('0')
                              << static_cast<int>(data[i]) << " ";
                }
                std::cout << std::dec << std::endl;
            }
        }

        void HexIn(const unsigned char* data, size_t length)
        {
            if (level <= LogLevelDebug)
            {
                std::cout << "[" << GetTimeStamp() << "] [" << name << "] [DEBUG] RX: ";
                for (size_t i = 0; i < length; ++i)
                {
                    std::cout << std::hex << std::setw(2) << std::setfill('0')
                              << static_cast<int>(data[i]) << " ";
                }
                std::cout << std::dec << std::endl;
            }
        }

    private:
        std::string name;
        LogLevel level;

        void Log(const std::string& levelStr, const std::string& message)
        {
            std::cout << "[" << GetTimeStamp() << "] [" << name << "] ["
                      << levelStr << "] " << message << std::endl;
        }

        std::string GetTimeStamp()
        {
            time_t now = time(nullptr);
            struct tm* timeinfo = localtime(&now);
            char buffer[20];
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
            return std::string(buffer);
        }
    };
}

