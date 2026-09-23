#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Own implementation from stock 1.0.3 instruction-level evidence, not copied
 * third-party source. Parser VAs 0x42043244/0x4204335c, dispatch 0x4200b43c;
 * see build/card-wallet-redesign/reference/protocol-findings.txt.
 * This module alone does not establish mini-program compatibility. */
typedef enum { STOCK_TRANSFER_IMAGE, STOCK_TRANSFER_AUDIO } stock_transfer_kind_t;
typedef enum {
    STOCK_SINK_OK, STOCK_SINK_LIMIT, STOCK_SINK_IO, STOCK_SINK_INVALID
} stock_sink_result_t;
typedef struct {
    void *context;
    stock_sink_result_t (*begin)(void *context, uint32_t total);
    stock_sink_result_t (*data)(void *context, const uint8_t *bytes, size_t length);
    /* Audio end must validate RTTTL and persist before reporting OK. Image end
     * commits storage only; successful transport is not successful decoding. */
    stock_sink_result_t (*end)(void *context);
} stock_transfer_sink_t;
typedef struct {
    stock_transfer_kind_t kind;
    uint32_t capacity, declared, received;
    bool active;
} stock_transfer_t;
typedef struct { uint8_t status; bool completed; } stock_transfer_result_t;
void stock_transfer_init(stock_transfer_t *state, stock_transfer_kind_t kind,
                         uint32_t capacity);
/* Clears transport state, preserving kind/capacity. Caller owns backend cleanup. */
void stock_transfer_reset(stock_transfer_t *state);
/* Worker-only: invokes potentially blocking caller callbacks synchronously.
 * Payload is already stripped of its four-byte envelope and <=256 bytes.
 * BEGIN: 00,u32LE(image) or 00,u16LE(audio); DATA:01,bytes; END:02.
 * Extra BEGIN/END bytes are ignored as stock does. DATA beyond declared size
 * may accumulate within capacity, but END rejects length mismatch. Caller must
 * implement busy checks before BEGIN and must stop/reset on disconnect. */
stock_transfer_result_t stock_transfer_receive(stock_transfer_t *state,
    const stock_transfer_sink_t *sink, const uint8_t *payload, size_t length);
