#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STOCK_PROFILE_SIZE 244u
#define STOCK_PROFILE_BLOB_SIZE 245u
#define STOCK_PROFILE_VERSION 0x10u

/* Explicit stock RV32 structure offsets (add one for the versioned blob).
 * No C ABI, packing, alignment, or host endianness is part of this API. */
enum {
    STOCK_PROFILE_NICKNAME = 0, STOCK_PROFILE_TITLE = 48,
    STOCK_PROFILE_INTRO = 88, STOCK_PROFILE_BATTERY = 176,
    STOCK_PROFILE_LEVEL = 180, STOCK_PROFILE_XP = 184,
    STOCK_PROFILE_XP_MAX = 188, STOCK_PROFILE_ONLINE = 192,
    STOCK_PROFILE_TOKEN = 196, STOCK_PROFILE_TOKEN_MAX = 200,
    STOCK_PROFILE_SLEEP_MIN = 204, STOCK_PROFILE_SCORE_TOTAL = 208,
    STOCK_PROFILE_SCORE_BEST = 212, STOCK_PROFILE_IMG_MODE = 216,
    STOCK_PROFILE_VOLUME = 220, STOCK_PROFILE_FLAG_224 = 224,
    STOCK_PROFILE_FLAG_225 = 225, STOCK_PROFILE_AVATAR_NAME = 226
};
typedef struct { uint8_t bytes[STOCK_PROFILE_SIZE]; } stock_profile_t;
typedef struct {
    bool nickname, title, intro, battery, progress, online, token, token_max;
    bool sleep_min, img_mode, avatar_name, volume, game_clear, time_changed;
    int64_t time;
} stock_profile_changes_t;

void stock_profile_defaults(stock_profile_t *profile);
/* Exact version/size pairs: 16/245, 15/213, 14/213, 13/197. Failure leaves
 * output untouched. Current version preserves ALL bytes, including padding,
 * malformed legacy strings and unknown flags224/225; never use unbounded
 * string operations on a decoded field. Migration zeroes new padding.
 * migrated is optional, cleared on failure; decoding itself never persists. */
bool stock_profile_decode(stock_profile_t *output, const uint8_t *blob,
                          size_t length, bool *migrated);
size_t stock_profile_encode(const stock_profile_t *profile, uint8_t *blob,
                             size_t capacity);
/* Bounds checked raw integer accessors; callers use the named int32 offsets.
 * get returns zero / set returns false for invalid arguments. */
int32_t stock_profile_get_i32(const stock_profile_t *profile, size_t offset);
bool stock_profile_set_i32(stock_profile_t *profile, size_t offset, int32_t value);

/* cJSON updater, separate translation unit. -1 invalid input, 0 no known
 * field, positive recognized-field count. Atomic: invalid/no-known input
 * leaves profile unchanged; changes is zeroed on either. game_clear/time
 * are commands, not immediately applied profile mutations. Avatar syntax
 * only is validated here; caller must validate built-in asset availability.
 * No persistence, dispatch, rendering, or device access occurs.
 * Safety deviations from stock: strict full JSON consumption, valid Unicode
 * scalar sequences only, reject nonfinite known numbers, saturate before
 * integer casts. See stock_profile_json.c. No BLE size limit is imposed here. */
int stock_profile_apply_json(stock_profile_t *profile, const char *json,
                              size_t length, stock_profile_changes_t *changes);
