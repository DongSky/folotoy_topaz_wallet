#include "stock_identity.h"
#include "nvs.h"
#include "esp_partition.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* All fixtures are invented. Never read a device or real provisioning dump. */
static const char *s_keys[] = {"DeviceKey", "DeviceSecret", "ProductKey", "HardwareVersion"};
static const char *s_values[4];
static int s_init_error, s_open_error, s_get_error;
static unsigned s_init_calls, s_open_calls, s_get_calls, s_close_calls;
static int s_malformed_key;
static esp_partition_t s_partition;
static bool s_partition_present;
static unsigned s_find_calls;

const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
                                                esp_partition_subtype_t subtype,
                                                const char *label)
{
    assert(type == ESP_PARTITION_TYPE_DATA);
    assert(subtype == ESP_PARTITION_SUBTYPE_DATA_NVS);
    assert(strcmp(label, "cardid") == 0);
    ++s_find_calls;
    return s_partition_present ? &s_partition : NULL;
}

esp_err_t nvs_flash_init_partition(const char *partition)
{
    assert(strcmp(partition, "cardid") == 0);
    assert(s_find_calls != 0 && s_partition_present && s_partition.readonly);
    ++s_init_calls;
    return s_init_error;
}

esp_err_t nvs_open_from_partition(const char *partition, const char *namespace_name,
                                  nvs_open_mode_t mode, nvs_handle_t *handle)
{
    assert(strcmp(partition, "cardid") == 0);
    assert(strcmp(namespace_name, "folotoy-key") == 0);
    assert(mode == NVS_READONLY);
    ++s_open_calls;
    if (s_open_error == ESP_OK) *handle = 42;
    return s_open_error;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *output, size_t *length)
{
    assert(handle == 42 && length != NULL);
    ++s_get_calls;
    if (s_get_error != ESP_OK) return s_get_error;
    size_t index = 0;
    while (index < 4 && strcmp(key, s_keys[index]) != 0) ++index;
    assert(index < 4);
    if (s_values[index] == NULL) return ESP_ERR_NVS_NOT_FOUND;
    size_t required = strlen(s_values[index]) + 1;
    if (output != NULL) {
        if (*length < required) { *length = required; return ESP_ERR_NVS_INVALID_LENGTH; }
        memcpy(output, s_values[index], required);
        if ((int)index == s_malformed_key) output[required - 1] = 'X';
    }
    *length = required;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(handle == 42);
    ++s_close_calls;
}

static void reset_fake(void)
{
    s_values[0] = "TEST-SN-0001";
    s_values[1] = "synthetic-secret-not-production";
    s_values[2] = "synthetic-product";
    s_values[3] = "HW-TEST-1";
    s_init_error = s_open_error = s_get_error = ESP_OK;
    s_init_calls = s_open_calls = s_get_calls = s_close_calls = 0;
    s_malformed_key = -1;
    s_partition.readonly = true;
    s_partition_present = true;
    s_find_calls = 0;
}

static stock_identity_t synthetic(void)
{
    stock_identity_t id = {0};
    strcpy(id.device_key, "TEST-SN-0001");
    strcpy(id.device_secret, "synthetic-secret");
    strcpy(id.product_key, "must-not-be-in-wire-output");
    strcpy(id.hardware_version, "HW-TEST-1");
    return id;
}

static void assert_zero(const void *data, size_t length)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < length; ++i) assert(bytes[i] == 0);
}

static void test_exact_packet_and_product_exclusion(void)
{
    stock_identity_t id = synthetic();
    uint8_t packet[74];
    memset(packet, 0xa5, sizeof(packet));
    assert(stock_identity_serialize(&id, packet, sizeof(packet)));
    uint8_t expected[72] = {1, 1};
    memcpy(expected + 2, "TEST-SN-0001", 12);
    memcpy(expected + 26, "synthetic-secret", 16);
    memcpy(expected + 58, "HW-TEST-", 8);
    assert(memcmp(packet, expected, sizeof(expected)) == 0);
    assert(packet[72] == 0xa5 && packet[73] == 0xa5);
    assert(stock_identity_ready(&id));
}

static void test_readiness_uses_secret_and_product_not_serial(void)
{
    stock_identity_t id = synthetic();
    uint8_t packet[72];
    id.device_key[0] = '\0';
    assert(stock_identity_ready(&id));
    assert(stock_identity_serialize(&id, packet, sizeof(packet)) && packet[1] == 1);
    id.product_key[0] = '\0';
    assert(!stock_identity_ready(&id));
    assert(stock_identity_serialize(&id, packet, sizeof(packet)) && packet[1] == 0);
    assert(memcmp(packet + 26, "synthetic-secret", 16) == 0);
    strcpy(id.product_key, "fake-product");
    id.device_secret[0] = '\0';
    assert(!stock_identity_ready(&id));
    assert(stock_identity_serialize(&id, packet, sizeof(packet)) && packet[1] == 0);
}

static void test_secret_40_byte_temporary_boundary(void)
{
    const size_t lengths[] = {0, 1, 31, 32, 33, 39, 40, 63, 64};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        stock_identity_t id = synthetic();
        memset(id.device_secret, 'S', lengths[i]);
        id.device_secret[lengths[i]] = '\0';
        uint8_t packet[72];
        assert(stock_identity_serialize(&id, packet, sizeof(packet)));
        assert(packet[1] == (lengths[i] != 0));
        size_t copied = lengths[i] >= 40 ? 0 : (lengths[i] < 32 ? lengths[i] : 32);
        for (size_t j = 0; j < copied; ++j) assert(packet[26 + j] == 'S');
        assert_zero(packet + 26 + copied, 32 - copied);
        assert_zero(packet + 66, 6);
    }
}

static void test_all_field_boundaries_and_nontermination(void)
{
    stock_identity_t id = {0};
    memset(id.device_key, 'N', 12);
    memset(id.device_secret, 'S', 64);
    memset(id.product_key, 'P', 32);
    memset(id.hardware_version, 'H', 16);
    uint8_t packet[72];
    assert(stock_identity_ready(&id));
    assert(stock_identity_serialize(&id, packet, sizeof(packet)));
    for (size_t i = 0; i < 12; ++i) assert(packet[2 + i] == 'N');
    assert_zero(packet + 14, 12);
    assert_zero(packet + 26, 32);
    for (size_t i = 0; i < 8; ++i) assert(packet[58 + i] == 'H');
    for (unsigned field = 0; field < 4; ++field) {
        stock_identity_t malformed = id;
        switch (field) {
            case 0: malformed.device_key[12] = 'X'; break;
            case 1: malformed.device_secret[64] = 'X'; break;
            case 2: malformed.product_key[32] = 'X'; break;
            default: malformed.hardware_version[16] = 'X'; break;
        }
        memset(packet, 0xa5, sizeof(packet));
        assert(!stock_identity_ready(&malformed));
        assert(!stock_identity_serialize(&malformed, packet, sizeof(packet)));
        assert_zero(packet, sizeof(packet));
    }
}

static void test_invalid_arguments_and_explicit_clear(void)
{
    stock_identity_t id = synthetic();
    uint8_t packet[72];
    memset(packet, 0xa5, sizeof(packet));
    assert(!stock_identity_serialize(&id, packet, 71));
    assert(packet[0] == 0xa5 && packet[71] == 0xa5);
    assert(!stock_identity_serialize(&id, NULL, 72));
    assert(!stock_identity_serialize(NULL, packet, 72));
    assert_zero(packet, sizeof(packet));
    assert(!stock_identity_ready(NULL));
    assert(stock_identity_load(NULL) == STOCK_IDENTITY_INVALID_ARG);
    stock_identity_clear(&id);
    assert_zero(&id, sizeof(id));
    stock_identity_clear(NULL);
}

static void test_loader_is_readonly_bounded_and_preserves_fields(void)
{
    reset_fake();
    stock_identity_t id;
    assert(stock_identity_load(&id) == STOCK_IDENTITY_OK);
    assert(s_init_calls == 1 && s_open_calls == 1 && s_close_calls == 1 && s_get_calls == 8);
    assert(strcmp(id.device_key, s_values[0]) == 0);
    assert(strcmp(id.device_secret, s_values[1]) == 0);
    assert(strcmp(id.product_key, s_values[2]) == 0);
    assert(strcmp(id.hardware_version, s_values[3]) == 0);
    s_values[0] = s_values[3] = NULL;
    assert(stock_identity_load(&id) == STOCK_IDENTITY_OK);
    assert(id.device_key[0] == 0 && id.hardware_version[0] == 0 && stock_identity_ready(&id));
    s_values[2] = NULL;
    assert(stock_identity_load(&id) == STOCK_IDENTITY_NOT_PROVISIONED);
    assert(id.device_secret[0] != 0 && !stock_identity_ready(&id));
    reset_fake();
    s_values[1] = "";
    assert(stock_identity_load(&id) == STOCK_IDENTITY_NOT_PROVISIONED);
}

static void test_rejects_writable_partition_before_nvs_initialization(void)
{
    stock_identity_t id;
    reset_fake();
    s_partition_present = false;
    memset(&id, 0xa5, sizeof(id));
    assert(stock_identity_load(&id) == STOCK_IDENTITY_MISSING_PARTITION);
    assert(s_init_calls == 0 && s_open_calls == 0 && s_get_calls == 0 && s_close_calls == 0);
    assert_zero(&id, sizeof(id));
    reset_fake();
    s_partition.readonly = false;
    memset(&id, 0xa5, sizeof(id));
    assert(stock_identity_load(&id) == STOCK_IDENTITY_READONLY_REQUIRED);
    assert(s_init_calls == 0 && s_open_calls == 0 && s_get_calls == 0 && s_close_calls == 0);
    assert(!s_partition.readonly); /* Shared descriptor was not changed. */
    assert_zero(&id, sizeof(id));
    s_partition.readonly = true;
    assert(stock_identity_load(&id) == STOCK_IDENTITY_OK);
    assert(s_init_calls == 1 && s_open_calls == 1 && s_close_calls == 1);
}

static void test_missing_partition_namespace_and_storage_errors(void)
{
    stock_identity_t id;
    reset_fake();
    s_init_error = ESP_ERR_NOT_FOUND;
    memset(&id, 0xa5, sizeof(id));
    assert(stock_identity_load(&id) == STOCK_IDENTITY_MISSING_PARTITION);
    assert(s_open_calls == 0 && s_get_calls == 0 && s_close_calls == 0);
    assert_zero(&id, sizeof(id));
    reset_fake(); s_init_error = -1;
    assert(stock_identity_load(&id) == STOCK_IDENTITY_IO_ERROR && s_open_calls == 0);
    reset_fake(); s_open_error = ESP_ERR_NVS_NOT_FOUND;
    assert(stock_identity_load(&id) == STOCK_IDENTITY_NOT_PROVISIONED);
    assert(s_get_calls == 0 && s_close_calls == 0);
    assert_zero(&id, sizeof(id));
    reset_fake(); s_open_error = -1;
    assert(stock_identity_load(&id) == STOCK_IDENTITY_IO_ERROR && s_close_calls == 0);
    reset_fake(); s_get_error = -1;
    assert(stock_identity_load(&id) == STOCK_IDENTITY_IO_ERROR && s_close_calls == 1);
    assert_zero(&id, sizeof(id));
    reset_fake(); s_get_error = ESP_ERR_NVS_TYPE_MISMATCH;
    assert(stock_identity_load(&id) == STOCK_IDENTITY_INVALID_DATA && s_close_calls == 1);
}

static void test_loader_rejects_overlength_and_unterminated_data(void)
{
    const size_t capacities[] = {13, 65, 33, 17};
    for (unsigned field = 0; field < 4; ++field) {
        reset_fake();
        char oversized[66] = {0};
        memset(oversized, 'X', capacities[field]);
        s_values[field] = oversized;
        stock_identity_t id;
        memset(&id, 0xa5, sizeof(id));
        assert(stock_identity_load(&id) == STOCK_IDENTITY_INVALID_DATA);
        assert(s_close_calls == 1);
        assert_zero(&id, sizeof(id));
        reset_fake(); s_malformed_key = (int)field;
        assert(stock_identity_load(&id) == STOCK_IDENTITY_INVALID_DATA);
        assert(s_close_calls == 1);
        assert_zero(&id, sizeof(id));
    }
}

int main(void)
{
    test_exact_packet_and_product_exclusion();
    test_readiness_uses_secret_and_product_not_serial();
    test_secret_40_byte_temporary_boundary();
    test_all_field_boundaries_and_nontermination();
    test_invalid_arguments_and_explicit_clear();
    test_loader_is_readonly_bounded_and_preserves_fields();
    test_rejects_writable_partition_before_nvs_initialization();
    test_missing_partition_namespace_and_storage_errors();
    test_loader_rejects_overlength_and_unterminated_data();
    puts("Stock identity: PASS (synthetic fixtures only)");
    return 0;
}
