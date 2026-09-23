#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*stock_music_guard_t)(void *context, uint32_t session);
/* Application-lifetime singleton. Main must initialize/sleep BSP audio first;
 * after this call the music worker exclusively owns all BSP audio operations.
 * Caller serializes init/handle/disconnect. No LVGL access. Init loads only
 * default-NVS trae_cfg/boot_rtttl (raw blob, no added NUL), never erases NVS.
 * Missing score is valid; read/validation/allocation failures return false and
 * preserve stored bytes. No recovery erase, migration or startup save occurs.
 * Successful init is idempotent. A valid saved score is queued for startup.
 */
bool stock_music_init(unsigned volume);
/* Worker/caller context, never BLE/button callback: can block on NVS.
 * Payload is stripped type3 BEGIN(00,u16LE), DATA(01,bytes), END(02), <=256B.
 * Returns stock status:0 intermediate,1 accepted END,12hex invalid/cancelled,
 * 13hex invalid RTTTL,15hex capacity,16hex storage/queue/unavailable.
 * Session changes reset transfer, including ingress overflow. Guard/context
 * are borrowed only for this call; checked around NVS and before publication.
 * Already-issued NVS set/commit may persist after cancellation; no rollback
 * is claimed. Queuing failure can also leave durable bytes, but never returns
 * success. The last guard before queue publication authorizes playback;
 * accepted music persists across later disconnect. Caller must suppress any
 * response if its session ceases to be current after this function returns.
 * END validates the entire bounded score (see stock_rtttl.h) before saving.
 * Accepted scores are copied into a one-slot latest-wins queue, so replacing
 * a pending/playing score never changes bytes the worker is reading.
 * Acceptance means persisted+queued, not proven audible playback.
 */
uint8_t stock_music_handle(uint32_t session, stock_music_guard_t guard, void *context,
                           const uint8_t *payload, size_t length);
/* Clears incomplete upload only; never erases/stops already accepted music. */
void stock_music_disconnect(void);
/* Atomic desired volume0..100; caller does not touch codec/I2S. Worker applies
 * it before playback and between256-sample chunks (32ms nominal at8kHz).
 * New scores interrupt at the same boundaries. BSP write has no timeout API:
 * actual latency additionally depends on driver scheduling/I/O completion.
 */
void stock_music_set_volume(unsigned volume);
