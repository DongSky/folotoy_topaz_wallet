#include "wallet_store.h"

#include <string.h>

#ifndef WALLET_STORE_HOST_TEST
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#endif

#define WALLET_MARKER_OFFSET (WALLET_BANK_SIZE - 32u)
#define WALLET_MARKER_SIZE 32u
#define WALLET_VALIDATE_CHUNK 1024u

_Static_assert(WALLET_MAX_SIZE < WALLET_MARKER_OFFSET,
               "Snapshot must leave room for its durable bank marker");

typedef struct {
    void *context;
    int (*read)(void *context, unsigned bank, size_t offset, void *data, size_t length);
    int (*write)(void *context, unsigned bank, size_t offset,
                 const void *data, size_t length);
    int (*erase_bank)(void *context, unsigned bank);
    int (*load_active_bank)(void *context, unsigned *bank);
    int (*save_active_bank)(void *context, unsigned bank);
} store_backend_t;

typedef struct {
    bool valid;
    uint32_t generation;
    uint32_t package_length;
    uint32_t package_crc;
    wallet_store_info_t info;
} bank_validation_t;

typedef struct {
    store_backend_t backend;
#ifndef WALLET_STORE_HOST_TEST
    SemaphoreHandle_t mutex;
    const esp_partition_t *partitions[WALLET_BANK_COUNT];
#endif
    bool backend_ready;
    bool initialized;
    int active_bank;
    wallet_store_info_t active_info;
    bool staging;
    unsigned staging_bank;
    uint32_t expected_length;
    uint32_t expected_crc;
    uint32_t written;
} wallet_store_state_t;

static wallet_store_state_t s_store = {.active_bank = -1};

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

#ifdef WALLET_STORE_HOST_TEST
static void store_lock(void) {}
static void store_unlock(void) {}

void wallet_store_test_bind(const wallet_store_backend_t *backend)
{
    memset(&s_store, 0, sizeof(s_store));
    s_store.active_bank = -1;
    if (backend != NULL) {
        s_store.backend = (store_backend_t){
            .context = backend->context,
            .read = backend->read,
            .write = backend->write,
            .erase_bank = backend->erase_bank,
            .load_active_bank = backend->load_active_bank,
            .save_active_bank = backend->save_active_bank,
        };
        s_store.backend_ready = true;
    }
}
#else
static void store_lock(void)
{
    xSemaphoreTake(s_store.mutex, portMAX_DELAY);
}

static void store_unlock(void)
{
    xSemaphoreGive(s_store.mutex);
}

static int partition_read(void *context, unsigned bank, size_t offset,
                          void *data, size_t length)
{
    wallet_store_state_t *store = context;
    return esp_partition_read(store->partitions[bank], offset, data, length);
}

static int partition_write(void *context, unsigned bank, size_t offset,
                           const void *data, size_t length)
{
    wallet_store_state_t *store = context;
    return esp_partition_write(store->partitions[bank], offset, data, length);
}

static int partition_erase(void *context, unsigned bank)
{
    wallet_store_state_t *store = context;
    return esp_partition_erase_range(store->partitions[bank], 0, WALLET_BANK_SIZE);
}

static int nvs_load_active(void *context, unsigned *bank)
{
    nvs_handle_t handle;
    uint8_t value;
    esp_err_t result;

    (void)context;
    result = nvs_open("wallet", NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return WALLET_STORE_BACKEND_NOT_FOUND;
    }
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_get_u8(handle, "active", &value);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return WALLET_STORE_BACKEND_NOT_FOUND;
    }
    if (result != ESP_OK) {
        return result;
    }
    *bank = value;
    return 0;
}

static int nvs_save_active(void *context, unsigned bank)
{
    nvs_handle_t handle;
    esp_err_t result;

    (void)context;
    result = nvs_open("wallet", NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_u8(handle, "active", (uint8_t)bank);
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

static bool setup_backend(void)
{
    static const char *const labels[WALLET_BANK_COUNT] = {
        WALLET_BANK_A_LABEL, WALLET_BANK_B_LABEL
    };

    if (s_store.backend_ready) {
        return true;
    }
    for (unsigned bank = 0; bank < WALLET_BANK_COUNT; ++bank) {
        s_store.partitions[bank] = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, labels[bank]);
        if (s_store.partitions[bank] == NULL ||
            s_store.partitions[bank]->size != WALLET_BANK_SIZE) {
            return false;
        }
    }
    s_store.backend = (store_backend_t){
        .context = &s_store,
        .read = partition_read,
        .write = partition_write,
        .erase_bank = partition_erase,
        .load_active_bank = nvs_load_active,
        .save_active_bank = nvs_save_active,
    };
    s_store.backend_ready = true;
    return true;
}
#endif

static bool backend_complete(void)
{
    return s_store.backend_ready && s_store.backend.read != NULL &&
           s_store.backend.write != NULL && s_store.backend.erase_bank != NULL &&
           s_store.backend.load_active_bank != NULL &&
           s_store.backend.save_active_bank != NULL;
}

static bool read_bank(unsigned bank, size_t offset, void *data, size_t length)
{
    return bank < WALLET_BANK_COUNT && offset <= WALLET_BANK_SIZE &&
           length <= WALLET_BANK_SIZE - offset &&
           s_store.backend.read(s_store.backend.context, bank, offset, data, length) == 0;
}

static bool stream_crc(unsigned bank, size_t offset, size_t length, uint32_t *crc)
{
    uint8_t buffer[WALLET_VALIDATE_CHUNK];
    uint32_t value = 0;

    while (length != 0) {
        size_t chunk = length < sizeof(buffer) ? length : sizeof(buffer);
        if (!read_bank(bank, offset, buffer, chunk)) {
            return false;
        }
        value = wallet_crc32(value, buffer, chunk);
        offset += chunk;
        length -= chunk;
    }
    *crc = value;
    return true;
}

static wallet_store_result_t validate_package(unsigned bank,
                                              uint32_t package_length,
                                              uint32_t package_crc,
                                              wallet_store_info_t *info)
{
    uint8_t header_bytes[WALLET_HEADER_SIZE];
    wallet_header_t header;
    uint32_t computed;
    uint16_t counts[WALLET_KIND_COUNT] = {0};

    if (package_length < WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE ||
        package_length > WALLET_MAX_SIZE || package_length >= WALLET_MARKER_OFFSET) {
        return WALLET_STORE_FORMAT_ERROR;
    }
    if (!read_bank(bank, 0, header_bytes, sizeof(header_bytes))) {
        return WALLET_STORE_IO_ERROR;
    }
    if (!wallet_header_parse(header_bytes, sizeof(header_bytes), &header) ||
        header.total_size != package_length) {
        return WALLET_STORE_FORMAT_ERROR;
    }
    if (!stream_crc(bank, 0, package_length, &computed)) {
        return WALLET_STORE_IO_ERROR;
    }
    if (computed != package_crc) {
        return WALLET_STORE_CRC_ERROR;
    }
    if (!stream_crc(bank, WALLET_HEADER_SIZE,
                    package_length - WALLET_HEADER_SIZE, &computed)) {
        return WALLET_STORE_IO_ERROR;
    }
    if (computed != header.body_crc) {
        return WALLET_STORE_CRC_ERROR;
    }
    for (uint16_t page = 0; page < header.page_count; ++page) {
        uint8_t prefix[4 + 64];
        size_t offset = WALLET_HEADER_SIZE + (size_t)page * WALLET_PAGE_RECORD_SIZE;
        if (!read_bank(bank, offset, prefix, sizeof(prefix))) {
            return WALLET_STORE_IO_ERROR;
        }
        if (!wallet_page_valid(prefix) || (page == 0 && prefix[0] != 0)) {
            return WALLET_STORE_FORMAT_ERROR;
        }
        for (unsigned color = 0; color < 16; ++color) {
            if (prefix[4 + color * 4 + 3] != 0xff) {
                return WALLET_STORE_FORMAT_ERROR;
            }
        }
        ++counts[prefix[0]];
    }
    if (counts[0] != 1) {
        return WALLET_STORE_FORMAT_ERROR;
    }
    memset(info, 0, sizeof(*info));
    info->available = true;
    info->header = header;
    memcpy(info->counts, counts, sizeof(counts));
    return WALLET_STORE_OK;
}

static void encode_marker(uint8_t marker[WALLET_MARKER_SIZE], unsigned bank,
                          uint32_t generation, uint32_t package_length,
                          uint32_t package_crc)
{
    memset(marker, 0, WALLET_MARKER_SIZE);
    memcpy(marker, "WCM1", 4);
    put16(marker + 4, 1);
    marker[6] = (uint8_t)bank;
    put32(marker + 8, generation);
    put32(marker + 12, package_length);
    put32(marker + 16, package_crc);
    put32(marker + 28, wallet_crc32(0, marker, 28));
}

static bool decode_marker(const uint8_t marker[WALLET_MARKER_SIZE], unsigned bank,
                          bank_validation_t *validation)
{
    if (memcmp(marker, "WCM1", 4) != 0 || read16(marker + 4) != 1 ||
        marker[6] != bank || marker[7] != 0 || read32(marker + 20) != 0 ||
        read32(marker + 24) != 0 ||
        read32(marker + 28) != wallet_crc32(0, marker, 28)) {
        return false;
    }
    validation->generation = read32(marker + 8);
    validation->package_length = read32(marker + 12);
    validation->package_crc = read32(marker + 16);
    return validation->generation != 0;
}

static void validate_committed_bank(unsigned bank, bank_validation_t *validation)
{
    uint8_t marker[WALLET_MARKER_SIZE];

    memset(validation, 0, sizeof(*validation));
    if (!read_bank(bank, WALLET_MARKER_OFFSET, marker, sizeof(marker)) ||
        !decode_marker(marker, bank, validation)) {
        return;
    }
    if (validate_package(bank, validation->package_length,
                         validation->package_crc, &validation->info) != WALLET_STORE_OK) {
        memset(validation, 0, sizeof(*validation));
        return;
    }
    validation->valid = true;
    validation->info.revision = validation->generation;
}

wallet_store_result_t wallet_store_init(void)
{
    bank_validation_t banks[WALLET_BANK_COUNT];
    unsigned selected = WALLET_BANK_COUNT;
    unsigned saved = WALLET_BANK_COUNT;

#ifndef WALLET_STORE_HOST_TEST
    if (s_store.mutex == NULL) {
        s_store.mutex = xSemaphoreCreateMutex();
        if (s_store.mutex == NULL) {
            return WALLET_STORE_IO_ERROR;
        }
    }
    if (!setup_backend()) {
        return WALLET_STORE_NOT_FOUND;
    }
#endif
    if (!backend_complete()) {
        return WALLET_STORE_INVALID_STATE;
    }
    store_lock();
    for (unsigned bank = 0; bank < WALLET_BANK_COUNT; ++bank) {
        validate_committed_bank(bank, &banks[bank]);
    }
    int load_result = s_store.backend.load_active_bank(s_store.backend.context, &saved);
    if (load_result == 0 && saved < WALLET_BANK_COUNT && banks[saved].valid) {
        selected = saved;
    } else {
        for (unsigned bank = 0; bank < WALLET_BANK_COUNT; ++bank) {
            if (banks[bank].valid &&
                (selected == WALLET_BANK_COUNT ||
                 banks[bank].generation > banks[selected].generation)) {
                selected = bank;
            }
        }
        if (selected != WALLET_BANK_COUNT) {
            (void)s_store.backend.save_active_bank(s_store.backend.context, selected);
        }
    }
    s_store.active_bank = selected == WALLET_BANK_COUNT ? -1 : (int)selected;
    memset(&s_store.active_info, 0, sizeof(s_store.active_info));
    if (s_store.active_bank >= 0) {
        s_store.active_info = banks[selected].info;
    }
    s_store.staging = false;
    s_store.initialized = true;
    store_unlock();
    return WALLET_STORE_OK;
}

wallet_store_result_t wallet_store_begin(uint32_t expected_length,
                                         uint32_t expected_crc)
{
    unsigned bank;
    if (!s_store.initialized) {
        return WALLET_STORE_INVALID_STATE;
    }
    if (expected_length < WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE ||
        expected_length > WALLET_MAX_SIZE || expected_length >= WALLET_MARKER_OFFSET) {
        return WALLET_STORE_NO_SPACE;
    }
    store_lock();
    bank = s_store.active_bank == 0 ? 1u : 0u;
    s_store.staging = false;
    store_unlock();
    /* The worker serializes transfers, and this is always the inactive bank.
     * Do not hold the UI read mutex across a full 2 MB erase. */
    if (s_store.backend.erase_bank(s_store.backend.context, bank) != 0) {
        return WALLET_STORE_IO_ERROR;
    }
    store_lock();
    s_store.staging = true;
    s_store.staging_bank = bank;
    s_store.expected_length = expected_length;
    s_store.expected_crc = expected_crc;
    s_store.written = 0;
    store_unlock();
    return WALLET_STORE_OK;
}

wallet_store_result_t wallet_store_write(uint32_t offset,
                                         const uint8_t *data,
                                         size_t length)
{
    wallet_store_result_t result = WALLET_STORE_OK;
    if (data == NULL || length == 0) {
        return WALLET_STORE_INVALID_ARG;
    }
    store_lock();
    if (!s_store.staging) {
        result = WALLET_STORE_INVALID_STATE;
    } else if (offset != s_store.written ||
               length > (size_t)(s_store.expected_length - s_store.written)) {
        result = WALLET_STORE_INVALID_ARG;
    } else if (s_store.backend.write(s_store.backend.context, s_store.staging_bank,
                                     offset, data, length) != 0) {
        result = WALLET_STORE_IO_ERROR;
    } else {
        s_store.written += (uint32_t)length;
    }
    store_unlock();
    return result;
}

wallet_store_result_t wallet_store_commit_guarded(wallet_store_commit_guard_t guard,
                                                  void *guard_context)
{
    wallet_store_result_t result;
    wallet_store_info_t info;
    uint8_t marker[WALLET_MARKER_SIZE];
    uint32_t generation;
    unsigned staging_bank;
    uint32_t expected_length;
    uint32_t expected_crc;

    store_lock();
    if (!s_store.staging || s_store.written != s_store.expected_length) {
        store_unlock();
        return WALLET_STORE_INVALID_STATE;
    }
    staging_bank = s_store.staging_bank;
    expected_length = s_store.expected_length;
    expected_crc = s_store.expected_crc;
    generation = s_store.active_info.revision + 1u;
    store_unlock();
    /* Validation reads only the inactive bank. UI page reads remain available
     * from the active bank while CRCs and page records are checked. */
    result = validate_package(staging_bank, expected_length, expected_crc, &info);
    if (result != WALLET_STORE_OK) {
        store_lock();
        s_store.staging = false;
        store_unlock();
        return result;
    }
    if (guard != NULL && !guard(guard_context)) {
        store_lock();
        s_store.staging = false;
        store_unlock();
        return WALLET_STORE_INVALID_STATE;
    }
    if (generation == 0) {
        generation = 1;
    }
    encode_marker(marker, staging_bank, generation, expected_length, expected_crc);
    /* Point NVS at the still-uncommitted bank first. A reset in this interval
     * sees no marker and falls back to the previous committed bank. The marker
     * write below is the single durable commit point. */
    if (s_store.backend.save_active_bank(s_store.backend.context,
                                         staging_bank) != 0) {
        store_lock();
        s_store.staging = false;
        store_unlock();
        return WALLET_STORE_IO_ERROR;
    }
    if (guard != NULL && !guard(guard_context)) {
        store_lock();
        s_store.staging = false;
        store_unlock();
        return WALLET_STORE_INVALID_STATE;
    }
    /* Irreversible durable commit phase: the pointer currently targets an
     * unmarked bank, so a reset still falls back. The marker write below is
     * the linearization point. Do not report a later disconnect as failure;
     * the phone may not receive success, but storage is atomically complete. */
    if (s_store.backend.write(s_store.backend.context, staging_bank,
                              WALLET_MARKER_OFFSET, marker, sizeof(marker)) != 0) {
        store_lock();
        s_store.staging = false;
        store_unlock();
        return WALLET_STORE_IO_ERROR;
    }
    store_lock();
    s_store.active_bank = (int)staging_bank;
    info.revision = generation;
    s_store.active_info = info;
    s_store.staging = false;
    store_unlock();
    return WALLET_STORE_OK;
}

wallet_store_result_t wallet_store_commit(void)
{
    return wallet_store_commit_guarded(NULL, NULL);
}

void wallet_store_abort(void)
{
    if (!s_store.initialized) {
        return;
    }
    store_lock();
    s_store.staging = false;
    s_store.expected_length = 0;
    s_store.expected_crc = 0;
    s_store.written = 0;
    store_unlock();
}

wallet_store_result_t wallet_store_get_info(wallet_store_info_t *out)
{
    if (out == NULL) {
        return WALLET_STORE_INVALID_ARG;
    }
    if (!s_store.initialized) {
        return WALLET_STORE_INVALID_STATE;
    }
    store_lock();
    *out = s_store.active_info;
    store_unlock();
    return WALLET_STORE_OK;
}

static wallet_store_result_t read_page(const uint32_t *expected_revision, uint8_t kind,
                                             uint16_t ordinal,
                                             uint8_t metadata[4],
                                             uint8_t *image,
                                             size_t image_size)
{
    wallet_store_result_t result = WALLET_STORE_NOT_FOUND;

    if (kind >= WALLET_KIND_COUNT || metadata == NULL || image == NULL ||
        image_size < WALLET_PAGE_IMAGE_SIZE) {
        return WALLET_STORE_INVALID_ARG;
    }
    if (!s_store.initialized) {
        return WALLET_STORE_INVALID_STATE;
    }
    store_lock();
    if (expected_revision != NULL && *expected_revision != s_store.active_info.revision) {
        store_unlock();
        return WALLET_STORE_STALE_SNAPSHOT;
    }
    if (s_store.active_bank < 0) {
        store_unlock();
        return WALLET_STORE_NOT_FOUND;
    }
    for (uint16_t page = 0; page < s_store.active_info.header.page_count; ++page) {
        uint8_t page_metadata[4];
        size_t offset = WALLET_HEADER_SIZE + (size_t)page * WALLET_PAGE_RECORD_SIZE;
        if (!read_bank((unsigned)s_store.active_bank, offset,
                       page_metadata, sizeof(page_metadata))) {
            result = WALLET_STORE_IO_ERROR;
            break;
        }
        if (page_metadata[0] != kind) {
            continue;
        }
        if (ordinal != 0) {
            --ordinal;
            continue;
        }
        if (!read_bank((unsigned)s_store.active_bank, offset + 4,
                       image, WALLET_PAGE_IMAGE_SIZE)) {
            result = WALLET_STORE_IO_ERROR;
            break;
        }
        memcpy(metadata, page_metadata, sizeof(page_metadata));
        result = WALLET_STORE_OK;
        break;
    }
    store_unlock();
    return result;
}

wallet_store_result_t wallet_store_read_page(uint8_t kind, uint16_t ordinal,
    uint8_t metadata[4], uint8_t *image, size_t image_size)
{
    return read_page(NULL, kind, ordinal, metadata, image, image_size);
}

wallet_store_result_t wallet_store_read_page_at_revision(uint32_t expected_revision,
    uint8_t kind, uint16_t ordinal, uint8_t metadata[4], uint8_t *image,
    size_t image_size)
{
    return read_page(&expected_revision, kind, ordinal, metadata, image, image_size);
}
