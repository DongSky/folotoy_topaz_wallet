#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STOCK_AVATAR_WIDTH 96u
#define STOCK_AVATAR_HEIGHT 156u
#define STOCK_AVATAR_BGRA_SIZE (STOCK_AVATAR_WIDTH * STOCK_AVATAR_HEIGHT * 4u)
typedef enum {
    STOCK_AVATAR_OK, STOCK_AVATAR_INVALID, STOCK_AVATAR_CRC, STOCK_AVATAR_NOT_FOUND,
    STOCK_AVATAR_IO, STOCK_AVATAR_READONLY_REQUIRED, STOCK_AVATAR_BUSY,
    STOCK_AVATAR_CANCELLED, STOCK_AVATAR_NO_MEMORY
} stock_avatar_result_t;
typedef struct { const uint8_t *bytes; size_t size, total; uint8_t count; uint16_t index_end; } stock_avatar_archive_t;
typedef struct { uint16_t width, height; uint8_t channels; size_t compressed_size; } stock_avatar_png_info_t;
typedef bool (*stock_avatar_guard_t)(void *context);
typedef bool (*stock_avatar_row_sink_t)(void *context, unsigned row, const uint8_t *bgra, size_t length);
typedef struct { const uint8_t *bgra; size_t length; uint16_t width, height, stride; uint32_t lease; } stock_avatar_image_t;

/* Pure AVA1 parser, offsets independent of ABI. Input must remain immutable
 * and alive until archive is discarded. Validation checks index CRC, names,
 * all ranges/alignment/duplicates/overlaps. Lookup additionally checks payload
 * CRC. No assumptions about which names a particular factory archive contains. */
stock_avatar_result_t stock_avatar_archive_validate(const uint8_t *bytes, size_t size, stock_avatar_archive_t *archive);
stock_avatar_result_t stock_avatar_archive_find(const stock_avatar_archive_t *archive, const char *name, const uint8_t **png, size_t *length);
/* Strict PNG container validation before writes: exact96x156, bit depth8,
 * noninterlaced color0/2/6, every chunk CRC, ordering/ranges, complete IEND.
 * Rejects unknown critical chunks and tRNS (no unsupported transparency loss).
 * Accepted ancillary chunks do not change color interpretation. IDAT zlib
 * integrity, row filters and exact output budget are checked while decoding. */
stock_avatar_result_t stock_avatar_png_info(const uint8_t *png, size_t length, stock_avatar_png_info_t *info);
/* In-place PNG filter reversal, previous may be NULL for the first row. */
bool stock_avatar_unfilter(uint8_t filter, uint8_t *row, const uint8_t *previous, size_t length, unsigned channels);
/* Worker-only. ROM tinfl streaming:32KB dictionary+decoder state+three rows,
 * never a full decoded image allocation. Sink must consume row before return;
 * false aborts. No flash is accessed by this pure decoder. */
stock_avatar_result_t stock_avatar_png_decode_rows(const uint8_t *png, size_t length, stock_avatar_row_sink_t sink, void *context);

/* Singleton worker-only adapter. Call once after partition setup; no concurrent
 * calls. Maps readonly imgava for application lifetime, never writes it. No NVS,
 * cardid, imguser or imgstore access. Repeated successful init is idempotent. */
stock_avatar_result_t stock_avatar_init(void);
stock_avatar_result_t stock_avatar_find(const char *name, const uint8_t **png, size_t *length);
/* Caller must stop UI/decoders and release ALL external imgframe maps before
 * prepare: imgframe is shared with JPEG scratch and cannot be overwritten while
 * another decoder uses it. This adapter rejects its OWN active raw-image lease.
 * Validates container before erasing, then streams BGRA8888 at offset0. Failure
 * after erase may leave partial scratch; no output/map is published. Guard is
 * optional, checked before/after erase and each row write. An operation already
 * issued cannot be undone when the guard changes. Release image only after UI
 * stops reading. On failure caller's output is zeroed (must be empty on entry).
 * PNG decode is not a guarantee of correct physical-device color rendering. */
stock_avatar_result_t stock_avatar_prepare(const char *name, stock_avatar_guard_t guard, void *context, stock_avatar_image_t *image);
void stock_avatar_release(stock_avatar_image_t *image);
