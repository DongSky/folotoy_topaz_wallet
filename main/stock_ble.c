#include "stock_ble.h"
#include "stock_protocol.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "os/os_mbuf.h"
#include <string.h>

#define CONN_NONE 0xffffu
#define TX_QUEUE_LENGTH 4u
#define TX_PACKET_MAX 517u

typedef struct {
    uint64_t received_ms;
    uint32_t session;
    uint16_t length;
    uint8_t bytes[STOCK_BLE_WRITE_MAX];
} ingress_t;
typedef struct {
    uint32_t session;
    bool screenshot;
    uint16_t length;
    uint8_t bytes[TX_PACKET_MAX];
} outgoing_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    bool initialized, registered, connected, polling, blocked;
    bool status_subscribed, screenshot_subscribed;
    uint16_t connection, response_handle, screenshot_handle;
    uint32_t session;
    uint8_t pending_error;
    uint8_t identity[STOCK_BLE_IDENTITY_SIZE], status[STOCK_BLE_STATUS_SIZE];
    ingress_t queue[STOCK_BLE_QUEUE_LENGTH];
    size_t head, count;
    outgoing_t outgoing[TX_QUEUE_LENGTH];
    size_t tx_head, tx_count;
} s_ble;
static struct ble_npl_event s_tx_event;
static bool s_tx_event_initialized;
static void tx_event(struct ble_npl_event *event);
/* Only the poll owner accesses parser/timestamp; connection changes are
 * observed through session, without touching worker-owned state in callbacks. */
static stock_stream_t s_stream;
static uint32_t s_parser_session;
static uint64_t s_last_received;

static void next_session_locked(void)
{
    ++s_ble.session;
    if (!s_ble.session) ++s_ble.session;
    s_ble.head = s_ble.count = 0;
    s_ble.tx_head = s_ble.tx_count = 0;
}

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(STOCK_BLE_SERVICE_UUID_BYTES);
#define CHR_UUID(suffix) BLE_UUID128_INIT(suffix,0,0,0,0,0,0,0,0x44,0x52,0x41,0x43,0x45,0x41,0x52,0x54)
static const ble_uuid128_t s_write_uuid = CHR_UUID(0x10);
static const ble_uuid128_t s_response_uuid = CHR_UUID(0x11);
static const ble_uuid128_t s_identity_uuid = CHR_UUID(0x12);
static const ble_uuid128_t s_status_uuid = CHR_UUID(0x13);
static const ble_uuid128_t s_screenshot_uuid = CHR_UUID(0x14);

static int access_cb(uint16_t connection, uint16_t attribute,
                      struct ble_gatt_access_ctxt *ctxt, void *arg);
static const struct ble_gatt_svc_def s_services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &s_service_uuid.u,
     .characteristics = (struct ble_gatt_chr_def[]) {
        {.uuid=&s_write_uuid.u, .access_cb=access_cb,
         .flags=BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP, .arg=(void *)0x10},
        {.uuid=&s_response_uuid.u, .access_cb=access_cb,
         .flags=BLE_GATT_CHR_F_NOTIFY, .val_handle=&s_ble.response_handle, .arg=(void *)0x11},
        {.uuid=&s_identity_uuid.u, .access_cb=access_cb,
         .flags=BLE_GATT_CHR_F_READ, .arg=(void *)0x12},
        {.uuid=&s_status_uuid.u, .access_cb=access_cb,
         .flags=BLE_GATT_CHR_F_READ, .arg=(void *)0x13},
        {.uuid=&s_screenshot_uuid.u, .access_cb=access_cb,
         .flags=BLE_GATT_CHR_F_NOTIFY, .val_handle=&s_ble.screenshot_handle, .arg=(void *)0x14},
        {0}
     }}, {0}
};

int stock_ble_init(const uint8_t identity[72], const uint8_t status[9])
{
    if (!identity || !status) return -1;
    portENTER_CRITICAL(&s_lock);
    if (s_ble.connected || s_ble.polling) { portEXIT_CRITICAL(&s_lock); return -1; }
    next_session_locked();
    s_ble.connection = CONN_NONE;
    s_ble.blocked = false;
    s_ble.pending_error = 0;
    s_ble.status_subscribed = s_ble.screenshot_subscribed = false;
    memcpy(s_ble.identity, identity, 72);
    memcpy(s_ble.status, status, 9);
    s_ble.initialized = true;
    portEXIT_CRITICAL(&s_lock);
    if (!s_tx_event_initialized) {
        ble_npl_event_init(&s_tx_event, tx_event, NULL);
        s_tx_event_initialized = true;
    }
    return 0;
}

int stock_ble_register(void)
{
    if (!s_ble.initialized) return -1;
    if (s_ble.registered) return 0;
    int error = ble_gatts_count_cfg(s_services);
    if (error) return error;
    error = ble_gatts_add_svcs(s_services);
    if (!error) s_ble.registered = true;
    return error;
}

bool stock_ble_deinit(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_ble.connected || s_ble.polling) { portEXIT_CRITICAL(&s_lock); return false; }
    next_session_locked();
    s_ble.initialized = s_ble.registered = false;
    s_ble.blocked = false;
    s_ble.pending_error = 0;
    s_ble.response_handle = s_ble.screenshot_handle = 0;
    memset(s_ble.identity, 0, sizeof(s_ble.identity));
    memset(s_ble.status, 0, sizeof(s_ble.status));
    memset(s_ble.queue, 0, sizeof(s_ble.queue));
    portEXIT_CRITICAL(&s_lock);
    /* Host and worker must already be stopped, as documented. */
    if (s_tx_event_initialized) {
        ble_npl_eventq_remove(nimble_port_get_dflt_eventq(), &s_tx_event);
        s_tx_event_initialized = false;
    }
    return true;
}

void stock_ble_on_connect(uint16_t connection)
{
    portENTER_CRITICAL(&s_lock);
    if (s_ble.initialized && !s_ble.connected && connection != CONN_NONE) {
        next_session_locked();
        s_ble.connected = true;
        s_ble.connection = connection;
        s_ble.status_subscribed = s_ble.screenshot_subscribed = false;
        s_ble.blocked = false;
        s_ble.pending_error = 0;
    }
    portEXIT_CRITICAL(&s_lock);
}

void stock_ble_on_disconnect(uint16_t connection)
{
    portENTER_CRITICAL(&s_lock);
    if (s_ble.connected && s_ble.connection == connection) {
        next_session_locked();
        s_ble.connected = false;
        s_ble.connection = CONN_NONE;
        s_ble.status_subscribed = s_ble.screenshot_subscribed = false;
        s_ble.blocked = false;
        s_ble.pending_error = 0;
    }
    portEXIT_CRITICAL(&s_lock);
}

void stock_ble_on_subscribe(uint16_t connection, uint16_t attribute, bool notify)
{
    portENTER_CRITICAL(&s_lock);
    if (s_ble.connected && s_ble.connection == connection) {
        if (attribute == s_ble.response_handle) s_ble.status_subscribed = notify;
        if (attribute == s_ble.screenshot_handle) s_ble.screenshot_subscribed = notify;
    }
    portEXIT_CRITICAL(&s_lock);
}

bool stock_ble_session_current(uint32_t session)
{
    portENTER_CRITICAL(&s_lock);
    bool current = s_ble.initialized && s_ble.connected && s_ble.session == session && !s_ble.blocked;
    portEXIT_CRITICAL(&s_lock);
    return current;
}

void stock_ble_update_status(const uint8_t status[9])
{
    if (!status) return;
    portENTER_CRITICAL(&s_lock);
    if (s_ble.initialized) memcpy(s_ble.status, status, 9);
    portEXIT_CRITICAL(&s_lock);
}

static void reject_locked(uint8_t status)
{
    next_session_locked();
    s_ble.blocked = true;
    s_ble.pending_error = status;
}

static int access_cb(uint16_t connection, uint16_t attribute,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attribute;
    uintptr_t kind = (uintptr_t)arg;
    if (!ctxt || !ctxt->om) return BLE_ATT_ERR_UNLIKELY;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && (kind == 0x12 || kind == 0x13)) {
        uint8_t bytes[72];
        size_t length = kind == 0x12 ? 72 : 9;
        portENTER_CRITICAL(&s_lock);
        bool allowed = s_ble.initialized && s_ble.connected && s_ble.connection == connection;
        if (allowed) memcpy(bytes, kind == 0x12 ? s_ble.identity : s_ble.status, length);
        portEXIT_CRITICAL(&s_lock);
        if (!allowed) return BLE_ATT_ERR_UNLIKELY;
        int result = os_mbuf_append(ctxt->om, bytes, length);
        /* Clear the sensitive temporary even if the append failed. */
        volatile uint8_t *clear = bytes;
        for (size_t i = 0; i < sizeof(bytes); ++i) clear[i] = 0;
        return result ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || kind != 0x10)
        return BLE_ATT_ERR_UNLIKELY;
    size_t length = OS_MBUF_PKTLEN(ctxt->om);
    ingress_t event = {.received_ms = (uint64_t)(esp_timer_get_time() / 1000), .length = (uint16_t)length};
    if (length <= STOCK_BLE_WRITE_MAX && os_mbuf_copydata(ctxt->om, 0, length, event.bytes))
        return BLE_ATT_ERR_UNLIKELY;
    portENTER_CRITICAL(&s_lock);
    int error = 0;
    if (!s_ble.initialized || !s_ble.connected || s_ble.connection != connection)
        error = BLE_ATT_ERR_UNLIKELY;
    else if (s_ble.blocked) error = BLE_ATT_ERR_INSUFFICIENT_RES;
    else if (length > STOCK_BLE_WRITE_MAX) {
        reject_locked(0x15);
        error = BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    } else if (s_ble.count == STOCK_BLE_QUEUE_LENGTH) {
        reject_locked(0x1b);
        error = BLE_ATT_ERR_INSUFFICIENT_RES;
    } else {
        event.session = s_ble.session;
        s_ble.queue[(s_ble.head + s_ble.count) % STOCK_BLE_QUEUE_LENGTH] = event;
        ++s_ble.count;
    }
    portEXIT_CRITICAL(&s_lock);
    return error;
}

static int notify(uint32_t session, bool screenshot, const uint8_t *data, size_t length)
{
    if (!data || !length || length > TX_PACKET_MAX) return -1;
    portENTER_CRITICAL(&s_lock);
    bool allowed = s_ble.initialized && s_ble.connected && s_ble.session == session &&
        !s_ble.blocked && (screenshot ? s_ble.screenshot_subscribed : s_ble.status_subscribed);
    uint16_t connection = s_ble.connection;
    portEXIT_CRITICAL(&s_lock);
    if (!allowed) return -1;
    uint16_t mtu = ble_att_mtu(connection);
    if (mtu < 3 || length > (size_t)mtu - 3) return -1;
    portENTER_CRITICAL(&s_lock);
    allowed = s_ble.initialized && s_ble.connected && s_ble.session == session &&
        !s_ble.blocked && s_ble.tx_count < TX_QUEUE_LENGTH;
    if (allowed) {
        outgoing_t *out = &s_ble.outgoing[(s_ble.tx_head + s_ble.tx_count) % TX_QUEUE_LENGTH];
        out->session = session;
        out->screenshot = screenshot;
        out->length = (uint16_t)length;
        memcpy(out->bytes, data, length);
        ++s_ble.tx_count;
    }
    portEXIT_CRITICAL(&s_lock);
    if (!allowed) return -1;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_tx_event);
    return 0;
}

static void tx_event(struct ble_npl_event *event)
{
    (void)event;
    /* All GAP callbacks and GATT writes run on this host task. A connection
     * cannot disappear and reuse its handle between validation and send. */
    for (size_t i = 0; i < TX_QUEUE_LENGTH; ++i) {
        outgoing_t out;
        portENTER_CRITICAL(&s_lock);
        bool have = s_ble.tx_count != 0;
        if (have) {
            out = s_ble.outgoing[s_ble.tx_head];
            s_ble.tx_head = (s_ble.tx_head + 1) % TX_QUEUE_LENGTH;
            --s_ble.tx_count;
        }
        bool allowed = have && s_ble.initialized && s_ble.connected && !s_ble.blocked &&
            s_ble.session == out.session &&
            (out.screenshot ? s_ble.screenshot_subscribed : s_ble.status_subscribed);
        uint16_t connection = s_ble.connection;
        uint16_t handle = have && out.screenshot ? s_ble.screenshot_handle : s_ble.response_handle;
        portEXIT_CRITICAL(&s_lock);
        if (!have) break;
        if (!allowed || !handle) continue;
        uint16_t mtu = ble_att_mtu(connection);
        if (mtu < 3 || out.length > (size_t)mtu - 3) continue;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(out.bytes, out.length);
        if (om) ble_gatts_notify_custom(connection, handle, om); /* consumes om */
    }
    portENTER_CRITICAL(&s_lock);
    bool more = s_ble.tx_count != 0;
    portEXIT_CRITICAL(&s_lock);
    if (more) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_tx_event);
}

size_t stock_ble_screenshot_capacity(uint32_t session)
{
    uint16_t connection = CONN_NONE;
    portENTER_CRITICAL(&s_lock);
    if (s_ble.connected && s_ble.session == session && s_ble.screenshot_subscribed)
        connection = s_ble.connection;
    portEXIT_CRITICAL(&s_lock);
    if (connection == CONN_NONE) return 0;
    uint16_t mtu = ble_att_mtu(connection);
    return mtu > 3 ? mtu - 3 : 0;
}

int stock_ble_notify_status(uint32_t session, uint8_t type, uint8_t status)
{
    uint8_t bytes[2] = {type, status};
    return notify(session, false, bytes, sizeof(bytes));
}
int stock_ble_notify_screenshot(uint32_t session, const uint8_t *data, size_t length)
{ return notify(session, true, data, length); }

typedef struct {
    uint32_t session;
    stock_ble_frame_handler_t handler;
    void *context;
} dispatch_t;
static void dispatch(void *arg, uint8_t type, const uint8_t *payload, size_t length)
{
    dispatch_t *d = arg;
    if (stock_ble_session_current(d->session)) d->handler(d->context, d->session, type, payload, length);
}

size_t stock_ble_poll(uint64_t now_ms, stock_ble_frame_handler_t handler, void *context)
{
    if (!handler) return 0;
    portENTER_CRITICAL(&s_lock);
    if (!s_ble.initialized || s_ble.polling) { portEXIT_CRITICAL(&s_lock); return 0; }
    s_ble.polling = true;
    uint32_t session = s_ble.session;
    uint8_t pending_error = s_ble.pending_error;
    s_ble.pending_error = 0;
    s_ble.blocked = false;
    portEXIT_CRITICAL(&s_lock);
    if (session != s_parser_session || pending_error) {
        stock_stream_reset(&s_stream);
        s_parser_session = session;
        s_last_received = 0;
    }
    if (pending_error) stock_ble_notify_status(session, 0, pending_error);
    size_t processed = 0;
    while (processed < STOCK_BLE_QUEUE_LENGTH) {
        ingress_t event;
        portENTER_CRITICAL(&s_lock);
        bool have = s_ble.count != 0 && s_ble.session == session && !s_ble.blocked;
        if (have) {
            event = s_ble.queue[s_ble.head];
            s_ble.head = (s_ble.head + 1) % STOCK_BLE_QUEUE_LENGTH;
            --s_ble.count;
        }
        portEXIT_CRITICAL(&s_lock);
        if (!have) break;
        ++processed;
        if (event.session != session || !stock_ble_session_current(session)) continue;
        if (s_stream.used && event.received_ms >= s_last_received &&
            event.received_ms - s_last_received >= STOCK_BLE_IDLE_MS) {
            stock_stream_reset(&s_stream);
            stock_ble_notify_status(session, 0, 0x11);
        }
        dispatch_t d = {.session=session, .handler=handler, .context=context};
        stock_frame_result_t result = stock_stream_feed(&s_stream, event.bytes, event.length, dispatch, &d);
        s_last_received = event.received_ms;
        if (result != STOCK_FRAME_OK)
            stock_ble_notify_status(session, 0, result == STOCK_FRAME_TOO_LARGE ? 0x15 : 0x10);
    }
    portENTER_CRITICAL(&s_lock);
    bool idle = s_ble.count == 0 && s_ble.session == session && !s_ble.blocked;
    portEXIT_CRITICAL(&s_lock);
    if (idle && s_stream.used && now_ms >= s_last_received && now_ms - s_last_received >= STOCK_BLE_IDLE_MS) {
        stock_stream_reset(&s_stream);
        stock_ble_notify_status(session, 0, 0x11);
    }
    portENTER_CRITICAL(&s_lock);
    s_ble.polling = false;
    portEXIT_CRITICAL(&s_lock);
    return processed;
}
