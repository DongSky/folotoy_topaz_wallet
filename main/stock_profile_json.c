#include "stock_profile.h"
#include "cJSON.h"
#include <math.h>
#include <string.h>

/* Uses ESP-IDF5.5.3's MIT-licensed bundled cJSON1.7.19; no parser code is
 * vendored here. Verified submodule revision c859b25da02955fef659d658b8f324b5cde87be3.
 * Link this translation unit with IDF's json component when integrating. */

/* Stock parser0x42042c40 uses case-sensitive object lookup, first duplicate
 * wins, and counts recognized fields even when their values are unchanged.
 * Safety differences: consume the full input, reject malformed UTF-8 instead
 * of copying/skipping invalid sequences, reject nonfinite known numbers,
 * and saturate enormous finite values before integer conversion. These do
 * not change normal finite, valid UTF-8 stock commands. cJSON decodes escaped
 * U+0000 as a C-string terminator, matching the stock library/helper behavior.
 * Asset existence and game/time side effects belong to the caller. */

static size_t utf8_width(const uint8_t *s, size_t remaining)
{
    if (!remaining) return 0;
    uint8_t a = s[0];
    if (a < 0x80) return 1;
    size_t n = a >= 0xc2 && a <= 0xdf ? 2 :
               a >= 0xe0 && a <= 0xef ? 3 :
               a >= 0xf0 && a <= 0xf4 ? 4 : 0;
    if (!n || remaining < n) return 0;
    for (size_t i = 1; i < n; ++i) if ((s[i] & 0xc0) != 0x80) return 0;
    if ((a == 0xe0 && s[1] < 0xa0) || (a == 0xed && s[1] >= 0xa0) ||
        (a == 0xf0 && s[1] < 0x90) || (a == 0xf4 && s[1] >= 0x90)) return 0;
    return n;
}

static bool valid_utf8(const char *s, size_t length)
{
    for (size_t i = 0; i < length;) {
        size_t n = utf8_width((const uint8_t *)s + i, length - i);
        if (!n || s[i] == 0) return false;
        i += n;
    }
    return true;
}

/* cJSON's default 1000-level recursion limit is unsafe on an 8 KiB task.
 * Stock fields are flat; allow modest unknown structures without accepting
 * recursive stack exhaustion. Strings/escaped quotes do not affect depth. */
static bool bounded_depth(const char *json, size_t length)
{
    unsigned depth = 0;
    bool string = false, escape = false;
    for (size_t i = 0; i < length; ++i) {
        char ch = json[i];
        if (string) {
            if (escape) escape = false;
            else if (ch == '\\') escape = true;
            else if (ch == '"') string = false;
        } else if (ch == '"') string = true;
        else if (ch == '[' || ch == '{') {
            if (++depth > 8) return false;
        } else if (ch == ']' || ch == '}') {
            if (!depth) return false;
            --depth;
        }
    }
    return !string && depth == 0;
}

static void copy_string(uint8_t *out, size_t capacity, const char *s, size_t max_chars)
{
    size_t used = 0, chars = 0, length = strlen(s);
    while (used < length && chars < max_chars) {
        size_t n = utf8_width((const uint8_t *)s + used, length - used);
        if (!n || n > capacity - 1 - used) break;
        used += n;
        ++chars;
    }
    /* Stock does not clear the unused tail of an existing string field. */
    memcpy(out, s, used);
    out[used] = 0;
}

static const cJSON *item(const cJSON *object, const char *key)
{ return cJSON_GetObjectItemCaseSensitive(object, key); }

static int update_string(stock_profile_t *p, const cJSON *object,
                          const char *key, const char *alias, size_t offset,
                          size_t capacity, size_t max_chars, bool *changed)
{
    const cJSON *v = item(object, key);
    if (!cJSON_IsString(v) && alias) v = item(object, alias);
    if (!cJSON_IsString(v) || !v->valuestring) return 0;
    if (!valid_utf8(v->valuestring, strlen(v->valuestring))) return -1;
    copy_string(p->bytes + offset, capacity, v->valuestring, max_chars);
    *changed = true;
    return 1;
}

static bool avatar_name_valid(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n >= 16) return false;
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') ||
              s[i] == '_' || s[i] == '-')) return false;
    return true;
}

static int32_t bounded_i32(double value, int32_t minimum, int32_t maximum)
{
    if (value <= minimum) return minimum;
    if (value >= maximum) return maximum;
    return (int32_t)value; /* finite and representable, truncates toward zero */
}

int stock_profile_apply_json(stock_profile_t *profile, const char *json,
                              size_t length, stock_profile_changes_t *changes)
{
    if (changes) memset(changes, 0, sizeof(*changes));
    if (!profile || !json || !length) return -1;
    if (json[length - 1] == 0) --length; /* allow an explicitly included final NUL */
    if (!length || !valid_utf8(json, length)) return -1;
    const char *end = NULL;
    if (!bounded_depth(json, length)) return -1;
    cJSON *object = cJSON_ParseWithLengthOpts(json, length, &end, 0);
    if (!object) return -1;
    if (!cJSON_IsObject(object) || !end || end < json || end > json + length) {
        cJSON_Delete(object);
        return -1;
    }
    while (end < json + length && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    if (end != json + length) { cJSON_Delete(object); return -1; }

    stock_profile_t next = *profile;
    stock_profile_changes_t flags = {0};
    int count = 0;
    struct string_rule {
        const char *key, *alias;
        size_t offset, capacity, max_chars;
        bool *flag;
    } strings[] = {
        {"nickname", "name", 0, 48, SIZE_MAX, &flags.nickname},
        {"title", "role", 48, 40, SIZE_MAX, &flags.title},
        {"intro", "subtitle", 88, 85, 28, &flags.intro}
    };
    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); ++i) {
        struct string_rule *r = &strings[i];
        int n = update_string(&next, object, r->key, r->alias, r->offset,
                                r->capacity, r->max_chars, r->flag);
        if (n < 0) goto invalid;
        count += n;
    }
    const cJSON *v = item(object, "avatar_name");
    if (cJSON_IsString(v) && v->valuestring && avatar_name_valid(v->valuestring)) {
        copy_string(next.bytes + STOCK_PROFILE_AVATAR_NAME, 16, v->valuestring, 15);
        flags.avatar_name = true;
        ++count;
    }
    struct number_rule {
        const char *key;
        size_t offset;
        int32_t minimum, maximum;
        bool *flag;
    } numbers[] = {
        {"battery", STOCK_PROFILE_BATTERY, 0, 100, &flags.battery},
        {"level", STOCK_PROFILE_LEVEL, 0, 999, &flags.progress},
        {"xp", STOCK_PROFILE_XP, 0, INT32_MAX, &flags.progress},
        {"xp_max", STOCK_PROFILE_XP_MAX, 1, INT32_MAX, &flags.progress},
        {"token", STOCK_PROFILE_TOKEN, 0, 1000000000, &flags.token},
        {"token_max", STOCK_PROFILE_TOKEN_MAX, 1, 1000000000, &flags.token_max},
        {"sleep_min", STOCK_PROFILE_SLEEP_MIN, 0, 1440, &flags.sleep_min},
        {"volume", STOCK_PROFILE_VOLUME, 0, 100, &flags.volume}
    };
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        struct number_rule *r = &numbers[i];
        v = item(object, r->key);
        if (!cJSON_IsNumber(v)) continue;
        if (!isfinite(v->valuedouble)) goto invalid;
        stock_profile_set_i32(&next, r->offset, bounded_i32(v->valuedouble, r->minimum, r->maximum));
        *r->flag = true;
        ++count;
    }
    v = item(object, "online");
    if (cJSON_IsBool(v)) {
        next.bytes[STOCK_PROFILE_ONLINE] = cJSON_IsTrue(v) ? 1 : 0;
        flags.online = true;
        ++count;
    }
    v = item(object, "time");
    if (cJSON_IsNumber(v)) {
        if (!isfinite(v->valuedouble)) goto invalid;
        if (v->valuedouble > 0) {
            /* INT64_MAX rounds to2^63 as double: use an exclusive bound. */
            flags.time = v->valuedouble >= 0x1p63 ? INT64_MAX : (int64_t)v->valuedouble;
            flags.time_changed = true;
            ++count;
        }
    }
    v = item(object, "game_clear");
    if (cJSON_IsNumber(v) && !isfinite(v->valuedouble)) goto invalid;
    if (cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valuedouble != 0)) {
        flags.game_clear = true;
        ++count;
    }
    v = item(object, "img_mode");
    if (cJSON_IsString(v) && v->valuestring &&
        (!strcmp(v->valuestring, "fullscreen") || !strcmp(v->valuestring, "avatar"))) {
        next.bytes[STOCK_PROFILE_IMG_MODE] = !strcmp(v->valuestring, "fullscreen") ? 1 : 0;
        flags.img_mode = true;
        ++count;
    }
    cJSON_Delete(object);
    if (count) {
        *profile = next;
        if (changes) *changes = flags;
    }
    return count;
invalid:
    cJSON_Delete(object);
    return -1;
}
