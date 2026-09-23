#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "passport_ui.h"
#include "passport_jpeg.h"
#include "passport_capture.h"
#include "stock_screenshot_session.h"
#include "stock_music.h"
#include "stock_ble.h"
#include "stock_content.h"
#include "stock_identity.h"
#include "stock_avatar.h"
#include "wallet_ble.h"
#include "wallet_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "passport_card";
static QueueHandle_t inputs;
static stock_content_t content;
static stock_content_image_t image;
static stock_avatar_image_t avatar;
static char avatar_name[16];
static uint8_t page[WALLET_PAGE_IMAGE_SIZE];
static bool dirty = true, stock_ready;
static uint32_t last_session;
static struct _lv_display_t *display;
static stock_shot_t shot;
static uint16_t next_shot;
static bool capture_cleanup, screen_capture_safe;
static const char *notice;
static int64_t notice_until;
typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } input_t;

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (!inputs || (event != BSP_BTN_CLICK && event != BSP_BTN_LONG)) return;
    input_t in = {button, event};
    (void)xQueueSend(inputs, &in, 0);
}
static bool release_image(void)
{
    if (!image.owner) return true;
    if (!bsp_lvgl_lock(500)) return false;
    passport_ui_clear_images();
    bsp_lvgl_unlock();
    stock_content_image_release(&image);
    dirty = true;
    return true;
}
static bool release_avatar(void)
{
    if (!avatar.lease) return true;
    if (!bsp_lvgl_lock(500)) return false;
    passport_ui_clear_images();
    bsp_lvgl_unlock();
    stock_avatar_release(&avatar);
    avatar_name[0] = 0;
    dirty = true;
    return true;
}
static bool avatar_exists(void *context, const char *name)
{
    (void)context;
    const uint8_t *png;
    size_t length;
    return stock_avatar_find(name, &png, &length) == STOCK_AVATAR_OK;
}
static bool session_valid(void *context, uint32_t session)
{
    (void)context;
    return stock_ble_session_current(session);
}
static bool capture_frozen(void)
{
    return capture_cleanup || shot.phase == STOCK_SHOT_CAPTURING;
}
static void release_capture(void)
{
    capture_cleanup = !passport_capture_release();
}
static void shot_fail(uint32_t session, uint8_t status)
{
    shot.phase = STOCK_SHOT_FAILED;
    shot.pixels = NULL;
    shot.awaiting_ack = false;
    release_capture();
    (void)stock_ble_notify_status(session, 6, status);
}
static void shot_command(uint32_t session, const uint8_t *bytes, size_t length)
{
    if (length == 1 && bytes[0] == 1) {
        wallet_ble_info_t info;
        wallet_ble_get_info(&info);
        /* Legacy screenshots are unauthenticated. Never expose a wallet page
         * or the Android pairing passkey through the legacy characteristic. */
        if (!screen_capture_safe || info.pairing_pending || capture_cleanup || content.transfer.active) {
            (void)stock_ble_notify_status(session, 6, 0x1b); return;
        }
        if (stock_ble_screenshot_capacity(session) < 20) {
            (void)stock_ble_notify_status(session, 6, 0x1c); return;
        }
        if (++next_shot == 0) ++next_shot;
    }
    stock_shot_action_t action = stock_shot_command(&shot, bytes, length, next_shot,
                                                   esp_timer_get_time() / 1000);
    if (action.release) release_capture();
    if (action.capture && (capture_cleanup || !passport_capture_start(display, esp_timer_get_time() / 1000))) {
        shot_fail(session, 0x1c); return;
    }
    if (action.reply) (void)stock_ble_notify_status(session, 6, action.status);
}
static void shot_tick(uint64_t now)
{
    if (capture_cleanup) release_capture();
    if (!last_session || !stock_ble_session_current(last_session)) return;
    stock_shot_action_t action = stock_shot_poll(&shot, now);
    if (action.release) release_capture();
    if (action.reply) (void)stock_ble_notify_status(last_session, 6, action.status);
    if (shot.phase == STOCK_SHOT_CAPTURING) {
        passport_capture_state_t state = passport_capture_poll(now);
        if (state == PASSPORT_CAPTURE_ERROR) { shot_fail(last_session, 0x1c); return; }
        if (state == PASSPORT_CAPTURE_READY) {
            size_t size;
            const uint8_t *pixels = passport_capture_pixels(&size);
            if (!stock_shot_captured(&shot, pixels, size) ||
                stock_ble_notify_status(last_session, 6, 0) != 0) {
                shot_fail(last_session, 0x1c); return;
            }
        }
    }
    if (shot.phase >= STOCK_SHOT_BEGIN && shot.phase <= STOCK_SHOT_END) {
        size_t capacity = stock_ble_screenshot_capacity(last_session);
        if (capacity < 20 || (shot.packet_length && capacity < shot.packet_length)) {
            shot_fail(last_session, 0x1c); return;
        }
        const uint8_t *packet;
        size_t length = stock_shot_packet(&shot, capacity, now, &packet);
        if (length) {
            if (stock_ble_notify_screenshot(last_session, packet, length) != 0) shot_fail(last_session, 0x1c);
            else stock_shot_mark_sent(&shot, now);
        }
    }
}
static void update_stock_status(void)
{
    uint8_t status[9] = {1};
    const unsigned offsets[] = {STOCK_PROFILE_SCORE_TOTAL, STOCK_PROFILE_SCORE_BEST};
    for (unsigned i = 0; i < 2; ++i) {
        int32_t value = stock_profile_get_i32(&content.profile, offsets[i]);
        uint32_t score = value > 0 ? (uint32_t)value : 0;
        for (unsigned b = 0; b < 4; ++b) status[1 + i * 4 + b] = (uint8_t)(score >> (b * 8));
    }
    stock_ble_update_status(status);
}
static unsigned profile_volume(void)
{
    int32_t value = stock_profile_get_i32(&content.profile, STOCK_PROFILE_VOLUME);
    return value < 0 ? 0u : value > 100 ? 100u : (unsigned)value;
}
static void reconcile_profile(void)
{
    stock_profile_t saved;
    stock_profile_store_result_t result = stock_profile_nvs_load(&saved, NULL);
    if (result == STOCK_PROFILE_STORE_OK) {
        content.profile = saved;
    } else if (result == STOCK_PROFILE_STORE_NOT_FOUND) {
        /* Verified absence is the same valid defaults-only state as startup. */
        stock_profile_defaults(&content.profile);
    } else {
        /* A cancelled NVS operation may already be durable. Never allow a
         * subsequent edit to overwrite it from an unverified old RAM copy. */
        content.initialized = false;
        notice = "资料读取失败，请重启";
        notice_until = INT64_MAX;
    }
    update_stock_status();
    stock_music_set_volume(profile_volume());
    dirty = true;
}
static void received(void *context, uint32_t session, uint8_t type,
                       const uint8_t *bytes, size_t length)
{
    passport_nav_t *nav = context;
    if (!stock_ble_session_current(session)) return;
    if (last_session && last_session != session) {
        stock_content_disconnect(&content);
        stock_music_disconnect();
        shot = (stock_shot_t){0};
        release_capture();
        reconcile_profile();
    }
    last_session = session;
    if (type == 6) { shot_command(session, bytes, length); return; }
    if (type == 3) {
        uint8_t status = stock_music_handle(session, session_valid, NULL, bytes, length);
        (void)stock_ble_notify_status(session, type, status); return;
    }
    if (capture_frozen() && (type == 1 || type == 2)) {
        (void)stock_ble_notify_status(session, type, 0x1b); return;
    }
    if (type == 2 && length && bytes[0] == 0 && !release_image()) {
        (void)stock_ble_notify_status(session, type, 0x1b);
        return;
    }
    stock_content_result_t result = stock_content_handle_guarded(&content, session,
        session_valid, NULL, type, bytes, length, false);
    if (!stock_ble_session_current(session)) {
        stock_content_disconnect(&content);
        reconcile_profile();
        return;
    }
    if (result.event != STOCK_CONTENT_NONE) {
        dirty = true;
        if (result.event == STOCK_CONTENT_FULLSCREEN_READY && nav) {
            nav->screen = PASSPORT_IMAGE;
            nav->home_selection = 1;
            passport_nav_hide(nav);
        }
        if (result.event == STOCK_CONTENT_PROFILE) {
            update_stock_status();
            stock_music_set_volume(profile_volume());
            if (result.changes.time_changed) {
                if (result.changes.time <= INT64_MAX / 1000000) {
                    struct timeval time = {.tv_sec = result.changes.time, .tv_usec = 0};
                    if (settimeofday(&time, NULL) != 0) result.response[1] = 0x16;
                } else result.response[1] = 0x13;
            }
        }
    }
    (void)stock_ble_notify_status(session, result.response[0], result.response[1]);
}
static void profile_text(char *out, size_t capacity, size_t offset, size_t field_size)
{
    size_t len = strnlen((const char *)content.profile.bytes + offset, field_size);
    if (len >= capacity) len = capacity - 1;
    memcpy(out, content.profile.bytes + offset, len);
    out[len] = 0;
}
static void transient(const char *text, int64_t now)
{
    notice = text; notice_until = now + 4000000; dirty = true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Passport card starting; stock compatibility requires device acceptance");
    (void)bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !(display = bsp_lvgl_init())) {
        ESP_LOGE(TAG, "Display initialization failed"); return;
    }
    bsp_display_backlight(80);
    (void)bsp_battery_init();
    bool audio_ready = bsp_audio_init() == ESP_OK;
    if (audio_ready) (void)bsp_audio_sleep();
    bool nvs_ready = nvs_flash_init() == ESP_OK;
    stock_profile_store_result_t profile_result = stock_content_init(&content);
    bool profile_ready = profile_result == STOCK_PROFILE_STORE_OK || profile_result == STOCK_PROFILE_STORE_NOT_FOUND;
    if (nvs_ready && audio_ready && profile_ready &&
        !stock_music_init(profile_volume()))
        ESP_LOGW(TAG, "Music unavailable; stored score preserved");
    bool avatars_ready = stock_avatar_init() == STOCK_AVATAR_OK;
    if (avatars_ready) stock_content_set_avatar_validator(&content, avatar_exists, NULL);
    bool store_ready = nvs_ready && wallet_store_init() == WALLET_STORE_OK;
    stock_identity_t identity;
    uint8_t identity_wire[72] = {0}, status[9] = {1};
    stock_identity_result_t identity_result = stock_identity_load(&identity);
    if (profile_ready && identity_result == STOCK_IDENTITY_OK && stock_identity_ready(&identity) &&
        stock_identity_serialize(&identity, identity_wire, sizeof(identity_wire))) {
        stock_ready = wallet_ble_init_stock(identity_wire, status, identity.device_key) == WALLET_BLE_OK;
        if (stock_ready) update_stock_status();
    }
    stock_identity_clear(&identity);
    volatile uint8_t *sensitive = identity_wire;
    for (size_t i = 0; i < sizeof(identity_wire); ++i) sensitive[i] = 0;
    if (!stock_ready) ESP_LOGE(TAG, "Stock BLE unavailable; identity/profile retained (identity=%d, profile=%d)", identity_result, profile_result);
    inputs = xQueueCreate(12, sizeof(input_t));
    if (!inputs || bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Buttons unavailable"); return;
    }
    if (!bsp_lvgl_lock(1000)) return;
    passport_ui_init();
    bsp_lvgl_unlock();

    passport_nav_t nav = {0};
    wallet_store_info_t snapshot = {0};
    wallet_ble_info_t ble = {0};
    uint32_t revision = UINT32_MAX;
    int64_t last_input = esp_timer_get_time(), last_refresh = 0;
    bool dimmed = false, dark = false, reset_confirm = false, was_pairing = false, was_released = false;
    for (;;) {
        input_t in;
        bool have_input = xQueueReceive(inputs, &in, pdMS_TO_TICKS(40)) == pdTRUE;
        int64_t now = esp_timer_get_time();
        if (stock_ready) {
            (void)stock_ble_poll(now / 1000, received, &nav);
            if (last_session && !stock_ble_session_current(last_session)) {
                stock_content_disconnect(&content);
                stock_music_disconnect();
                shot = (stock_shot_t){0};
                release_capture();
                reconcile_profile();
                last_session = 0;
            }
            wallet_ble_get_info(&ble);
        }
        shot_tick(now / 1000);
        /* Keep the exact rendered view and borrowed image leases stable until
         * every strip is captured; callbacks and BLE cancellation still run. */
        if (capture_frozen()) continue;
        if (store_ready) (void)wallet_store_get_info(&snapshot);
        if (snapshot.revision != revision) {
            revision = snapshot.revision; nav.page = 0; passport_nav_hide(&nav); dirty = true;
        }
        if (ble.pairing_pending != was_pairing || ble.passkey_released != was_released) {
            was_pairing = ble.pairing_pending; was_released = ble.passkey_released; dirty = true;
            if (ble.pairing_pending) { last_input = now; dimmed = dark = false; bsp_display_backlight(80); }
        }
        if (have_input) {
            last_input = now;
            if (dimmed || dark) {
                dimmed = dark = false; bsp_display_backlight(80); dirty = true;
            } else if (ble.pairing_pending) {
                if (in.button == BSP_BTN_OK && in.event == BSP_BTN_LONG) (void)wallet_ble_confirm_passkey(false);
                else if (in.button == BSP_BTN_OK && in.event == BSP_BTN_CLICK && !ble.passkey_released) (void)wallet_ble_confirm_passkey(true);
            } else if (reset_confirm) {
                if (in.button == BSP_BTN_OK && in.event == BSP_BTN_CLICK) (void)wallet_ble_reset_owner();
                reset_confirm = false; dirty = true;
            } else if (in.event == BSP_BTN_LONG && in.button == BSP_BTN_UP) {
                if (stock_ready && store_ready && wallet_ble_open_pairing_window(WALLET_BLE_PAIRING_WINDOW_MS) == WALLET_BLE_OK)
                    transient("手机同步已开启", now);
                else transient("同步暂不可用", now);
            } else if (in.event == BSP_BTN_LONG && in.button == BSP_BTN_DOWN) {
                reset_confirm = true; dirty = true;
            } else if (in.event == BSP_BTN_CLICK || in.button == BSP_BTN_OK) {
                wallet_key_t key = in.event == BSP_BTN_LONG ? WALLET_KEY_BACK :
                    in.button == BSP_BTN_UP ? WALLET_KEY_UP : in.button == BSP_BTN_DOWN ? WALLET_KEY_DOWN : WALLET_KEY_OK;
                passport_nav_key(&nav, key, snapshot.counts); dirty = true;
            }
        }
        if (notice && now >= notice_until) { notice = NULL; dirty = true; }
        if (now - last_input >= 30000000 && !dimmed) {
            passport_nav_hide(&nav); reset_confirm = false; bsp_display_backlight(12); dimmed = true; dirty = true;
        }
        if (now - last_input >= 90000000 && !dark) { bsp_display_backlight(0); dark = true; }
        if (now - last_refresh >= 30000000) { last_refresh = now; dirty = true; }
        if (!dirty || dark) continue;
        if (!release_image()) continue;
        char selected_avatar[17];
        profile_text(selected_avatar, sizeof(selected_avatar), STOCK_PROFILE_AVATAR_NAME, 16);
        if (avatar.lease && strcmp(avatar_name, selected_avatar) != 0 && !release_avatar()) continue;
        char nickname[49], intro[86], clock_text[16] = "--:--";
        profile_text(nickname, sizeof(nickname), STOCK_PROFILE_NICKNAME, 48);
        profile_text(intro, sizeof(intro), STOCK_PROFILE_INTRO, 85);
        time_t wall = time(NULL);
        /* Stock 1.0.3 formats this value through gmtime_r. The companion may
         * already adjust its seconds; do not silently apply a second offset. */
        if (wall > 1600000000) { struct tm t; gmtime_r(&wall, &t); strftime(clock_text, sizeof(clock_text), "%H:%M", &t); }
        passport_view_t view = {.nav = nav, .nickname = nickname, .intro = intro, .clock = clock_text,
            .battery = bsp_battery_soc(), .connected = ble.connected, .pairing = ble.pairing_pending,
            .passkey = ble.passkey, .passkey_released = ble.passkey_released, .reset_confirm = reset_confirm,
            .notice = notice ? notice : !stock_ready ? "蓝牙资料不可用" : NULL};
        if (nav.screen == PASSPORT_PAGE) {
            /* Drop the old borrowed-page descriptor before its sole backing
             * buffer is overwritten by a blocking storage read outside LVGL. */
            if (!bsp_lvgl_lock(500)) continue;
            passport_ui_clear_images();
            bsp_lvgl_unlock();
            uint8_t kind = passport_nav_kind(&nav), metadata[4];
            view.page_count = snapshot.counts[kind];
            if (snapshot.available) {
                wallet_store_result_t result = wallet_store_read_page_at_revision(
                    snapshot.revision, kind, nav.page, metadata, page, sizeof(page));
                if (result == WALLET_STORE_STALE_SNAPSHOT) {
                    /* A commit raced this frame. Refresh metadata next loop;
                     * never apply an old reveal permission to new pixels. */
                    passport_nav_hide(&nav);
                    dirty = true;
                    continue;
                }
                if (result == WALLET_STORE_OK) {
                    view.page = page; view.private_page = (metadata[1] & 1u) != 0;
                }
            }
        } else if (profile_ready && (nav.screen == PASSPORT_HOME || nav.screen == PASSPORT_IMAGE)) {
            stock_content_mode_t mode = nav.screen == PASSPORT_HOME ? STOCK_CONTENT_AVATAR : STOCK_CONTENT_FULLSCREEN;
            bool custom = mode == STOCK_CONTENT_FULLSCREEN || content.profile.bytes[STOCK_PROFILE_AVATAR_NAME] == 0;
            if (!custom && avatars_ready) {
                if (!avatar.lease && stock_avatar_prepare(selected_avatar, NULL, NULL, &avatar) == STOCK_AVATAR_OK)
                    snprintf(avatar_name, sizeof(avatar_name), "%.15s", selected_avatar);
                if (avatar.lease) view.avatar_bgra = avatar.bgra;
                else view.notice = "头像暂不可用";
            } else if (custom && stock_content_image_acquire(&content, mode, &image)) {
                if (passport_jpeg_dimensions(image.jpeg, image.length, &view.jpeg_width, &view.jpeg_height)) {
                    view.jpeg = image.jpeg; view.jpeg_size = image.length;
                } else { stock_content_image_release(&image); view.notice = "图片格式暂不支持"; }
            }
        }
        if (bsp_lvgl_lock(500)) {
            passport_ui_render(&view); bsp_lvgl_unlock(); dirty = false;
            screen_capture_safe = nav.screen != PASSPORT_PAGE && !view.pairing && !view.reset_confirm;
        } else if (image.owner) stock_content_image_release(&image);
    }
}
