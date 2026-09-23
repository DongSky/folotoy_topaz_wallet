#include "stock_profile.h"
#include <string.h>

int32_t stock_profile_get_i32(const stock_profile_t *profile, size_t offset)
{
    if (profile == NULL || offset > STOCK_PROFILE_SIZE - 4) return 0;
    const uint8_t *p = profile->bytes + offset;
    uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    /* Avoid implementation-defined unsigned-to-signed conversion. */
    return bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
}

bool stock_profile_set_i32(stock_profile_t *profile, size_t offset, int32_t value)
{
    if (profile == NULL || offset > STOCK_PROFILE_SIZE - 4) return false;
    uint32_t bits = (uint32_t)value;
    for (unsigned i = 0; i < 4; ++i) profile->bytes[offset + i] = (uint8_t)(bits >> (8 * i));
    return true;
}

void stock_profile_defaults(stock_profile_t *profile)
{
    if (profile == NULL) return;
    memset(profile, 0, sizeof(*profile));
    memcpy(profile->bytes + STOCK_PROFILE_NICKNAME, "FoloToy", 8);
    memcpy(profile->bytes + STOCK_PROFILE_TITLE, "NETRUNNER", 10);
    memcpy(profile->bytes + STOCK_PROFILE_AVATAR_NAME, "default", 8);
    stock_profile_set_i32(profile, STOCK_PROFILE_BATTERY, 82);
    stock_profile_set_i32(profile, STOCK_PROFILE_LEVEL, 1);
    stock_profile_set_i32(profile, STOCK_PROFILE_XP_MAX, 100);
    profile->bytes[STOCK_PROFILE_ONLINE] = 1;
    stock_profile_set_i32(profile, STOCK_PROFILE_TOKEN_MAX, 40000);
    stock_profile_set_i32(profile, STOCK_PROFILE_SLEEP_MIN, 5);
    stock_profile_set_i32(profile, STOCK_PROFILE_VOLUME, 95);
}

bool stock_profile_decode(stock_profile_t *output, const uint8_t *blob,
                          size_t length, bool *migrated)
{
    if (migrated) *migrated = false;
    if (output == NULL || blob == NULL || length == 0) return false;
    uint8_t version = blob[0];
    if (version == 16 && length == 245) {
        memmove(output->bytes, blob + 1, STOCK_PROFILE_SIZE);
        return true;
    }
    if (!((version == 13 && length == 197) ||
          ((version == 14 || version == 15) && length == 213))) return false;
    stock_profile_t converted;
    stock_profile_defaults(&converted);
    const uint8_t *old = blob + 1;
    memcpy(converted.bytes, old, 48 + 40 + 56);
    /* Old fields from battery onward moved by32 after intro grew56->85. */
    static const uint8_t old_i32[] = {144, 148, 152, 156, 164, 168, 172, 176, 180, 188};
    for (size_t i = 0; i < sizeof(old_i32); ++i)
        memcpy(converted.bytes + old_i32[i] + 32, old + old_i32[i], 4);
    converted.bytes[192] = old[160];
    converted.bytes[216] = old[184];
    converted.bytes[224] = old[192];
    converted.bytes[225] = old[193];
    if (version >= 14) memcpy(converted.bytes + 226, old + 194, 16);
    if (version <= 14 && stock_profile_get_i32(&converted, STOCK_PROFILE_SLEEP_MIN) == 10)
        stock_profile_set_i32(&converted, STOCK_PROFILE_SLEEP_MIN, 5);
    *output = converted;
    if (migrated) *migrated = true;
    return true;
}

size_t stock_profile_encode(const stock_profile_t *profile, uint8_t *blob,
                             size_t capacity)
{
    if (profile == NULL || blob == NULL || capacity < STOCK_PROFILE_BLOB_SIZE) return 0;
    memmove(blob + 1, profile->bytes, STOCK_PROFILE_SIZE);
    blob[0] = STOCK_PROFILE_VERSION;
    return STOCK_PROFILE_BLOB_SIZE;
}
