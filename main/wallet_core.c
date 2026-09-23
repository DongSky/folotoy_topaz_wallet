#include "wallet_core.h"
#include <string.h>

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t wallet_crc32(uint32_t seed, const void *data, size_t length)
{
    uint32_t crc = ~seed;
    const uint8_t *bytes = data;
    for (size_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

bool wallet_header_parse(const uint8_t *data, size_t length, wallet_header_t *out)
{
    if (!data || !out || length < WALLET_HEADER_SIZE ||
        memcmp(data, "PCW1", 4) || read16(data + 4) != 1 ||
        read16(data + 8) != WALLET_WIDTH || read16(data + 10) != WALLET_HEIGHT ||
        read32(data + 28) != 0) return false;
    uint16_t count = read16(data + 6);
    if (!count || count > WALLET_MAX_PAGES) return false;
    uint32_t size = WALLET_HEADER_SIZE + (uint32_t)count * WALLET_PAGE_RECORD_SIZE;
    if (read32(data + 12) != size) return false;
    *out = (wallet_header_t){
        .page_count = count,
        .total_size = size,
        .body_crc = read32(data + 24),
        .created_at = (uint64_t)read32(data + 16) | ((uint64_t)read32(data + 20) << 32)
    };
    return true;
}

bool wallet_page_valid(const uint8_t *metadata)
{
    return metadata && metadata[0] < WALLET_KIND_COUNT &&
           (metadata[1] & ~1u) == 0 && metadata[2] == 0 && metadata[3] == 0 &&
           (metadata[0] != 0 || metadata[1] == 0) &&
           (metadata[0] != 4 || (metadata[1] & 1u));
}

uint8_t wallet_nav_kind(const wallet_nav_t *nav)
{
    if (nav->root == 0) return nav->level == 2 ? 1 : 0;
    if (nav->root == 1 && nav->level == 2) return nav->category + 2;
    return 0;
}

void wallet_nav_hide(wallet_nav_t *nav)
{
    nav->revealed = false;
}

static unsigned wrap(unsigned value, unsigned count, bool forward)
{
    if (!count) return 0;
    return forward ? (value + 1) % count : (value + count - 1) % count;
}

void wallet_nav_input(wallet_nav_t *nav, wallet_key_t key,
                      const uint16_t counts[WALLET_KIND_COUNT])
{
    if (key == WALLET_KEY_BACK) {
        nav->revealed = false;
        nav->page = 0;
        nav->level = (nav->level == 2 && nav->root == 1) ? 1 : 0;
        return;
    }
    if (key == WALLET_KEY_UP || key == WALLET_KEY_DOWN) {
        bool forward = key == WALLET_KEY_DOWN;
        if (nav->level == 0) {
            nav->root = wrap(nav->root, 3, forward);
            nav->revealed = false;
        } else if (nav->root == 1 && nav->level == 1) {
            nav->category = wrap(nav->category, 3, forward);
        } else if (nav->level == 2) {
            nav->page = wrap(nav->page, counts[wallet_nav_kind(nav)], forward);
        }
        return;
    }
    if (key != WALLET_KEY_OK) return;
    if (nav->level == 0) {
        nav->page = 0;
        nav->level = nav->root == 0 ? 2 : 1;
    } else if (nav->root == 1 && nav->level == 1) {
        nav->page = 0;
        nav->level = 2;
        nav->revealed = false;
    } else if (nav->level == 2) {
        nav->revealed = !nav->revealed;
    }
}
