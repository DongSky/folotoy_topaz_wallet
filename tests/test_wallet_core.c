#include "wallet_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (8 * i));
}

static void test_header(void)
{
    uint8_t h[32] = {'P','C','W','1',1,0,1,0,216,0,0,1};
    wallet_header_t parsed;
    put32(h + 12, 27748);
    h[16] = 42;
    assert(wallet_header_parse(h, sizeof(h), &parsed));
    assert(parsed.page_count == 1 && parsed.total_size == 27748 && parsed.created_at == 42);
    assert(!wallet_header_parse(h, 31, &parsed));
    h[6] = 52;
    put32(h + 12, 32 + 52 * 27716);
    assert(wallet_header_parse(h, 32, &parsed));
    h[6] = 53;
    put32(h + 12, 32 + 53 * 27716);
    assert(!wallet_header_parse(h, 32, &parsed));
    h[6] = 1;
    put32(h + 12, 0xffffffff);
    assert(!wallet_header_parse(h, 32, &parsed));
    put32(h + 12, 27748);
    h[8] = 215;
    assert(!wallet_header_parse(h, 32, &parsed));
    h[8] = 216;
    h[28] = 1;
    assert(!wallet_header_parse(h, 32, &parsed));
    h[28] = 0;
    h[4] = 2;
    assert(!wallet_header_parse(h, 32, &parsed));
    assert(!wallet_header_parse(NULL, 32, &parsed));
}

static void test_pages(void)
{
    uint8_t m[4] = {4,1,0,0};
    assert(wallet_page_valid(m));
    m[1] = 0;
    assert(!wallet_page_valid(m));
    m[0] = 0;
    assert(wallet_page_valid(m));
    m[1] = 1;
    assert(!wallet_page_valid(m)); // Root card has no reveal action.
    m[1] = 2;
    assert(!wallet_page_valid(m));
    m[1] = 0;
    m[2] = 1;
    assert(!wallet_page_valid(m));
    m[2] = 0;
    m[0] = 5;
    assert(!wallet_page_valid(m));
}

static void test_navigation(void)
{
    wallet_nav_t n = {0};
    const uint16_t counts[5] = {1,2,2,3,4};
    wallet_nav_input(&n, WALLET_KEY_DOWN, counts);
    assert(n.root == 1 && n.level == 0);
    wallet_nav_input(&n, WALLET_KEY_OK, counts);
    assert(n.level == 1 && n.category == 0);
    wallet_nav_input(&n, WALLET_KEY_UP, counts);
    assert(n.category == 2);
    wallet_nav_input(&n, WALLET_KEY_OK, counts);
    assert(n.level == 2 && wallet_nav_kind(&n) == 4 && !n.revealed);
    wallet_nav_input(&n, WALLET_KEY_OK, counts);
    assert(n.revealed);
    wallet_nav_input(&n, WALLET_KEY_DOWN, counts);
    assert(n.page == 1 && n.revealed);
    wallet_nav_input(&n, WALLET_KEY_BACK, counts);
    assert(n.level == 1 && !n.revealed && n.page == 0);
    wallet_nav_input(&n, WALLET_KEY_BACK, counts);
    assert(n.level == 0);
    wallet_nav_input(&n, WALLET_KEY_UP, counts);
    assert(n.root == 0);
    wallet_nav_input(&n, WALLET_KEY_OK, counts);
    assert(n.level == 2 && wallet_nav_kind(&n) == 1);
    wallet_nav_input(&n, WALLET_KEY_UP, counts);
    assert(n.page == 1);
    wallet_nav_input(&n, WALLET_KEY_BACK, counts);
    assert(n.level == 0 && n.root == 0);
    wallet_nav_input(&n, WALLET_KEY_UP, counts);
    assert(n.root == 2);
    wallet_nav_input(&n, WALLET_KEY_OK, counts);
    assert(n.level == 1);
    const uint16_t empty[5] = {0};
    n = (wallet_nav_t){.root=1,.level=2,.category=2};
    wallet_nav_input(&n, WALLET_KEY_DOWN, empty);
    assert(n.page == 0);
    n.revealed = true;
    wallet_nav_hide(&n);
    assert(!n.revealed);
}

int main(void)
{
    assert(wallet_crc32(0, "123456789", 9) == 0xcbf43926u);
    uint32_t crc = wallet_crc32(0, "1234", 4);
    assert(wallet_crc32(crc, "56789", 5) == 0xcbf43926u);
    assert(wallet_crc32(0, "", 0) == 0);
    test_header();
    test_pages();
    test_navigation();
    puts("Wallet core: PASS");
    return 0;
}
