#pragma once
#include <stddef.h>
#include <stdint.h>

/* Independent implementation of stock 1.0.3 serializer VA0x42044800 and CRC
 * VA0x42044b8e. Evidence: build/card-wallet-redesign/reference/protocol-findings.txt.
 * Raw ...0014 notification bytes, WITHOUT command envelope. No scheduling,
 * ACK/retry state or screenshot pixel-format conversion is implemented here;
 * these encoders alone do not establish mini-program compatibility. */
#define STOCK_SCREENSHOT_DATA_MAX 500u
#define STOCK_SCREENSHOT_PACKET_MAX 517u
/* All integers on wire are little-endian; format byte is 1. Caller supplies
 * native stock-format 2-byte pixels; byte/channel ordering needs integration
 * verification. Encoders return 0 for invalid input or insufficient capacity. */
size_t stock_screenshot_begin(uint16_t session, uint16_t sequence,
    uint16_t width, uint16_t height, uint8_t *output, size_t capacity);
size_t stock_screenshot_data(uint16_t session, uint16_t sequence,
    uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t offset,
    const uint8_t *pixels, size_t length, uint8_t *output, size_t capacity);
size_t stock_screenshot_end(uint16_t session, uint16_t sequence,
    uint16_t data_packets, uint32_t total_bytes, uint32_t crc,
    uint8_t *output, size_t capacity);
/* Initial crc=0; accepts its previous return value for incremental updates.
 * NULL bytes are valid only for zero length; invalid NULL leaves crc unchanged. */
uint32_t stock_screenshot_crc32(uint32_t crc, const uint8_t *bytes, size_t length);
