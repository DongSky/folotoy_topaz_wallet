#pragma once
#include "stock_screenshot.h"
#include <stdbool.h>
typedef enum { STOCK_SHOT_IDLE, STOCK_SHOT_CAPTURING, STOCK_SHOT_BEGIN,
               STOCK_SHOT_DATA, STOCK_SHOT_END, STOCK_SHOT_COMPLETE, STOCK_SHOT_FAILED } stock_shot_phase_t;
typedef struct {
    stock_shot_phase_t phase;
    uint16_t session, sequence, packets;
    uint32_t offset, crc;
    const uint8_t *pixels;
    size_t pixels_size, packet_length, packet_pixels;
    uint8_t packet[STOCK_SCREENSHOT_PACKET_MAX];
    bool awaiting_ack;
    unsigned retries;
    uint64_t deadline, completed_at;
} stock_shot_t;
typedef struct { bool reply, capture, release; uint8_t status; } stock_shot_action_t;
/* Worker-owned state; one immutable captured RGB565LE frame until release.
 * Caller creates nonzero session IDs and binds this instance to BLE session. */
stock_shot_action_t stock_shot_command(stock_shot_t *shot, const uint8_t *bytes,
    size_t length, uint16_t new_session, uint64_t now_ms);
bool stock_shot_captured(stock_shot_t *shot, const uint8_t *pixels, size_t size);
/* Packet pointer stays valid through retries. mark_sent only after transport
 * accepts it. poll reports timeout (0x11) and release after three retries. */
size_t stock_shot_packet(stock_shot_t *shot, size_t att_capacity, uint64_t now_ms,
                          const uint8_t **bytes);
void stock_shot_mark_sent(stock_shot_t *shot, uint64_t now_ms);
stock_shot_action_t stock_shot_poll(stock_shot_t *shot, uint64_t now_ms);
