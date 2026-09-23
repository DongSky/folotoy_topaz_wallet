#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Stock 1.0.3 frame envelope, statically verified against saved application.
 * This module alone does not implement or establish mini-program compatibility.
 * Caller owns timeouts, GATT, persistence and command-specific responses. */
#define STOCK_PAYLOAD_MAX 256u
#define STOCK_FRAME_MAX (STOCK_PAYLOAD_MAX + 4u)
typedef enum {
    STOCK_FRAME_OK,
    STOCK_FRAME_BAD_HEADER, /* wire response 00 10 */
    STOCK_FRAME_TOO_LARGE   /* wire response 00 15 */
} stock_frame_result_t;
typedef void (*stock_frame_callback_t)(void *context, uint8_t type,
                                      const uint8_t *payload, size_t length);
typedef struct {
    uint8_t bytes[STOCK_FRAME_MAX];
    size_t used;
} stock_stream_t;
void stock_stream_reset(stock_stream_t *stream);
/* One call represents ONE ATT write. Stock returns after its first full frame,
 * dropping trailing bytes in this write. Partial frames span calls. Version
 * validation occurs only after all four header bytes arrive. Timeout/disconnect
 * reset belongs to caller. Callback must not recursively feed the same stream.
 * Evidence: stock 1.0.3 VAs 0x4204318c and 0x42013e62; local analysis notes in
 * build/card-wallet-redesign/reference/protocol-findings.txt. */
stock_frame_result_t stock_stream_feed(stock_stream_t *stream,
                                      const uint8_t *data, size_t length,
                                      stock_frame_callback_t callback, void *context);
/* Return encoded byte count, or zero if input/capacity invalid. */
size_t stock_frame_encode(uint8_t type, const uint8_t *payload, size_t length,
                          uint8_t *output, size_t capacity);
/* Notifications use command TYPE then STATUS, not protocol-version then status. */
void stock_response_encode(uint8_t type, uint8_t status, uint8_t output[2]);
