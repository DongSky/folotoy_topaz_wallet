#include "stock_transfer.h"
#include "stock_protocol.h"

void stock_transfer_init(stock_transfer_t *state, stock_transfer_kind_t kind,
                         uint32_t capacity)
{
    if (state) *state = (stock_transfer_t){.kind = kind, .capacity = capacity};
}

void stock_transfer_reset(stock_transfer_t *state)
{
    if (!state) return;
    state->active = false;
    state->declared = 0;
    state->received = 0;
}

static stock_transfer_result_t result(uint8_t status, bool completed)
{
    return (stock_transfer_result_t){status, completed};
}

stock_transfer_result_t stock_transfer_receive(stock_transfer_t *state,
    const stock_transfer_sink_t *sink, const uint8_t *payload, size_t length)
{
    if (!state || !payload || !length || length > STOCK_PAYLOAD_MAX ||
        (state->kind != STOCK_TRANSFER_IMAGE && state->kind != STOCK_TRANSFER_AUDIO))
        return result(0x12, false);
    if (!sink || !sink->begin || !sink->data || !sink->end)
        return result(0x16, false);

    if (payload[0] == 0) {
        size_t required = state->kind == STOCK_TRANSFER_IMAGE ? 5u : 3u;
        if (length < required) return result(0x12, false);
        uint32_t total = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8);
        if (required == 5)
            total |= ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
        if (total > state->capacity) {
            state->active = false;
            return result(0x15, false);
        }
        stock_sink_result_t rc = sink->begin(sink->context, total);
        if (rc != STOCK_SINK_OK) {
            state->active = false;
            return result(rc == STOCK_SINK_LIMIT ? 0x15 : 0x16, false);
        }
        state->declared = total;
        state->received = 0;
        state->active = true;
        return result(0, false);
    }
    if (payload[0] == 1) {
        if (!state->active) return result(0x12, false);
        size_t count = length - 1;
        /* Stock image storage delegates capacity checks to flash APIs. Bound
         * them here before calling a backend and never wrap its byte counter. */
        if (state->received > state->capacity ||
            count > state->capacity - state->received) {
            state->active = false;
            return result(state->kind == STOCK_TRANSFER_IMAGE ? 0x16 : 0x15, false);
        }
        if (count) {
            stock_sink_result_t rc = sink->data(sink->context, payload + 1, count);
            if (rc != STOCK_SINK_OK) {
                state->active = false;
                return result(0x16, false);
            }
            state->received += (uint32_t)count;
        }
        return result(0, false);
    }
    if (payload[0] == 2) {
        if (!state->active) return result(0x12, false);
        state->active = false;
        if (state->received != state->declared) return result(0x12, false);
        stock_sink_result_t rc = sink->end(sink->context);
        if (rc != STOCK_SINK_OK) {
            return result(state->kind == STOCK_TRANSFER_AUDIO &&
                          rc == STOCK_SINK_INVALID ? 0x13 : 0x16, false);
        }
        return result(state->kind == STOCK_TRANSFER_AUDIO ? 1 : 0, true);
    }
    return result(0x12, false);
}
