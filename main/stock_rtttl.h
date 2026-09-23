#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STOCK_RTTTL_CAPACITY 1024u
#define STOCK_RTTTL_MAX_DURATION_MS 600000u
#define STOCK_RTTTL_SAMPLE_RATE 8000u
typedef struct {
    const uint8_t *cursor, *end;
    uint32_t denominator, octave, whole_ms, elapsed_ms;
} stock_rtttl_t;
typedef struct { uint32_t frequency, duration_ms; } stock_rtttl_note_t;

/* Independent implementation from stock application instructions. Defaults
 * d4/o6/b63; integer timing/frequency table and square-wave amplitude match stock.
 * Borrowed input must remain immutable until parsing ends. No NUL required.
 * Intentional safety restrictions: <=1024 bytes, <=10 minutes total, numbers
 * <=65535, octave0..8 (including B# carry), every note >=1ms, at least one note.
 * Full validation rejects embedded NUL, malformed/unknown defaults and notes,
 * and incomplete headers; stock only validates its permissive header at END.
 * These restrictions prevent unbounded playback, zero-time loops and shifts.
 */
bool stock_rtttl_init(stock_rtttl_t *parser, const uint8_t *bytes, size_t length);
/* 1 note, 0 end, -1 invalid. Runtime budget is checked as notes are consumed. */
int stock_rtttl_next(stock_rtttl_t *parser, stock_rtttl_note_t *note);
bool stock_rtttl_validate(const uint8_t *bytes, size_t length, uint32_t *duration_ms);
/* Signed16 mono at8kHz; phase zero at each note, continuous across its chunks.
 * Pause frequency0 emits zero and resets phase. Positive amplitude is6000. */
void stock_rtttl_pcm(int16_t *out, size_t samples, uint32_t frequency, uint16_t *phase);
