#define WALLET_BLE_HOST_TEST 1
#include "wallet_ble.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_only_the_owner_gets_an_authenticated_data_link(void)
{
    assert(wallet_ble_link_authorized(true, true, true, 16, true));
    assert(!wallet_ble_link_authorized(false, true, true, 16, true));
    assert(!wallet_ble_link_authorized(true, false, true, 16, true));
    assert(!wallet_ble_link_authorized(true, true, false, 16, true));
    assert(!wallet_ble_link_authorized(true, true, true, 12, true));
    assert(!wallet_ble_link_authorized(true, true, true, 16, false));
}

static void test_pairing_requires_the_local_window_and_never_replaces_owner(void)
{
    assert(wallet_ble_peer_allowed(true, false, false));
    assert(wallet_ble_peer_allowed(true, true, true));
    assert(!wallet_ble_peer_allowed(false, false, false));
    assert(!wallet_ble_peer_allowed(false, true, true));
    assert(!wallet_ble_peer_allowed(true, true, false));
}

static void test_passkey_is_not_released_without_physical_confirmation_context(void)
{
    assert(wallet_ble_passkey_can_release(true, true, true));
    assert(!wallet_ble_passkey_can_release(false, true, true));
    assert(!wallet_ble_passkey_can_release(true, false, true));
    assert(!wallet_ble_passkey_can_release(true, true, false));
}

static void test_uuid_bytes_match_pcw1_canonical_values(void)
{
    const uint8_t service[16] = {WALLET_BLE_SERVICE_UUID_BYTES};
    const uint8_t write[16] = {WALLET_BLE_WRITE_UUID_BYTES};
    const uint8_t status[16] = {WALLET_BLE_STATUS_UUID_BYTES};
    const uint8_t suffix[12] = {
        0x74, 0x70, 0x73, 0x73, 0x61, 0x70, 0x55, 0x9a,
        0x65, 0x4c, 0x6f, 0x6c
    };
    assert(memcmp(service, suffix, sizeof(suffix)) == 0);
    assert(memcmp(write, suffix, sizeof(suffix)) == 0);
    assert(memcmp(status, suffix, sizeof(suffix)) == 0);
    assert(service[12] == 1 && write[12] == 2 && status[12] == 3);
    assert(service[13] == 0 && service[14] == 0x10 && service[15] == 0xf0);
}

static void test_stale_session_events_cannot_reuse_a_connection_handle(void)
{
    assert(wallet_ble_session_matches(8, 12, true, 8, 12));
    assert(!wallet_ble_session_matches(9, 12, true, 8, 12));
    assert(!wallet_ble_session_matches(8, 13, true, 8, 12));
    assert(!wallet_ble_session_matches(8, 12, false, 8, 12));
}

static void test_local_close_disconnects_owner_but_window_expiry_keeps_secure_sync(void)
{
    assert(wallet_ble_close_should_disconnect(true, true, true));
    assert(wallet_ble_close_should_disconnect(true, true, false));
    assert(!wallet_ble_close_should_disconnect(false, true, true));
    assert(wallet_ble_close_should_disconnect(false, true, false));
    assert(!wallet_ble_close_should_disconnect(true, false, false));
}

static void test_malformed_owner_records_are_classified_for_local_recovery(void)
{
    assert(wallet_ble_owner_record_valid(7, 0));
    assert(wallet_ble_owner_record_valid(7, 3));
    assert(!wallet_ble_owner_record_valid(6, 0));
    assert(!wallet_ble_owner_record_valid(8, 0));
    assert(!wallet_ble_owner_record_valid(7, 4));
}

static void test_stock_access_does_not_weaken_custom_pairing_policy(void)
{
    assert(wallet_ble_advertising_allowed(true,false,true,false,false,true));
    assert(!wallet_ble_advertising_allowed(false,false,true,false,false,false));
    assert(!wallet_ble_advertising_allowed(false,true,true,false,false,true));
    assert(!wallet_ble_advertising_allowed(true,true,true,true,false,false));
    assert(!wallet_ble_advertising_allowed(true,true,false,false,false,false));
    assert(!wallet_ble_advertising_allowed(true,true,true,false,true,false));
    assert(wallet_ble_connection_allowed(true,false,true,false,true,false));
    assert(!wallet_ble_connection_allowed(true,false,true,false,true,true));
    assert(!wallet_ble_connection_allowed(false,true,true,false,false,false));
    assert(!wallet_ble_connection_allowed(false,true,false,false,true,false));
    assert(wallet_ble_connection_allowed(false,true,false,false,false,false));
    assert(!wallet_ble_peer_allowed(false,false,false)); /* custom pairing still closed */
    assert(!wallet_ble_window_disconnect(true,false,true,false,false));
    assert(wallet_ble_window_disconnect(true,false,true,false,true));
    assert(!wallet_ble_window_disconnect(true,false,true,true,false));
    assert(wallet_ble_window_disconnect(true,true,true,true,false));
    assert(wallet_ble_window_disconnect(false,false,true,false,false));
    assert(!wallet_ble_window_disconnect(false,false,true,true,false));
}

static void test_stock_init_requires_the_actual_serial(void)
{
    uint8_t wire[72]={1,1}; const char serial[13]="a1b2c3d4e5f6";
    memcpy(wire+2,serial,12);
    assert(wallet_ble_stock_serial_valid(wire,serial));
    wire[1]=0;assert(wallet_ble_stock_serial_valid(wire,serial));
    assert(!wallet_ble_stock_serial_valid(wire,"A1b2c3d4e5f6"));
    assert(!wallet_ble_stock_serial_valid(wire,"a1b2c3d4e5f7"));
    assert(!wallet_ble_stock_serial_valid(wire,"short"));
    wire[14]='x';assert(!wallet_ble_stock_serial_valid(wire,serial));wire[14]=0;
    wire[0]=2;assert(!wallet_ble_stock_serial_valid(wire,serial));
    assert(!wallet_ble_stock_serial_valid(NULL,serial));
    assert(!wallet_ble_stock_serial_valid(wire,NULL));
}

int main(void)
{
    test_only_the_owner_gets_an_authenticated_data_link();
    test_pairing_requires_the_local_window_and_never_replaces_owner();
    test_passkey_is_not_released_without_physical_confirmation_context();
    test_uuid_bytes_match_pcw1_canonical_values();
    test_stale_session_events_cannot_reuse_a_connection_handle();
    test_local_close_disconnects_owner_but_window_expiry_keeps_secure_sync();
    test_malformed_owner_records_are_classified_for_local_recovery();
    test_stock_access_does_not_weaken_custom_pairing_policy();
    test_stock_init_requires_the_actual_serial();
    puts("Wallet BLE policy: PASS");
    return 0;
}
