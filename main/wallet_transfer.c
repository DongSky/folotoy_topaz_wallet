#include "wallet_transfer.h"

#include <string.h>

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void clear_action(wallet_transfer_action_t *action)
{
    memset(action, 0, sizeof(*action));
}

static wallet_transfer_result_t fail(wallet_transfer_t *transfer,
                                     wallet_transfer_action_t *action,
                                     wallet_error_t error,
                                     bool abort_staging)
{
    transfer->status.state = WALLET_TRANSFER_ERROR;
    transfer->status.error = (uint8_t)error;
    transfer->pending = WALLET_ACTION_NONE;
    transfer->pending_length = 0;
    clear_action(action);
    if (abort_staging) {
        action->type = WALLET_ACTION_ABORT;
    }
    return WALLET_TRANSFER_REJECTED;
}

void wallet_transfer_init(wallet_transfer_t *transfer)
{
    if (transfer != NULL) {
        memset(transfer, 0, sizeof(*transfer));
        transfer->status.state = WALLET_TRANSFER_IDLE;
    }
}

wallet_transfer_result_t wallet_transfer_input(wallet_transfer_t *transfer,
                                               bool authorized,
                                               const uint8_t *message,
                                               size_t length,
                                               wallet_transfer_action_t *action)
{
    uint32_t offset;
    uint32_t expected;

    if (transfer == NULL || action == NULL) {
        return WALLET_TRANSFER_REJECTED;
    }
    clear_action(action);
    if (!authorized) {
        return fail(transfer, action, WALLET_ERROR_AUTH,
                    transfer->status.state == WALLET_TRANSFER_READY ||
                    transfer->status.state == WALLET_TRANSFER_RECEIVING);
    }
    if (message == NULL || length == 0) {
        return fail(transfer, action, WALLET_ERROR_PROTOCOL, false);
    }
    if (transfer->pending != WALLET_ACTION_NONE ||
        transfer->status.state == WALLET_TRANSFER_BUSY) {
        return WALLET_TRANSFER_RETRY;
    }

    switch (message[0]) {
    case WALLET_MSG_BEGIN:
        if (length != 9) {
            return fail(transfer, action, WALLET_ERROR_PROTOCOL, false);
        }
        expected = read32(message + 1);
        if (expected < WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE ||
            expected > WALLET_MAX_SIZE) {
            return fail(transfer, action, WALLET_ERROR_SIZE, false);
        }
        if (transfer->status.state == WALLET_TRANSFER_READY ||
            transfer->status.state == WALLET_TRANSFER_RECEIVING) {
            return fail(transfer, action, WALLET_ERROR_PROTOCOL, true);
        }
        transfer->status.state = WALLET_TRANSFER_BUSY;
        transfer->status.error = WALLET_ERROR_NONE;
        transfer->status.received = 0;
        transfer->status.expected = expected;
        transfer->pending = WALLET_ACTION_BEGIN;
        action->type = WALLET_ACTION_BEGIN;
        action->expected = expected;
        action->crc32 = read32(message + 5);
        return WALLET_TRANSFER_ACCEPTED;

    case WALLET_MSG_DATA:
        if (length <= 5 || (transfer->status.state != WALLET_TRANSFER_READY &&
                           transfer->status.state != WALLET_TRANSFER_RECEIVING)) {
            return fail(transfer, action, WALLET_ERROR_PROTOCOL,
                        transfer->status.state == WALLET_TRANSFER_READY ||
                        transfer->status.state == WALLET_TRANSFER_RECEIVING);
        }
        offset = read32(message + 1);
        if (offset != transfer->status.received) {
            return fail(transfer, action, WALLET_ERROR_OFFSET, true);
        }
        if (length - 5 > (size_t)(transfer->status.expected - transfer->status.received)) {
            return fail(transfer, action, WALLET_ERROR_SIZE, true);
        }
        transfer->pending = WALLET_ACTION_DATA;
        transfer->pending_length = length - 5;
        action->type = WALLET_ACTION_DATA;
        action->offset = offset;
        action->data = message + 5;
        action->length = length - 5;
        return WALLET_TRANSFER_ACCEPTED;

    case WALLET_MSG_COMMIT:
        if (length != 1) {
            return fail(transfer, action, WALLET_ERROR_PROTOCOL, true);
        }
        if (transfer->status.state != WALLET_TRANSFER_RECEIVING &&
            transfer->status.state != WALLET_TRANSFER_READY) {
            return fail(transfer, action, WALLET_ERROR_PROTOCOL, false);
        }
        if (transfer->status.received != transfer->status.expected) {
            return fail(transfer, action, WALLET_ERROR_INCOMPLETE, true);
        }
        transfer->status.state = WALLET_TRANSFER_BUSY;
        transfer->pending = WALLET_ACTION_COMMIT;
        action->type = WALLET_ACTION_COMMIT;
        return WALLET_TRANSFER_ACCEPTED;

    case WALLET_MSG_ABORT:
        if (length != 1) {
            return fail(transfer, action, WALLET_ERROR_PROTOCOL, true);
        }
        wallet_transfer_init(transfer);
        action->type = WALLET_ACTION_ABORT;
        return WALLET_TRANSFER_ACCEPTED;

    default:
        return fail(transfer, action, WALLET_ERROR_PROTOCOL,
                    transfer->status.state == WALLET_TRANSFER_READY ||
                    transfer->status.state == WALLET_TRANSFER_RECEIVING);
    }
}

void wallet_transfer_complete(wallet_transfer_t *transfer,
                              const wallet_transfer_action_t *action,
                              bool success,
                              wallet_error_t error)
{
    if (transfer == NULL || action == NULL || action->type != transfer->pending) {
        return;
    }
    transfer->pending = WALLET_ACTION_NONE;
    if (!success) {
        transfer->pending_length = 0;
        transfer->status.state = WALLET_TRANSFER_ERROR;
        transfer->status.error = error == WALLET_ERROR_NONE ? WALLET_ERROR_STORAGE : error;
        return;
    }
    transfer->status.error = WALLET_ERROR_NONE;
    switch (action->type) {
    case WALLET_ACTION_BEGIN:
        transfer->status.state = WALLET_TRANSFER_READY;
        break;
    case WALLET_ACTION_DATA:
        transfer->status.received += (uint32_t)transfer->pending_length;
        transfer->pending_length = 0;
        transfer->status.state = WALLET_TRANSFER_RECEIVING;
        break;
    case WALLET_ACTION_COMMIT:
        transfer->status.state = WALLET_TRANSFER_COMMITTED;
        break;
    case WALLET_ACTION_ABORT:
        wallet_transfer_init(transfer);
        break;
    default:
        break;
    }
}

bool wallet_transfer_disconnect(wallet_transfer_t *transfer,
                                wallet_transfer_action_t *action)
{
    bool unfinished;

    if (transfer == NULL || action == NULL) {
        return false;
    }
    unfinished = transfer->status.state == WALLET_TRANSFER_READY ||
                 transfer->status.state == WALLET_TRANSFER_RECEIVING ||
                 transfer->status.state == WALLET_TRANSFER_BUSY ||
                 transfer->pending != WALLET_ACTION_NONE;
    clear_action(action);
    if (!unfinished) {
        return false;
    }
    action->type = WALLET_ACTION_ABORT;
    wallet_transfer_init(transfer);
    return true;
}

bool wallet_transfer_timeout(wallet_transfer_t *transfer,
                             wallet_transfer_action_t *action)
{
    bool unfinished;

    if (transfer == NULL || action == NULL) {
        return false;
    }
    unfinished = transfer->status.state == WALLET_TRANSFER_READY ||
                 transfer->status.state == WALLET_TRANSFER_RECEIVING ||
                 transfer->status.state == WALLET_TRANSFER_BUSY ||
                 transfer->pending != WALLET_ACTION_NONE;
    clear_action(action);
    if (!unfinished) {
        return false;
    }
    action->type = WALLET_ACTION_ABORT;
    transfer->pending = WALLET_ACTION_NONE;
    transfer->pending_length = 0;
    transfer->status.state = WALLET_TRANSFER_ERROR;
    transfer->status.error = WALLET_ERROR_TIMEOUT;
    return true;
}

wallet_transfer_status_t wallet_transfer_get_status(const wallet_transfer_t *transfer)
{
    const wallet_transfer_status_t empty = {0};
    return transfer == NULL ? empty : transfer->status;
}

void wallet_transfer_encode_status(const wallet_transfer_t *transfer,
                                   uint8_t output[WALLET_STATUS_SIZE])
{
    wallet_transfer_status_t status = wallet_transfer_get_status(transfer);

    if (output == NULL) {
        return;
    }
    output[0] = status.state;
    output[1] = status.error;
    output[2] = 0;
    output[3] = 0;
    put32(output + 4, status.received);
    put32(output + 8, status.expected);
}
