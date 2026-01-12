/*
MyTrainControl - CAN Protokoll Hilfsfunktionen
Basierend auf RailControl von Teddy / Dominik Mahrer
*/

#pragma once

#include <cstdint>

namespace Utils
{
    // Konvertiere Big-Endian Byte-Array in Integer
    inline uint32_t DataBigEndianToInt(const unsigned char* data)
    {
        return (static_cast<uint32_t>(data[0]) << 24)
             | (static_cast<uint32_t>(data[1]) << 16)
             | (static_cast<uint32_t>(data[2]) << 8)
             | static_cast<uint32_t>(data[3]);
    }

    inline uint16_t DataBigEndianToShort(const unsigned char* data)
    {
        return (static_cast<uint16_t>(data[0]) << 8)
             | static_cast<uint16_t>(data[1]);
    }

    // Konvertiere Integer in Big-Endian Byte-Array
    inline void IntToDataBigEndian(const uint32_t value, unsigned char* data)
    {
        data[0] = static_cast<unsigned char>((value >> 24) & 0xFF);
        data[1] = static_cast<unsigned char>((value >> 16) & 0xFF);
        data[2] = static_cast<unsigned char>((value >> 8) & 0xFF);
        data[3] = static_cast<unsigned char>(value & 0xFF);
    }

    inline void ShortToDataBigEndian(const uint16_t value, unsigned char* data)
    {
        data[0] = static_cast<unsigned char>((value >> 8) & 0xFF);
        data[1] = static_cast<unsigned char>(value & 0xFF);
    }

    // Hex-String zu Integer
    inline uint32_t HexToInteger(const std::string& hex, uint32_t defaultValue = 0)
    {
        try
        {
            return static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
        }
        catch (...)
        {
            return defaultValue;
        }
    }
}

