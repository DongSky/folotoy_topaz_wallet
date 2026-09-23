#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { ESP_PARTITION_TYPE_DATA = 1 } esp_partition_type_t;
typedef enum { ESP_PARTITION_SUBTYPE_DATA_NVS = 2 } esp_partition_subtype_t;
typedef struct { bool readonly; } esp_partition_t;
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label);
