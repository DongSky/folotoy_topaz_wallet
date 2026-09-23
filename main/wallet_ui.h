#pragma once

#include "wallet_core.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    wallet_nav_t nav;
    uint16_t counts[WALLET_KIND_COUNT];
    const uint8_t *image; // palette + packed pixels, copied under the LVGL lock
    bool image_private;
    bool snapshot_present;
    bool pairing;
    bool passkey_released;
    bool reset_confirm;
    uint32_t passkey;
    int battery;
    const char *connection;
    const char *notice;
} wallet_view_t;

// These functions require the caller to hold bsp_lvgl_lock().
void wallet_ui_init(void);
void wallet_ui_render(const wallet_view_t *view);
