#include "passport_ui.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/misc/lv_text_private.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(card_font_18);
#define NAVY 0x08273D
#define WHITE 0xEEF7F6
#define GREEN 0x68E8B0
#define MUTED 0xA1BBC8
#define PANEL 0x11354B
#define LINE 0x234557
static lv_obj_t *screen;
static lv_image_dsc_t page_image = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_I4,
               .w = WALLET_WIDTH, .h = WALLET_HEIGHT, .stride = WALLET_WIDTH / 2},
    .data_size = WALLET_PAGE_IMAGE_SIZE,
};
static lv_image_dsc_t jpeg_image;
static lv_image_dsc_t avatar_image;

static bool text_supported(const char *text)
{
    if (!text) return true;
    uint32_t index = 0;
    while (text[index]) {
        uint32_t cp = lv_text_encoded_next(text, &index);
        if (cp == '\n' || cp == '\r') continue;
        lv_font_glyph_dsc_t glyph = {0};
        if (!cp || !lv_font_get_glyph_dsc(&card_font_18, &glyph, cp, 0) || glyph.is_placeholder)
            return false;
    }
    return true;
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       int width, uint32_t color, bool centered)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, &card_font_18, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(obj, centered ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(obj, text_supported(text) ? (text ? text : "") : "暂不支持此文字");
    return obj;
}
static void one_line(lv_obj_t *obj)
{
    lv_obj_set_height(obj, 24);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
}
static lv_obj_t *panel(int x, int y, int w, int h, bool selected)
{
    lv_obj_t *obj = lv_obj_create(screen);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(PANEL), 0);
    lv_obj_set_style_border_width(obj, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(selected ? GREEN : LINE), 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
void passport_ui_init(void)
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(NAVY), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(screen);
}
void passport_ui_clear_images(void)
{
    if (!screen) return;
    lv_obj_clean(screen);
    lv_image_cache_drop(&page_image);
    lv_image_cache_drop(&jpeg_image);
    lv_image_cache_drop(&avatar_image);
    memset(&jpeg_image, 0, sizeof(jpeg_image));
    memset(&avatar_image, 0, sizeof(avatar_image));
}
static void draw_jpeg(const passport_view_t *v, int x, int y)
{
    jpeg_image = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW,
                   .w = v->jpeg_width, .h = v->jpeg_height},
        .data_size = v->jpeg_size, .data = v->jpeg,
    };
    lv_obj_t *img = lv_image_create(screen);
    lv_image_set_src(img, &jpeg_image);
    lv_obj_set_pos(img, x, y);
}
void passport_ui_render(const passport_view_t *v)
{
    if (!screen || !v) return;
    passport_ui_clear_images();
    if (v->reset_confirm) {
        label(screen, "更换同步手机？", 20, 64, 200, WHITE, true);
        label(screen, "解除当前钱包配对\n已保存的页面会保留", 20, 120, 200, MUTED, true);
        label(screen, "按 OK 确认\n其他按键取消", 20, 204, 200, GREEN, true);
        return;
    }
    if (v->pairing) {
        label(screen, "允许手机同步？", 20, 56, 200, WHITE, true);
        char code[16];
        snprintf(code, sizeof(code), "%06lu", (unsigned long)v->passkey);
        lv_obj_t *passkey = label(screen, code, 20, 106, 200, GREEN, true);
        lv_obj_set_style_text_font(passkey, &lv_font_montserrat_20, 0);
        label(screen, v->passkey_released ? "在手机输入此配对码" : "按 OK 允许配对", 20, 166, 200, WHITE, true);
        label(screen, "长按 OK 拒绝", 20, 220, 200, MUTED, true);
        return;
    }
    if (v->nav.screen == PASSPORT_IMAGE && v->jpeg) {
        draw_jpeg(v, (240 - v->jpeg_width) / 2, (320 - v->jpeg_height) / 2);
        return;
    }
    char battery[16];
    snprintf(battery, sizeof(battery), v->battery >= 0 ? "%d%%" : "--", v->battery);
    label(screen, v->clock ? v->clock : "--:--", 22, 12, 96, MUTED, false);
    label(screen, battery, 168, 12, 52, MUTED, false);
    if (v->nav.screen == PASSPORT_HOME) {
        if (v->avatar_bgra) {
            avatar_image = (lv_image_dsc_t){
                .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888,
                           .w = 96, .h = 156, .stride = 384},
                .data_size = 96 * 156 * 4, .data = v->avatar_bgra};
            lv_obj_t *img = lv_image_create(screen);
            lv_image_set_src(img, &avatar_image);
            lv_obj_set_pos(img, 72, 47);
        } else if (v->jpeg && v->jpeg_width <= 96 && v->jpeg_height <= 160)
            draw_jpeg(v, (240 - v->jpeg_width) / 2, 45 + (160 - v->jpeg_height) / 2);
        else {
            lv_obj_t *avatar = panel(72, 48, 96, 148, false);
            label(avatar, "P", 0, 56, 96, GREEN, true);
        }
        one_line(label(screen, v->nickname && *v->nickname ? v->nickname : "托帕钱包", 18, 211, 204, WHITE, true));
        one_line(label(screen, v->intro && *v->intro ? v->intro : "把自己介绍给世界", 18, 235, 204, MUTED, true));
        lv_obj_t *card = panel(18, 266, 98, 38, v->nav.home_selection == 0);
        lv_obj_t *photo = panel(124, 266, 98, 38, v->nav.home_selection == 1);
        label(card, "名片", 0, 6, 94, v->nav.home_selection == 0 ? GREEN : WHITE, true);
        label(photo, "图片", 0, 6, 94, v->nav.home_selection == 1 ? GREEN : WHITE, true);
    } else if (v->nav.screen == PASSPORT_CARD_MENU) {
        label(screen, "托帕钱包", 22, 44, 196, WHITE, false);
        const char *titles[] = {"名片详情", "社交账号", "收款码", "加密货币", "资产与估值"};
        for (unsigned i = 0; i < 5; ++i) {
            lv_obj_t *row = panel(18, 77 + i * 41, 204, 36, v->nav.category == i);
            label(row, titles[i], 14, 5, 174, v->nav.category == i ? GREEN : WHITE, false);
        }
        label(screen, "OK 进入 · 长按返回", 22, 287, 196, MUTED, true);
    } else if (v->nav.screen == PASSPORT_PAGE) {
        if (v->page && (!v->private_page || v->nav.revealed)) {
            page_image.data = v->page;
            lv_obj_t *img = lv_image_create(screen);
            lv_image_set_src(img, &page_image);
            lv_obj_set_pos(img, 12, 36);
        } else {
            label(screen, v->page ? "资产已隐藏" : "还没有内容", 22, 113, 196, GREEN, true);
            label(screen, v->page ? "按 OK 查看余额与估值" : "请在手机添加并同步", 22, 155, 196, WHITE, true);
        }
        char footer[48];
        snprintf(footer, sizeof(footer), "%u / %u · 长按返回", v->page_count ? v->nav.page + 1 : 0, v->page_count);
        label(screen, footer, 22, 294, 196, MUTED, true);
    } else {
        label(screen, "还没有图片", 22, 118, 196, GREEN, true);
        label(screen, "从微信小程序或手机\n发送一张全屏图片", 22, 158, 196, WHITE, true);
        label(screen, "长按 OK 返回", 22, 282, 196, MUTED, true);
    }
    if (v->notice && *v->notice) {
        lv_obj_t *notice = panel(18, 36, 204, 27, false);
        one_line(label(notice, v->notice, 5, 1, 194, GREEN, true));
    }
}
