#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void fake_enter(void);
void fake_exit(void);
#define portENTER_CRITICAL(lock) ((void)(lock),fake_enter())
#define portEXIT_CRITICAL(lock) ((void)(lock),fake_exit())
int64_t esp_timer_get_time(void);
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint8_t value[16]; } ble_uuid128_t;
#define BLE_UUID128_INIT(...) {{128},{__VA_ARGS__}}
struct os_mbuf { size_t length; uint8_t data[600]; };
#define OS_MBUF_PKTLEN(om) ((om)->length)
int os_mbuf_append(struct os_mbuf *,const void *,size_t);
int os_mbuf_copydata(const struct os_mbuf *,int,int,void *);
void os_mbuf_free_chain(struct os_mbuf *);
struct os_mbuf *ble_hs_mbuf_from_flat(const void *,uint16_t);
#define BLE_GATT_CHR_F_READ 2
#define BLE_GATT_CHR_F_WRITE_NO_RSP 4
#define BLE_GATT_CHR_F_WRITE 8
#define BLE_GATT_CHR_F_NOTIFY 16
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GATT_ACCESS_OP_READ_CHR 0
#define BLE_GATT_ACCESS_OP_WRITE_CHR 1
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
#define BLE_HS_ENOMEM 6
struct ble_gatt_access_ctxt { uint8_t op; struct os_mbuf *om; };
struct ble_gatt_chr_def {
    const ble_uuid_t *uuid;
    int (*access_cb)(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);
    void *arg;
    uint16_t *val_handle;
    uint16_t flags;
};
struct ble_gatt_svc_def {
    uint8_t type;
    const ble_uuid_t *uuid;
    const struct ble_gatt_chr_def *characteristics;
};
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *);
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *);
int ble_gatts_notify_custom(uint16_t,uint16_t,struct os_mbuf *);
uint16_t ble_att_mtu(uint16_t);
struct ble_npl_event { void (*callback)(struct ble_npl_event *); };
struct ble_npl_eventq { struct ble_npl_event *event; };
void ble_npl_event_init(struct ble_npl_event *,void (*)(struct ble_npl_event *),void *);
void ble_npl_eventq_put(struct ble_npl_eventq *,struct ble_npl_event *);
void ble_npl_eventq_remove(struct ble_npl_eventq *,struct ble_npl_event *);
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void);
