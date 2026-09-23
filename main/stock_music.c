#include "stock_music.h"
#include "stock_rtttl.h"
#include "stock_transfer.h"
#include "bsp_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdatomic.h>
#include <string.h>

#define MUSIC_NAMESPACE "trae_cfg"
#define MUSIC_KEY "boot_rtttl"
#define MUSIC_CHUNK_SAMPLES 256u
typedef struct { uint16_t length; uint8_t bytes[STOCK_RTTTL_CAPACITY]; } music_score_t;
static music_score_t received;
static stock_transfer_t transfer;
static QueueHandle_t pending;
static atomic_uint desired_volume;
static bool ready, session_known, cancelled;
static uint32_t active_session;
static stock_music_guard_t call_guard;
static void *call_context;

void stock_music_set_volume(unsigned volume)
{
    atomic_store_explicit(&desired_volume, volume > 100 ? 100 : volume, memory_order_relaxed);
}
static unsigned apply_volume(unsigned old)
{
    unsigned volume = atomic_load_explicit(&desired_volume, memory_order_relaxed);
    if (volume != old) bsp_audio_set_volume((uint8_t)volume);
    return volume;
}
static void audio_worker(void *argument)
{
    (void)argument;
    music_score_t score;
    int16_t pcm[MUSIC_CHUNK_SAMPLES];
    unsigned applied = 101;
    for (;;) {
        if (xQueueReceive(pending, &score, portMAX_DELAY) != pdTRUE) continue;
restart:;
        /* score is exclusively owned by this worker. Only a whole queue receive
         * can replace it, and restart discards all old parser pointers/state. */
        stock_rtttl_t parser;
        if (!stock_rtttl_init(&parser, score.bytes, score.length)) continue;
        applied = apply_volume(applied);
        if (bsp_audio_wake() != ESP_OK ||
            bsp_audio_set_format(STOCK_RTTTL_SAMPLE_RATE, 16, 1) != ESP_OK) {
            (void)bsp_audio_sleep();
            continue;
        }
        stock_rtttl_note_t note;
        bool failed = false;
        for (;;) {
            if (xQueueReceive(pending, &score, 0) == pdTRUE) goto restart;
            if (stock_rtttl_next(&parser, &note) != 1) break;
            uint16_t phase = 0;
            uint32_t remaining = note.duration_ms * (STOCK_RTTTL_SAMPLE_RATE / 1000u);
            while (remaining) {
                if (xQueueReceive(pending, &score, 0) == pdTRUE) goto restart;
                applied = apply_volume(applied);
                size_t count = remaining < MUSIC_CHUNK_SAMPLES ? remaining : MUSIC_CHUNK_SAMPLES;
                stock_rtttl_pcm(pcm, count, note.frequency, &phase);
                if (bsp_audio_write(pcm, count * sizeof(*pcm)) != ESP_OK) {
                    failed = true;
                    break;
                }
                remaining -= (uint32_t)count;
            }
            if (failed) break;
        }
        /* Worker owns format/writes/sleep even on failure and interruption.
         * A later accepted score may retry hardware; no busy error spin. */
        (void)bsp_audio_sleep();
    }
}
static bool load_score(void)
{
    received.length = 0;
    if (nvs_flash_init() != ESP_OK) return false;
    nvs_handle_t handle;
    esp_err_t error = nvs_open(MUSIC_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return true;
    if (error != ESP_OK) return false;
    size_t required = 0;
    error = nvs_get_blob(handle, MUSIC_KEY, NULL, &required);
    bool ok = error == ESP_ERR_NVS_NOT_FOUND;
    if (error == ESP_OK && required > 0 && required <= sizeof(received.bytes)) {
        size_t actual = sizeof(received.bytes);
        error = nvs_get_blob(handle, MUSIC_KEY, received.bytes, &actual);
        ok = error == ESP_OK && actual == required &&
             stock_rtttl_validate(received.bytes, actual, NULL);
        if (ok) received.length = (uint16_t)actual;
    }
    nvs_close(handle);
    return ok;
}
bool stock_music_init(unsigned volume)
{
    if (ready) return true;
    if (!load_score()) return false;
    stock_music_set_volume(volume);
    pending = xQueueCreate(1, sizeof(music_score_t));
    if (!pending) return false;
    if ((received.length && xQueueOverwrite(pending, &received) != pdPASS) ||
        xTaskCreate(audio_worker, "stock_music", 4096, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(pending);
        pending = NULL;
        return false;
    }
    stock_transfer_init(&transfer, STOCK_TRANSFER_AUDIO, STOCK_RTTTL_CAPACITY);
    received.length = 0;
    session_known = false;
    ready = true;
    return true;
}
static bool allowed(void)
{
    if (!cancelled && call_guard && !call_guard(call_context, active_session)) cancelled = true;
    return !cancelled;
}
static stock_sink_result_t begin_score(void *context, uint32_t total)
{
    (void)context;
    if (!allowed()) return STOCK_SINK_IO;
    received.length = 0;
    return total <= STOCK_RTTTL_CAPACITY ? STOCK_SINK_OK : STOCK_SINK_LIMIT;
}
static stock_sink_result_t append_score(void *context, const uint8_t *bytes, size_t length)
{
    (void)context;
    if (!allowed() || length > sizeof(received.bytes) - received.length) return STOCK_SINK_IO;
    memcpy(received.bytes + received.length, bytes, length);
    received.length += (uint16_t)length;
    return allowed() ? STOCK_SINK_OK : STOCK_SINK_IO;
}
static stock_sink_result_t save_score(void *context)
{
    (void)context;
    if (!allowed()) return STOCK_SINK_IO;
    if (!stock_rtttl_validate(received.bytes, received.length, NULL)) return STOCK_SINK_INVALID;
    if (!allowed()) return STOCK_SINK_IO;
    esp_err_t error = nvs_flash_init();
    if (!allowed() || error != ESP_OK) return STOCK_SINK_IO;
    nvs_handle_t handle;
    error = nvs_open(MUSIC_NAMESPACE, NVS_READWRITE, &handle);
    bool still_allowed = allowed();
    if (error != ESP_OK) return STOCK_SINK_IO;
    stock_sink_result_t result = STOCK_SINK_IO;
    if (!still_allowed) goto close;
    /* nvs_set_blob may already persist; guard cancellation is not rollback. */
    error = nvs_set_blob(handle, MUSIC_KEY, received.bytes, received.length);
    if (!allowed() || error != ESP_OK) goto close;
    error = nvs_commit(handle);
    if (!allowed() || error != ESP_OK) goto close;
    result = STOCK_SINK_OK;
close:
    nvs_close(handle);
    if (!allowed() || result != STOCK_SINK_OK) return STOCK_SINK_IO;
    /* This guard authorizes publication. Do not retain its context in the
     * asynchronous job: the job is a committed, independent snapshot. */
    return xQueueOverwrite(pending, &received) == pdPASS ? STOCK_SINK_OK : STOCK_SINK_IO;
}
void stock_music_disconnect(void)
{
    stock_transfer_reset(&transfer);
    received.length = 0;
    session_known = false;
}
uint8_t stock_music_handle(uint32_t session, stock_music_guard_t guard, void *context,
                           const uint8_t *payload, size_t length)
{
    if (!session_known || active_session != session) {
        stock_music_disconnect();
        active_session = session;
        session_known = true;
    }
    call_guard = guard; call_context = context; cancelled = false;
    uint8_t status = 0x16;
    if (!allowed()) status = 0x12;
    else if (ready) {
        const stock_transfer_sink_t sink = {NULL, begin_score, append_score, save_score};
        status = stock_transfer_receive(&transfer, &sink, payload, length).status;
    }
    /* There is intentionally no post-publication cancellation promise: the
     * caller checks its session before notifying. Every NVS stage has both
     * checks; a queue copy authorized immediately before it is accepted work. */
    if (cancelled) { stock_music_disconnect(); status = 0x12; }
    call_guard = NULL; call_context = NULL;
    return status;
}
