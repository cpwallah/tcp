#ifndef CRC32_H
#define CRC32_H

#include <cstdint>

class CRC32
{
public:
    static uint32_t update(uint32_t crc, const char *data, size_t length)
    {
        crc = ~crc;
        for (size_t i = 0; i < length; ++i)
        {
            crc ^= static_cast<uint8_t>(data[i]);
            for (int j = 0; j < 8; ++j)
            {
                if (crc & 1)
                    crc = (crc >> 1) ^ 0xEDB88320;
                else
                    crc >>= 1;
            }
        }
        return ~crc;
    }
};

#endif