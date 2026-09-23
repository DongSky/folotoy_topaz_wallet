#pragma once

#include "wallet_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WALLET_STATUS_SIZE 12u

typedef enum {
    WALLET_TRANSFER_IDLE = 0,
    WALLET_TRANSFER_READY = 1,
    WALLET_TRANSFER_RECEIVING = 2,
    WALLET_TRANSFER_COMMITTED = 3,
    WALLET_TRANSFER_ERROR = 4,
    WALLET_TRANSFER_BUSY = 5,
} wallet_transfer_state_t;

typedef enum {
    WALLET_ERROR_NONE = 0,
    WALLET_ERROR_AUTH = 1,
    WALLET_ERROR_PROTOCOL = 2,
    WALLET_ERROR_SIZE = 3,
    WALLET_ERROR_OFFSET = 4,
    WALLET_ERROR_INCOMPLETE = 5,
    WALLET_ERROR_CRC = 6,
    WALLET_ERROR_STORAGE = 7,
    WALLET_ERROR_OWNER = 8,
    WALLET_ERROR_TIMEOUT = 9,
    WALLET_ERROR_INTERNAL = 10,
} wallet_error_t;

typedef enum {
    WALLET_MSG_BEGIN = 0x01,
    WALLET_MSG_DATA = 0x02,
    WALLET_MSG_COMMIT = 0x03,
    WALLET_MSG_ABORT = 0x04,
} wallet_message_type_t;

typedef struct {
    uint8_t state;
    uint8_t error;
    uint32_t received;
    uint32_t expected;
} wallet_transfer_status_t;

typedef enum {
    WALLET_ACTION_NONE,
    WALLET_ACTION_BEGIN,
    WALLET_ACTION_DATA,
    WALLET_ACTION_COMMIT,
    WALLET_ACTION_ABORT,
} wallet_transfer_action_type_t;

typedef struct {
    wallet_transfer_action_type_t type;
    uint32_t offset;
    uint32_t expected;
    uint32_t crc32;
    const uint8_t *data;
    size_t length;
} wallet_transfer_action_t;

typedef enum {
    WALLET_TRANSFER_ACCEPTED,
    WALLET_TRANSFER_REJECTED,
    WALLET_TRANSFER_RETRY,
} wallet_transfer_result_t;

typedef struct {
    wallet_transfer_status_t status;
    wallet_transfer_action_type_t pending;
    size_t pending_length;
} wallet_transfer_t;

void wallet_transfer_init(wallet_transfer_t *transfer);
wallet_transfer_result_t wallet_transfer_input(wallet_transfer_t *transfer,
                                               bool authorized,
                                               const uint8_t *message,
                                               size_t length,
                                               wallet_transfer_action_t *action);
void wallet_transfer_complete(wallet_transfer_t *transfer,
                              const wallet_transfer_action_t *action,
                              bool success,
                              wallet_error_t error);
bool wallet_transfer_disconnect(wallet_transfer_t *transfer,
                                wallet_transfer_action_t *action);
bool wallet_transfer_timeout(wallet_transfer_t *transfer,
                             wallet_transfer_action_t *action);
wallet_transfer_status_t wallet_transfer_get_status(const wallet_transfer_t *transfer);
void wallet_transfer_encode_status(const wallet_transfer_t *transfer,
                                   uint8_t output[WALLET_STATUS_SIZE]);
