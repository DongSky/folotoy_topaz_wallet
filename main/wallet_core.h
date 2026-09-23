#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WALLET_WIDTH 216u
#define WALLET_HEIGHT 256u
#define WALLET_MAX_PAGES 52u
#define WALLET_HEADER_SIZE 32u
#define WALLET_PAGE_IMAGE_SIZE 27712u
#define WALLET_PAGE_RECORD_SIZE 27716u
#define WALLET_MAX_SIZE (WALLET_HEADER_SIZE + WALLET_MAX_PAGES * WALLET_PAGE_RECORD_SIZE)
#define WALLET_KIND_COUNT 5u

typedef struct {
    uint16_t page_count;
    uint32_t total_size;
    uint32_t body_crc;
    uint64_t created_at;
} wallet_header_t;

// IEEE CRC32, with zlib-compatible incremental seed (initial seed is zero).
uint32_t wallet_crc32(uint32_t seed, const void *data, size_t length);
bool wallet_header_parse(const uint8_t *data, size_t length, wallet_header_t *out);
// Validates the four metadata bytes. Palette validation belongs to the reader.
bool wallet_page_valid(const uint8_t *metadata);

typedef enum {
    WALLET_KEY_UP, WALLET_KEY_DOWN, WALLET_KEY_OK, WALLET_KEY_BACK
} wallet_key_t;

typedef struct {
    uint8_t root;       // 0 card, 1 wallet, 2 connect
    uint8_t level;      // 0 root, 1 category/connect, 2 content
    uint8_t category;   // payment, crypto, assets
    uint16_t page;      // index within kind
    bool revealed;
} wallet_nav_t;

uint8_t wallet_nav_kind(const wallet_nav_t *nav);
void wallet_nav_input(wallet_nav_t *nav, wallet_key_t key,
                      const uint16_t counts[WALLET_KIND_COUNT]);
void wallet_nav_hide(wallet_nav_t *nav);
