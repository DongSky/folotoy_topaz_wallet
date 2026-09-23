#include "../main/wallet_ble.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct fake_queue {unsigned capacity,count,head;size_t size;uint8_t *data;};
struct fake_hs_cfg ble_hs_cfg;
static struct ble_npl_eventq host_queue;
static struct ble_gap_conn_desc peer;
static bool host_context,adv_active,stock_initialized,stock_connected;
static int fail_task,task_calls,fail_register,owner_load_error,nimble_deinits,stock_deinits;
static int stock_connects,stock_disconnects,stock_subscribes,security_starts,terminations,store_aborts;
static int64_t time_us;
static ble_uuid128_t advertised_uuid;
static char advertised_name[32];
static int gatt_registrations;
static int live_queues,live_mutexes,passkey_injections;

QueueHandle_t xQueueCreate(unsigned count,size_t size){struct fake_queue *q=calloc(1,sizeof(*q));assert(q);q->capacity=count;q->size=size;q->data=calloc(count,size);assert(q->data);++live_queues;return q;}
int xQueueSend(QueueHandle_t q,const void *v,unsigned wait){assert(wait==0);if(q->count==q->capacity)return 0;memcpy(q->data+((q->head+q->count)%q->capacity)*q->size,v,q->size);++q->count;return 1;}
int xQueueReceive(QueueHandle_t q,void *v,unsigned wait){(void)wait;if(!q->count)return 0;memcpy(v,q->data+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;--q->count;return 1;}
void vQueueDelete(QueueHandle_t q){free(q->data);free(q);--live_queues;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){++live_mutexes;return calloc(1,sizeof(int));}
int xSemaphoreTake(SemaphoreHandle_t m,unsigned wait){(void)wait;assert(m&&!*m);*m=1;return 1;}
int xSemaphoreGive(SemaphoreHandle_t m){assert(m&&*m);*m=0;return 1;}
void vSemaphoreDelete(SemaphoreHandle_t m){assert(!*m);free(m);--live_mutexes;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out)
{(void)fn;(void)name;(void)stack;(void)arg;(void)priority;++task_calls;if(task_calls==fail_task)return 0;*out=(void *)(uintptr_t)task_calls;return 1;}
void vTaskDelete(TaskHandle_t t){(void)t;}
int64_t esp_timer_get_time(void){return time_us;}
int esp_timer_create(const esp_timer_create_args_t *a,esp_timer_handle_t *out){assert(a->callback);*out=(void *)99;return 0;}
int esp_timer_start_periodic(esp_timer_handle_t t,uint64_t period){assert(t&&period==1000000);return 0;}
int esp_timer_stop(esp_timer_handle_t t){assert(t);return 0;}
int esp_timer_delete(esp_timer_handle_t t){assert(t);return 0;}
uint32_t esp_random(void){return 123456;}
int nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *out){assert(!strcmp(name,"wallet_ble"));if(mode==NVS_READONLY&&owner_load_error)return owner_load_error;*out=1;return 0;}
int nvs_get_blob(nvs_handle_t h,const char *key,void *p,size_t *n){(void)p;(void)n;assert(h==1&&!strcmp(key,"owner"));return ESP_ERR_NVS_NOT_FOUND;}
int nvs_set_blob(nvs_handle_t h,const char *key,const void *p,size_t n){(void)p;assert(h==1&&!strcmp(key,"owner")&&n==7);return 0;}
int nvs_erase_key(nvs_handle_t h,const char *key){assert(h==1&&!strcmp(key,"owner"));return 0;}
int nvs_commit(nvs_handle_t h){assert(h==1);return 0;}
void nvs_close(nvs_handle_t h){assert(h==1);}
int ble_gap_adv_active(void){return adv_active;}
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *f){assert(f->num_uuids128==1);advertised_uuid=*f->uuids128;return 0;}
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *f){assert(f->name_len<sizeof(advertised_name));memset(advertised_name,0,sizeof(advertised_name));memcpy(advertised_name,f->name,f->name_len);return 0;}
int ble_gap_adv_start(uint8_t t,const void *p,int32_t d,const struct ble_gap_adv_params *a,int (*cb)(struct ble_gap_event *,void *),void *ctx)
{(void)t;(void)p;(void)d;(void)a;(void)ctx;assert(cb==wallet_gap_event);adv_active=true;return 0;}
int ble_gap_adv_stop(void){adv_active=false;return 0;}
int ble_gap_unpair(const ble_addr_t *p){(void)p;return 0;}
int ble_gap_rd_local_resolv_addr(uint8_t t,const ble_addr_t *p,uint8_t *out){(void)t;(void)p;(void)out;return BLE_HS_HCI_ERR(BLE_ERR_UNK_CONN_ID);}
int ble_gap_terminate(uint16_t h,int reason){(void)reason;assert(host_context&&h!=0xffff);++terminations;return 0;}
int ble_gap_conn_find(uint16_t h,struct ble_gap_conn_desc *out){if(h!=peer.conn_handle)return BLE_HS_ENOENT;*out=peer;return 0;}
int ble_gap_security_initiate(uint16_t h){assert(h==peer.conn_handle);++security_starts;return 0;}
int ble_sm_inject_io(uint16_t h,const struct ble_sm_io *io){assert(host_context&&h==peer.conn_handle&&io->action==BLE_SM_IOACT_DISP);++passkey_injections;return 0;}
int ble_store_iterate(int type,int (*fn)(int,union ble_store_value *,void *),void *ctx){(void)type;(void)fn;(void)ctx;return 0;}
int ble_store_util_count(int type,int *out){(void)type;*out=0;return 0;}
void ble_store_config_init(void){}
int ble_hs_util_ensure_addr(int flag){(void)flag;return 0;}
int ble_hs_id_infer_auto(int flag,uint8_t *out){(void)flag;*out=0;return 0;}
int os_mbuf_append(struct os_mbuf *m,const void *p,size_t n){assert(m->length+n<=sizeof(m->data));memcpy(m->data+m->length,p,n);m->length+=n;return 0;}
int os_mbuf_copydata(const struct os_mbuf *m,int off,int n,void *p){assert(off>=0&&n>=0&&(size_t)(off+n)<=m->length);memcpy(p,m->data+off,(size_t)n);return 0;}
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *s){assert(s==s_services);return 0;}
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *s){assert(s==s_services);++gatt_registrations;return 0;}
void ble_svc_gap_init(void){}
void ble_svc_gatt_init(void){}
int ble_svc_gap_device_name_set(const char *s){assert(s&&strlen(s)<32);return 0;}
int nimble_port_init(void){return 0;}
int nimble_port_deinit(void){assert(!stock_initialized&&!host_queue.event);++nimble_deinits;return 0;}
void nimble_port_run(void){}
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void){return &host_queue;}
void ble_npl_event_init(struct ble_npl_event *ev,void (*fn)(struct ble_npl_event *),void *ctx){(void)ctx;ev->fn=fn;}
void ble_npl_eventq_put(struct ble_npl_eventq *q,struct ble_npl_event *ev){q->event=ev;}
void ble_npl_eventq_remove(struct ble_npl_eventq *q,struct ble_npl_event *ev){if(q->event==ev)q->event=NULL;}
int stock_ble_init(const uint8_t id[72],const uint8_t status[9]){assert(id[0]==1&&status[0]==1);stock_initialized=true;return 0;}
int stock_ble_register(void){assert(stock_initialized&&gatt_registrations);return fail_register;}
bool stock_ble_deinit(void){assert(!stock_connected);stock_initialized=false;++stock_deinits;return true;}
void stock_ble_on_connect(uint16_t h){assert(host_context&&h==peer.conn_handle);stock_connected=true;++stock_connects;}
void stock_ble_on_disconnect(uint16_t h){assert(host_context);if(h==peer.conn_handle)stock_connected=false;++stock_disconnects;}
void stock_ble_on_subscribe(uint16_t h,uint16_t a,bool n){assert(host_context&&h==peer.conn_handle&&a==101&&n);++stock_subscribes;}
wallet_store_result_t wallet_store_begin(uint32_t size,uint32_t crc){(void)size;(void)crc;return WALLET_STORE_OK;}
wallet_store_result_t wallet_store_write(uint32_t off,const uint8_t *p,size_t n){(void)off;(void)p;(void)n;return WALLET_STORE_OK;}
wallet_store_result_t wallet_store_commit_guarded(wallet_store_commit_guard_t guard,void *ctx){return guard(ctx)?WALLET_STORE_OK:WALLET_STORE_INVALID_STATE;}
void wallet_store_abort(void){++store_aborts;}

static void run_host(void){host_context=true;while(host_queue.event){struct ble_npl_event *e=host_queue.event;host_queue.event=NULL;e->fn(e);}host_context=false;}
static int gap(struct ble_gap_event *e){host_context=true;int result=wallet_gap_event(e,NULL);host_context=false;return result;}
static void run_worker(void){wallet_ble_work_t w;while(xQueueReceive(s_ble.control_queue,&w,0)||xQueueReceive(s_ble.queue,&w,0))process_work(&w);}
static void connect_peer(void){struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.conn_handle=7}};peer.conn_handle=7;assert(gap(&e)==0);}
static void disconnect_peer(void){struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT};e.disconnect.conn.conn_handle=7;assert(gap(&e)==0);run_worker();run_host();}
static void cleanup(void){if(s_ble.conn_handle!=WALLET_BLE_CONN_NONE)disconnect_peer();ble_npl_eventq_remove(&host_queue,&s_stock_control_event);if(s_ble.stock_mode){assert(stock_ble_deinit());}assert(nimble_port_deinit()==0);vQueueDelete(s_ble.queue);vQueueDelete(s_ble.control_queue);vSemaphoreDelete(s_ble.mutex);memset(&s_ble,0,sizeof(s_ble));s_ble.conn_handle=WALLET_BLE_CONN_NONE;s_init_blocked=false;adv_active=false;memset(&ble_hs_cfg,0,sizeof(ble_hs_cfg));memset(&peer,0,sizeof(peer));task_calls=0;assert(!live_queues&&!live_mutexes);}
static void begin_stock(void){uint8_t identity[72]={1,1},status[9]={1};memcpy(identity+2,"a1b2c3d4e5f6",12);assert(wallet_ble_init_stock(identity,status,"a1b2c3d4e5f6")==WALLET_BLE_OK);host_context=true;on_sync();host_context=false;run_worker();run_host();}
static void test_legacy_connections_and_pairing_expiry(void)
{
    begin_stock();assert(adv_active&&ble_hs_cfg.sm_sec_lvl==1&&ble_hs_cfg.sm_mitm&&ble_hs_cfg.sm_sc_only);
    assert(!strcmp(advertised_name,"a1b2c3d4e5f6"));const uint8_t expected[]={STOCK_BLE_SERVICE_UUID_BYTES};assert(!memcmp(advertised_uuid.value,expected,16));
    assert(s_services[0].characteristics[0].flags==(BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN));
    connect_peer();assert(stock_connected&&s_ble.connected);run_worker();assert(security_starts==0);
    struct ble_gap_event sub={.type=BLE_GAP_EVENT_SUBSCRIBE,.subscribe={.conn_handle=7,.attr_handle=101,.cur_notify=1}};assert(gap(&sub)==0&&stock_subscribes==1);
    struct os_mbuf m={0};struct ble_gatt_access_ctxt c={.op=BLE_GATT_ACCESS_OP_READ_CHR,.om=&m};assert(wallet_gatt_access(7,0,&c,NULL)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
    assert(wallet_ble_open_pairing_window(100)==WALLET_BLE_OK);run_worker();assert(ble_hs_cfg.sm_sec_lvl==1);run_host();assert(ble_hs_cfg.sm_sec_lvl==4);
    unsigned prior=terminations;time_us+=100001;process_tick();run_host();assert(ble_hs_cfg.sm_sec_lvl==1&&stock_connected&&s_ble.connected&&terminations==(int)prior);
    assert(wallet_gatt_access(7,0,&c,NULL)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN);
    /* Another connection cannot replace either service's accepted session. */
    peer.conn_handle=8;struct ble_gap_event extra={.type=BLE_GAP_EVENT_CONNECT,.connect={.conn_handle=8}};assert(gap(&extra)==0);assert(s_ble.conn_handle==7&&stock_connects==1);peer.conn_handle=7;
    disconnect_peer();assert(adv_active&&!stock_connected);cleanup();
}
static void test_secure_transfer_survives_expiry_but_close_invalidates_stock_on_host(void)
{
    begin_stock();connect_peer();run_worker();s_ble.owner_present=true;s_ble.owner=peer.peer_id_addr;
    assert(wallet_ble_open_pairing_window(100)==WALLET_BLE_OK);run_worker();run_host();
    peer.sec_state=(struct ble_gap_sec_state){true,true,true,16};struct ble_gap_event e={.type=BLE_GAP_EVENT_ENC_CHANGE,.enc_change={.conn_handle=7}};assert(gap(&e)==0);run_worker();assert(s_ble.secure);
    unsigned prior=terminations;time_us+=100001;process_tick();run_host();assert(s_ble.secure&&s_ble.connected&&ble_hs_cfg.sm_sec_lvl==1&&terminations==(int)prior);
    assert(gap(&e)==0);run_worker();assert(s_ble.secure); /* rekey while windowclosed */
    wallet_ble_close_pairing_window();assert(!s_ble.secure);assert(stock_connected);run_host();assert(!stock_connected);run_worker();assert(terminations>(int)prior);cleanup();
}
static void test_owner_failure_only_blocks_custom_and_pairing_requires_window(void)
{
    owner_load_error=99;begin_stock();assert(s_ble.owner_recovery_required&&adv_active);connect_peer();run_worker();assert(stock_connected&&s_ble.connected);
    assert(wallet_ble_open_pairing_window(1000)==WALLET_BLE_OK);run_worker();run_host();assert(ble_hs_cfg.sm_sec_lvl==1);
    struct ble_gap_event p={.type=BLE_GAP_EVENT_PASSKEY_ACTION,.passkey={.conn_handle=7,.params={.action=BLE_SM_IOACT_DISP}}};assert(gap(&p)==BLE_HS_EREJECT&&!s_ble.pairing_pending);cleanup();owner_load_error=0;
    begin_stock();connect_peer();run_worker();assert(gap(&p)==BLE_HS_EREJECT);assert(wallet_ble_open_pairing_window(100)==WALLET_BLE_OK);run_worker();run_host();assert(gap(&p)==0);run_worker();assert(s_ble.pairing_pending);
    unsigned prior=terminations;time_us+=100001;process_tick();run_host();assert(terminations>(int)prior);cleanup();
}
static void test_reset_hook_and_registration_rollback(void)
{
    begin_stock();connect_peer();run_worker();host_context=true;on_reset(99);host_context=false;assert(!stock_connected&&!s_ble.connected&&!s_ble.host_synced);run_worker();cleanup();
    int before=stock_deinits,nd=nimble_deinits;fail_register=1;
    uint8_t identity[72]={1,1},status[9]={1};memcpy(identity+2,"a1b2c3d4e5f6",12);
    assert(wallet_ble_init_stock(identity,status,"a1b2c3d4e5f6")==WALLET_BLE_PLATFORM_ERROR);
    assert(!live_queues&&!live_mutexes&&!stock_initialized&&stock_deinits==before+1&&nimble_deinits==nd+1);fail_register=0;
    fail_task=2;task_calls=0;assert(wallet_ble_init_stock(identity,status,"a1b2c3d4e5f6")==WALLET_BLE_NO_MEMORY);assert(!live_queues&&!live_mutexes&&!stock_initialized);fail_task=0;task_calls=0;
}
static void test_original_wallet_only_mode_is_preserved(void)
{
    assert(wallet_ble_init()==WALLET_BLE_OK);on_sync();run_worker();assert(!adv_active&&ble_hs_cfg.sm_sec_lvl==4&&!stock_initialized);
    assert(wallet_ble_open_pairing_window(1000)==WALLET_BLE_OK);run_worker();assert(adv_active&&!strcmp(advertised_name,WALLET_BLE_DEVICE_NAME));connect_peer();run_worker();assert(security_starts==1);cleanup();
}
static void test_delayed_close_never_terminates_a_reused_handle_or_reopens_pairing(void)
{
    begin_stock();connect_peer();run_worker();
    wallet_ble_close_pairing_window();
    wallet_ble_work_t stale_close;
    assert(xQueueReceive(s_ble.control_queue,&stale_close,0));
    disconnect_peer();connect_peer();run_worker();assert(stock_connected);
    int prior=terminations;
    process_work(&stale_close);run_host();
    assert(terminations==prior&&stock_connected&&s_ble.connected);
    assert(wallet_ble_open_pairing_window(1000)==WALLET_BLE_OK);
    wallet_ble_close_pairing_window();run_worker();run_host();
    assert(!s_ble.window_open&&ble_hs_cfg.sm_sec_lvl==1);
    cleanup();
}
static void test_failed_open_preserves_pending_close(void)
{
    begin_stock();connect_peer();run_worker();
    wallet_ble_close_pairing_window();
    wallet_ble_work_t filler={.type=BLE_WORK_TICK};
    while(enqueue(&filler)) {}
    uint32_t control=s_ble.control_generation;
    assert(wallet_ble_open_pairing_window(1000)==WALLET_BLE_NO_MEMORY);
    assert(s_ble.control_generation==control);
    int prior=terminations;
    run_worker();run_host();assert(terminations>prior&&!stock_connected);
    cleanup();
}
static void test_stale_passkey_never_releases_or_terminates_new_session(void)
{
    begin_stock();connect_peer();run_worker();
    assert(wallet_ble_open_pairing_window(1000)==WALLET_BLE_OK);run_worker();run_host();
    struct ble_gap_event p={.type=BLE_GAP_EVENT_PASSKEY_ACTION,.passkey={.conn_handle=7,.params={.action=BLE_SM_IOACT_DISP}}};
    assert(gap(&p)==0);run_worker();
    assert(wallet_ble_confirm_passkey(true)==WALLET_BLE_OK);run_worker();
    int before=passkey_injections,prior=terminations;
    /* Hold the host control event until the physical connection is replaced. */
    struct ble_gap_event d={.type=BLE_GAP_EVENT_DISCONNECT};d.disconnect.conn.conn_handle=7;assert(gap(&d)==0);
    connect_peer();run_worker();run_host();
    assert(passkey_injections==before&&terminations==prior&&stock_connected);
    wallet_ble_work_t stale={.type=BLE_WORK_CONFIRM,.generation=s_ble.generation-2,.conn_handle=7,.accept=false};
    process_work(&stale);run_host();assert(terminations==prior);
    cleanup();
}
int main(void)
{
    test_legacy_connections_and_pairing_expiry();test_secure_transfer_survives_expiry_but_close_invalidates_stock_on_host();
    test_owner_failure_only_blocks_custom_and_pairing_requires_window();test_reset_hook_and_registration_rollback();test_original_wallet_only_mode_is_preserved();
    test_delayed_close_never_terminates_a_reused_handle_or_reopens_pairing();
    test_failed_open_preserves_pending_close();
    test_stale_passkey_never_releases_or_terminates_new_session();
    puts("Wallet/stock shared host lifecycle/security: PASS (synthetic platform)");
}
