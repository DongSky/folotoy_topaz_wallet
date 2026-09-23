#include "wallet_ui.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <string.h>

#define INK 0x162D3A
#define MUTED 0x596E78
#define PAPER 0xF5F7F5
#define TEAL 0x087F80

static lv_obj_t *s_screen;
static uint32_t s_image_data[WALLET_PAGE_IMAGE_SIZE / sizeof(uint32_t)];
static lv_image_dsc_t s_image = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_I4,
        .w = WALLET_WIDTH,
        .h = WALLET_HEIGHT,
        .stride = WALLET_WIDTH / 2
    },
    .data_size = WALLET_PAGE_IMAGE_SIZE,
    .data = (const uint8_t *)s_image_data
};

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       int width, bool large, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text ? text : "");
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj,
        large ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    return obj;
}

static void centered(const char *text, int y, bool large, uint32_t color)
{
    lv_obj_t *obj = label(s_screen, text, 20, y, 200, large, color);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
}

static void tile(const char *title, const char *subtitle, int y, bool selected)
{
    lv_obj_t *obj = lv_obj_create(s_screen);
    lv_obj_set_pos(obj, 16, y);
    lv_obj_set_size(obj, 208, 66);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_border_width(obj, selected ? 2 : 0, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(TEAL), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(selected ? 0xE0F2ED : 0xFFFFFF), 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    label(obj, title, 12, 8, 180, true, INK);
    label(obj, subtitle, 12, 36, 180, false, MUTED);
}

void wallet_ui_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(PAPER), 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(s_screen);
}

void wallet_ui_render(const wallet_view_t *view)
{
    if (!s_screen) return;
    lv_obj_clean(s_screen);
    // The source descriptor is reused for each page. Invalidate decoded cache
    // before replacing its backing pixels, otherwise old balances/QR may remain.
    lv_image_cache_drop(&s_image);
    static const char *roots[] = {"MY CARD", "WALLET", "CONNECT"};
    static const char *kinds[] = {"MY CARD", "SOCIAL", "PAYMENT", "CRYPTO", "ASSETS"};
    uint8_t kind = wallet_nav_kind(&view->nav);
    const char *title = view->nav.level == 2 ? kinds[kind] : roots[view->nav.root];
    label(s_screen, title, 23, 13, 150, false, TEAL);
    char battery[16];
    if (view->battery >= 0) snprintf(battery, sizeof(battery), "%d%%", view->battery);
    else snprintf(battery, sizeof(battery), "--%%");
    label(s_screen, battery, 177, 13, 48, false, MUTED);

    bool content = view->nav.level == 2 ||
                   (view->nav.root == 0 && view->nav.level == 0);
    if (view->pairing) {
        centered("PAIR PHONE", 62, true, INK);
        char passkey[16];
        snprintf(passkey, sizeof(passkey), "%06lu", (unsigned long)view->passkey);
        centered(passkey, 116, true, TEAL);
        centered(view->passkey_released ? "Enter code on phone\nHold OK: reject" :
                 "OK: allow this pairing\nThen enter code on phone\nHold OK: reject", 169, false, MUTED);
    } else if (content && view->image &&
               (!view->image_private || view->nav.revealed)) {
        memcpy(s_image_data, view->image, sizeof(s_image_data));
        lv_obj_t *img = lv_image_create(s_screen);
        lv_image_set_src(img, &s_image);
        lv_obj_set_pos(img, 12, 36);
    } else if (content && (kind == 4 || view->image_private) && !view->nav.revealed) {
        centered("PRIVATE", 101, true, TEAL);
        centered("Press OK to show\nbalances and estimates", 148, false, INK);
        centered("Hidden again on exit\nor after inactivity", 218, false, MUTED);
    } else if (content) {
        centered(view->snapshot_present ? "NOT ADDED YET" : "YOUR POCKET CARD", 94, true, INK);
        centered("Open the Android app\nto add your details,\nthen connect and sync.", 148, false, MUTED);
    } else if (view->nav.root == 1) {
        if (view->nav.level == 0) {
            centered("READY TO RECEIVE", 83, true, INK);
            centered("Payment codes\nCrypto addresses\nBalances and estimates", 135, false, MUTED);
            centered("OK: open wallet", 243, false, TEAL);
        } else {
            tile("Payment codes", "Alipay / WeChat", 53, view->nav.category == 0);
            tile("Crypto receiving", "Address + network", 126, view->nav.category == 1);
            tile("Assets", "Private by default", 199, view->nav.category == 2);
        }
    } else {
        centered("PHONE COMPANION", 55, true, INK);
        centered(view->connection, 96, false, MUTED);
        centered(view->reset_confirm ? "Local confirmation required" :
                 view->nav.level == 0 ? "OK: connection options" :
                 "OK: open 2-minute window\nUP: close connection\nDOWN: change owner", 205, false, TEAL);
    }
    if (view->notice && view->notice[0] && !view->pairing) {
        // Status footer never overlays the QR or imported page.
        label(s_screen, view->notice, 22, 296, 196, false, MUTED);
    } else if (!view->pairing) {
        char footer[48];
        if (view->nav.level == 2) {
            snprintf(footer, sizeof(footer), "%u/%u UP/DOWN Hold OK",
                     view->counts[kind] ? view->nav.page + 1 : 0, view->counts[kind]);
        } else {
            snprintf(footer, sizeof(footer), "UP/DOWN   OK: open");
        }
        label(s_screen, footer, 22, 296, 196, false, MUTED);
    }
}
