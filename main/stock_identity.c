#include "stock_identity.h"

#include <string.h>
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"

static bool field_length(const char *field, size_t capacity, size_t *length)
{
    const char *end = memchr(field, '\0', capacity);
    if (end == NULL) return false;
    *length = (size_t)(end - field);
    return true;
}

static bool identity_lengths(const stock_identity_t *identity, size_t lengths[4])
{
    return identity != NULL &&
        field_length(identity->device_key, sizeof(identity->device_key), &lengths[0]) &&
        field_length(identity->device_secret, sizeof(identity->device_secret), &lengths[1]) &&
        field_length(identity->product_key, sizeof(identity->product_key), &lengths[2]) &&
        field_length(identity->hardware_version, sizeof(identity->hardware_version), &lengths[3]);
}

void stock_identity_clear(stock_identity_t *identity)
{
    if (identity == NULL) return;
    volatile unsigned char *bytes = (volatile unsigned char *)identity;
    for (size_t i = 0; i < sizeof(*identity); ++i) bytes[i] = 0;
}

bool stock_identity_ready(const stock_identity_t *identity)
{
    size_t lengths[4];
    return identity_lengths(identity, lengths) && lengths[1] != 0 && lengths[2] != 0;
}

static void copy_legacy_slot(uint8_t *output, size_t width,
                             const char *source, size_t length)
{
    /* Getter VA 0x4201613a rejects strlen >= capacity. Serializer VA
     * 0x4200b1d8 uses a zero-initialized 40-byte temporary for every getter. */
    if (length >= 40u) return;
    memcpy(output, source, length < width ? length : width);
}

bool stock_identity_serialize(const stock_identity_t *identity,
                              uint8_t *output, size_t capacity)
{
    size_t lengths[4];
    if (output == NULL || capacity < STOCK_IDENTITY_WIRE_SIZE) return false;
    memset(output, 0, STOCK_IDENTITY_WIRE_SIZE);
    if (!identity_lengths(identity, lengths)) return false;
    output[0] = 1;
    /* Provisioning predicate VA 0x42016072: secret AND product key, not SN. */
    output[1] = lengths[1] != 0 && lengths[2] != 0;
    copy_legacy_slot(output + 2, 24, identity->device_key, lengths[0]);
    copy_legacy_slot(output + 26, 32, identity->device_secret, lengths[1]);
    copy_legacy_slot(output + 58, 8, identity->hardware_version, lengths[3]);
    return true;
}

static stock_identity_result_t read_string(nvs_handle_t handle, const char *key,
                                            char *output, size_t capacity)
{
    size_t required = 0;
    esp_err_t error = nvs_get_str(handle, key, NULL, &required);
    if (error == ESP_ERR_NVS_NOT_FOUND) return STOCK_IDENTITY_OK;
    if (error == ESP_ERR_NVS_TYPE_MISMATCH || error == ESP_ERR_NVS_INVALID_LENGTH)
        return STOCK_IDENTITY_INVALID_DATA;
    if (error != ESP_OK) return STOCK_IDENTITY_IO_ERROR;
    if (required == 0 || required > capacity) return STOCK_IDENTITY_INVALID_DATA;
    size_t actual = capacity;
    error = nvs_get_str(handle, key, output, &actual);
    if (error == ESP_ERR_NVS_TYPE_MISMATCH || error == ESP_ERR_NVS_INVALID_LENGTH)
        return STOCK_IDENTITY_INVALID_DATA;
    if (error != ESP_OK) return STOCK_IDENTITY_IO_ERROR;
    if (actual == 0 || actual > capacity || actual != required ||
        memchr(output, '\0', actual) != output + actual - 1)
        return STOCK_IDENTITY_INVALID_DATA;
    return STOCK_IDENTITY_OK;
}

stock_identity_result_t stock_identity_load(stock_identity_t *output)
{
    if (output == NULL) return STOCK_IDENTITY_INVALID_ARG;
    stock_identity_clear(output);
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, STOCK_IDENTITY_PARTITION);
    if (partition == NULL) return STOCK_IDENTITY_MISSING_PARTITION;
    /* Handle access mode is too late: NVS init can recover/erase pages. Require
     * the real partition descriptor's readonly flag so both NVS recovery and
     * esp_partition write/erase APIs enforce protection. A copied descriptor
     * passed to init_partition_ptr is insufficient: cached storage by label
     * can retain an earlier writable descriptor. Never mutate the shared one. */
    if (!partition->readonly) return STOCK_IDENTITY_READONLY_REQUIRED;
    esp_err_t error = nvs_flash_init_partition(STOCK_IDENTITY_PARTITION);
    if (error == ESP_ERR_NOT_FOUND || error == ESP_ERR_NVS_PART_NOT_FOUND)
        return STOCK_IDENTITY_MISSING_PARTITION;
    if (error != ESP_OK) return STOCK_IDENTITY_IO_ERROR;
    nvs_handle_t handle;
    error = nvs_open_from_partition(STOCK_IDENTITY_PARTITION,
                                     STOCK_IDENTITY_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return STOCK_IDENTITY_NOT_PROVISIONED;
    if (error == ESP_ERR_NOT_FOUND || error == ESP_ERR_NVS_PART_NOT_FOUND)
        return STOCK_IDENTITY_MISSING_PARTITION;
    if (error != ESP_OK) return STOCK_IDENTITY_IO_ERROR;

    stock_identity_t loaded = {0};
    stock_identity_result_t result = read_string(handle, "DeviceKey",
                                                  loaded.device_key, sizeof(loaded.device_key));
    if (result == STOCK_IDENTITY_OK)
        result = read_string(handle, "DeviceSecret", loaded.device_secret, sizeof(loaded.device_secret));
    if (result == STOCK_IDENTITY_OK)
        result = read_string(handle, "ProductKey", loaded.product_key, sizeof(loaded.product_key));
    if (result == STOCK_IDENTITY_OK)
        result = read_string(handle, "HardwareVersion", loaded.hardware_version, sizeof(loaded.hardware_version));
    nvs_close(handle);
    if (result == STOCK_IDENTITY_OK) {
        *output = loaded;
        if (!stock_identity_ready(output)) result = STOCK_IDENTITY_NOT_PROVISIONED;
    }
    stock_identity_clear(&loaded);
    return result;
}
