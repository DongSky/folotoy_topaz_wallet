#include "wallet_ble.h"

bool wallet_ble_peer_allowed(bool window_open, bool owner_present, bool owner_match)
{
    return window_open && (!owner_present || owner_match);
}

bool wallet_ble_link_authorized(bool encrypted, bool authenticated, bool bonded,
                                uint8_t key_size, bool owner_match)
{
    return encrypted && authenticated && bonded && key_size == 16u && owner_match;
}

bool wallet_ble_passkey_can_release(bool window_open, bool pairing_pending,
                                    bool peer_allowed)
{
    return window_open && pairing_pending && peer_allowed;
}

bool wallet_ble_session_matches(uint32_t active_generation, uint16_t active_handle,
                                bool secure, uint32_t event_generation,
                                uint16_t event_handle)
{
    return secure && active_generation == event_generation && active_handle == event_handle;
}

bool wallet_ble_close_should_disconnect(bool explicit_close, bool connected, bool secure)
{
    return connected && (explicit_close || !secure);
}

bool wallet_ble_owner_record_valid(size_t size, uint8_t address_type)
{
    /* NimBLE identity addresses use the public, random, public-ID and
     * random-ID values 0..3. The persisted payload is one type byte + 6 bytes. */
    return size == 7u && address_type <= 3u;
}

bool wallet_ble_stock_serial_valid(const uint8_t identity[72], const char serial[13])
{
    if (!identity || !serial || identity[0] != 1 || identity[1] > 1) return false;
    for (size_t i = 0; i < 12; ++i) {
        if (!((serial[i] >= '0' && serial[i] <= '9') ||
              (serial[i] >= 'a' && serial[i] <= 'f')) || identity[2+i] != (uint8_t)serial[i]) return false;
    }
    if (serial[12] != 0) return false;
    for (size_t i = 14; i < 26; ++i) if (identity[i] != 0) return false;
    for (size_t i = 66; i < 72; ++i) if (identity[i] != 0) return false;
    return true;
}
bool wallet_ble_advertising_allowed(bool stock_mode, bool window_open, bool host_synced,
    bool connected, bool reset_pending, bool owner_recovery)
{
    return host_synced && !connected && !reset_pending &&
        (stock_mode || (window_open && !owner_recovery));
}
bool wallet_ble_connection_allowed(bool stock_mode, bool window_open, bool owner_present,
    bool owner_match, bool owner_recovery, bool reset_pending)
{
    return !reset_pending && (stock_mode ||
        (!owner_recovery && wallet_ble_peer_allowed(window_open, owner_present, owner_match)));
}
bool wallet_ble_window_disconnect(bool stock_mode, bool explicit_close,
    bool connected, bool secure, bool pairing_pending)
{
    return connected && (explicit_close || (!secure && (!stock_mode || pairing_pending)));
}

#ifndef WALLET_BLE_HOST_TEST

#include "wallet_store.h"
#include "stock_ble.h"

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nvs.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <string.h>

#define WALLET_BLE_QUEUE_LENGTH 8u
#define WALLET_BLE_TASK_STACK 4096u
#define WALLET_BLE_TASK_PRIORITY 5u
#define WALLET_BLE_CONN_NONE 0xffffu
#define WALLET_BLE_OWNER_KEY "owner"
#define WALLET_BLE_NVS_NAMESPACE "wallet_ble"

typedef enum {
    BLE_WORK_SYNC,
    BLE_WORK_OPEN,
    BLE_WORK_CLOSE,
    BLE_WORK_CONNECT,
    BLE_WORK_DISCONNECT,
    BLE_WORK_ENCRYPTION,
    BLE_WORK_PASSKEY,
    BLE_WORK_CONFIRM,
    BLE_WORK_RX,
    BLE_WORK_TICK,
    BLE_WORK_ADV_COMPLETE,
    BLE_WORK_RESET_OWNER,
    BLE_WORK_HOST_RESET,
} wallet_ble_work_type_t;

typedef struct {
    wallet_ble_work_type_t type;
    uint32_t generation;
    uint32_t control_generation;
    uint16_t conn_handle;
    int status;
    uint32_t duration_ms;
    bool accept;
    ble_addr_t peer;
    size_t length;
    uint8_t data[WALLET_BLE_PACKET_MAX];
} wallet_ble_work_t;

typedef struct {
    uint8_t type;
    uint8_t value[6];
} wallet_owner_record_t;

typedef struct {
    SemaphoreHandle_t mutex;
    QueueHandle_t queue;
    QueueHandle_t control_queue;
    TaskHandle_t worker_task;
    TaskHandle_t host_task;
    esp_timer_handle_t tick_timer;
    wallet_transfer_t transfer;
    ble_addr_t owner;
    ble_addr_t peer;
    uint16_t conn_handle;
    uint32_t generation;
    uint32_t control_generation;
    uint32_t transfer_generation;
    uint8_t own_addr_type;
    bool initialized;
    bool host_synced;
    bool window_open;
    bool advertising;
    bool connected;
    bool secure;
    bool owner_present;
    bool owner_recovery_required;
    bool pairing_pending;
    bool passkey_released;
    bool reset_pending;
    bool abort_pending;
    bool stock_mode;
    char stock_serial[13];
    uint16_t stock_cancel_handle;
    uint32_t stock_cancel_generation;
    uint16_t terminate_handle;
    uint32_t terminate_generation;
    int terminate_reason;
    uint16_t confirm_handle;
    uint32_t confirm_generation;
    uint32_t confirm_passkey;
    uint32_t passkey;
    int last_error;
    int64_t window_deadline_us;
    int64_t last_activity_us;
} wallet_ble_state_t;

static wallet_ble_state_t s_ble = {.conn_handle = WALLET_BLE_CONN_NONE};
static bool s_init_blocked;
static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(WALLET_BLE_SERVICE_UUID_BYTES);
static const ble_uuid128_t s_write_uuid = BLE_UUID128_INIT(WALLET_BLE_WRITE_UUID_BYTES);
static const ble_uuid128_t s_status_uuid = BLE_UUID128_INIT(WALLET_BLE_STATUS_UUID_BYTES);
static const ble_uuid128_t s_stock_service_uuid = BLE_UUID128_INIT(STOCK_BLE_SERVICE_UUID_BYTES);
static struct ble_npl_event s_stock_control_event;

static int wallet_gap_event(struct ble_gap_event *event, void *context);
static int wallet_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *context, void *arg);

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_write_uuid.u,
                .access_cb = wallet_gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC |
                         BLE_GATT_CHR_F_WRITE_AUTHEN,
            },
            {
                .uuid = &s_status_uuid.u,
                .access_cb = wallet_gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                         BLE_GATT_CHR_F_READ_AUTHEN,
            },
            {0},
        },
    },
    {0},
};

/* ESP-IDF exposes the hooks but does not declare this helper publicly. */
void ble_store_config_init(void);

static bool enqueue(const wallet_ble_work_t *work)
{
    return s_ble.queue != NULL && xQueueSend(s_ble.queue, work, 0) == pdTRUE;
}

static bool enqueue_control(const wallet_ble_work_t *work)
{
    return s_ble.control_queue != NULL &&
           xQueueSend(s_ble.control_queue, work, 0) == pdTRUE;
}

static bool address_equal(const ble_addr_t *left, const ble_addr_t *right)
{
    return left->type == right->type && memcmp(left->val, right->val, sizeof(left->val)) == 0;
}

/* Only the NimBLE host task changes SM policy or invalidates a stock link.
 * PairReq is explicitly rejected at level1 (IDF ble_sm.c); level4 retains
 * SC/MITM/128-bit policy inside the physical custom-pairing window.
 * ble_att_svr_check_perms returns before security checks for unprotected stock
 * attributes, so this host pairing policy never gates their read/write/CCC.
 * The existing PCW ENC/AUTHEN flags and owner/session checks remain mandatory. */
static void stock_host_control(struct ble_npl_event *event)
{
    (void)event;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    bool pairing = s_ble.window_open && !s_ble.owner_recovery_required && !s_ble.reset_pending;
    bool cancel = s_ble.stock_cancel_handle != WALLET_BLE_CONN_NONE &&
        s_ble.stock_cancel_handle == s_ble.conn_handle &&
        s_ble.stock_cancel_generation == s_ble.generation;
    uint16_t handle = s_ble.stock_cancel_handle;
    bool terminate = s_ble.terminate_handle != WALLET_BLE_CONN_NONE &&
        s_ble.terminate_handle == s_ble.conn_handle &&
        s_ble.terminate_generation == s_ble.generation;
    uint16_t terminate_handle = s_ble.terminate_handle;
    int terminate_reason = s_ble.terminate_reason;
    bool confirm = s_ble.confirm_handle != WALLET_BLE_CONN_NONE &&
        s_ble.confirm_handle == s_ble.conn_handle &&
        s_ble.confirm_generation == s_ble.generation && s_ble.connected &&
        s_ble.window_open && s_ble.pairing_pending && s_ble.passkey_released &&
        !s_ble.reset_pending && !s_ble.owner_recovery_required &&
        s_ble.confirm_passkey == s_ble.passkey;
    uint16_t confirm_handle = s_ble.confirm_handle;
    uint32_t confirm_passkey = s_ble.confirm_passkey;
    s_ble.confirm_handle = WALLET_BLE_CONN_NONE;
    s_ble.terminate_handle = WALLET_BLE_CONN_NONE;
    s_ble.stock_cancel_handle = WALLET_BLE_CONN_NONE;
    xSemaphoreGive(s_ble.mutex);
    if (s_ble.stock_mode) {
        ble_hs_cfg.sm_sec_lvl = pairing ? 4 : 1;
        if (cancel) stock_ble_on_disconnect(handle);
    }
    /* Connection callbacks and termination execute on the same host task;
     * no disconnect/reconnect can reuse the validated handle in between. */
    if (terminate) (void)ble_gap_terminate(terminate_handle, terminate_reason);
    else if (confirm) {
        struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = confirm_passkey};
        int result = ble_sm_inject_io(confirm_handle, &io);
        if (result != 0) {
            xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
            s_ble.last_error = result;
            xSemaphoreGive(s_ble.mutex);
        }
    }
}

static void schedule_stock_control(void)
{
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_stock_control_event);
}

static void request_termination(uint16_t handle, uint32_t generation, int reason)
{
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    bool current = handle != WALLET_BLE_CONN_NONE && handle == s_ble.conn_handle &&
                   generation == s_ble.generation;
    if (current) {
        s_ble.terminate_handle = handle;
        s_ble.terminate_generation = generation;
        s_ble.terminate_reason = reason;
    }
    xSemaphoreGive(s_ble.mutex);
    if (current) schedule_stock_control();
}

static uint32_t random_passkey(void)
{
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % 1000000u);
    uint32_t value;
    do {
        value = esp_random();
    } while (value >= limit);
    return value % 1000000u;
}

static void set_error(int error)
{
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    s_ble.last_error = error;
    xSemaphoreGive(s_ble.mutex);
}

static int load_owner(void)
{
    nvs_handle_t handle;
    wallet_owner_record_t record;
    size_t size = sizeof(record);
    esp_err_t result = nvs_open(WALLET_BLE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_get_blob(handle, WALLET_BLE_OWNER_KEY, &record, &size);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }
    if (result == ESP_ERR_NVS_INVALID_LENGTH || result == ESP_ERR_NVS_TYPE_MISMATCH ||
        (result == ESP_OK && !wallet_ble_owner_record_valid(size, record.type))) {
        /* Keep the malformed record intact for diagnosis. BLE remains up but
         * fails closed until physical owner reset removes this key and bonds. */
        s_ble.owner_recovery_required = true;
        s_ble.owner_present = false;
        return 0;
    }
    if (result != ESP_OK) {
        return result;
    }
    s_ble.owner.type = record.type;
    memcpy(s_ble.owner.val, record.value, sizeof(record.value));
    s_ble.owner_present = true;
    return 0;
}

static int save_owner(const ble_addr_t *owner)
{
    nvs_handle_t handle;
    wallet_owner_record_t record = {.type = owner->type};
    memcpy(record.value, owner->val, sizeof(record.value));
    esp_err_t result = nvs_open(WALLET_BLE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_blob(handle, WALLET_BLE_OWNER_KEY, &record, sizeof(record));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

static int erase_owner_record(void)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(WALLET_BLE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_erase_key(handle, WALLET_BLE_OWNER_KEY);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_OK;
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

typedef struct {
    ble_addr_t peers[2];
    size_t count;
} owner_peer_list_t;

static int collect_owner_peer(int object_type, union ble_store_value *value, void *context)
{
    owner_peer_list_t *list = context;
    (void)object_type;
    for (size_t index = 0; index < list->count; ++index) {
        if (address_equal(&list->peers[index], &value->sec.peer_addr)) return 0;
    }
    if (list->count >= sizeof(list->peers) / sizeof(list->peers[0])) {
        return BLE_HS_ENOMEM;
    }
    list->peers[list->count++] = value->sec.peer_addr;
    return 0;
}

static int fail_closed_store_status(struct ble_store_status_event *event, void *arg)
{
    (void)event;
    (void)arg;
    return BLE_HS_ESTORE_CAP;
}

static int start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields response = {0};
    struct ble_gap_adv_params parameters = {0};
    bool allowed;
    int result;

    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    allowed = wallet_ble_advertising_allowed(s_ble.stock_mode, s_ble.window_open,
        s_ble.host_synced, s_ble.connected, s_ble.reset_pending, s_ble.owner_recovery_required) &&
        s_ble.conn_handle == WALLET_BLE_CONN_NONE;
    xSemaphoreGive(s_ble.mutex);
    if (!allowed || ble_gap_adv_active()) {
        return 0;
    }
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)(s_ble.stock_mode ? &s_stock_service_uuid : &s_service_uuid);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    result = ble_gap_adv_set_fields(&fields);
    if (result != 0) return result;
    response.name = (uint8_t *)(s_ble.stock_mode ? s_ble.stock_serial : WALLET_BLE_DEVICE_NAME);
    response.name_len = s_ble.stock_mode ? 12 : strlen(WALLET_BLE_DEVICE_NAME);
    response.name_is_complete = 1;
    result = ble_gap_adv_rsp_set_fields(&response);
    if (result != 0) return result;
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    result = ble_gap_adv_start(s_ble.own_addr_type, NULL, BLE_HS_FOREVER,
                               &parameters, wallet_gap_event, NULL);
    if (result == 0) {
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        s_ble.advertising = true;
        xSemaphoreGive(s_ble.mutex);
    }
    return result;
}

static void stop_advertising(void)
{
    if (ble_gap_adv_active()) {
        (void)ble_gap_adv_stop();
    }
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    s_ble.advertising = false;
    xSemaphoreGive(s_ble.mutex);
}

static void finish_owner_reset(void)
{
    owner_peer_list_t peers = {0};
    int result = 0;
    int count = 0;
    uint8_t resolved[6];

    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    if (s_ble.owner_present) peers.peers[peers.count++] = s_ble.owner;
    xSemaphoreGive(s_ble.mutex);
    if (result == 0) result = ble_store_iterate(BLE_STORE_OBJ_TYPE_PEER_SEC,
                                                collect_owner_peer, &peers);
    if (result == 0) result = ble_store_iterate(BLE_STORE_OBJ_TYPE_OUR_SEC,
                                                collect_owner_peer, &peers);
    for (size_t index = 0; result == 0 && index < peers.count; ++index) {
        ble_addr_t *owner = &peers.peers[index];
        result = ble_gap_unpair(owner);
        if (result == BLE_HS_ENOENT) result = 0;
        if (result == 0) {
            uint8_t controller_type = owner->type > BLE_ADDR_RANDOM
                                          ? owner->type % 2u : owner->type;
            result = ble_gap_rd_local_resolv_addr(controller_type, owner, resolved);
            if (result == BLE_HS_HCI_ERR(BLE_ERR_UNK_CONN_ID)) result = 0;
            else if (result == 0) result = BLE_HS_ESTORE_FAIL;
        }
    }
    if (result == 0) result = ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count);
    if (result == 0 && count != 0) result = BLE_HS_ESTORE_FAIL;
    if (result == 0) result = ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &count);
    if (result == 0 && count != 0) result = BLE_HS_ESTORE_FAIL;
    if (result == 0) result = erase_owner_record();
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    if (result == 0) {
        memset(&s_ble.owner, 0, sizeof(s_ble.owner));
        s_ble.owner_present = false;
        s_ble.owner_recovery_required = false;
        s_ble.reset_pending = false;
    } else {
        s_ble.last_error = result;
        if (s_ble.stock_mode) {
            s_ble.owner_recovery_required = true;
            s_ble.reset_pending = false;
        }
    }
    xSemaphoreGive(s_ble.mutex);
    schedule_stock_control();
    if (result == 0 || s_ble.stock_mode) {
        int adv_result = start_advertising();
        if (adv_result != 0) set_error(adv_result);
    }
}

static wallet_error_t store_error(wallet_store_result_t result)
{
    if (result == WALLET_STORE_CRC_ERROR) return WALLET_ERROR_CRC;
    if (result == WALLET_STORE_FORMAT_ERROR || result == WALLET_STORE_INVALID_ARG ||
        result == WALLET_STORE_NO_SPACE) return WALLET_ERROR_PROTOCOL;
    return WALLET_ERROR_STORAGE;
}

typedef struct {
    uint32_t generation;
    uint16_t conn_handle;
} commit_guard_context_t;

static bool commit_session_valid(void *context)
{
    const commit_guard_context_t *guard = context;
    bool valid;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    valid = wallet_ble_session_matches(s_ble.generation, s_ble.conn_handle, s_ble.secure,
                                       guard->generation, guard->conn_handle);
    xSemaphoreGive(s_ble.mutex);
    return valid;
}

static void execute_transfer(const wallet_ble_work_t *work)
{
    wallet_transfer_action_t action;
    wallet_transfer_result_t input;
    wallet_store_result_t result = WALLET_STORE_OK;
    commit_guard_context_t guard = {
        .generation = work->generation,
        .conn_handle = work->conn_handle,
    };

    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    bool session_valid = wallet_ble_session_matches(
        s_ble.generation, s_ble.conn_handle, s_ble.secure,
        work->generation, work->conn_handle);
    if (!session_valid) {
        xSemaphoreGive(s_ble.mutex);
        return;
    }
    input = wallet_transfer_input(&s_ble.transfer, session_valid,
                                  work->data, work->length, &action);
    if (input == WALLET_TRANSFER_ACCEPTED && action.type == WALLET_ACTION_BEGIN) {
        s_ble.transfer_generation = work->generation;
    }
    xSemaphoreGive(s_ble.mutex);
    if (input == WALLET_TRANSFER_RETRY || action.type == WALLET_ACTION_NONE) {
        return;
    }
    if (action.type == WALLET_ACTION_ABORT) {
        wallet_store_abort();
        return;
    }
    if (action.type == WALLET_ACTION_BEGIN) {
        result = wallet_store_begin(action.expected, action.crc32);
    } else if (action.type == WALLET_ACTION_DATA) {
        result = wallet_store_write(action.offset, action.data, action.length);
    } else if (action.type == WALLET_ACTION_COMMIT) {
        result = wallet_store_commit_guarded(commit_session_valid, &guard);
    }
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    session_valid = wallet_ble_session_matches(
        s_ble.generation, s_ble.conn_handle, s_ble.secure,
        work->generation, work->conn_handle);
    bool action_succeeded = result == WALLET_STORE_OK &&
                            (session_valid || action.type == WALLET_ACTION_COMMIT);
    wallet_transfer_complete(&s_ble.transfer, &action,
                             action_succeeded,
                             result == WALLET_STORE_OK ? WALLET_ERROR_AUTH : store_error(result));
    xSemaphoreGive(s_ble.mutex);
    if (result != WALLET_STORE_OK ||
        (!session_valid && action.type != WALLET_ACTION_COMMIT)) {
        wallet_store_abort();
    }
}

static void close_window(bool explicit_close)
{
    uint16_t conn_handle = WALLET_BLE_CONN_NONE;
    uint32_t generation;
    bool secure = false;
    bool connected = false;
    bool pairing_pending = false;
    bool abort_transfer = false;
    wallet_transfer_action_t action;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    s_ble.window_open = false;
    s_ble.window_deadline_us = 0;
    connected = s_ble.connected;
    secure = s_ble.secure;
    pairing_pending = s_ble.pairing_pending;
    conn_handle = s_ble.conn_handle;
    if (explicit_close && connected) {
        abort_transfer = wallet_transfer_disconnect(&s_ble.transfer, &action);
        s_ble.secure = false;
        s_ble.connected = false;
        s_ble.pairing_pending = false;
        s_ble.passkey_released = false;
        ++s_ble.generation;
        if (s_ble.generation == 0) ++s_ble.generation;
        s_ble.stock_cancel_handle = conn_handle;
        s_ble.stock_cancel_generation = s_ble.generation;
    }
    generation = s_ble.generation;
    xSemaphoreGive(s_ble.mutex);
    if (abort_transfer) wallet_store_abort();
    schedule_stock_control();
    if (!s_ble.stock_mode || explicit_close) stop_advertising();
    if (wallet_ble_window_disconnect(s_ble.stock_mode, explicit_close, connected, secure, pairing_pending) &&
        conn_handle != WALLET_BLE_CONN_NONE) {
        request_termination(conn_handle, generation, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void process_tick(void)
{
    int64_t now = esp_timer_get_time();
    bool expire_window;
    bool transfer_timed_out;
    bool abort_pending;
    bool reset_without_link;
    wallet_transfer_status_t status;
    wallet_transfer_action_t action;

    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    expire_window = s_ble.window_open && now >= s_ble.window_deadline_us;
    status = wallet_transfer_get_status(&s_ble.transfer);
    transfer_timed_out = s_ble.secure && s_ble.last_activity_us != 0 &&
        (status.state == WALLET_TRANSFER_READY || status.state == WALLET_TRANSFER_RECEIVING ||
         status.state == WALLET_TRANSFER_BUSY) &&
        now - s_ble.last_activity_us >= (int64_t)WALLET_BLE_TRANSFER_IDLE_MS * 1000;
    if (transfer_timed_out) {
        (void)wallet_transfer_timeout(&s_ble.transfer, &action);
    }
    abort_pending = s_ble.abort_pending;
    if (abort_pending) {
        (void)wallet_transfer_disconnect(&s_ble.transfer, &action);
        s_ble.abort_pending = false;
    }
    reset_without_link = s_ble.reset_pending && s_ble.conn_handle == WALLET_BLE_CONN_NONE;
    xSemaphoreGive(s_ble.mutex);
    if (expire_window) close_window(false);
    if (transfer_timed_out || abort_pending) wallet_store_abort();
    if (reset_without_link) finish_owner_reset();
    if (s_ble.stock_mode) {
        int result = start_advertising();
        if (result != 0) set_error(result);
    }
}

static void process_work(const wallet_ble_work_t *work)
{
    switch (work->type) {
    case BLE_WORK_SYNC: {
        int result = ble_hs_util_ensure_addr(0);
        if (result == 0) result = ble_hs_id_infer_auto(0, &s_ble.own_addr_type);
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        s_ble.host_synced = result == 0;
        s_ble.last_error = result;
        xSemaphoreGive(s_ble.mutex);
        if (result == 0) result = start_advertising();
        if (result != 0) set_error(result);
        break;
    }
    case BLE_WORK_OPEN:
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        if (work->control_generation != s_ble.control_generation || s_ble.reset_pending) {
            xSemaphoreGive(s_ble.mutex);
            break;
        }
        s_ble.window_open = true;
        s_ble.window_deadline_us = esp_timer_get_time() + (int64_t)work->duration_ms * 1000;
        xSemaphoreGive(s_ble.mutex);
        schedule_stock_control();
        set_error(start_advertising());
        if (s_ble.stock_mode) {
            /* A bonded owner may already have encrypted its stock connection
             * before the physical window opened; reevaluate, do not initiate
             * security for an ordinary unpaired stock client. */
            struct ble_gap_conn_desc desc;
            wallet_ble_work_t encrypted = {.type = BLE_WORK_ENCRYPTION};
            xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
            encrypted.conn_handle = s_ble.conn_handle;
            encrypted.generation = s_ble.generation;
            xSemaphoreGive(s_ble.mutex);
            if (encrypted.conn_handle != WALLET_BLE_CONN_NONE &&
                ble_gap_conn_find(encrypted.conn_handle, &desc) == 0 && desc.sec_state.encrypted)
                (void)enqueue_control(&encrypted);
        }
        break;
    case BLE_WORK_CLOSE: {
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        bool current = work->control_generation == s_ble.control_generation;
        bool terminate = current && work->generation == s_ble.generation &&
            work->conn_handle == s_ble.conn_handle && work->conn_handle != WALLET_BLE_CONN_NONE;
        xSemaphoreGive(s_ble.mutex);
        if (!current) break;
        close_window(false);
        if (s_ble.stock_mode) stop_advertising();
        if (terminate) {
            request_termination(work->conn_handle, work->generation, BLE_ERR_REM_USER_CONN_TERM);
        }
        break;
    }
    case BLE_WORK_CONNECT: {
        bool allowed;
        bool owner_present;
        bool owner_match;
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        if (work->generation != s_ble.generation ||
            work->conn_handle != s_ble.conn_handle) {
            xSemaphoreGive(s_ble.mutex);
            break;
        }
        owner_present = s_ble.owner_present;
        owner_match = owner_present && address_equal(&s_ble.owner, &work->peer);
        allowed = wallet_ble_connection_allowed(s_ble.stock_mode, s_ble.window_open,
            owner_present, owner_match, s_ble.owner_recovery_required, s_ble.reset_pending);
        if (allowed) {
            s_ble.connected = true;
            s_ble.conn_handle = work->conn_handle;
            s_ble.peer = work->peer;
            s_ble.advertising = false;
            s_ble.last_activity_us = esp_timer_get_time();
        }
        xSemaphoreGive(s_ble.mutex);
        if (!allowed) {
            request_termination(work->conn_handle, work->generation, BLE_ERR_AUTH_FAIL);
        } else if (!s_ble.stock_mode) {
            int result = ble_gap_security_initiate(work->conn_handle);
            if (result != 0) set_error(result);
        }
        break;
    }
    case BLE_WORK_PASSKEY: {
        bool allowed;
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        allowed = !s_ble.owner_recovery_required && s_ble.connected &&
                  s_ble.generation == work->generation &&
                  s_ble.conn_handle == work->conn_handle &&
                  wallet_ble_peer_allowed(s_ble.window_open, s_ble.owner_present,
                    !s_ble.owner_present || address_equal(&s_ble.owner, &s_ble.peer));
        if (allowed) {
            s_ble.passkey = random_passkey();
            s_ble.pairing_pending = true;
            s_ble.passkey_released = false;
        }
        xSemaphoreGive(s_ble.mutex);
        if (!allowed) request_termination(work->conn_handle, work->generation, BLE_ERR_AUTH_FAIL);
        break;
    }
    case BLE_WORK_CONFIRM: {
        uint16_t conn;
        uint32_t passkey;
        bool release;
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        conn = s_ble.conn_handle;
        passkey = s_ble.passkey;
        release = work->accept && s_ble.generation == work->generation &&
            s_ble.conn_handle == work->conn_handle && s_ble.passkey == work->duration_ms &&
            wallet_ble_passkey_can_release(
            s_ble.window_open, s_ble.pairing_pending,
            s_ble.connected && (!s_ble.owner_present || address_equal(&s_ble.owner, &s_ble.peer)));
        if (release) {
            s_ble.passkey_released = true;
            s_ble.confirm_handle = conn;
            s_ble.confirm_generation = work->generation;
            s_ble.confirm_passkey = passkey;
        }
        xSemaphoreGive(s_ble.mutex);
        if (release) {
            schedule_stock_control();
        } else if (conn != WALLET_BLE_CONN_NONE) {
            request_termination(work->conn_handle, work->generation, BLE_ERR_AUTH_FAIL);
        }
        break;
    }
    case BLE_WORK_ENCRYPTION: {
        struct ble_gap_conn_desc description = {0};
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        bool current = !s_ble.owner_recovery_required &&
                       s_ble.generation == work->generation && s_ble.connected &&
                       (s_ble.window_open || s_ble.secure) && s_ble.conn_handle == work->conn_handle;
        xSemaphoreGive(s_ble.mutex);
        if (!current) break;
        int result = work->status == 0 ? ble_gap_conn_find(work->conn_handle, &description)
                                       : work->status;
        bool raw_secure = result == 0 && description.sec_state.encrypted &&
                          description.sec_state.authenticated && description.sec_state.bonded &&
                          description.sec_state.key_size == 16u;
        bool owner_present;
        bool owner_match;
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        owner_present = s_ble.owner_present;
        owner_match = owner_present && address_equal(&s_ble.owner, &description.peer_id_addr);
        xSemaphoreGive(s_ble.mutex);
        if (raw_secure && !owner_present) {
            result = save_owner(&description.peer_id_addr);
            if (result == 0) {
                xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
                current = !s_ble.owner_recovery_required &&
                          s_ble.generation == work->generation && s_ble.connected &&
                          s_ble.window_open && s_ble.conn_handle == work->conn_handle;
                if (current) {
                    s_ble.owner = description.peer_id_addr;
                    s_ble.owner_present = true;
                }
                xSemaphoreGive(s_ble.mutex);
                owner_match = current;
                if (!current) {
                    (void)erase_owner_record();
                    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
                    s_ble.owner_recovery_required = true;
                    xSemaphoreGive(s_ble.mutex);
                }
            }
            if (result != 0) {
                xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
                s_ble.owner_recovery_required = true;
                xSemaphoreGive(s_ble.mutex);
            }
        }
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        current = !s_ble.owner_recovery_required &&
                  s_ble.generation == work->generation && s_ble.connected &&
                  (s_ble.window_open || s_ble.secure) && s_ble.conn_handle == work->conn_handle;
        /* A disconnect can occur while NVS persistence runs. Never mutate
         * the replacement session using the old encryption completion. */
        if (work->generation != s_ble.generation || s_ble.conn_handle != work->conn_handle) {
            xSemaphoreGive(s_ble.mutex);
            break;
        }
        s_ble.secure = current && result == 0 && wallet_ble_link_authorized(
            description.sec_state.encrypted, description.sec_state.authenticated,
            description.sec_state.bonded, description.sec_state.key_size, owner_match);
        s_ble.pairing_pending = false;
        s_ble.passkey_released = false;
        s_ble.last_error = result;
        xSemaphoreGive(s_ble.mutex);
        bool secure;
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        secure = s_ble.secure;
        xSemaphoreGive(s_ble.mutex);
        if (!secure) request_termination(work->conn_handle, work->generation, BLE_ERR_AUTH_FAIL);
        break;
    }
    case BLE_WORK_RX:
        execute_transfer(work);
        break;
    case BLE_WORK_DISCONNECT: {
        wallet_transfer_action_t action;
        bool abort;
        bool reset;
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        abort = s_ble.transfer_generation == work->generation &&
                wallet_transfer_disconnect(&s_ble.transfer, &action);
        reset = s_ble.reset_pending;
        xSemaphoreGive(s_ble.mutex);
        if (abort) wallet_store_abort();
        if (reset) finish_owner_reset();
        else {
            int result = start_advertising();
            if (result != 0) set_error(result);
        }
        break;
    }
    case BLE_WORK_ADV_COMPLETE:
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        s_ble.advertising = false;
        xSemaphoreGive(s_ble.mutex);
        set_error(start_advertising());
        break;
    case BLE_WORK_TICK:
        process_tick();
        break;
    case BLE_WORK_RESET_OWNER: {
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        bool pending = s_ble.reset_pending && work->control_generation == s_ble.control_generation;
        xSemaphoreGive(s_ble.mutex);
        if (!pending) break;
        close_window(false);
        if (s_ble.stock_mode) stop_advertising();
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        s_ble.reset_pending = true;
        xSemaphoreGive(s_ble.mutex);
        if (work->conn_handle != WALLET_BLE_CONN_NONE) {
            request_termination(work->conn_handle, work->generation, BLE_ERR_REM_USER_CONN_TERM);
        } else {
            finish_owner_reset();
        }
        break;
    }
    case BLE_WORK_HOST_RESET:
        {
            wallet_transfer_action_t action;
            bool abort;
            xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
            abort = wallet_transfer_disconnect(&s_ble.transfer, &action);
            xSemaphoreGive(s_ble.mutex);
            if (abort) wallet_store_abort();
        }
        set_error(work->status);
        break;
    default:
        break;
    }
}

static void wallet_worker_task(void *context)
{
    wallet_ble_work_t work;
    (void)context;
    for (;;) {
        if (xQueueReceive(s_ble.control_queue, &work, 0) == pdTRUE ||
            xQueueReceive(s_ble.queue, &work, pdMS_TO_TICKS(20)) == pdTRUE) {
            process_work(&work);
        }
    }
}

static void wallet_host_task(void *context)
{
    (void)context;
    nimble_port_run();
    vTaskDelete(NULL);
}

static void on_sync(void)
{
    const wallet_ble_work_t work = {.type = BLE_WORK_SYNC};
    (void)enqueue_control(&work);
}

static void on_reset(int reason)
{
    wallet_ble_work_t work = {.type = BLE_WORK_HOST_RESET, .status = reason};
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    work.generation = s_ble.generation;
    work.conn_handle = s_ble.conn_handle;
    s_ble.host_synced = false;
    s_ble.advertising = false;
    s_ble.secure = false;
    s_ble.connected = false;
    s_ble.pairing_pending = false;
    s_ble.passkey_released = false;
    s_ble.conn_handle = WALLET_BLE_CONN_NONE;
    ++s_ble.generation;
    if (s_ble.generation == 0) ++s_ble.generation;
    xSemaphoreGive(s_ble.mutex);
    if (s_ble.stock_mode) {
        stock_ble_on_disconnect(work.conn_handle);
        stock_host_control(NULL);
    }
    (void)enqueue_control(&work);
}

static void on_tick(void *context)
{
    const wallet_ble_work_t work = {.type = BLE_WORK_TICK};
    (void)context;
    (void)enqueue(&work);
}

static int wallet_gap_event(struct ble_gap_event *event, void *context)
{
    wallet_ble_work_t work = {0};
    struct ble_gap_conn_desc description;
    (void)context;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            work.type = BLE_WORK_ADV_COMPLETE;
            work.status = event->connect.status;
            (void)enqueue(&work);
            return 0;
        }
        if (ble_gap_conn_find(event->connect.conn_handle, &description) != 0) {
            (void)ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return BLE_HS_EUNKNOWN;
        }
        work.type = BLE_WORK_CONNECT;
        work.conn_handle = event->connect.conn_handle;
        work.peer = description.peer_id_addr;
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        bool accept = s_ble.conn_handle == WALLET_BLE_CONN_NONE &&
            wallet_ble_connection_allowed(s_ble.stock_mode, s_ble.window_open,
                s_ble.owner_present, s_ble.owner_present && address_equal(&s_ble.owner, &work.peer),
                s_ble.owner_recovery_required, s_ble.reset_pending);
        if (!accept) {
            xSemaphoreGive(s_ble.mutex);
            (void)ble_gap_terminate(work.conn_handle, BLE_ERR_AUTH_FAIL);
            return 0;
        }
        ++s_ble.generation;
        if (s_ble.generation == 0) ++s_ble.generation;
        work.generation = s_ble.generation;
        s_ble.conn_handle = work.conn_handle;
        s_ble.secure = false;
        s_ble.connected = true;
        s_ble.peer = work.peer;
        s_ble.advertising = false;
        s_ble.pairing_pending = false;
        s_ble.passkey_released = false;
        xSemaphoreGive(s_ble.mutex);
        if (s_ble.stock_mode) stock_ble_on_connect(work.conn_handle);
        if (!enqueue_control(&work)) {
            if (s_ble.stock_mode) stock_ble_on_disconnect(work.conn_handle);
            (void)ble_gap_terminate(work.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return BLE_HS_ENOMEM;
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        work.type = BLE_WORK_DISCONNECT;
        work.conn_handle = event->disconnect.conn.conn_handle;
        work.status = event->disconnect.reason;
        if (s_ble.stock_mode) stock_ble_on_disconnect(work.conn_handle);
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        if (s_ble.conn_handle == work.conn_handle) {
            work.generation = s_ble.generation;
            s_ble.secure = false;
            s_ble.connected = false;
            s_ble.pairing_pending = false;
            s_ble.passkey_released = false;
            s_ble.abort_pending = true;
            s_ble.conn_handle = WALLET_BLE_CONN_NONE;
            ++s_ble.generation;
            if (s_ble.generation == 0) ++s_ble.generation;
        }
        xSemaphoreGive(s_ble.mutex);
        (void)enqueue_control(&work);
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        work.type = BLE_WORK_ENCRYPTION;
        work.conn_handle = event->enc_change.conn_handle;
        work.status = event->enc_change.status;
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        work.generation = s_ble.conn_handle == work.conn_handle ? s_ble.generation : 0;
        xSemaphoreGive(s_ble.mutex);
        (void)enqueue_control(&work);
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action != BLE_SM_IOACT_DISP) return BLE_HS_ENOTSUP;
        work.type = BLE_WORK_PASSKEY;
        work.conn_handle = event->passkey.conn_handle;
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        work.generation = s_ble.conn_handle == work.conn_handle ? s_ble.generation : 0;
        bool pairing_allowed = s_ble.connected && work.generation != 0 &&
            !s_ble.owner_recovery_required && !s_ble.reset_pending &&
            wallet_ble_peer_allowed(s_ble.window_open, s_ble.owner_present,
                s_ble.owner_present && address_equal(&s_ble.owner, &s_ble.peer));
        if (pairing_allowed) s_ble.pairing_pending = true;
        xSemaphoreGive(s_ble.mutex);
        if (!pairing_allowed || !enqueue_control(&work)) {
            (void)ble_gap_terminate(work.conn_handle, BLE_ERR_AUTH_FAIL);
            return BLE_HS_EREJECT;
        }
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (s_ble.stock_mode) stock_ble_on_subscribe(event->subscribe.conn_handle,
            event->subscribe.attr_handle, event->subscribe.cur_notify != 0);
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        work.type = BLE_WORK_ADV_COMPLETE;
        work.status = event->adv_complete.reason;
        (void)enqueue_control(&work);
        return 0;
    default:
        return 0;
    }
}

static int wallet_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *context, void *arg)
{
    wallet_ble_work_t work = {0};
    bool authorized;
    (void)attr_handle;
    (void)arg;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    authorized = s_ble.secure && s_ble.connected && s_ble.conn_handle == conn_handle;
    work.generation = s_ble.generation;
    if (authorized) s_ble.last_activity_us = esp_timer_get_time();
    xSemaphoreGive(s_ble.mutex);
    if (!authorized) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t status[WALLET_STATUS_SIZE];
        xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
        wallet_transfer_encode_status(&s_ble.transfer, status);
        xSemaphoreGive(s_ble.mutex);
        return os_mbuf_append(context->om, status, sizeof(status)) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    work.length = OS_MBUF_PKTLEN(context->om);
    if (work.length == 0 || work.length > sizeof(work.data) ||
        os_mbuf_copydata(context->om, 0, work.length, work.data) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    work.type = BLE_WORK_RX;
    work.conn_handle = conn_handle;
    return enqueue(&work) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static wallet_ble_result_t init_common(const uint8_t *identity,
    const uint8_t *status, const char *serial)
{
    int result;
    wallet_ble_result_t failure = WALLET_BLE_PLATFORM_ERROR;
    bool nimble_ready = false;
    bool stock_ready = false;
    const esp_timer_create_args_t timer_args = {
        .callback = on_tick,
        .name = "wallet_ble_tick",
    };
    if (s_ble.initialized || s_init_blocked) return WALLET_BLE_INVALID_STATE;
    memset(&s_ble, 0, sizeof(s_ble));
    s_ble.conn_handle = WALLET_BLE_CONN_NONE;
    s_ble.stock_cancel_handle = WALLET_BLE_CONN_NONE;
    s_ble.terminate_handle = WALLET_BLE_CONN_NONE;
    s_ble.confirm_handle = WALLET_BLE_CONN_NONE;
    s_ble.stock_mode = identity != NULL;
    if (s_ble.stock_mode) memcpy(s_ble.stock_serial, serial, sizeof(s_ble.stock_serial));
    wallet_transfer_init(&s_ble.transfer);
    s_ble.mutex = xSemaphoreCreateMutex();
    s_ble.queue = xQueueCreate(WALLET_BLE_QUEUE_LENGTH, sizeof(wallet_ble_work_t));
    s_ble.control_queue = xQueueCreate(WALLET_BLE_QUEUE_LENGTH, sizeof(wallet_ble_work_t));
    if (s_ble.mutex == NULL || s_ble.queue == NULL || s_ble.control_queue == NULL) {
        failure = WALLET_BLE_NO_MEMORY;
        goto failed;
    }
    result = load_owner();
    if (result != 0) {
        if (!s_ble.stock_mode) goto failed;
        s_ble.owner_recovery_required = true;
        s_ble.last_error = result;
    }
    result = nimble_port_init();
    if (result != ESP_OK) {
        /* ESP-IDF does not guarantee a failed host init left a retryable
         * lifecycle. Do not wipe that uncertainty and attempt a second init. */
        s_init_blocked = true;
        goto failed;
    }
    nimble_ready = true;
    ble_npl_event_init(&s_stock_control_event, stock_host_control, NULL);
    if (s_ble.stock_mode) {
        result = stock_ble_init(identity, status);
        if (result != 0) goto failed;
        stock_ready = true;
    }
    ble_store_config_init();
    {
        int peer_count = 0;
        int our_count = 0;
        int peer_result = ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &peer_count);
        int our_result = ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &our_count);
        if (peer_result != 0 || our_result != 0 ||
            (s_ble.owner_present && (peer_count != 1 || our_count != 1)) ||
            (!s_ble.owner_present && (peer_count != 0 || our_count != 0))) {
            /* Keep startup recoverable but fail closed. The device-local owner
             * reset enumerates and verifies removal of orphaned bonds. */
            s_ble.last_error = BLE_HS_ESTORE_FAIL;
            s_ble.owner_recovery_required = true;
        }
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = fail_closed_store_status;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 1;
    ble_hs_cfg.sm_sec_lvl = s_ble.stock_mode ? 1 : 4;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    result = ble_gatts_count_cfg(s_services);
    if (result == 0) result = ble_gatts_add_svcs(s_services);
    if (result == 0 && s_ble.stock_mode) result = stock_ble_register();
    if (result == 0) result = ble_svc_gap_device_name_set(s_ble.stock_mode ? s_ble.stock_serial : WALLET_BLE_DEVICE_NAME);
    if (result != 0) goto failed;
    if (esp_timer_create(&timer_args, &s_ble.tick_timer) != ESP_OK ||
        esp_timer_start_periodic(s_ble.tick_timer, 1000000) != ESP_OK) {
        goto failed;
    }
    if (xTaskCreate(wallet_worker_task, "wallet_ble", WALLET_BLE_TASK_STACK, NULL,
                    WALLET_BLE_TASK_PRIORITY, &s_ble.worker_task) != pdPASS) {
        failure = WALLET_BLE_NO_MEMORY;
        goto failed;
    }
    /* Host creation is deliberately the final fallible step. Once NimBLE can
     * execute callbacks, init returns success and no rollback frees callback
     * resources underneath the host task. */
    if (xTaskCreate(wallet_host_task, "nimble_host", NIMBLE_HS_STACK_SIZE, NULL,
                    configMAX_PRIORITIES - 4, &s_ble.host_task) != pdPASS) {
        failure = WALLET_BLE_NO_MEMORY;
        goto failed;
    }
    s_ble.initialized = true;
    return WALLET_BLE_OK;

failed:
    if (s_ble.tick_timer != NULL) {
        (void)esp_timer_stop(s_ble.tick_timer);
        (void)esp_timer_delete(s_ble.tick_timer);
        s_ble.tick_timer = NULL;
    }
    if (s_ble.worker_task != NULL) {
        vTaskDelete(s_ble.worker_task);
        s_ble.worker_task = NULL;
    }
    if (nimble_ready) ble_npl_eventq_remove(nimble_port_get_dflt_eventq(), &s_stock_control_event);
    if (stock_ready && !stock_ble_deinit()) s_init_blocked = true;
    if (nimble_ready && nimble_port_deinit() != ESP_OK) s_init_blocked = true;
    if (s_ble.control_queue != NULL) vQueueDelete(s_ble.control_queue);
    if (s_ble.queue != NULL) vQueueDelete(s_ble.queue);
    if (s_ble.mutex != NULL) vSemaphoreDelete(s_ble.mutex);
    memset(&s_ble, 0, sizeof(s_ble));
    s_ble.conn_handle = WALLET_BLE_CONN_NONE;
    return failure;
}

wallet_ble_result_t wallet_ble_init(void)
{
    return init_common(NULL, NULL, NULL);
}

wallet_ble_result_t wallet_ble_init_stock(const uint8_t identity[72],
    const uint8_t status[9], const char serial[13])
{
    if (!status || status[0] != 1 || !wallet_ble_stock_serial_valid(identity, serial))
        return WALLET_BLE_INVALID_ARG;
    return init_common(identity, status, serial);
}

wallet_ble_result_t wallet_ble_open_pairing_window(uint32_t duration_ms)
{
    wallet_ble_work_t work = {.type = BLE_WORK_OPEN, .duration_ms = duration_ms};
    if (!s_ble.initialized || duration_ms == 0) return WALLET_BLE_INVALID_ARG;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    if (s_ble.reset_pending) {
        xSemaphoreGive(s_ble.mutex);
        return WALLET_BLE_INVALID_STATE;
    }
    work.control_generation = s_ble.control_generation + 1;
    bool queued = enqueue(&work);
    if (queued) s_ble.control_generation = work.control_generation;
    xSemaphoreGive(s_ble.mutex);
    return queued ? WALLET_BLE_OK : WALLET_BLE_NO_MEMORY;
}

void wallet_ble_close_pairing_window(void)
{
    wallet_ble_work_t work = {.type = BLE_WORK_CLOSE, .conn_handle = WALLET_BLE_CONN_NONE};
    if (!s_ble.initialized) return;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    work.conn_handle = s_ble.conn_handle;
    work.control_generation = ++s_ble.control_generation;
    s_ble.window_open = false;
    s_ble.secure = false;
    s_ble.connected = false;
    s_ble.pairing_pending = false;
    s_ble.passkey_released = false;
    s_ble.abort_pending = true;
    ++s_ble.generation;
    if (s_ble.generation == 0) ++s_ble.generation;
    work.generation = s_ble.generation;
    s_ble.stock_cancel_handle = work.conn_handle;
    s_ble.stock_cancel_generation = s_ble.generation;
    xSemaphoreGive(s_ble.mutex);
    schedule_stock_control();
    request_termination(work.conn_handle, work.generation, BLE_ERR_REM_USER_CONN_TERM);
    (void)enqueue_control(&work);
}

wallet_ble_result_t wallet_ble_confirm_passkey(bool accept)
{
    wallet_ble_work_t work = {.type = BLE_WORK_CONFIRM, .accept = accept};
    if (!s_ble.initialized) return WALLET_BLE_INVALID_STATE;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    work.generation = s_ble.generation;
    work.conn_handle = s_ble.conn_handle;
    work.duration_ms = s_ble.passkey;
    xSemaphoreGive(s_ble.mutex);
    return enqueue_control(&work) ? WALLET_BLE_OK : WALLET_BLE_NO_MEMORY;
}

wallet_ble_result_t wallet_ble_reset_owner(void)
{
    wallet_ble_work_t work = {.type = BLE_WORK_RESET_OWNER,
                              .conn_handle = WALLET_BLE_CONN_NONE};
    if (!s_ble.initialized) return WALLET_BLE_INVALID_STATE;
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    work.conn_handle = s_ble.conn_handle;
    s_ble.window_open = false;
    work.control_generation = ++s_ble.control_generation;
    s_ble.reset_pending = true;
    s_ble.secure = false;
    s_ble.connected = false;
    s_ble.pairing_pending = false;
    s_ble.passkey_released = false;
    s_ble.abort_pending = true;
    ++s_ble.generation;
    if (s_ble.generation == 0) ++s_ble.generation;
    work.generation = s_ble.generation;
    s_ble.stock_cancel_handle = work.conn_handle;
    s_ble.stock_cancel_generation = s_ble.generation;
    xSemaphoreGive(s_ble.mutex);
    schedule_stock_control();
    request_termination(work.conn_handle, work.generation, BLE_ERR_REM_USER_CONN_TERM);
    if (enqueue_control(&work)) return WALLET_BLE_OK;
    if (work.conn_handle == WALLET_BLE_CONN_NONE) finish_owner_reset();
    return WALLET_BLE_NO_MEMORY;
}

void wallet_ble_get_info(wallet_ble_info_t *out)
{
    int64_t now;
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (s_ble.mutex == NULL) return;
    now = esp_timer_get_time();
    xSemaphoreTake(s_ble.mutex, portMAX_DELAY);
    out->initialized = s_ble.initialized;
    out->window_open = s_ble.window_open;
    out->advertising = s_ble.advertising;
    out->connected = s_ble.connected;
    out->secure = s_ble.secure;
    out->owner_present = s_ble.owner_present;
    out->owner_recovery_required = s_ble.owner_recovery_required;
    out->pairing_pending = s_ble.pairing_pending;
    out->passkey_released = s_ble.passkey_released;
    out->passkey = s_ble.passkey;
    out->window_remaining_ms = s_ble.window_open && s_ble.window_deadline_us > now
        ? (uint32_t)((s_ble.window_deadline_us - now) / 1000) : 0;
    out->transfer = wallet_transfer_get_status(&s_ble.transfer);
    out->last_error = s_ble.last_error;
    xSemaphoreGive(s_ble.mutex);
}

#endif
