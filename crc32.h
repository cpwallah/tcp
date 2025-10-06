#ifndef CRC32_H // Prevents multiple inclusions of this header file
#define CRC32_H // Defines the header guard macro

#include <cstdint> // Includes standard integer types like uint32_t and uint8_t

class CRC32 // Defines a class to encapsulate CRC32 checksum functionality
{
public: // Public access specifier for the class members
    static uint32_t update(uint32_t crc, const char *data, size_t length) // Static method to compute CRC32 checksum
    {
        crc = ~crc; // Inverts the input CRC value (bitwise NOT) to initialize the computation
        for (size_t i = 0; i < length; ++i) // Iterates over each byte in the input data
        {
            crc ^= static_cast<uint8_t>(data[i]); // XORs the current byte with the CRC value
            for (int j = 0; j < 8; ++j) // Processes each bit of the current byte
            {
                if (crc & 1) // Checks if the least significant bit of CRC is 1
                    crc = (crc >> 1) ^ 0xEDB88320; // Right-shifts CRC and XORs with the CRC32 polynomial
                else // If the least significant bit is 0
                    crc >>= 1; // Right-shifts CRC by one bit
            }
        }
        return ~crc; // Returns the inverted CRC value as the final checksum
    }
};

#endif // Ends the header guard
