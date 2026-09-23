#include "stock_screenshot.h"
#include <string.h>

static void u16le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void u32le(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

size_t stock_screenshot_begin(uint16_t session, uint16_t sequence,
    uint16_t width, uint16_t height, uint8_t *output, size_t capacity)
{
    if (!output || capacity < 14 || !session || !width || !height ||
        width > 240 || height > 320) return 0;
    output[0] = 1;
    u16le(output + 1, session);
    u16le(output + 3, sequence);
    u16le(output + 5, width);
    u16le(output + 7, height);
    output[9] = 1;
    u32le(output + 10, (uint32_t)width * height * 2u);
    return 14;
}

size_t stock_screenshot_data(uint16_t session, uint16_t sequence,
    uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t offset,
    const uint8_t *pixels, size_t length, uint8_t *output, size_t capacity)
{
    if (!output || !pixels || !session || !width || !height || !length ||
        length > STOCK_SCREENSHOT_DATA_MAX || (length & 1u) || (offset & 1u) ||
        capacity < 17u + length || x >= 240 || y >= 320 ||
        width > 240u - x || height > 320u - y) return 0;
    uint32_t total = (uint32_t)width * height * 2u;
    if (offset > total || length > total - offset) return 0;
    output[0] = 2;
    u16le(output + 1, session);
    u16le(output + 3, sequence);
    u16le(output + 5, x);
    u16le(output + 7, y);
    u16le(output + 9, width);
    u16le(output + 11, height);
    u32le(output + 13, offset);
    memcpy(output + 17, pixels, length);
    return 17u + length;
}

size_t stock_screenshot_end(uint16_t session, uint16_t sequence,
    uint16_t data_packets, uint32_t total_bytes, uint32_t crc,
    uint8_t *output, size_t capacity)
{
    if (!output || capacity < 15 || !session) return 0;
    output[0] = 3;
    u16le(output + 1, session);
    u16le(output + 3, sequence);
    u16le(output + 5, data_packets);
    u32le(output + 7, total_bytes);
    u32le(output + 11, crc);
    return 15;
}

uint32_t stock_screenshot_crc32(uint32_t crc, const uint8_t *bytes, size_t length)
{
    if (!bytes) return crc;
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    return ~crc;
}
