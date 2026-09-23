#include "passport_capture.h"
#include "bsp_display.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "lvgl.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 240u
#define HEIGHT 320u
#define STRIP 40u
#define CAPTURE_BYTES (WIDTH * HEIGHT * 2u)
typedef struct {
    uint8_t pixels[WIDTH * STRIP * 2];
    uint8_t coverage[WIDTH * STRIP / 8];
    unsigned covered;
} staging_t;
static staging_t *stage;
static lv_display_t *display;
static bool observer;
static const esp_partition_t *partition;
static const uint8_t *mapped;
static esp_partition_mmap_handle_t mapping;
static unsigned strip_y;
static uint64_t deadline;
static _Atomic unsigned strip_state; /* 0 disarmed, 1 collecting, 2 complete */
static passport_capture_state_t state;

static void on_flush(lv_event_t *event)
{
    if (atomic_load_explicit(&strip_state, memory_order_acquire) != 1) return;
    const lv_area_t *area = lv_event_get_param(event);
    lv_draw_buf_t *buffer = lv_display_get_buf_active(display);
    if (!area || !buffer || !buffer->data || area->x1 < 0 || area->x2 >= (int)WIDTH ||
        area->y1 < 0 || area->y2 >= (int)HEIGHT || area->x2 < area->x1 || area->y2 < area->y1 ||
        lv_display_get_color_format(display) != LV_COLOR_FORMAT_RGB565 ||
        buffer->header.stride < (unsigned)lv_area_get_width(area) * 2) return;
    int first = area->y1 > (int)strip_y ? area->y1 : (int)strip_y;
    int last = area->y2 < (int)(strip_y + STRIP - 1) ? area->y2 : (int)(strip_y + STRIP - 1);
    for (int y = first; y <= last; ++y) {
        const uint8_t *row = buffer->data + (y - area->y1) * buffer->header.stride;
        for (int x = area->x1; x <= area->x2; ++x) {
            unsigned index = (y - strip_y) * WIDTH + x;
            uint8_t bit = 1u << (index & 7u);
            if (stage->coverage[index >> 3] & bit) continue;
            memcpy(stage->pixels + index * 2, row + (x - area->x1) * 2, 2);
            stage->coverage[index >> 3] |= bit;
            ++stage->covered;
        }
    }
    if (stage->covered == WIDTH * STRIP)
        atomic_store_explicit(&strip_state, 2, memory_order_release);
}
static bool arm(uint64_t now)
{
    if (!bsp_lvgl_lock(100)) return false;
    atomic_store(&strip_state, 0);
    memset(stage, 0, sizeof(*stage));
    lv_area_t area = {.x1 = 0, .x2 = WIDTH - 1, .y1 = strip_y, .y2 = strip_y + STRIP - 1};
    atomic_store_explicit(&strip_state, 1, memory_order_release);
    lv_obj_invalidate_area(lv_display_get_screen_active(display), &area);
    bsp_lvgl_unlock();
    uint64_t armed = esp_timer_get_time() / 1000;
    deadline = (armed > now ? armed : now) + 3000;
    return true;
}
bool passport_capture_release(void)
{
    if (stage || observer) {
        if (!bsp_lvgl_lock(100)) return false;
        atomic_store(&strip_state, 0);
        if (observer) lv_display_remove_event_cb_with_user_data(display, on_flush, NULL);
        observer = false;
        free(stage); stage = NULL;
        bsp_lvgl_unlock();
    }
    if (mapped) { esp_partition_munmap(mapping); mapped = NULL; }
    state = PASSPORT_CAPTURE_IDLE;
    return true;
}
bool passport_capture_start(lv_display_t *target, uint64_t now)
{
    if (!target || state == PASSPORT_CAPTURE_BUSY || !passport_capture_release()) return false;
    display = target;
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "screenshot");
    if (!partition || partition->readonly || partition->encrypted || partition->size < 0x26000) return false;
    stage = calloc(1, sizeof(*stage));
    if (!stage) return false;
    if (esp_partition_erase_range(partition, 0, 0x26000) != ESP_OK) { free(stage); stage = NULL; return false; }
    if (!bsp_lvgl_lock(100)) { free(stage); stage = NULL; return false; }
    unsigned count = lv_display_get_event_count(display);
    lv_display_add_event_cb(display, on_flush, LV_EVENT_FLUSH_START, NULL);
    bool registered = lv_display_get_event_count(display) == count + 1;
    observer = registered;
    bsp_lvgl_unlock();
    if (!registered) { (void)passport_capture_release(); return false; }
    strip_y = 0;
    state = PASSPORT_CAPTURE_BUSY;
    if (!arm(now)) { state = PASSPORT_CAPTURE_ERROR; return false; }
    return true;
}
passport_capture_state_t passport_capture_poll(uint64_t now)
{
    if (state != PASSPORT_CAPTURE_BUSY) return state;
    if (atomic_load_explicit(&strip_state, memory_order_acquire) != 2) {
        if (now >= deadline) state = PASSPORT_CAPTURE_ERROR;
        return state;
    }
    atomic_store(&strip_state, 0);
    if (esp_partition_write(partition, strip_y * WIDTH * 2, stage->pixels, sizeof(stage->pixels)) != ESP_OK) {
        state = PASSPORT_CAPTURE_ERROR; return state;
    }
    strip_y += STRIP;
    if (strip_y < HEIGHT) {
        if (!arm(now)) state = PASSPORT_CAPTURE_ERROR;
        return state;
    }
    if (!bsp_lvgl_lock(100)) { state = PASSPORT_CAPTURE_ERROR; return state; }
    lv_display_remove_event_cb_with_user_data(display, on_flush, NULL); observer = false;
    free(stage); stage = NULL;
    bsp_lvgl_unlock();
    const void *pixels;
    if (esp_partition_mmap(partition, 0, CAPTURE_BYTES, ESP_PARTITION_MMAP_DATA, &pixels, &mapping) != ESP_OK) {
        state = PASSPORT_CAPTURE_ERROR; return state;
    }
    mapped = pixels;
    state = PASSPORT_CAPTURE_READY;
    return state;
}
const uint8_t *passport_capture_pixels(size_t *length)
{
    if (length) *length = state == PASSPORT_CAPTURE_READY ? CAPTURE_BYTES : 0;
    return state == PASSPORT_CAPTURE_READY ? mapped : NULL;
}
