#pragma once
#include "wallet_core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { PASSPORT_HOME, PASSPORT_CARD_MENU, PASSPORT_PAGE, PASSPORT_IMAGE } passport_screen_t;
typedef struct {
    passport_screen_t screen;
    uint8_t home_selection; /* 0 Card, 1 Image */
    uint8_t category;       /* card0 / social1 / payment2 / crypto3 / assets4 */
    uint16_t page;
    bool revealed;
} passport_nav_t;
void passport_nav_key(passport_nav_t *nav, wallet_key_t key,
                       const uint16_t counts[WALLET_KIND_COUNT]);
void passport_nav_hide(passport_nav_t *nav);
uint8_t passport_nav_kind(const passport_nav_t *nav);

typedef struct {
    passport_nav_t nav;
    const char *nickname, *intro, *clock, *notice;
    int battery;
    bool connected, pairing, passkey_released, reset_confirm;
    uint32_t passkey;
    uint16_t page_count;
    const uint8_t *page; /* borrowed I4 snapshot; immutable until clear/render */
    bool private_page;
    const uint8_t *jpeg; /* borrowed until clear/render under LVGL lock */
    size_t jpeg_size;
    uint16_t jpeg_width, jpeg_height;
    const uint8_t *avatar_bgra; /* borrowed 96x156 BGRA8888, immutable lease */
} passport_view_t;

/* All UI functions require bsp_lvgl_lock. Clear before releasing mapped JPEG.
 * render copies text, but borrows image storage. Clear before modifying any
 * borrowed pixel buffer. This avoids a second 27 KiB snapshot on no-PSRAM C3. */
void passport_ui_init(void);
void passport_ui_clear_images(void);
void passport_ui_render(const passport_view_t *view);
