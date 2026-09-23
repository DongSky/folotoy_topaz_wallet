#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
#define configMAX_PRIORITIES 25
#define NIMBLE_HS_STACK_SIZE 4096
typedef struct fake_queue *QueueHandle_t;
typedef int *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef void *esp_timer_handle_t;
typedef struct { void (*callback)(void *);const char *name; } esp_timer_create_args_t;
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueSend(QueueHandle_t,const void *,unsigned);
int xQueueReceive(QueueHandle_t,void *,unsigned);
void vQueueDelete(QueueHandle_t);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
int64_t esp_timer_get_time(void);
int esp_timer_create(const esp_timer_create_args_t *,esp_timer_handle_t *);
int esp_timer_start_periodic(esp_timer_handle_t,uint64_t);
int esp_timer_stop(esp_timer_handle_t);
int esp_timer_delete(esp_timer_handle_t);
uint32_t esp_random(void);

typedef int esp_err_t;
typedef uint32_t nvs_handle_t;
typedef enum {NVS_READONLY,NVS_READWRITE} nvs_open_mode_t;
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_TYPE_MISMATCH 0x1103
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
int nvs_open(const char *,nvs_open_mode_t,nvs_handle_t *);
int nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
int nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
int nvs_erase_key(nvs_handle_t,const char *);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);

typedef struct { uint8_t type; uint8_t val[6]; } ble_addr_t;
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u;uint8_t value[16]; } ble_uuid128_t;
#define BLE_UUID128_INIT(...) {{128},{__VA_ARGS__}}
#define BLE_ADDR_RANDOM 1
#define BLE_STORE_OBJ_TYPE_PEER_SEC 1
#define BLE_STORE_OBJ_TYPE_OUR_SEC 2
union ble_store_value { struct {ble_addr_t peer_addr;} sec; };
struct ble_store_status_event {int unused;};
struct ble_gap_sec_state {bool encrypted,authenticated,bonded;uint8_t key_size;};
struct ble_gap_conn_desc {uint16_t conn_handle;ble_addr_t peer_id_addr;struct ble_gap_sec_state sec_state;};
struct ble_gap_event {
    int type;
    struct {int status;uint16_t conn_handle;} connect;
    struct {int reason;struct ble_gap_conn_desc conn;} disconnect;
    struct {int status;uint16_t conn_handle;} enc_change;
    struct {uint16_t conn_handle;struct {int action;} params;} passkey;
    struct {uint16_t conn_handle,attr_handle;int cur_notify;} subscribe;
    struct {int reason;} adv_complete;
};
#define BLE_GAP_EVENT_CONNECT 1
#define BLE_GAP_EVENT_DISCONNECT 2
#define BLE_GAP_EVENT_ENC_CHANGE 3
#define BLE_GAP_EVENT_PASSKEY_ACTION 4
#define BLE_GAP_EVENT_REPEAT_PAIRING 5
#define BLE_GAP_EVENT_ADV_COMPLETE 6
#define BLE_GAP_EVENT_SUBSCRIBE 7
#define BLE_GAP_REPEAT_PAIRING_IGNORE 2
#define BLE_HS_ENOMEM 6
#define BLE_HS_EUNKNOWN 7
#define BLE_HS_EREJECT 8
#define BLE_HS_ENOTSUP 9
#define BLE_HS_ENOENT 10
#define BLE_HS_EALREADY 11
#define BLE_HS_ESTORE_CAP 12
#define BLE_HS_ESTORE_FAIL 13
#define BLE_ERR_UNK_CONN_ID 14
#define BLE_ERR_REM_USER_CONN_TERM 15
#define BLE_ERR_AUTH_FAIL 16
#define BLE_HS_HCI_ERR(x) ((x)+256)
#define BLE_HS_ADV_F_DISC_GEN 2
#define BLE_HS_ADV_F_BREDR_UNSUP 4
#define BLE_HS_FOREVER INT32_MAX
#define BLE_GAP_CONN_MODE_UND 1
#define BLE_GAP_DISC_MODE_GEN 2
struct ble_hs_adv_fields {
    uint8_t flags;ble_uuid128_t *uuids128;uint8_t num_uuids128,uuids128_is_complete;
    uint8_t *name;size_t name_len;uint8_t name_is_complete;
};
struct ble_gap_adv_params {int conn_mode,disc_mode;};
struct ble_sm_io {int action;uint32_t passkey;};
#define BLE_SM_IOACT_DISP 1
#define BLE_SM_IO_CAP_DISP_ONLY 0
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2
extern struct fake_hs_cfg {
    void (*reset_cb)(int);void (*sync_cb)(void);
    int (*store_status_cb)(struct ble_store_status_event *,void *);
    int sm_io_cap,sm_bonding,sm_mitm,sm_sc,sm_sc_only,sm_sec_lvl;
    int sm_our_key_dist,sm_their_key_dist;
} ble_hs_cfg;
int ble_gap_adv_active(void);
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *);
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *);
int ble_gap_adv_start(uint8_t,const void *,int32_t,const struct ble_gap_adv_params *,
    int (*)(struct ble_gap_event *,void *),void *);
int ble_gap_adv_stop(void);
int ble_gap_unpair(const ble_addr_t *);
int ble_gap_rd_local_resolv_addr(uint8_t,const ble_addr_t *,uint8_t *);
int ble_gap_terminate(uint16_t,int);
int ble_gap_conn_find(uint16_t,struct ble_gap_conn_desc *);
int ble_gap_security_initiate(uint16_t);
int ble_sm_inject_io(uint16_t,const struct ble_sm_io *);
int ble_store_iterate(int,int (*)(int,union ble_store_value *,void *),void *);
int ble_store_util_count(int,int *);
int ble_hs_util_ensure_addr(int);
int ble_hs_id_infer_auto(int,uint8_t *);

struct os_mbuf {size_t length;uint8_t data[600];};
#define OS_MBUF_PKTLEN(m) ((m)->length)
int os_mbuf_append(struct os_mbuf *,const void *,size_t);
int os_mbuf_copydata(const struct os_mbuf *,int,int,void *);
#define BLE_GATT_CHR_F_READ 2
#define BLE_GATT_CHR_F_WRITE 8
#define BLE_GATT_CHR_F_READ_ENC 0x200
#define BLE_GATT_CHR_F_READ_AUTHEN 0x400
#define BLE_GATT_CHR_F_WRITE_ENC 0x1000
#define BLE_GATT_CHR_F_WRITE_AUTHEN 0x2000
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GATT_ACCESS_OP_READ_CHR 0
#define BLE_GATT_ACCESS_OP_WRITE_CHR 1
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
#define BLE_ATT_ERR_INSUFFICIENT_AUTHEN 5
struct ble_gatt_access_ctxt {uint8_t op;struct os_mbuf *om;};
struct ble_gatt_chr_def {
    const ble_uuid_t *uuid;
    int (*access_cb)(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);
    uint16_t flags;
};
struct ble_gatt_svc_def {uint8_t type;const ble_uuid_t *uuid;struct ble_gatt_chr_def *characteristics;};
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *);
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *);
void ble_svc_gap_init(void);
void ble_svc_gatt_init(void);
int ble_svc_gap_device_name_set(const char *);
int nimble_port_init(void);
int nimble_port_deinit(void);
void nimble_port_run(void);
struct ble_npl_event {void (*fn)(struct ble_npl_event *);};
struct ble_npl_eventq {struct ble_npl_event *event;};
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void);
void ble_npl_event_init(struct ble_npl_event *,void (*)(struct ble_npl_event *),void *);
void ble_npl_eventq_put(struct ble_npl_eventq *,struct ble_npl_event *);
void ble_npl_eventq_remove(struct ble_npl_eventq *,struct ble_npl_event *);
