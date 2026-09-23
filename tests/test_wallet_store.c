#define WALLET_STORE_HOST_TEST 1
#include "wallet_store.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *bank[2];
    int active;
    bool active_present;
    bool fail_activate;
    bool *clear_guard_on_marker;
} fake_flash_t;

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

static int fake_read(void *context, unsigned bank, size_t offset, void *data, size_t length)
{
    fake_flash_t *flash = context;
    if (bank > 1 || offset > WALLET_BANK_SIZE || length > WALLET_BANK_SIZE - offset) return -1;
    memcpy(data, flash->bank[bank] + offset, length);
    return 0;
}

static int fake_write(void *context, unsigned bank, size_t offset,
                      const void *data, size_t length)
{
    fake_flash_t *flash = context;
    const uint8_t *source = data;
    if (bank > 1 || offset > WALLET_BANK_SIZE || length > WALLET_BANK_SIZE - offset) return -1;
    for (size_t i = 0; i < length; ++i) {
        uint8_t *target = &flash->bank[bank][offset + i];
        if ((*target & source[i]) != source[i]) return -2;
        *target &= source[i];
    }
    if (offset == WALLET_BANK_SIZE - 32u && flash->clear_guard_on_marker != NULL) {
        *flash->clear_guard_on_marker = false;
    }
    return 0;
}

static int fake_erase(void *context, unsigned bank)
{
    fake_flash_t *flash = context;
    if (bank > 1) return -1;
    memset(flash->bank[bank], 0xff, WALLET_BANK_SIZE);
    return 0;
}

static int fake_load_active(void *context, unsigned *bank)
{
    fake_flash_t *flash = context;
    if (!flash->active_present) return WALLET_STORE_BACKEND_NOT_FOUND;
    *bank = (unsigned)flash->active;
    return 0;
}

static int fake_save_active(void *context, unsigned bank)
{
    fake_flash_t *flash = context;
    if (flash->fail_activate) return -1;
    flash->active = (int)bank;
    flash->active_present = true;
    return 0;
}

static wallet_store_backend_t fake_backend(fake_flash_t *flash)
{
    const wallet_store_backend_t backend = {
        .context = flash,
        .read = fake_read,
        .write = fake_write,
        .erase_bank = fake_erase,
        .load_active_bank = fake_load_active,
        .save_active_bank = fake_save_active,
    };
    return backend;
}

static uint8_t *make_package(uint8_t kind, uint8_t pixel, uint32_t *length, uint32_t *crc)
{
    *length = WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE;
    uint8_t *package = calloc(1, *length);
    assert(package != NULL);
    memcpy(package, "PCW1", 4);
    put16(package + 4, 1);
    put16(package + 6, 1);
    put16(package + 8, WALLET_WIDTH);
    put16(package + 10, WALLET_HEIGHT);
    put32(package + 12, *length);
    package[16] = 42;
    uint8_t *record = package + WALLET_HEADER_SIZE;
    record[0] = kind;
    record[1] = kind == 4 ? 1 : 0;
    for (size_t i = 0; i < 16; ++i) record[4 + i * 4 + 3] = 0xff;
    memset(record + 4 + 64, pixel, WALLET_PAGE_IMAGE_SIZE - 64);
    put32(package + 24, wallet_crc32(0, package + WALLET_HEADER_SIZE,
                                    *length - WALLET_HEADER_SIZE));
    *crc = wallet_crc32(0, package, *length);
    return package;
}

static void import_package(const uint8_t *package, uint32_t length, uint32_t crc)
{
    assert(wallet_store_begin(length, crc) == WALLET_STORE_OK);
    size_t first = 137;
    assert(wallet_store_write(0, package, first) == WALLET_STORE_OK);
    assert(wallet_store_write((uint32_t)first, package + first, length - first) == WALLET_STORE_OK);
    assert(wallet_store_commit() == WALLET_STORE_OK);
}

static bool reject_commit(void *context)
{
    (void)context;
    return false;
}

static bool dynamic_commit_guard(void *context)
{
    return *(const bool *)context;
}

static void test_valid_commit_survives_restart_and_can_be_read_by_kind(void)
{
    fake_flash_t flash = {0};
    flash.bank[0] = malloc(WALLET_BANK_SIZE);
    flash.bank[1] = malloc(WALLET_BANK_SIZE);
    assert(flash.bank[0] && flash.bank[1]);
    memset(flash.bank[0], 0xff, WALLET_BANK_SIZE);
    memset(flash.bank[1], 0xff, WALLET_BANK_SIZE);
    wallet_store_backend_t backend = fake_backend(&flash);
    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);

    uint32_t length, crc;
    uint8_t *package = make_package(0, 0xa5, &length, &crc);
    import_package(package, length, crc);
    wallet_store_info_t info;
    assert(wallet_store_get_info(&info) == WALLET_STORE_OK);
    assert(info.available && info.header.page_count == 1 && info.counts[0] == 1);
    assert(info.revision == 1);
    uint8_t meta[4];
    uint8_t *image = malloc(WALLET_PAGE_IMAGE_SIZE);
    assert(image != NULL);
    assert(wallet_store_read_page(0, 0, meta, image, WALLET_PAGE_IMAGE_SIZE) == WALLET_STORE_OK);
    assert(meta[0] == 0 && image[64] == 0xa5);

    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);
    assert(wallet_store_get_info(&info) == WALLET_STORE_OK && info.available);
    assert(wallet_store_read_page(0, 0, meta, image, WALLET_PAGE_IMAGE_SIZE) == WALLET_STORE_OK);
    assert(image[64] == 0xa5);
    free(image);
    free(package);
    free(flash.bank[0]);
    free(flash.bank[1]);
}

static void test_crc_failure_and_unmarked_staging_preserve_active_bank(void)
{
    fake_flash_t flash = {0};
    flash.bank[0] = malloc(WALLET_BANK_SIZE);
    flash.bank[1] = malloc(WALLET_BANK_SIZE);
    assert(flash.bank[0] && flash.bank[1]);
    memset(flash.bank[0], 0xff, WALLET_BANK_SIZE);
    memset(flash.bank[1], 0xff, WALLET_BANK_SIZE);
    wallet_store_backend_t backend = fake_backend(&flash);
    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);
    uint32_t old_length, old_crc, new_length, new_crc;
    uint8_t *old_package = make_package(0, 0x11, &old_length, &old_crc);
    uint8_t *new_package = make_package(0, 0x22, &new_length, &new_crc);
    import_package(old_package, old_length, old_crc);

    assert(wallet_store_begin(new_length, new_crc ^ 1u) == WALLET_STORE_OK);
    assert(wallet_store_write(0, new_package, new_length) == WALLET_STORE_OK);
    assert(wallet_store_commit() == WALLET_STORE_CRC_ERROR);
    wallet_store_info_t info;
    assert(wallet_store_get_info(&info) == WALLET_STORE_OK && info.revision == 1);

    /* A complete package without its final commit marker is never a fallback. */
    assert(wallet_store_begin(new_length, new_crc) == WALLET_STORE_OK);
    assert(wallet_store_write(0, new_package, new_length) == WALLET_STORE_OK);
    wallet_store_test_bind(&backend); /* Simulated power loss. */
    assert(wallet_store_init() == WALLET_STORE_OK);
    assert(wallet_store_get_info(&info) == WALLET_STORE_OK && info.revision == 1);
    uint8_t meta[4];
    uint8_t *image = malloc(WALLET_PAGE_IMAGE_SIZE);
    assert(image != NULL);
    assert(wallet_store_read_page(0, 0, meta, image, WALLET_PAGE_IMAGE_SIZE) == WALLET_STORE_OK);
    assert(image[64] == 0x11);
    free(image);
    free(old_package);
    free(new_package);
    free(flash.bank[0]);
    free(flash.bank[1]);
}

static void test_failed_nvs_activation_keeps_previous_bank_active(void)
{
    fake_flash_t flash = {0};
    flash.bank[0] = malloc(WALLET_BANK_SIZE);
    flash.bank[1] = malloc(WALLET_BANK_SIZE);
    assert(flash.bank[0] && flash.bank[1]);
    memset(flash.bank[0], 0xff, WALLET_BANK_SIZE);
    memset(flash.bank[1], 0xff, WALLET_BANK_SIZE);
    wallet_store_backend_t backend = fake_backend(&flash);
    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);
    uint32_t length, crc;
    uint8_t *first = make_package(0, 0x31, &length, &crc);
    import_package(first, length, crc);
    uint8_t *second = make_package(0, 0x32, &length, &crc);
    flash.fail_activate = true;
    assert(wallet_store_begin(length, crc) == WALLET_STORE_OK);
    assert(wallet_store_write(0, second, length) == WALLET_STORE_OK);
    assert(wallet_store_commit() == WALLET_STORE_IO_ERROR);
    wallet_store_info_t info;
    assert(wallet_store_get_info(&info) == WALLET_STORE_OK && info.revision == 1);
    free(first);
    free(second);
    free(flash.bank[0]);
    free(flash.bank[1]);
}

static void test_first_activation_failure_never_becomes_boot_fallback(void)
{
    fake_flash_t flash = {0};
    flash.bank[0] = malloc(WALLET_BANK_SIZE);
    flash.bank[1] = malloc(WALLET_BANK_SIZE);
    assert(flash.bank[0] && flash.bank[1]);
    memset(flash.bank[0], 0xff, WALLET_BANK_SIZE);
    memset(flash.bank[1], 0xff, WALLET_BANK_SIZE);
    wallet_store_backend_t backend = fake_backend(&flash);
    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);
    uint32_t length, crc;
    uint8_t *package = make_package(0, 0x41, &length, &crc);
    flash.fail_activate = true;
    assert(wallet_store_begin(length, crc) == WALLET_STORE_OK);
    assert(wallet_store_write(0, package, length) == WALLET_STORE_OK);
    assert(wallet_store_commit() == WALLET_STORE_IO_ERROR);
    flash.fail_activate = false;
    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);
    wallet_store_info_t info;
    assert(wallet_store_get_info(&info) == WALLET_STORE_OK && !info.available);
    free(package);
    free(flash.bank[0]);
    free(flash.bank[1]);
}

static void test_disconnect_guard_cancels_before_durable_commit_point(void)
{
    fake_flash_t flash = {0};
    flash.bank[0] = malloc(WALLET_BANK_SIZE);
    flash.bank[1] = malloc(WALLET_BANK_SIZE);
    assert(flash.bank[0] && flash.bank[1]);
    memset(flash.bank[0], 0xff, WALLET_BANK_SIZE);
    memset(flash.bank[1], 0xff, WALLET_BANK_SIZE);
    wallet_store_backend_t backend = fake_backend(&flash);
    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);
    uint32_t length, crc;
    uint8_t *first = make_package(0, 0x51, &length, &crc);
    import_package(first, length, crc);
    uint8_t *second = make_package(0, 0x52, &length, &crc);
    assert(wallet_store_begin(length, crc) == WALLET_STORE_OK);
    assert(wallet_store_write(0, second, length) == WALLET_STORE_OK);
    assert(wallet_store_commit_guarded(reject_commit, NULL) == WALLET_STORE_INVALID_STATE);
    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);
    uint8_t meta[4];
    uint8_t *image = malloc(WALLET_PAGE_IMAGE_SIZE);
    assert(image != NULL);
    assert(wallet_store_read_page(0, 0, meta, image, WALLET_PAGE_IMAGE_SIZE) == WALLET_STORE_OK);
    assert(image[64] == 0x51);
    free(image);
    free(first);
    free(second);
    free(flash.bank[0]);
    free(flash.bank[1]);
}

static void test_disconnect_during_marker_finishes_atomic_commit(void)
{
    fake_flash_t flash = {0};
    flash.bank[0] = malloc(WALLET_BANK_SIZE);
    flash.bank[1] = malloc(WALLET_BANK_SIZE);
    assert(flash.bank[0] && flash.bank[1]);
    memset(flash.bank[0], 0xff, WALLET_BANK_SIZE);
    memset(flash.bank[1], 0xff, WALLET_BANK_SIZE);
    wallet_store_backend_t backend = fake_backend(&flash);
    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);
    uint32_t length, crc;
    uint8_t *first = make_package(0, 0x61, &length, &crc);
    import_package(first, length, crc);
    uint8_t *second = make_package(0, 0x62, &length, &crc);
    assert(wallet_store_begin(length, crc) == WALLET_STORE_OK);
    assert(wallet_store_write(0, second, length) == WALLET_STORE_OK);
    bool guard_allowed = true;
    flash.clear_guard_on_marker = &guard_allowed;
    assert(wallet_store_commit_guarded(dynamic_commit_guard, &guard_allowed) == WALLET_STORE_OK);
    assert(!guard_allowed);

    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);
    uint8_t meta[4];
    uint8_t *image = malloc(WALLET_PAGE_IMAGE_SIZE);
    assert(image != NULL);
    assert(wallet_store_read_page(0, 0, meta, image, WALLET_PAGE_IMAGE_SIZE) == WALLET_STORE_OK);
    assert(image[64] == 0x62);
    free(image);
    free(first);
    free(second);
    free(flash.bank[0]);
    free(flash.bank[1]);
}

static void test_page_validation_rejects_non_card_first_page_and_bad_alpha(void)
{
    fake_flash_t flash = {0};
    flash.bank[0] = malloc(WALLET_BANK_SIZE);
    flash.bank[1] = malloc(WALLET_BANK_SIZE);
    assert(flash.bank[0] && flash.bank[1]);
    memset(flash.bank[0], 0xff, WALLET_BANK_SIZE);
    memset(flash.bank[1], 0xff, WALLET_BANK_SIZE);
    wallet_store_backend_t backend = fake_backend(&flash);
    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);
    uint32_t length, crc;
    uint8_t *package = make_package(2, 0, &length, &crc);
    assert(wallet_store_begin(length, crc) == WALLET_STORE_OK);
    assert(wallet_store_write(0, package, length) == WALLET_STORE_OK);
    assert(wallet_store_commit() == WALLET_STORE_FORMAT_ERROR);

    free(package);
    package = make_package(0, 0, &length, &crc);
    package[WALLET_HEADER_SIZE + 4 + 3] = 0;
    put32(package + 24, wallet_crc32(0, package + WALLET_HEADER_SIZE,
                                    length - WALLET_HEADER_SIZE));
    crc = wallet_crc32(0, package, length);
    assert(wallet_store_begin(length, crc) == WALLET_STORE_OK);
    assert(wallet_store_write(0, package, length) == WALLET_STORE_OK);
    assert(wallet_store_commit() == WALLET_STORE_FORMAT_ERROR);

    free(package);
    package = make_package(0, 0, &length, &crc);
    package = realloc(package, WALLET_HEADER_SIZE + 2 * WALLET_PAGE_RECORD_SIZE);
    assert(package != NULL);
    memcpy(package + WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE,
           package + WALLET_HEADER_SIZE, WALLET_PAGE_RECORD_SIZE);
    length = WALLET_HEADER_SIZE + 2 * WALLET_PAGE_RECORD_SIZE;
    put16(package + 6, 2);
    put32(package + 12, length);
    put32(package + 24, wallet_crc32(0, package + WALLET_HEADER_SIZE,
                                    length - WALLET_HEADER_SIZE));
    crc = wallet_crc32(0, package, length);
    assert(wallet_store_begin(length, crc) == WALLET_STORE_OK);
    assert(wallet_store_write(0, package, length) == WALLET_STORE_OK);
    assert(wallet_store_commit() == WALLET_STORE_FORMAT_ERROR);
    free(package);
    free(flash.bank[0]);
    free(flash.bank[1]);
}

static void test_private_page_cannot_cross_snapshot_revision(void)
{
    fake_flash_t flash = {0};
    flash.bank[0] = malloc(WALLET_BANK_SIZE);
    flash.bank[1] = malloc(WALLET_BANK_SIZE);
    assert(flash.bank[0] && flash.bank[1]);
    memset(flash.bank[0], 0xff, WALLET_BANK_SIZE);
    memset(flash.bank[1], 0xff, WALLET_BANK_SIZE);
    wallet_store_backend_t backend = fake_backend(&flash);
    wallet_store_test_bind(&backend);
    assert(wallet_store_init() == WALLET_STORE_OK);

    uint32_t length, crc;
    uint8_t *package = make_package(0, 0x11, &length, &crc);
    length = WALLET_HEADER_SIZE + 2 * WALLET_PAGE_RECORD_SIZE;
    package = realloc(package, length);
    assert(package);
    uint8_t *private_record = package + WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE;
    memcpy(private_record, package + WALLET_HEADER_SIZE, WALLET_PAGE_RECORD_SIZE);
    private_record[0] = 4;
    private_record[1] = 1;
    put16(package + 6, 2);
    put32(package + 12, length);
    put32(package + 24, wallet_crc32(0, package + WALLET_HEADER_SIZE,
                                    length - WALLET_HEADER_SIZE));
    crc = wallet_crc32(0, package, length);
    import_package(package, length, crc);
    wallet_store_info_t old;
    assert(wallet_store_get_info(&old) == WALLET_STORE_OK);
    uint8_t metadata[4];
    uint8_t *image = malloc(WALLET_PAGE_IMAGE_SIZE);
    assert(image);
    assert(wallet_store_read_page_at_revision(old.revision, 4, 0, metadata,
        image, WALLET_PAGE_IMAGE_SIZE) == WALLET_STORE_OK);
    assert(metadata[1] == 1 && image[64] == 0x11);

    /* Deterministic interleaving: UI has read revision/reveal state, then the
     * BLE worker publishes a different private page before the UI page read. */
    memset(private_record + 4 + 64, 0x22, WALLET_PAGE_IMAGE_SIZE - 64);
    put32(package + 24, wallet_crc32(0, package + WALLET_HEADER_SIZE,
                                    length - WALLET_HEADER_SIZE));
    crc = wallet_crc32(0, package, length);
    import_package(package, length, crc);
    memset(image, 0xa5, WALLET_PAGE_IMAGE_SIZE);
    memset(metadata, 0xa5, sizeof(metadata));
    assert(wallet_store_read_page_at_revision(old.revision, 4, 0, metadata,
        image, WALLET_PAGE_IMAGE_SIZE) == WALLET_STORE_STALE_SNAPSHOT);
    for (size_t i = 0; i < WALLET_PAGE_IMAGE_SIZE; ++i) assert(image[i] == 0xa5);
    for (size_t i = 0; i < sizeof(metadata); ++i) assert(metadata[i] == 0xa5);
    wallet_store_info_t current;
    assert(wallet_store_get_info(&current) == WALLET_STORE_OK);
    assert(current.revision != old.revision && current.counts[4] == 1);
    assert(wallet_store_read_page_at_revision(current.revision, 4, 0, metadata,
        image, WALLET_PAGE_IMAGE_SIZE) == WALLET_STORE_OK);
    assert(metadata[0] == 4 && metadata[1] == 1 && image[64] == 0x22);
    free(image);
    free(package);
    free(flash.bank[0]);
    free(flash.bank[1]);
}

int main(void)
{
    test_valid_commit_survives_restart_and_can_be_read_by_kind();
    test_crc_failure_and_unmarked_staging_preserve_active_bank();
    test_failed_nvs_activation_keeps_previous_bank_active();
    test_first_activation_failure_never_becomes_boot_fallback();
    test_disconnect_guard_cancels_before_durable_commit_point();
    test_disconnect_during_marker_finishes_atomic_commit();
    test_page_validation_rejects_non_card_first_page_and_bad_alpha();
    test_private_page_cannot_cross_snapshot_revision();
    puts("Wallet store: PASS");
    return 0;
}
