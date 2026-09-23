#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STOCK_IDENTITY_PARTITION "cardid"
#define STOCK_IDENTITY_NAMESPACE "folotoy-key"
#define STOCK_IDENTITY_WIRE_SIZE 72u

/* Stock 1.0.3 NVS capacities, INCLUDING the terminating NUL. These fields
 * contain provisioned credentials; never log, export, or include in dumps. */
typedef struct {
    char device_key[13];       /* DeviceKey: device serial number. */
    char device_secret[65];
    char product_key[33];
    char hardware_version[17];
} stock_identity_t;

typedef enum {
    STOCK_IDENTITY_OK = 0,
    STOCK_IDENTITY_INVALID_ARG,
    STOCK_IDENTITY_MISSING_PARTITION,
    STOCK_IDENTITY_READONLY_REQUIRED,
    STOCK_IDENTITY_NOT_PROVISIONED,
    STOCK_IDENTITY_INVALID_DATA,
    STOCK_IDENTITY_IO_ERROR,
} stock_identity_result_t;

/* Blocking, read-only lookup. Call from initialization/worker context, not a
 * BLE callback. Requires cardid to carry the partition-table readonly flag
 * BEFORE any NVS initialization (otherwise READONLY_REQUIRED). NVS_READONLY
 * alone does not prevent initialization recovery writes on a writable partition.
 * Initializes only that protected partition and opens folotoy-key NVS_READONLY.
 * Never erases, resets, writes or generates identity. Caller owns output and serialization/concurrency decisions.
 * NOT_PROVISIONED returns any valid fields that were present, with readiness
 * false. Other failures clear output. Missing SN/HW alone does not prevent
 * readiness: the stock predicate requires nonempty DeviceSecret+ProductKey. */
stock_identity_result_t stock_identity_load(stock_identity_t *output);

/* Rejects nonterminated fixed fields. No string read extends past a field. */
bool stock_identity_ready(const stock_identity_t *identity);

/* Legacy sensitive characteristic 0012, 72 bytes. Byte 0=1, byte 1=ready;
 * [2,26)=SN, [26,58)=DeviceSecret, [58,66)=HW, [66,72)=zero. ProductKey is NEVER
 * copied into this packet. Stock's temporary getter buffer is 40 bytes: a
 * DeviceSecret of 40..64 characters gives an EMPTY slot, not first32 bytes.
 * Sources shorter than 40 use min(strlen(source), slot width), zero padded.
 * This is intended only for official legacy compatibility, not app logging,
 * diagnostics, backups, or ordinary public wallet transport. Integration must
 * retain the same access constraints as the intended legacy characteristic.
 * Pure serializer; caller-supplied synthetic identity is sufficient for tests.
 * Returns false for malformed input/capacity. If capacity >=72, output is
 * cleared even on invalid identity; a smaller output is left untouched. */
bool stock_identity_serialize(const stock_identity_t *identity,
                              uint8_t *output, size_t capacity);

/* Explicit volatile clearing for caller-owned credential buffers after use. */
void stock_identity_clear(stock_identity_t *identity);
