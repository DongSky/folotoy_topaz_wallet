#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct _lv_display_t;
typedef enum { PASSPORT_CAPTURE_IDLE, PASSPORT_CAPTURE_BUSY,
               PASSPORT_CAPTURE_READY, PASSPORT_CAPTURE_ERROR } passport_capture_state_t;
/* Main worker only, never under the LVGL lock. start registers a nonblocking
 * flush observer and invalidates one strip at a time. Caller freezes app view,
 * image leases and navigation while BUSY. Flash writes occur only in poll.
 * No framebuffer or BLE operation is allocated/executed in the LVGL callback.
 * READY exposes an immutable Flash mapping until release. */
bool passport_capture_start(struct _lv_display_t *display, uint64_t now_ms);
passport_capture_state_t passport_capture_poll(uint64_t now_ms);
const uint8_t *passport_capture_pixels(size_t *length);
bool passport_capture_release(void);
