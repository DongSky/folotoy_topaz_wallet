#include "wallet_transfer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static wallet_transfer_action_t begin_transfer(wallet_transfer_t *transfer, uint32_t length)
{
    uint8_t begin[9] = {WALLET_MSG_BEGIN};
    wallet_transfer_action_t action;

    put32(begin + 1, length);
    put32(begin + 5, 0x78563412u);
    assert(wallet_transfer_input(transfer, true, begin, sizeof(begin), &action) ==
           WALLET_TRANSFER_ACCEPTED);
    assert(action.type == WALLET_ACTION_BEGIN);
    assert(action.expected == length && action.crc32 == 0x78563412u);
    assert(wallet_transfer_get_status(transfer).state == WALLET_TRANSFER_BUSY);
    wallet_transfer_complete(transfer, &action, true, WALLET_ERROR_NONE);
    assert(wallet_transfer_get_status(transfer).state == WALLET_TRANSFER_READY);
    return action;
}

static void test_authentication_is_required_before_protocol_parsing(void)
{
    wallet_transfer_t transfer;
    wallet_transfer_action_t action;
    uint8_t begin[9] = {WALLET_MSG_BEGIN};

    wallet_transfer_init(&transfer);
    put32(begin + 1, WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE);
    assert(wallet_transfer_input(&transfer, false, begin, sizeof(begin), &action) ==
           WALLET_TRANSFER_REJECTED);
    assert(action.type == WALLET_ACTION_NONE);
    assert(wallet_transfer_get_status(&transfer).error == WALLET_ERROR_AUTH);
}

static void test_data_must_be_exactly_sequential(void)
{
    wallet_transfer_t transfer;
    wallet_transfer_action_t action;
    uint8_t data[10] = {WALLET_MSG_DATA};

    wallet_transfer_init(&transfer);
    begin_transfer(&transfer, WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE);
    put32(data + 1, 1);
    assert(wallet_transfer_input(&transfer, true, data, sizeof(data), &action) ==
           WALLET_TRANSFER_REJECTED);
    assert(action.type == WALLET_ACTION_ABORT);
    assert(wallet_transfer_get_status(&transfer).state == WALLET_TRANSFER_ERROR);
    assert(wallet_transfer_get_status(&transfer).error == WALLET_ERROR_OFFSET);
}

static void test_incomplete_commit_aborts_staging(void)
{
    wallet_transfer_t transfer;
    wallet_transfer_action_t action;
    const uint8_t commit[] = {WALLET_MSG_COMMIT};

    wallet_transfer_init(&transfer);
    begin_transfer(&transfer, WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE);
    assert(wallet_transfer_input(&transfer, true, commit, sizeof(commit), &action) ==
           WALLET_TRANSFER_REJECTED);
    assert(action.type == WALLET_ACTION_ABORT);
    assert(wallet_transfer_get_status(&transfer).error == WALLET_ERROR_INCOMPLETE);
}

static void test_disconnect_aborts_only_unfinished_transfer(void)
{
    wallet_transfer_t transfer;
    wallet_transfer_action_t action;
    uint8_t data[5 + WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE] = {WALLET_MSG_DATA};
    const uint32_t package_size = WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE;

    wallet_transfer_init(&transfer);
    begin_transfer(&transfer, package_size);
    put32(data + 1, 0);
    assert(wallet_transfer_input(&transfer, true, data, sizeof(data), &action) ==
           WALLET_TRANSFER_ACCEPTED);
    assert(action.type == WALLET_ACTION_DATA && action.length == package_size);
    wallet_transfer_complete(&transfer, &action, true, WALLET_ERROR_NONE);
    assert(wallet_transfer_get_status(&transfer).received == package_size);
    assert(wallet_transfer_disconnect(&transfer, &action));
    assert(action.type == WALLET_ACTION_ABORT);
    assert(wallet_transfer_get_status(&transfer).state == WALLET_TRANSFER_IDLE);

    wallet_transfer_init(&transfer);
    begin_transfer(&transfer, package_size);
    put32(data + 1, 0);
    assert(wallet_transfer_input(&transfer, true, data, sizeof(data), &action) ==
           WALLET_TRANSFER_ACCEPTED);
    wallet_transfer_complete(&transfer, &action, true, WALLET_ERROR_NONE);
    const uint8_t commit[] = {WALLET_MSG_COMMIT};
    assert(wallet_transfer_input(&transfer, true, commit, sizeof(commit), &action) ==
           WALLET_TRANSFER_ACCEPTED);
    wallet_transfer_complete(&transfer, &action, true, WALLET_ERROR_NONE);
    assert(wallet_transfer_get_status(&transfer).state == WALLET_TRANSFER_COMMITTED);
    assert(!wallet_transfer_disconnect(&transfer, &action));
    assert(wallet_transfer_get_status(&transfer).state == WALLET_TRANSFER_COMMITTED);
}

static void test_status_encoding_is_exactly_twelve_little_endian_bytes(void)
{
    wallet_transfer_t transfer;
    uint8_t encoded[WALLET_STATUS_SIZE];

    wallet_transfer_init(&transfer);
    begin_transfer(&transfer, 0x00010203u);
    wallet_transfer_encode_status(&transfer, encoded);
    const uint8_t expected[WALLET_STATUS_SIZE] = {
        WALLET_TRANSFER_READY, WALLET_ERROR_NONE, 0, 0,
        0, 0, 0, 0, 3, 2, 1, 0
    };
    assert(memcmp(encoded, expected, sizeof(expected)) == 0);
}

static void test_oversized_data_and_busy_retries_are_bounded(void)
{
    wallet_transfer_t transfer;
    wallet_transfer_action_t action;
    uint8_t begin[9] = {WALLET_MSG_BEGIN};

    wallet_transfer_init(&transfer);
    put32(begin + 1, WALLET_MAX_SIZE + 1u);
    assert(wallet_transfer_input(&transfer, true, begin, sizeof(begin), &action) ==
           WALLET_TRANSFER_REJECTED);
    assert(wallet_transfer_get_status(&transfer).error == WALLET_ERROR_SIZE);

    wallet_transfer_init(&transfer);
    put32(begin + 1, WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE);
    assert(wallet_transfer_input(&transfer, true, begin, sizeof(begin), &action) ==
           WALLET_TRANSFER_ACCEPTED);
    assert(wallet_transfer_input(&transfer, true, begin, sizeof(begin), &action) ==
           WALLET_TRANSFER_RETRY);
    assert(wallet_transfer_get_status(&transfer).state == WALLET_TRANSFER_BUSY);
}

static void test_idle_timeout_aborts_unpublished_staging(void)
{
    wallet_transfer_t transfer;
    wallet_transfer_action_t action;

    wallet_transfer_init(&transfer);
    begin_transfer(&transfer, WALLET_HEADER_SIZE + WALLET_PAGE_RECORD_SIZE);
    assert(wallet_transfer_timeout(&transfer, &action));
    assert(action.type == WALLET_ACTION_ABORT);
    assert(wallet_transfer_get_status(&transfer).state == WALLET_TRANSFER_ERROR);
    assert(wallet_transfer_get_status(&transfer).error == WALLET_ERROR_TIMEOUT);

    wallet_transfer_init(&transfer);
    assert(!wallet_transfer_timeout(&transfer, &action));
}

int main(void)
{
    test_authentication_is_required_before_protocol_parsing();
    test_data_must_be_exactly_sequential();
    test_incomplete_commit_aborts_staging();
    test_disconnect_aborts_only_unfinished_transfer();
    test_status_encoding_is_exactly_twelve_little_endian_bytes();
    test_oversized_data_and_busy_retries_are_bounded();
    test_idle_timeout_aborts_unpublished_staging();
    puts("Wallet transfer: PASS");
    return 0;
}
