#include "stock_protocol.h"
#include <string.h>

void stock_stream_reset(stock_stream_t *stream)
{
    if (stream) memset(stream, 0, sizeof(*stream));
}

stock_frame_result_t stock_stream_feed(stock_stream_t *stream,
                                      const uint8_t *data, size_t length,
                                      stock_frame_callback_t callback, void *context)
{
    if (!stream || (!data && length) || !callback) return STOCK_FRAME_BAD_HEADER;
    while (length) {
        if (stream->used >= sizeof(stream->bytes)) {
            stock_stream_reset(stream);
            return STOCK_FRAME_BAD_HEADER;
        }
        stream->bytes[stream->used++] = *data++;
        --length;
        if (stream->used < 4) continue;
        if (stream->bytes[0] != 1) {
            stock_stream_reset(stream);
            return STOCK_FRAME_BAD_HEADER;
        }
        size_t payload = (size_t)stream->bytes[2] | ((size_t)stream->bytes[3] << 8);
        if (payload > STOCK_PAYLOAD_MAX) {
            stock_stream_reset(stream);
            return STOCK_FRAME_TOO_LARGE;
        }
        if (stream->used == payload + 4) {
            /* Callback may use payload only until it returns; no recursive feed. */
            callback(context, stream->bytes[1], stream->bytes + 4, payload);
            stock_stream_reset(stream);
            return STOCK_FRAME_OK;
        }
    }
    return STOCK_FRAME_OK;
}

size_t stock_frame_encode(uint8_t type, const uint8_t *payload, size_t length,
                          uint8_t *output, size_t capacity)
{
    if (!output || (!payload && length) || length > STOCK_PAYLOAD_MAX ||
        capacity < length + 4) return 0;
    output[0] = 1;
    output[1] = type;
    output[2] = (uint8_t)length;
    output[3] = (uint8_t)(length >> 8);
    if (length) memcpy(output + 4, payload, length);
    return length + 4;
}

void stock_response_encode(uint8_t type, uint8_t status, uint8_t output[2])
{
    output[0] = type;
    output[1] = status;
}
