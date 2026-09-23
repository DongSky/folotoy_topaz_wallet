#include "stock_ble.h"
#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct ble_gatt_svc_def *services;
static int critical, registered, fail_append, fail_alloc, notify_error;
static unsigned callbacks, notifications;
static uint32_t session;
static uint64_t now;
static uint16_t mtu=263;
static struct os_mbuf last;
static uint16_t last_handle;
static struct ble_npl_eventq evq;
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void) { return &evq; }
void ble_npl_event_init(struct ble_npl_event *ev,void (*fn)(struct ble_npl_event *),void *arg)
{ (void)arg;ev->callback=fn; }
void ble_npl_eventq_put(struct ble_npl_eventq *q,struct ble_npl_event *ev)
{ assert(!critical);q->event=ev; }
void ble_npl_eventq_remove(struct ble_npl_eventq *q,struct ble_npl_event *ev)
{ assert(!critical);if(q->event==ev)q->event=NULL; }
static void drain(void)
{ while(evq.event){struct ble_npl_event *ev=evq.event;evq.event=NULL;ev->callback(ev);} }
void fake_enter(void) { assert(!critical); critical=1; }
void fake_exit(void) { assert(critical); critical=0; }
int64_t esp_timer_get_time(void) { return (int64_t)now*1000; }
int os_mbuf_append(struct os_mbuf *m,const void *p,size_t n)
{ assert(!critical); if(fail_append)return 1; assert(m->length+n<=sizeof(m->data)); memcpy(m->data+m->length,p,n);m->length+=n;return 0; }
int os_mbuf_copydata(const struct os_mbuf *m,int off,int n,void *p)
{ assert(!critical && off>=0 && n>=0 && (size_t)(off+n)<=m->length);memcpy(p,m->data+off,(size_t)n);return 0; }
void os_mbuf_free_chain(struct os_mbuf *m) { assert(!critical);free(m); }
struct os_mbuf *ble_hs_mbuf_from_flat(const void *p,uint16_t n)
{ assert(!critical);if(fail_alloc)return NULL;struct os_mbuf *m=calloc(1,sizeof(*m));assert(m);memcpy(m->data,p,n);m->length=n;return m; }
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *s) { assert(!critical); services=s;return 0; }
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *s)
{ assert(!critical && s==services);++registered;*s[0].characteristics[1].val_handle=101;*s[0].characteristics[4].val_handle=104;return 0; }
int ble_gatts_notify_custom(uint16_t conn,uint16_t attr,struct os_mbuf *m)
{ assert(!critical && conn==7);++notifications;last=*m;last_handle=attr;free(m);return notify_error; }
uint16_t ble_att_mtu(uint16_t conn) { assert(!critical && conn==7);return mtu; }

static void frame(void *ctx,uint32_t s,uint8_t type,const uint8_t *bytes,size_t n)
{
    assert(!critical && ctx==(void *)123 && type==1 && n==2 && bytes[0]=='{' && bytes[1]=='}');
    ++callbacks;session=s;assert(stock_ble_session_current(s));
    assert(stock_ble_poll(now,frame,ctx)==0); /* concurrent/reentrant denied */
    assert(stock_ble_init(last.data,last.data)<0 && !stock_ble_deinit());
}
static int access(unsigned characteristic,uint8_t op,const uint8_t *bytes,size_t n,uint16_t conn)
{
    struct os_mbuf m={.length=n};if(n)memcpy(m.data,bytes,n);
    struct ble_gatt_access_ctxt c={.op=op,.om=&m};
    const struct ble_gatt_chr_def *chr=services[0].characteristics+characteristic;
    int error=chr->access_cb(conn,0,&c,chr->arg);
    if(op==BLE_GATT_ACCESS_OP_READ_CHR)last=m;
    return error;
}
static int write_bytes(const uint8_t *bytes,size_t n) { return access(0,1,bytes,n,7); }
static void poll(void) { stock_ble_poll(now,frame,(void *)123);drain(); }
static void error_is(uint8_t status)
{ assert(last_handle==101 && last.length==2 && last.data[0]==0 && last.data[1]==status); }

int main(void)
{
    uint8_t identity[72],status[9];memset(identity,0x5a,sizeof(identity));memset(status,0xa5,sizeof(status));
    assert(stock_ble_register()<0 && stock_ble_init(NULL,status)<0);
    assert(stock_ble_init(identity,status)==0 && stock_ble_register()==0 && stock_ble_register()==0 && registered==1);
    const ble_uuid128_t *uuid=(const ble_uuid128_t *)services[0].uuid;
    const uint8_t service_uuid[]={STOCK_BLE_SERVICE_UUID_BYTES};
    assert(!memcmp(uuid->value,service_uuid,16));
    for(unsigned i=0;i<5;++i){
        const struct ble_gatt_chr_def *c=&services[0].characteristics[i];
        uuid=(const ble_uuid128_t *)c->uuid;
        assert(uuid->value[0]==0x10+i && !memcmp(uuid->value+1,service_uuid+1,15));
        const uint16_t flags[]={12,16,2,2,16};assert(c->flags==flags[i]);
    }
    assert(!services[1].type && !services[0].characteristics[5].uuid);
    assert(access(2,0,NULL,0,7)!=0);
    stock_ble_on_connect(7);stock_ble_on_connect(7);stock_ble_on_connect(8);
    assert(access(2,0,NULL,0,8)!=0);
    assert(access(2,0,NULL,0,7)==0 && last.length==72 && !memcmp(last.data,identity,72));
    assert(access(3,0,NULL,0,7)==0 && last.length==9 && !memcmp(last.data,status,9));
    status[3]=0x77;stock_ble_update_status(status);assert(access(3,0,NULL,0,7)==0 && last.data[3]==0x77);
    fail_append=1;assert(access(2,0,NULL,0,7)==BLE_ATT_ERR_INSUFFICIENT_RES);fail_append=0;
    const uint8_t good[]={1,1,2,0,'{','}'};
    assert(write_bytes(good,3)==0 && write_bytes(good+3,3)==0 && callbacks==0);poll();assert(callbacks==1);
    assert(stock_ble_notify_status(session,1,1)<0);
    stock_ble_on_subscribe(8,101,true);assert(stock_ble_notify_status(session,1,1)<0);
    stock_ble_on_subscribe(7,101,true);assert(stock_ble_notify_status(session,1,1)==0);drain();assert(last.data[0]==1 && last.data[1]==1);
    uint8_t two[12];memcpy(two,good,6);memcpy(two+6,good,6);
    assert(write_bytes(two,12)==0);poll();assert(callbacks==2); /* one frame per ATT */
    const uint8_t bad[]={2,1,0,0},huge[]={1,1,1,1};
    assert(write_bytes(bad,4)==0);poll();error_is(0x10);
    assert(write_bytes(huge,4)==0);poll();error_is(0x15);
    assert(write_bytes(good,2)==0);poll();unsigned count=notifications;
    now=1999;poll();assert(notifications==count);
    now=2000;poll();error_is(0x11);assert(notifications==count+1);poll();assert(notifications==count+1);
    assert(write_bytes(good,2)==0);now+=2000;assert(write_bytes(good,6)==0);poll();assert(callbacks==3);error_is(0x11);
    uint32_t old_session=session;
    assert(write_bytes(good,2)==0);poll();
    for(unsigned i=0;i<8;++i)assert(write_bytes(good,6)==0);
    assert(write_bytes(good,6)==BLE_ATT_ERR_INSUFFICIENT_RES);
    assert(!stock_ble_session_current(old_session));
    assert(write_bytes(good,6)!=0);poll();error_is(0x1b);assert(callbacks==3);
    assert(write_bytes(good,6)==0);poll();assert(callbacks==4 && session!=old_session);
    assert(stock_ble_notify_status(old_session,1,1)<0);
    uint8_t oversized[261]={0};assert(write_bytes(oversized,261)==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);poll();error_is(0x15);
    assert(write_bytes(good,6)==0);poll();
    stock_ble_on_subscribe(7,104,true);
    assert(stock_ble_screenshot_capacity(session)==(size_t)mtu-3);
    assert(stock_ble_screenshot_capacity(old_session)==0);
    const uint8_t screenshot[]={1,2,3,4};
    assert(stock_ble_notify_screenshot(session,screenshot,4)==0);drain();assert(last_handle==104 && last.length==4);
    mtu=6;assert(stock_ble_notify_screenshot(session,screenshot,4)<0);mtu=263;
    fail_alloc=1;count=notifications;assert(stock_ble_notify_status(session,1,1)==0);drain();assert(notifications==count);fail_alloc=0;
    notify_error=42;assert(stock_ble_notify_status(session,1,1)==0);drain();notify_error=0;
    for(unsigned i=0;i<4;++i)assert(stock_ble_notify_status(session,1,1)==0);
    assert(stock_ble_notify_status(session,1,1)<0);drain();
    stock_ble_on_subscribe(7,104,false);assert(stock_ble_notify_screenshot(session,screenshot,4)<0);
    assert(stock_ble_screenshot_capacity(session)==0);
    old_session=session;assert(write_bytes(good,2)==0);poll();assert(write_bytes(good,6)==0);
    stock_ble_on_disconnect(8);assert(stock_ble_session_current(session));
    count=notifications;assert(stock_ble_notify_status(session,1,1)==0);
    stock_ble_on_disconnect(7);assert(!stock_ble_session_current(session));
    stock_ble_on_connect(7);unsigned before=callbacks;poll();assert(callbacks==before);
    assert(notifications==count); /* queued response never sent to reused handle */
    assert(write_bytes(good,6)==0);poll();assert(session!=old_session && callbacks==before+1);
    assert(stock_ble_notify_status(session,1,1)<0); /* reconnect clears CCC */
    stock_ble_on_disconnect(7);assert(stock_ble_init(identity,status)==0 && stock_ble_register()==0 && registered==1);
    assert(stock_ble_deinit());assert(stock_ble_register()<0);
    assert(stock_ble_init(identity,status)==0 && stock_ble_register()==0 && registered==2);
    puts("Stock GATT queue/session/timeout tests: PASS (synthetic transport only)");
}
