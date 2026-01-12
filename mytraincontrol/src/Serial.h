/*
MyTrainControl - Serielle Schnittstelle
Basierend auf RailControl von Teddy / Dominik Mahrer
*/

#pragma once

#include <string>
#include <mutex>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

#include "Logger.h"

namespace Network
{
    class Serial
    {
    public:
        Serial() = delete;
        Serial(const Serial&) = delete;
        Serial& operator=(const Serial&) = delete;

        Serial(Logger::Logger* logger,
               const std::string& tty,
               const unsigned int dataSpeed,
               const unsigned char dataBits = 8,
               const char parity = 'N',
               const unsigned char stopBits = 1,
               const bool hardwareFlowControl = false)
            : logger(logger),
              tty(tty),
              dataSpeed(dataSpeed),
              dataBits(dataBits),
              parity(parity),
              stopBits(stopBits),
              hardwareFlowControl(hardwareFlowControl),
              fileHandle(-1)
        {
            Init();
        }

        ~Serial()
        {
            Close();
        }

        void ReInit()
        {
            Close();
            Init();
        }

        bool IsConnected() const
        {
            return fileHandle != -1;
        }

        void ClearBuffers()
        {
            if (IsConnected())
            {
                tcflush(fileHandle, TCIOFLUSH);
            }
        }

        ssize_t Send(const unsigned char* data, const size_t size)
        {
            if (!IsConnected())
            {
                return 0;
            }
            std::lock_guard<std::mutex> guard(fileHandleMutex);
            return write(fileHandle, data, size);
        }

        ssize_t Receive(unsigned char* data, const size_t maxData,
                        const unsigned int timeoutS = 0,
                        const unsigned int timeoutUS = 100000)
        {
            if (!IsConnected())
            {
                return -1;
            }
            fd_set set;
            FD_ZERO(&set);
            FD_SET(fileHandle, &set);
            struct timeval tvTimeout;
            tvTimeout.tv_sec = timeoutS;
            tvTimeout.tv_usec = timeoutUS;

            ssize_t ret = select(FD_SETSIZE, &set, NULL, NULL, &tvTimeout);
            if (ret <= 0)
            {
                return -1;
            }
            ret = read(fileHandle, data, maxData);
            if (ret <= 0)
            {
                return -1;
            }
            return ret;
        }

        ssize_t ReceiveExact(unsigned char* data, const size_t length,
                             const unsigned int timeoutS = 0,
                             const unsigned int timeoutUS = 100000)
        {
            size_t actualSize = 0;
            while (actualSize < length)
            {
                ssize_t ret = Receive(data + actualSize, length - actualSize,
                                       timeoutS, timeoutUS);
                if (ret <= 0)
                {
                    return actualSize;
                }
                actualSize += ret;
            }
            return actualSize;
        }

    private:
        void Init()
        {
            fileHandle = open(tty.c_str(), O_RDWR | O_NOCTTY);
            if (!IsConnected())
            {
                logger->Error("Kann serielle Schnittstelle nicht öffnen: " + tty);
                return;
            }

            struct termios options;
            options.c_cflag = 0;
            options.c_cc[VMIN] = 1;
            options.c_cc[VTIME] = 0;
            options.c_lflag = 0;
            options.c_iflag = 0;
            options.c_oflag = 0;
            cfsetispeed(&options, dataSpeed);
            cfsetospeed(&options, dataSpeed);

            switch (dataBits)
            {
                case 5: options.c_cflag |= CS5; break;
                case 6: options.c_cflag |= CS6; break;
                case 7: options.c_cflag |= CS7; break;
                case 8:
                default: options.c_cflag |= CS8; break;
            }

            if (stopBits == 2)
            {
                options.c_cflag |= CSTOPB;
            }

            switch (parity)
            {
                case 'E':
                case 'e':
                    options.c_cflag |= PARENB;
                    break;
                case 'O':
                case 'o':
                    options.c_cflag |= PARENB;
                    options.c_cflag |= PARODD;
                    break;
            }

            if (hardwareFlowControl)
            {
                options.c_cflag |= CRTSCTS;
            }
            options.c_cflag |= CLOCAL;
            options.c_cflag |= CREAD;
            tcsetattr(fileHandle, TCSANOW, &options);

            ClearBuffers();
            logger->Info("Serielle Verbindung hergestellt: " + tty);
        }

        void Close()
        {
            if (!IsConnected())
            {
                return;
            }
            close(fileHandle);
            fileHandle = -1;
        }

        Logger::Logger* logger;
        const std::string tty;
        const unsigned int dataSpeed;
        const unsigned char dataBits;
        const char parity;
        const unsigned char stopBits;
        const bool hardwareFlowControl;
        int fileHandle;
        mutable std::mutex fileHandleMutex;
    };
}

