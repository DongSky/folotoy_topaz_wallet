#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 255
#define ESP_PARTITION_MMAP_DATA 0
typedef unsigned esp_partition_mmap_handle_t;
typedef struct {bool readonly, encrypted; size_t size;} esp_partition_t;
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label);
int esp_partition_erase_range(const esp_partition_t *p,size_t off,size_t len);
int esp_partition_write(const esp_partition_t *p,size_t off,const void *data,size_t len);
int esp_partition_mmap(const esp_partition_t *p,size_t off,size_t len,int mode,const void **data,esp_partition_mmap_handle_t *handle);
void esp_partition_munmap(esp_partition_mmap_handle_t handle);
