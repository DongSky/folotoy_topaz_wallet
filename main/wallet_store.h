#pragma once

#include "wallet_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WALLET_BANK_SIZE 0x160000u
#define WALLET_BANK_COUNT 2u
#define WALLET_BANK_A_LABEL "wallet_a"
#define WALLET_BANK_B_LABEL "wallet_b"
#define WALLET_STORE_BACKEND_NOT_FOUND 1

typedef enum {
    WALLET_STORE_OK = 0,
    WALLET_STORE_INVALID_ARG,
    WALLET_STORE_INVALID_STATE,
    WALLET_STORE_NOT_FOUND,
    WALLET_STORE_NO_SPACE,
    WALLET_STORE_FORMAT_ERROR,
    WALLET_STORE_CRC_ERROR,
    WALLET_STORE_IO_ERROR,
    WALLET_STORE_STALE_SNAPSHOT,
} wallet_store_result_t;

typedef struct {
    bool available;
    wallet_header_t header;
    uint16_t counts[WALLET_KIND_COUNT];
    uint32_t revision;
} wallet_store_info_t;

/* Initialize both banks and choose only a bank carrying a durable commit
 * marker. The NVS active-bank pointer wins when valid; otherwise the newest
 * committed bank is used. This function never erases NVS. */
wallet_store_result_t wallet_store_init(void);

/* Transfer operations are serialized by wallet_ble's worker. They also take
 * the store mutex so UI reads cannot race bank activation. begin() erases the
 * inactive bank and can block; none of these functions belongs in a BLE or
 * button callback. */
wallet_store_result_t wallet_store_begin(uint32_t expected_length,
                                         uint32_t expected_crc);
wallet_store_result_t wallet_store_write(uint32_t offset,
                                         const uint8_t *data,
                                         size_t length);
wallet_store_result_t wallet_store_commit(void);
typedef bool (*wallet_store_commit_guard_t)(void *context);
/* The guard is checked before entering the final durable commit phase and
 * again after the NVS pointer update. Once the second check passes, writing
 * the marker is the atomic commit point and finishes even if the link drops;
 * the caller may lose the response but reboot observes a whole new snapshot. */
wallet_store_result_t wallet_store_commit_guarded(wallet_store_commit_guard_t guard,
                                                  void *context);
void wallet_store_abort(void);

/* Snapshot metadata and page pixels are copied while holding the store mutex.
 * kind is the PCW1 kind (0..4); ordinal is within that kind. image_size must
 * be at least WALLET_PAGE_IMAGE_SIZE. A successful commit increments revision,
 * allowing the UI to invalidate cached pages only after atomic publication. */
wallet_store_result_t wallet_store_get_info(wallet_store_info_t *out);
wallet_store_result_t wallet_store_read_page(uint8_t kind,
                                             uint16_t ordinal,
                                             uint8_t metadata[4],
                                             uint8_t *image,
                                             size_t image_size);
/* Check expected_revision and copy the page under the SAME store mutex.
 * STALE_SNAPSHOT leaves metadata/image untouched. The UI must refresh its
 * snapshot and reset reveal state before retrying; new private pixels must
 * never inherit an older snapshot's reveal permission. */
wallet_store_result_t wallet_store_read_page_at_revision(uint32_t expected_revision,
    uint8_t kind, uint16_t ordinal, uint8_t metadata[4], uint8_t *image,
    size_t image_size);

#ifdef WALLET_STORE_HOST_TEST
typedef struct {
    void *context;
    int (*read)(void *context, unsigned bank, size_t offset,
                void *data, size_t length);
    int (*write)(void *context, unsigned bank, size_t offset,
                 const void *data, size_t length);
    int (*erase_bank)(void *context, unsigned bank);
    int (*load_active_bank)(void *context, unsigned *bank);
    int (*save_active_bank)(void *context, unsigned bank);
} wallet_store_backend_t;

/* Replaces the host backend and resets volatile state, simulating reboot. */
void wallet_store_test_bind(const wallet_store_backend_t *backend);
#endif
