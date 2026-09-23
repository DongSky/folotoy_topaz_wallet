#pragma once

#include "wallet_transfer.h"

#include <stdbool.h>
#include <stdint.h>

#define WALLET_BLE_PAIRING_WINDOW_MS 120000u
#define WALLET_BLE_TRANSFER_IDLE_MS 30000u
#define WALLET_BLE_PACKET_MAX 512u
#define WALLET_BLE_DEVICE_NAME "Folo Wallet"

/* BLE_UUID128_INIT byte order for the canonical UUIDs in the PCW1 spec. */
#define WALLET_BLE_SERVICE_UUID_BYTES \
    0x74, 0x70, 0x73, 0x73, 0x61, 0x70, 0x55, 0x9a, \
    0x65, 0x4c, 0x6f, 0x6c, 0x01, 0x00, 0x10, 0xf0
#define WALLET_BLE_WRITE_UUID_BYTES \
    0x74, 0x70, 0x73, 0x73, 0x61, 0x70, 0x55, 0x9a, \
    0x65, 0x4c, 0x6f, 0x6c, 0x02, 0x00, 0x10, 0xf0
#define WALLET_BLE_STATUS_UUID_BYTES \
    0x74, 0x70, 0x73, 0x73, 0x61, 0x70, 0x55, 0x9a, \
    0x65, 0x4c, 0x6f, 0x6c, 0x03, 0x00, 0x10, 0xf0

typedef enum {
    WALLET_BLE_OK = 0,
    WALLET_BLE_INVALID_ARG,
    WALLET_BLE_INVALID_STATE,
    WALLET_BLE_NO_MEMORY,
    WALLET_BLE_PLATFORM_ERROR,
} wallet_ble_result_t;

typedef struct {
    bool initialized;
    bool window_open;
    bool advertising;
    bool connected;
    bool secure;
    bool owner_present;
    bool owner_recovery_required;
    bool pairing_pending;
    bool passkey_released;
    uint32_t passkey;
    uint32_t window_remaining_ms;
    wallet_transfer_status_t transfer;
    int last_error;
} wallet_ble_info_t;

/* Wallet-only init starts the host without advertising; opening the physical
 * pairing window enables wallet advertisements. Callbacks capture session state
 * and enqueue bounded work; one worker owns transfer parsing and Flash calls. */
wallet_ble_result_t wallet_ble_init(void);
/* Starts the SAME host with both services. Stock service is intentionally
 * unpaired legacy access, advertised as TRAECARD with the actual 12-hex serial;
 * identity72 contains sensitive legacy credentials and is never logged.
 * Serial must be a NUL-terminated 12-character lowercase hex string matching
 * identity's SN slot. Status is the prepared 9-byte stock game-status packet.
 * Stock advertising/normal connections do not require the PCW pairing window.
 * The physical window enables authenticated SC pairing for the custom wallet
 * service; existing authorized secure links survive window expiry. Explicit
 * close/reset may disconnect. Owner-record failures disable ONLY custom PCW.
 * Caller must run stock_ble_poll/content on its own worker; not polled here.
 * PCW clients must scan stock UUID then discover the custom service capability.
 * Init APIs are mutually exclusive and may be called only before host start. */
wallet_ble_result_t wallet_ble_init_stock(const uint8_t identity[72],
    const uint8_t status[9], const char serial[13]);
wallet_ble_result_t wallet_ble_open_pairing_window(uint32_t duration_ms);
void wallet_ble_close_pairing_window(void);
/* Physical UI authorization. confirm_passkey(true) releases the displayed
 * six-digit key to the pending Android pairing; false rejects it. reset_owner
 * is called only after the device-local reset confirmation flow. It removes
 * the sole bond and never erases NVS wholesale. */
wallet_ble_result_t wallet_ble_confirm_passkey(bool accept);
wallet_ble_result_t wallet_ble_reset_owner(void);
void wallet_ble_get_info(wallet_ble_info_t *out);

/* Pure policy helpers used by the host security tests and firmware gates. */
bool wallet_ble_peer_allowed(bool window_open, bool owner_present, bool owner_match);
bool wallet_ble_link_authorized(bool encrypted, bool authenticated, bool bonded,
                                uint8_t key_size, bool owner_match);
bool wallet_ble_passkey_can_release(bool window_open, bool pairing_pending,
                                    bool peer_allowed);
bool wallet_ble_session_matches(uint32_t active_generation, uint16_t active_handle,
                                bool secure, uint32_t event_generation,
                                uint16_t event_handle);
bool wallet_ble_close_should_disconnect(bool explicit_close, bool connected, bool secure);
bool wallet_ble_owner_record_valid(size_t size, uint8_t address_type);
bool wallet_ble_stock_serial_valid(const uint8_t identity[72], const char serial[13]);
bool wallet_ble_advertising_allowed(bool stock_mode, bool window_open, bool host_synced,
    bool connected, bool reset_pending, bool owner_recovery);
bool wallet_ble_connection_allowed(bool stock_mode, bool window_open, bool owner_present,
    bool owner_match, bool owner_recovery, bool reset_pending);
bool wallet_ble_window_disconnect(bool stock_mode, bool explicit_close,
    bool connected, bool secure, bool pairing_pending);
