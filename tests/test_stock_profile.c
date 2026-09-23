#include "stock_profile.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

int main(void)
{
    stock_profile_t p;
    stock_profile_defaults(&p);
    assert(!strcmp((char *)p.bytes, "FoloToy"));
    assert(!strcmp((char *)p.bytes + 48, "NETRUNNER"));
    assert(p.bytes[176] == 82 && p.bytes[180] == 1 && p.bytes[192] == 1);
    assert(p.bytes[200] == 0x40 && p.bytes[201] == 0x9c);
    assert(p.bytes[204] == 5 && p.bytes[220] == 95);
    assert(!strcmp((char *)p.bytes + 226, "default"));
    for (size_t i = 173; i < 176; ++i) assert(p.bytes[i] == 0);
    uint8_t blob[245];
    assert(stock_profile_encode(&p, blob, sizeof(blob)) == 245);
    assert(blob[0] == 16 && blob[177] == 82 && blob[221] == 95);

    /* Independent synthetic v16 bytes: preserve unknown flags/padding and
     * arbitrary negative i32 bit patterns without assuming native structs. */
    for (size_t i = 1; i < sizeof(blob); ++i) blob[i] = (uint8_t)(i * 37);
    blob[0] = 16; put32(blob + 177, 0x80000000u);
    bool migrated = true;
    assert(stock_profile_decode(&p, blob, sizeof(blob), &migrated) && !migrated);
    assert(stock_profile_get_i32(&p, 176) == INT32_MIN);
    uint8_t roundtrip[245];
    assert(stock_profile_encode(&p, roundtrip, 245) == 245);
    assert(!memcmp(blob, roundtrip, 245));
    assert(stock_profile_set_i32(&p, 212, INT32_MAX));
    assert(stock_profile_get_i32(&p, 212) == INT32_MAX);
    assert(stock_profile_set_i32(&p, 212, -123456));
    assert(stock_profile_get_i32(&p, 212) == -123456);
    assert(!stock_profile_set_i32(&p, SIZE_MAX, 0));
    assert(stock_profile_get_i32(&p, SIZE_MAX) == 0);

    for (uint8_t version = 13; version <= 15; ++version) {
        uint8_t old[213] = {0}; old[0] = version;
        memcpy(old + 1, "Old name", 9); memcpy(old + 49, "Old title", 10);
        memset(old + 89, 'I', 55);
        put32(old + 145, 27); put32(old + 149, 999);
        put32(old + 153, 123); put32(old + 157, 456);
        old[161] = 1; put32(old + 165, 777); put32(old + 169, 888);
        put32(old + 173, 10); put32(old + 177, 9876); put32(old + 181, 4321);
        old[185] = 1; put32(old + 189, 66); old[193] = 0xa3; old[194] = 0xb4;
        if (version > 13) memcpy(old + 195, "avatar-old", 11);
        assert(stock_profile_decode(&p, old, version == 13 ? 197 : 213, &migrated));
        assert(migrated && !strcmp((char *)p.bytes, "Old name"));
        assert(p.bytes[143] == 0 && p.bytes[172] == 0);
        assert(stock_profile_get_i32(&p, 176) == 27);
        assert(stock_profile_get_i32(&p, 180) == 999);
        assert(stock_profile_get_i32(&p, 184) == 123);
        assert(stock_profile_get_i32(&p, 188) == 456 && p.bytes[192] == 1);
        assert(stock_profile_get_i32(&p, 196) == 777);
        assert(stock_profile_get_i32(&p, 200) == 888);
        assert(stock_profile_get_i32(&p, 204) == (version == 15 ? 10 : 5));
        assert(stock_profile_get_i32(&p, 208) == 9876);
        assert(stock_profile_get_i32(&p, 212) == 4321);
        assert(p.bytes[216] == 1 && stock_profile_get_i32(&p, 220) == 66);
        assert(p.bytes[224] == 0xa3 && p.bytes[225] == 0xb4);
        assert(!strcmp((char *)p.bytes + 226, version == 13 ? "default" : "avatar-old"));
        assert(p.bytes[193] == 0 && p.bytes[217] == 0 && p.bytes[242] == 0);
        stock_profile_t before = p;
        assert(!stock_profile_decode(&p, old, 212, &migrated));
        assert(!migrated && !memcmp(&p, &before, sizeof(p)));
    }
    stock_profile_t before = p;
    for (size_t n = 0; n < 245; ++n) assert(!stock_profile_decode(&p, blob, n, NULL));
    blob[0] = 17; assert(!stock_profile_decode(&p, blob, 245, NULL));
    assert(!memcmp(&p, &before, sizeof(p)));
    assert(stock_profile_encode(&p, blob, 244) == 0);
    assert(!stock_profile_decode(NULL, blob, 245, NULL));
    assert(!stock_profile_decode(&p, NULL, 245, NULL));
    assert(stock_profile_encode(NULL, blob, 245) == 0);
    assert(stock_profile_encode(&p, NULL, 245) == 0);
    puts("Stock profile codec/migration: PASS");
}
