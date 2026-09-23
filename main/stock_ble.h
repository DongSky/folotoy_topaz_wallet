#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STOCK_BLE_IDENTITY_SIZE 72u
#define STOCK_BLE_STATUS_SIZE 9u
#define STOCK_BLE_WRITE_MAX 260u
#define STOCK_BLE_QUEUE_LENGTH 8u
#define STOCK_BLE_IDLE_MS 2000u
/* NimBLE's little-endian128-bit UUID representation. */
#define STOCK_BLE_SERVICE_UUID_BYTES \
    0,0,0,0,0,0,0,0,0x44,0x52,0x41,0x43,0x45,0x41,0x52,0x54

typedef void (*stock_ble_frame_handler_t)(void *context, uint32_t session,
                                          uint8_t type, const uint8_t *payload,
                                          size_t length);

/* Returns0 on success, negative on invalid arguments/lifecycle. Fixed static
 * storage; no task, timer, host, allocation or advertising is started. Call
 * init, then register after nimble_port_init and before starting the host.
 * register returns NimBLE errors; duplicate successful registration is safe.
 * Re-init is allowed only when disconnected and no poll is active; it retains
 * existing GATT registration. For host teardown/recreation, stop host + worker
 * first, call deinit BEFORE nimble_port_deinit (its event queue must still
 * exist), then initialize/register again. No concurrent init or
 * register calls. deinit returnsfalse while connected or polling.
 * Identity cache is the actual legacy sensitive0012 response (contains a
 * DeviceSecret slot). It is readable without pairing, as stock1.0.3 defines.
 * Only register this service when intentionally enabling legacy compatibility;
 * never log/cache-export identity. This does not change other service flags. */
int stock_ble_init(const uint8_t identity[STOCK_BLE_IDENTITY_SIZE],
                    const uint8_t status[STOCK_BLE_STATUS_SIZE]);
int stock_ble_register(void);
bool stock_ble_deinit(void);

/* Forward accepted GAP events directly from the single NimBLE host callback
 * task (not deferred to another worker). Notifications are submitted on this
 * same host event queue to avoid handle-reuse races. Extra connections
 * are ignored here; the owner must reject them. Duplicate connect is harmless.
 * On host reset call on_disconnect for the active connection before restart. */
void stock_ble_on_connect(uint16_t connection);
void stock_ble_on_disconnect(uint16_t connection);
void stock_ble_on_subscribe(uint16_t connection, uint16_t attribute, bool notify);

/* One worker calls poll at least every100ms, INCLUDING while idle. now_ms is
 * monotonic esp_timer_get_time()/1000, same clock used by ingress. At most8 ATT
 * writes processed per call. Each write boundary is preserved. Frame callbacks
 * execute sequentially outside critical sections; payload valid only until
 * return. Reentrant/concurrent poll does no work. Timeout fires at>=2000ms.
 * Queue overflow/oversize invalidates session, flushes pending data, resets
 * partial parsing, rejects writes until worker reports00 1b/00 15 respectively.
 * No slow operation is executed by GATT/GAP callbacks. Session guards must be
 * rechecked by long operations before committing their results. */
size_t stock_ble_poll(uint64_t now_ms, stock_ble_frame_handler_t handler, void *context);
bool stock_ble_session_current(uint32_t session);
/* Current screenshot subscription's ATT payload capacity, or 0 if unavailable.
 * A later notify still rechecks session/subscription/MTU on the host task. */
size_t stock_ble_screenshot_capacity(uint32_t session);
void stock_ble_update_status(const uint8_t status[STOCK_BLE_STATUS_SIZE]);

/* Worker-only, return0 on acceptance into a bounded4-packet outgoing queue
 * (not stack submission or peer receipt). Host callback validates session,
 * subscription and MTU again before sending. Queue full returnsnegative.
 * Require current session and corresponding CCC subscription; reject stale
 * session, missing subscription, or payload exceeding negotiated MTU-3.
 * Screenshot is the already-serialized raw0014 packet, no outer envelope.
 * No retry/session screenshot policy is invented by this transport. Later
 * stack allocation/send failure drops the notification; no automatic retries.
 * Uses nimble_port_get_dflt_eventq(), same queue as nimble_port_run(). */
int stock_ble_notify_status(uint32_t session, uint8_t type, uint8_t status);
int stock_ble_notify_screenshot(uint32_t session, const uint8_t *data, size_t length);
