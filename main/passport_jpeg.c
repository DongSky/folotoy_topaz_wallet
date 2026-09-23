#include "passport_jpeg.h"
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
bool passport_jpeg_dimensions(const uint8_t *data, size_t length,
                               uint16_t *width, uint16_t *height)
{
    if (!data || !width || !height || length < 4 || data[0] != 0xff || data[1] != 0xd8)
        return false;
    size_t pos = 2;
    while (pos < length) {
        if (data[pos++] != 0xff) return false;
        while (pos < length && data[pos] == 0xff) ++pos;
        if (pos >= length) return false;
        uint8_t marker = data[pos++];
        if (marker == 0 || marker == 0xd8 || marker == 0xd9 || marker == 0xda) return false;
        if (marker == 1 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (length - pos < 2) return false;
        size_t segment = be16(data + pos);
        if (segment < 2 || segment > length - pos) return false;
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if (marker != 0xc0 || segment < 8 || data[pos + 2] != 8) return false;
            unsigned components = data[pos + 7];
            if ((components != 1 && components != 3) || segment != 8 + components * 3) return false;
            uint16_t h = be16(data + pos + 3), w = be16(data + pos + 5);
            if (!w || !h || w > 240 || h > 320) return false;
            *width = w; *height = h;
            return true;
        }
        pos += segment;
    }
    return false;
}
