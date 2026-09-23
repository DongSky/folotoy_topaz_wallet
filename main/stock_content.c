#include "stock_content.h"
#include "stock_protocol.h"
#include <string.h>

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static stock_content_result_t reply(uint8_t type, uint8_t status)
{
    stock_content_result_t out = {0};
    out.response[0] = type; out.response[1] = status;
    return out;
}
static bool partition_usable(const esp_partition_t *partition)
{
    return partition && !partition->readonly && !partition->encrypted &&
        partition->size >= STOCK_CONTENT_METADATA_SIZE &&
        partition->size % STOCK_CONTENT_METADATA_SIZE == 0;
}
stock_profile_store_result_t stock_content_init(stock_content_t *content)
{
    if (!content) return STOCK_PROFILE_STORE_INVALID_ARG;
    memset(content, 0, sizeof(*content));
    stock_profile_defaults(&content->profile);
    stock_profile_store_result_t rc = stock_profile_nvs_load(&content->profile, NULL);
    if (rc != STOCK_PROFILE_STORE_OK && rc != STOCK_PROFILE_STORE_NOT_FOUND) return rc;
    content->images[0] = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY, "imguser");
    content->images[1] = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY, "imgstore");
    stock_transfer_init(&content->transfer, STOCK_TRANSFER_IMAGE, 0);
    content->initialized = true;
    return rc;
}
void stock_content_set_avatar_validator(stock_content_t *content,
    bool (*exists)(void *, const char *), void *context)
{
    if (!content) return;
    content->avatar_exists = exists; content->avatar_context = context;
}
static bool session_allowed(void *context)
{
    stock_content_t *c = context;
    if (!c->cancelled && c->guard && !c->guard(c->guard_context, c->session))
        c->cancelled = true;
    return !c->cancelled;
}
static stock_sink_result_t image_begin(void *context, uint32_t total)
{
    stock_content_t *c = context;
    const esp_partition_t *p = c->images[c->receiving_mode];
    if (!partition_usable(p)) return STOCK_SINK_IO;
    if (total > p->size - STOCK_CONTENT_METADATA_SIZE) return STOCK_SINK_LIMIT;
    size_t erase = ((size_t)total + 4095u) / 4096u * 4096u + 4096u;
    if (!session_allowed(c)) return STOCK_SINK_INVALID;
    if (esp_partition_erase_range(p, 0, erase) != ESP_OK) return STOCK_SINK_IO;
    if (!session_allowed(c)) return STOCK_SINK_INVALID;
    c->written = 0;
    c->erased_capacity = (uint32_t)(erase - STOCK_CONTENT_METADATA_SIZE);
    return STOCK_SINK_OK;
}
static stock_sink_result_t image_data(void *context, const uint8_t *bytes, size_t length)
{
    stock_content_t *c = context;
    if (c->written > c->erased_capacity || length > c->erased_capacity - c->written)
        return STOCK_SINK_LIMIT;
    const esp_partition_t *p = c->images[c->receiving_mode];
    if (!session_allowed(c)) return STOCK_SINK_INVALID;
    if (esp_partition_write(p, STOCK_CONTENT_METADATA_SIZE + c->written, bytes, length) != ESP_OK)
        return STOCK_SINK_IO;
    if (!session_allowed(c)) return STOCK_SINK_INVALID;
    c->written += (uint32_t)length;
    return STOCK_SINK_OK;
}
static stock_sink_result_t image_end(void *context)
{
    stock_content_t *c = context;
    uint8_t metadata[8] = {'J','P','G','1'};
    put32(metadata + 4, c->written);
    if (!session_allowed(c)) return STOCK_SINK_INVALID;
    if (esp_partition_write(c->images[c->receiving_mode], 0, metadata, sizeof(metadata)) != ESP_OK)
        return STOCK_SINK_IO;
    return session_allowed(c) ? STOCK_SINK_OK : STOCK_SINK_INVALID;
}
static stock_content_result_t profile_command(stock_content_t *c, const uint8_t *bytes, size_t length)
{
    stock_content_result_t out = reply(1, 1);
    stock_profile_t candidate = c->profile;
    int count = stock_profile_apply_json(&candidate, (const char *)bytes, length, &out.changes);
    if (count <= 0) return reply(1, count < 0 ? 0x13 : 0x14);
    if (out.changes.img_mode && c->transfer.active) return reply(1, 0x1b);
    if (out.changes.avatar_name && (!c->avatar_exists ||
        !c->avatar_exists(c->avatar_context, (const char *)candidate.bytes + STOCK_PROFILE_AVATAR_NAME))) {
        memcpy(candidate.bytes + STOCK_PROFILE_AVATAR_NAME,
               c->profile.bytes + STOCK_PROFILE_AVATAR_NAME, 16);
        out.changes.avatar_name = false;
    }
    if (out.changes.game_clear) {
        stock_profile_set_i32(&candidate, STOCK_PROFILE_SCORE_TOTAL, 0);
        stock_profile_set_i32(&candidate, STOCK_PROFILE_SCORE_BEST, 0);
    }
    if (memcmp(&candidate, &c->profile, sizeof(candidate)) != 0 &&
        stock_profile_nvs_save_guarded(&candidate, session_allowed, c) != STOCK_PROFILE_STORE_OK) return reply(1, 0x16);
    if (!session_allowed(c)) return reply(1, 0x12);
    c->profile = candidate;
    out.event = STOCK_CONTENT_PROFILE;
    return out;
}
static stock_content_result_t handle_current(stock_content_t *c, uint8_t type,
    const uint8_t *payload, size_t length, bool decoder_busy)
{
    if (!c || !c->initialized) return reply(type, 0x16);
    if (type != 1 && type != 2) return reply(type, 0x18);
    if (type == 1) {
        if (length > STOCK_PAYLOAD_MAX) return reply(type, 0x12);
        return profile_command(c, payload, length);
    }
    if (!payload || !length || length > STOCK_PAYLOAD_MAX) return reply(type, 0x12);
    if (payload[0] == 0 && length >= 5) {
        uint8_t mode = c->profile.bytes[STOCK_PROFILE_IMG_MODE];
        if (mode > 1) return reply(type, 0x12);
        if (decoder_busy || c->leases[mode]) return reply(type, 0x1b);
        const esp_partition_t *p = c->images[mode];
        if (!partition_usable(p)) return reply(type, 0x16);
        c->receiving_mode = (stock_content_mode_t)mode;
        c->transfer.capacity = p->size - STOCK_CONTENT_METADATA_SIZE;
    }
    stock_transfer_sink_t sink = {c, image_begin, image_data, image_end};
    stock_transfer_result_t result = stock_transfer_receive(&c->transfer, &sink, payload, length);
    stock_content_result_t out = reply(type, result.status);
    if (result.completed) {
        out.event = c->receiving_mode == STOCK_CONTENT_AVATAR ?
            STOCK_CONTENT_AVATAR_READY : STOCK_CONTENT_FULLSCREEN_READY;
        if (c->receiving_mode == STOCK_CONTENT_AVATAR) {
            stock_profile_t next = c->profile;
            next.bytes[STOCK_PROFILE_AVATAR_NAME] = 0;
            if (stock_profile_nvs_save_guarded(&next, session_allowed, c) != STOCK_PROFILE_STORE_OK) {
                out.response[1] = 0x16;
                out.event = STOCK_CONTENT_NONE;
            } else c->profile = next;
        }
    }
    return out;
}
stock_content_result_t stock_content_handle_guarded(stock_content_t *c,
    uint32_t session, stock_content_session_guard_t guard, void *guard_context,
    uint8_t type, const uint8_t *payload, size_t length, bool decoder_busy)
{
    if (!c || !c->initialized) return reply(type, 0x16);
    if (!c->session_known || c->session != session) {
        stock_content_disconnect(c);
        c->session = session;
        c->session_known = true;
    }
    c->guard = guard;
    c->guard_context = guard_context;
    c->cancelled = false;
    stock_content_result_t out = reply(type, 0x12);
    if (session_allowed(c)) out = handle_current(c, type, payload, length, decoder_busy);
    if (!session_allowed(c)) {
        stock_content_disconnect(c);
        out = reply(type, 0x12);
    }
    c->guard = NULL;
    c->guard_context = NULL;
    return out;
}
stock_content_result_t stock_content_handle(stock_content_t *c, uint8_t type,
    const uint8_t *payload, size_t length, bool decoder_busy)
{
    return stock_content_handle_guarded(c, 0, NULL, NULL, type, payload, length, decoder_busy);
}
void stock_content_disconnect(stock_content_t *c)
{
    if (!c) return;
    stock_transfer_reset(&c->transfer);
    c->session_known = false;
    c->written = c->erased_capacity = 0;
}
bool stock_content_image_acquire(stock_content_t *c, stock_content_mode_t mode,
                                  stock_content_image_t *image)
{
    if (!c || !c->initialized || !image || (unsigned)mode > 1 ||
        (c->transfer.active && c->receiving_mode == mode)) return false;
    const esp_partition_t *p = c->images[mode];
    uint8_t metadata[8];
    if (!p || p->size < STOCK_CONTENT_METADATA_SIZE ||
        esp_partition_read(p, 0, metadata, sizeof(metadata)) != ESP_OK ||
        memcmp(metadata, "JPG1", 4) != 0) return false;
    uint32_t length = read32(metadata + 4);
    if (!length || length > p->size - STOCK_CONTENT_METADATA_SIZE) return false;
    stock_content_image_t mapped = {.owner = c, .mode = mode, .length = length};
    const void *data;
    if (esp_partition_mmap(p, STOCK_CONTENT_METADATA_SIZE, length,
        ESP_PARTITION_MMAP_DATA, &data, &mapped.handle) != ESP_OK) return false;
    mapped.jpeg = data;
    ++c->leases[mode];
    *image = mapped;
    return true;
}
void stock_content_image_release(stock_content_image_t *image)
{
    if (!image || !image->owner) return;
    esp_partition_munmap(image->handle);
    --image->owner->leases[image->mode];
    memset(image, 0, sizeof(*image));
}
