#ifndef CRC8_H
#define CRC8_H

#include <Arduino.h>

// CRC-8-CCITT configuration
// Polynomial: 0x07 (x^8 + x^2 + x + 1)
// Initial value: 0x00
// Final XOR value: 0x00
// Reflects input: false
// Reflects output: false

#define CRC8_POLYNOMIAL 0x07
#define CRC8_INIT 0x00

// Enable CRC-8 implementation with optional lookup table for faster computation.
#define CRC8_USE_TABLE

/**
 * Update CRC-8 checksum incrementally.
 *
 * @param crc Current CRC value (use CRC8_INIT for first call)
 * @param data Pointer to data bytes (must not be null if length > 0)
 * @param length Number of bytes to process
 * @return Updated CRC-8 value
 */
uint8_t crc8_update(uint8_t crc, const uint8_t* data, uint8_t length);

/**
 * Calculate CRC-8 checksum over a single data buffer.
 *
 * @param data Pointer to data bytes (must not be null if length > 0)
 * @param length Number of bytes to process
 * @return CRC-8 checksum value
 */
uint8_t crc8_checksum(const uint8_t* data, uint8_t length);


/**
 * Update CRC-8 checksum incrementally for a single byte of data.
 *
 * @param crc Current CRC value (use CRC8_INIT for first call)
 * @param data Single byte of data to process
 * @return Updated CRC-8 value
 */

#if defined(CRC8_USE_TABLE)
extern const uint8_t crc8_table[];

inline uint8_t crc8_update(uint8_t crc, const uint8_t data) {
    return crc8_table[crc ^ data];
}
#else

inline uint8_t crc8_update(uint8_t crc, const uint8_t data) {
    crc ^= data;
    for (uint8_t k = 0; k < 8; k++) {
        crc = crc & 0x80 ? (crc << 1) ^ CRC8_POLYNOMIAL : crc << 1;
    }
    return crc;
}

#endif

#endif // CRC8_H
