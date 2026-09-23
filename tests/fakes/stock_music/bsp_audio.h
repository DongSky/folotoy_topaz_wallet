#pragma once
#include "nvs.h"
esp_err_t bsp_audio_wake(void);
esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t channels);
esp_err_t bsp_audio_write(const void *pcm, size_t length);
esp_err_t bsp_audio_sleep(void);
void bsp_audio_set_volume(uint8_t percent);
