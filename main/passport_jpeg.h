#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Bounds-check a baseline JPEG header for the streaming decoder. This does
 * not validate compressed scan data or prove successful display decoding. */
bool passport_jpeg_dimensions(const uint8_t *data, size_t length,
                               uint16_t *width, uint16_t *height);
