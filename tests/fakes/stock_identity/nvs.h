#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_TYPE_MISMATCH 0x1103
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
#define ESP_ERR_NVS_PART_NOT_FOUND 0x110f
esp_err_t nvs_open_from_partition(const char *partition, const char *namespace_name,
                                  nvs_open_mode_t mode, nvs_handle_t *handle);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *output, size_t *length);
void nvs_close(nvs_handle_t handle);
