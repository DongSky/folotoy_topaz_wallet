#include "stock_profile_nvs.h"
#include "nvs.h"
#include "nvs_flash.h"

static stock_profile_store_result_t read_error(esp_err_t error)
{
    if (error == ESP_ERR_NVS_NOT_FOUND) return STOCK_PROFILE_STORE_NOT_FOUND;
    if (error == ESP_ERR_NVS_TYPE_MISMATCH || error == ESP_ERR_NVS_INVALID_LENGTH)
        return STOCK_PROFILE_STORE_INVALID_DATA;
    return STOCK_PROFILE_STORE_IO_ERROR;
}

stock_profile_store_result_t stock_profile_nvs_load(stock_profile_t *output,
                                                    bool *migrated)
{
    if (migrated) *migrated = false;
    if (!output) return STOCK_PROFILE_STORE_INVALID_ARG;
    /* Initialization failures are surfaced unchanged in severity: no erase,
     * deinit/reinit recovery or namespace creation on the load path. */
    if (nvs_flash_init() != ESP_OK) return STOCK_PROFILE_STORE_IO_ERROR;
    nvs_handle_t handle;
    esp_err_t error = nvs_open(STOCK_PROFILE_NAMESPACE, NVS_READONLY, &handle);
    if (error != ESP_OK) return read_error(error);
    size_t required = 0;
    error = nvs_get_blob(handle, STOCK_PROFILE_KEY, NULL, &required);
    stock_profile_store_result_t result;
    uint8_t blob[STOCK_PROFILE_BLOB_SIZE];
    if (error != ESP_OK) {
        result = read_error(error);
    } else if (required != 197 && required != 213 && required != 245) {
        result = STOCK_PROFILE_STORE_INVALID_DATA;
    } else {
        size_t actual = sizeof(blob);
        error = nvs_get_blob(handle, STOCK_PROFILE_KEY, blob, &actual);
        if (error != ESP_OK) result = read_error(error);
        else if (actual != required || !stock_profile_decode(output, blob, actual, migrated))
            result = STOCK_PROFILE_STORE_INVALID_DATA;
        else result = STOCK_PROFILE_STORE_OK;
    }
    nvs_close(handle);
    return result;
}

static bool save_allowed(stock_profile_save_guard_t guard, void *context)
{
    return guard == NULL || guard(context);
}

stock_profile_store_result_t stock_profile_nvs_save_guarded(const stock_profile_t *profile,
    stock_profile_save_guard_t guard, void *context)
{
    if (!profile) return STOCK_PROFILE_STORE_INVALID_ARG;
    uint8_t blob[STOCK_PROFILE_BLOB_SIZE];
    if (stock_profile_encode(profile, blob, sizeof(blob)) != sizeof(blob))
        return STOCK_PROFILE_STORE_INVALID_DATA;
    if (!save_allowed(guard, context)) return STOCK_PROFILE_STORE_CANCELLED;
    if (nvs_flash_init() != ESP_OK) return STOCK_PROFILE_STORE_IO_ERROR;
    if (!save_allowed(guard, context)) return STOCK_PROFILE_STORE_CANCELLED;
    nvs_handle_t handle;
    if (nvs_open(STOCK_PROFILE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return STOCK_PROFILE_STORE_IO_ERROR;
    stock_profile_store_result_t result = STOCK_PROFILE_STORE_CANCELLED;
    if (!save_allowed(guard, context)) goto close;
    esp_err_t error = nvs_set_blob(handle, STOCK_PROFILE_KEY, blob, sizeof(blob));
    if (error != ESP_OK) { result = STOCK_PROFILE_STORE_IO_ERROR; goto close; }
    if (!save_allowed(guard, context)) goto close;
    error = nvs_commit(handle);
    if (error != ESP_OK) { result = STOCK_PROFILE_STORE_IO_ERROR; goto close; }
    if (!save_allowed(guard, context)) goto close;
    result = STOCK_PROFILE_STORE_OK;
close:
    nvs_close(handle);
    return result;
}

stock_profile_store_result_t stock_profile_nvs_save(const stock_profile_t *profile)
{
    return stock_profile_nvs_save_guarded(profile, NULL, NULL);
}
