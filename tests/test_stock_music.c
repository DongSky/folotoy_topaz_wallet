#include "stock_music.h"
#include "stock_rtttl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "bsp_audio.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static uint8_t stored[1024], queued[1100];
static size_t stored_len, item_size;
static bool exists, queued_valid, queue_allocated, current=true, in_worker;
static bool fail_queue_create, fail_task_create, fail_queue_send;
static int fail_init, fail_read, fail_open, fail_set, fail_commit, fail_audio, fail_write;
static unsigned opened, closed, sets, commits, sends, writes, slept, volume, samples;
static unsigned cancel_stage; /* 1 init, 2 open, 3 set, 4 commit */
static void (*worker)(void *);
static void (*write_hook)(void);
static jmp_buf idle;
static bool guard(void *context, uint32_t session)
{ assert(context==(void *)123); return current && session==7; }
esp_err_t nvs_flash_init(void) { if(cancel_stage==1)current=false; return fail_init; }
esp_err_t nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *handle)
{
    assert(!strcmp(name,"trae_cfg"));
    if(cancel_stage==2)current=false;
    if(fail_open)return fail_open;
    if(mode==NVS_READONLY&&!exists)return ESP_ERR_NVS_NOT_FOUND;
    ++opened; *handle=1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n)
{
    assert(h==1&&!strcmp(key,"boot_rtttl"));
    if(fail_read)return fail_read;
    if(!exists)return ESP_ERR_NVS_NOT_FOUND;
    if(!out){*n=stored_len;return ESP_OK;}
    if(*n<stored_len)return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out,stored,stored_len);*n=stored_len;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *b,size_t n)
{
    assert(h==1&&!strcmp(key,"boot_rtttl")&&n<=sizeof(stored));++sets;
    if(cancel_stage==3)current=false;
    if(fail_set)return fail_set;
    memcpy(stored,b,n);stored_len=n;exists=true;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{assert(h==1);++commits;if(cancel_stage==4)current=false;return fail_commit;}
void nvs_close(nvs_handle_t h){assert(h==1);++closed;}
QueueHandle_t xQueueCreate(UBaseType_t count,UBaseType_t size)
{
    assert(count==1&&size<=sizeof(queued));
    if(fail_queue_create)return NULL;
    assert(!queue_allocated);queue_allocated=true;item_size=size;return queued;
}
void vQueueDelete(QueueHandle_t q)
{assert(q==queued&&queue_allocated);queue_allocated=false;queued_valid=false;}
BaseType_t xQueueOverwrite(QueueHandle_t q,const void *item)
{
    assert(q==queued&&queue_allocated);++sends;
    if(fail_queue_send)return pdFALSE;
    memcpy(queued,item,item_size);queued_valid=true;return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t q,void *item,TickType_t wait)
{
    assert(q==queued&&in_worker);
    if(!queued_valid){if(wait==portMAX_DELAY)longjmp(idle,1);return pdFALSE;}
    memcpy(item,queued,item_size);queued_valid=false;return pdTRUE;
}
BaseType_t xTaskCreate(void (*fn)(void *),const char *name,uint32_t stack,
                       void *arg,UBaseType_t priority,TaskHandle_t *handle)
{
    (void)name;(void)priority;assert(!arg&&stack>=3072);
    if(fail_task_create)return pdFALSE;
    worker=fn;if(handle)*handle=(void *)1;return pdPASS;
}
esp_err_t bsp_audio_wake(void){assert(in_worker);return fail_audio;}
esp_err_t bsp_audio_set_format(uint32_t hz,uint8_t bits,uint8_t channels)
{assert(in_worker&&hz==8000&&bits==16&&channels==1);return fail_audio;}
void bsp_audio_set_volume(uint8_t percent){assert(in_worker&&percent<=100);volume=percent;}
esp_err_t bsp_audio_write(const void *pcm,size_t n)
{
    assert(in_worker&&pcm&&n<=512&&n%2==0);++writes;samples+=(unsigned)n/2;
    const int16_t *p=pcm;
    for(size_t i=0;i<n/2;++i)assert(p[i]==0||p[i]==6000||p[i]==-6000);
    if(write_hook){void (*hook)(void)=write_hook;write_hook=NULL;hook();}
    return fail_write;
}
esp_err_t bsp_audio_sleep(void){assert(in_worker);++slept;return 0;}
static void run_worker(void)
{in_worker=true;if(setjmp(idle)==0)worker(NULL);in_worker=false;}
static uint8_t call(const uint8_t *p,size_t n)
{return stock_music_handle(7,guard,(void *)123,p,n);}
static void begin(const char *score)
{
    size_t n=strlen(score);assert(n<255);
    uint8_t b[]={0,(uint8_t)n,(uint8_t)(n>>8)};
    assert(call(b,sizeof(b))==0);
    uint8_t data[256]={1};memcpy(data+1,score,n);
    assert(call(data,n+1)==0);
}
static uint8_t end(void){const uint8_t e=2;return call(&e,1);}
static void replace_during_playback(void)
{begin("new:d=4,o=4,b=120:p");assert(end()==1);stock_music_set_volume(33);}
int main(void)
{
    fail_init=1;assert(!stock_music_init(50));fail_init=0;
    fail_queue_create=true;assert(!stock_music_init(50));fail_queue_create=false;
    fail_task_create=true;assert(!stock_music_init(50));fail_task_create=false;
    assert(!queue_allocated&&sets==0&&commits==0);
    exists=true;stored_len=4;memcpy(stored,"bad!",4);
    assert(!stock_music_init(50));assert(sets==0);
    const char *boot="boot:d=4,o=4,b=120:c";
    memcpy(stored,boot,strlen(boot));stored_len=strlen(boot);
    fail_read=1;assert(!stock_music_init(50));fail_read=0;
    fail_queue_send=true;assert(!stock_music_init(50));fail_queue_send=false;
    assert(!queue_allocated);
    assert(stock_music_init(50));assert(queued_valid&&sets==0&&commits==0&&writes==0);
    run_worker();assert(samples==4000&&volume==50&&slept==1);
    assert(stock_music_init(60)); /* idempotent, no startup replay */
    uint8_t data[]={1,'c'}, e=2;
    assert(call(data,sizeof(data))==0x12);
    current=false;unsigned rejected_sets=sets;
    assert(call(&e,1)==0x12&&sets==rejected_sets);current=true;
    uint8_t too_large[]={0,1,4};assert(call(too_large,3)==0x15);
    begin("x::invalid");assert(end()==0x13&&sets==0);
    begin("x::c");
    assert(stock_music_handle(8,NULL,NULL,&e,1)==0x12);
    begin("x::c");stock_music_disconnect();assert(end()==0x12);
    for(unsigned stage=1;stage<=4;++stage){
        begin("cancel::c");unsigned before_sets=sets,before_commits=commits,before_sends=sends;
        cancel_stage=stage;assert(end()==0x12);cancel_stage=0;current=true;
        assert(sets==before_sets+(stage>=3));
        assert(commits==before_commits+(stage>=4));
        assert(sends==before_sends&&!queued_valid);
    }
    begin("cancel_failure::c");cancel_stage=1;fail_init=1;
    assert(end()==0x12);cancel_stage=0;fail_init=0;current=true;
    begin("cancel_failure::c");cancel_stage=2;fail_open=1;
    assert(end()==0x12);cancel_stage=0;fail_open=0;current=true;
    begin("failure::c");fail_set=1;assert(end()==0x16);fail_set=0;
    begin("failure::c");fail_commit=1;assert(end()==0x16);fail_commit=0;
    begin("durable::c");fail_queue_send=true;assert(end()==0x16);fail_queue_send=false;
    assert(stored_len==strlen("durable::c")&&!memcmp(stored,"durable::c",stored_len));
    begin("first:d=4,o=4,b=120:c");assert(end()==1);
    begin("latest:d=4,o=4,b=120:d");assert(end()==1);
    unsigned before_samples=samples;stock_music_set_volume(999);
    run_worker();assert(samples-before_samples==4000&&volume==100);
    begin("snapshot:d=4,o=4,b=120:c");assert(end()==1);
    begin("incomplete:d=1,o=4,b=1:c"); /* mutates upload, not queued snapshot */
    before_samples=samples;run_worker();assert(samples-before_samples==4000);
    stock_music_disconnect();
    uint8_t maximum[1024];memset(maximum,'x',sizeof(maximum));
    memcpy(maximum+sizeof(maximum)-3,"::c",3);
    const uint8_t max_begin[]={0,0,4};assert(call(max_begin,3)==0);
    for(size_t pos=0;pos<sizeof(maximum);){
        uint8_t part[256]={1};size_t count=sizeof(maximum)-pos;
        if(count>255)count=255;
        memcpy(part+1,maximum+pos,count);assert(call(part,count+1)==0);pos+=count;
    }
    assert(end()==1&&stored_len==1024&&!memcmp(stored,maximum,1024));
    before_samples=samples;run_worker();assert(samples-before_samples==952*8);
    begin("long:d=1,o=4,b=60:c");assert(end()==1);
    before_samples=samples;write_hook=replace_during_playback;run_worker();
    assert(samples-before_samples==256+4000&&volume==33);
    begin("io::c");assert(end()==1);fail_audio=1;unsigned before_writes=writes;
    run_worker();assert(writes==before_writes);fail_audio=0;
    begin("write_error::c");assert(end()==1);fail_write=1;before_writes=writes;
    unsigned before_sleep=slept;run_worker();
    assert(writes==before_writes+1&&slept==before_sleep+1);fail_write=0;
    begin("recover:d=4,o=4,b=120:c");assert(end()==1);before_samples=samples;
    run_worker();assert(samples-before_samples==4000);
    assert(opened==closed);
    puts("stock_music: storage, session cancellation, queue and worker PASS");
    return 0;
}
