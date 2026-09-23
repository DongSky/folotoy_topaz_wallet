#pragma once
#include "stock_profile.h"
#include "stock_profile_nvs.h"
#include "stock_transfer.h"
#include "esp_partition.h"

#define STOCK_CONTENT_METADATA_SIZE 4096u
/* Confirmed stock mapping: profile img_mode 0 -> imguser; 1 -> imgstore. */
typedef enum { STOCK_CONTENT_AVATAR = 0, STOCK_CONTENT_FULLSCREEN = 1 } stock_content_mode_t;
typedef enum { STOCK_CONTENT_NONE, STOCK_CONTENT_PROFILE, STOCK_CONTENT_AVATAR_READY,
               STOCK_CONTENT_FULLSCREEN_READY } stock_content_event_t;
typedef struct {
    uint8_t response[2];
    stock_content_event_t event;
    stock_profile_changes_t changes;
} stock_content_result_t;
typedef bool (*stock_content_session_guard_t)(void *context, uint32_t session);
typedef struct {
    stock_profile_t profile;
    const esp_partition_t *images[2];
    stock_transfer_t transfer;
    stock_content_mode_t receiving_mode;
    uint32_t written, erased_capacity;
    unsigned leases[2];
    bool initialized;
    uint32_t session;
    bool session_known, cancelled;
    stock_content_session_guard_t guard;
    void *guard_context;
    bool (*avatar_exists)(void *context, const char *name);
    void *avatar_context;
} stock_content_t;
typedef struct {
    const uint8_t *jpeg;
    size_t length;
    esp_partition_mmap_handle_t handle;
    stock_content_t *owner;
    stock_content_mode_t mode;
} stock_content_image_t;

/* All operations worker-only; caller serializes calls and UI lease release.
 * Call init ONCE on zeroed storage, before views/commands. Loads existing
 * profile (defaults in memory only when absent), discovers image partitions.
 * No erase, image write, profile save or migration save at init. NVS load may
 * perform IDF normal recovery, as documented by stock_profile_nvs. Never touches
 * cardid, imgava, imgframe or audio. Identity remains a separate protected API.
 * INVALID_DATA/IO prevents subsequent writes until caller resolves the error. */
stock_profile_store_result_t stock_content_init(stock_content_t *content);
/* Unknown built-in avatar names are restored to the prior value like stock.
 * Without this optional validator all named-asset changes are rejected; no
 * speculative built-in resource parsing is performed. */
void stock_content_set_avatar_validator(stock_content_t *content,
    bool (*exists)(void *context, const char *name), void *context);
/* Types1 JSON /2 JPEG streaming only. Unsupported incl audio returns0x18.
 * Caller supplies decoder/screenshot busy flag. BEGIN locks selected mode for
 * the whole transfer; profile mode change during transfer returns0x1b.
 * JSON persists before publishing; changes.time is handed to caller to apply.
 * END success means committed bytes, NOT JPEG decode success. Caller updates
 * UI on event. Image replacement is destructive only after a valid BEGIN,
 * matching stock's erase+stream+JPG1 commit semantics; no power-loss rollback.
 * DATA is additionally bounded by the span erased at BEGIN (never write into
 * an unerased prior image tail). Avatar-profile save failure after committed
 * bytes returns0x16; the image bytes may exist but no UI event is published. */
stock_content_result_t stock_content_handle(stock_content_t *content, uint8_t type,
    const uint8_t *payload, size_t length, bool decoder_busy);
/* Use this entry point for BLE integration. Every session-token change resets
 * transfer state, including ingress overflow/session invalidation. Guard is
 * checked on entry, around each blocking flash operation and by the profile
 * NVS adapter before actual set/commit. False latches cancellation for this
 * call: returns0x12/no event and resets transfer. Guard may consult atomic BLE
 * state but must not call content APIs; no concurrent content mutation.
 * Callback/context live only for this call. Do not mix guarded and unguarded
 * calls within a transfer (unguarded uses session0 with no cancellation).
 * Last successful guard before each erase/write/set/commit authorizes that
 * operation. It may finish after disconnect; already-issued flash cannot be
 * undone, and nvs_set_blob itself may persist before commit. A cancelled result
 * suppresses stale UI/events, NOT guaranteed flash rollback. Reconcile/reload
 * profile if cancellation occurs during persistence and later state is needed. */
stock_content_result_t stock_content_handle_guarded(stock_content_t *content,
    uint32_t session, stock_content_session_guard_t guard, void *guard_context,
    uint8_t type, const uint8_t *payload, size_t length, bool decoder_busy);
/* Disconnect resets volatile transfer only; no write, erase, or fake commit. */
void stock_content_disconnect(stock_content_t *content);
/* Map validated JPG1 length as compressed JPEG bytes; caller must decode with
 * an appropriate LVGL JPEG decoder. The lease blocks replacement of that
 * partition. Pass an empty image handle; never overwrite an existing lease.
 * Release ONLY after UI/decoder stops reading, before next BEGIN.
 * No decode or JPEG-format acceptance is implied by mapping. */
bool stock_content_image_acquire(stock_content_t *content, stock_content_mode_t mode,
                                  stock_content_image_t *image);
void stock_content_image_release(stock_content_image_t *image);
