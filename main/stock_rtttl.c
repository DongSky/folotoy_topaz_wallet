#include "stock_rtttl.h"
#include <string.h>

static bool digit(uint8_t c) { return c >= '0' && c <= '9'; }
static bool separator(uint8_t c) { return c == ',' || c == ' '; }
static bool number(const uint8_t **cursor, const uint8_t *end, uint32_t *value)
{
    const uint8_t *p = *cursor;
    if (p == end || !digit(*p)) return false;
    uint32_t n = 0;
    do {
        unsigned d = *p++ - '0';
        if (n > (65535u - d) / 10u) return false;
        n = n * 10u + d;
    } while (p < end && digit(*p));
    *cursor = p; *value = n;
    return true;
}
bool stock_rtttl_init(stock_rtttl_t *parser, const uint8_t *bytes, size_t length)
{
    if (!parser) return false;
    *parser = (stock_rtttl_t){0};
    if (!bytes || !length || length > STOCK_RTTTL_CAPACITY || memchr(bytes, 0, length)) return false;
    const uint8_t *p = bytes, *end = bytes + length;
    while (p < end && *p != ':') ++p;
    if (p == end) return false;
    ++p;
    uint32_t duration = 4, octave = 6, bpm = 63;
    while (p < end && *p != ':') {
        if (separator(*p)) { ++p; continue; }
        uint8_t key = *p++;
        if (p < end && *p == '=') ++p;
        uint32_t value;
        if (!number(&p, end, &value)) return false;
        if (key == 'd' && value) duration = value;
        else if (key == 'o' && value <= 8) octave = value;
        else if (key == 'b' && value) bpm = value;
        else return false;
        if (p < end && *p != ':' && !separator(*p)) return false;
    }
    if (p == end) return false;
    *parser = (stock_rtttl_t){.cursor = p + 1, .end = end,
        .denominator = duration, .octave = octave, .whole_ms = 240000u / bpm};
    return true;
}
int stock_rtttl_next(stock_rtttl_t *parser, stock_rtttl_note_t *note)
{
    if (!parser || !note || !parser->cursor || !parser->end) return -1;
    const uint8_t *p = parser->cursor, *end = parser->end;
    while (p < end && separator(*p)) ++p;
    if (p == end) { parser->cursor = p; return 0; }
    uint32_t denominator = parser->denominator;
    if (digit(*p) && (!number(&p, end, &denominator) || !denominator)) return -1;
    if (p == end) return -1;
    uint8_t key = *p++;
    if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
    int semitone;
    switch (key) {
    case 'c': semitone = 0; break;
    case 'd': semitone = 2; break;
    case 'e': semitone = 4; break;
    case 'f': semitone = 5; break;
    case 'g': semitone = 7; break;
    case 'a': semitone = 9; break;
    case 'b': semitone = 11; break;
    case 'p': semitone = -1; break;
    default: return -1;
    }
    if (p < end && *p == '#') {
        if (semitone < 0) return -1;
        ++semitone; ++p;
    }
    bool dotted = false, explicit_octave = false;
    uint32_t octave = parser->octave;
    while (p < end && (*p == '.' || digit(*p))) {
        if (*p == '.') {
            if (dotted) return -1;
            dotted = true; ++p;
        } else {
            if (explicit_octave || !number(&p, end, &octave) || octave > 8) return -1;
            explicit_octave = true;
        }
    }
    if (p < end && !separator(*p)) return -1;
    uint32_t duration = parser->whole_ms / denominator;
    if (dotted) duration = duration * 3u / 2u;
    if (!duration || duration > STOCK_RTTTL_MAX_DURATION_MS - parser->elapsed_ms) return -1;
    uint32_t frequency = 0;
    if (semitone >= 0) {
        static const uint16_t frequencies[] = {262,277,294,311,330,349,370,392,415,440,466,494};
        octave += (unsigned)semitone / 12u;
        if (octave > 8) return -1;
        frequency = frequencies[semitone % 12];
        frequency = octave >= 4 ? frequency << (octave - 4) : frequency >> (4 - octave);
    }
    parser->elapsed_ms += duration;
    parser->cursor = p;
    *note = (stock_rtttl_note_t){frequency, duration};
    return 1;
}
bool stock_rtttl_validate(const uint8_t *bytes, size_t length, uint32_t *duration_ms)
{
    if (duration_ms) *duration_ms = 0;
    stock_rtttl_t parser;
    if (!stock_rtttl_init(&parser, bytes, length)) return false;
    stock_rtttl_note_t note;
    unsigned count = 0;
    int next;
    while ((next = stock_rtttl_next(&parser, &note)) == 1) ++count;
    if (next < 0 || !count) return false;
    if (duration_ms) *duration_ms = parser.elapsed_ms;
    return true;
}
void stock_rtttl_pcm(int16_t *out, size_t samples, uint32_t frequency, uint16_t *phase)
{
    if (!out || !phase) return;
    if (!frequency) { memset(out, 0, samples * sizeof(*out)); *phase = 0; return; }
    uint32_t step = (uint32_t)(((uint64_t)frequency << 16) / STOCK_RTTTL_SAMPLE_RATE);
    if (!step) step = 1;
    for (size_t i = 0; i < samples; ++i) {
        out[i] = (*phase & 0x8000u) ? -6000 : 6000;
        *phase = (uint16_t)(*phase + step);
    }
}
