#include "stock_content.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static esp_partition_t partitions[2];
static uint8_t flash[2][0x20000];
static stock_profile_t persisted;
static stock_profile_store_result_t load_result, save_result;
static unsigned erases, writes, saves, maps, unmaps;
static bool fail_write, fail_erase;
static size_t erased_length;
static bool session_live, cancel_erase, cancel_data, cancel_metadata, cancel_save;
static unsigned guard_checks, reject_guard_check;
static bool session_guard(void *context, uint32_t session)
{
    assert(context == &session_live);
    assert(session != 0);
    ++guard_checks;
    if (reject_guard_check && guard_checks == reject_guard_check) session_live = false;
    return session_live;
}
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype, const char *label)
{
    assert(type==ESP_PARTITION_TYPE_DATA && subtype==ESP_PARTITION_SUBTYPE_ANY);
    if(!strcmp(label,"imguser"))return &partitions[0];
    assert(!strcmp(label,"imgstore"));return &partitions[1];
}
esp_err_t esp_partition_read(const esp_partition_t *p,size_t offset,void *data,size_t len)
{
    assert(offset<=p->size && len<=p->size-offset);memcpy(data,flash[p->index]+offset,len);return ESP_OK;
}
esp_err_t esp_partition_write(const esp_partition_t *p,size_t offset,const void *data,size_t len)
{
    assert(!p->readonly && offset<=p->size && len<=p->size-offset);++writes;
    if(fail_write)return -1;
    const uint8_t *in=data;
    for(size_t i=0;i<len;i++){assert((flash[p->index][offset+i]&in[i])==in[i]);flash[p->index][offset+i]&=in[i];}
    if ((offset == 0 && cancel_metadata) || (offset != 0 && cancel_data)) session_live=false;
    return ESP_OK;
}
esp_err_t esp_partition_erase_range(const esp_partition_t *p,size_t offset,size_t len)
{
    assert(!p->readonly && offset==0 && len<=p->size && len%4096==0);++erases;erased_length=len;
    if(fail_erase)return -1;
    memset(flash[p->index]+offset,0xff,len);if(cancel_erase)session_live=false;return ESP_OK;
}
esp_err_t esp_partition_mmap(const esp_partition_t *p,size_t offset,size_t len,esp_partition_mmap_memory_t memory,const void **data,esp_partition_mmap_handle_t *handle)
{
    assert(memory==ESP_PARTITION_MMAP_DATA && offset==4096 && len<=p->size-offset);
    *data=flash[p->index]+offset;*handle=p->index+1;++maps;return ESP_OK;
}
void esp_partition_munmap(esp_partition_mmap_handle_t handle){assert(handle==1||handle==2);++unmaps;}
stock_profile_store_result_t stock_profile_nvs_load(stock_profile_t *out,bool *migrated)
{
    if (migrated) *migrated = false;
    if (load_result == STOCK_PROFILE_STORE_OK) *out = persisted;
    return load_result;
}
stock_profile_store_result_t stock_profile_nvs_save_guarded(const stock_profile_t *in,
    stock_profile_save_guard_t guard, void *context)
{
    if (guard && !guard(context)) return STOCK_PROFILE_STORE_CANCELLED;
    ++saves;
    if(save_result==STOCK_PROFILE_STORE_OK)persisted=*in;
    if(cancel_save)session_live=false;
    if (guard && !guard(context)) return STOCK_PROFILE_STORE_CANCELLED;
    return save_result;
}
stock_profile_store_result_t stock_profile_nvs_save(const stock_profile_t *in)
{return stock_profile_nvs_save_guarded(in,NULL,NULL);}
static void setup(stock_content_t *c)
{
    partitions[0]=(esp_partition_t){.size=sizeof(flash[0]),.index=0};
    partitions[1]=(esp_partition_t){.size=sizeof(flash[1]),.index=1};
    memset(flash,0x5a,sizeof(flash));stock_profile_defaults(&persisted);
    load_result=save_result=STOCK_PROFILE_STORE_OK;
    erases=writes=saves=maps=unmaps=0;fail_write=fail_erase=false;
    session_live=true;cancel_erase=cancel_data=cancel_metadata=cancel_save=false;
    guard_checks=reject_guard_check=0;
    assert(stock_content_init(c)==STOCK_PROFILE_STORE_OK);
    assert(erases==0&&writes==0&&saves==0);
}
static stock_content_result_t command(stock_content_t *c,const char *json)
{return stock_content_handle(c,1,(const uint8_t*)json,strlen(json),false);}
static stock_content_result_t begin(stock_content_t *c,uint32_t len,bool busy)
{
    uint8_t p[5]={0,(uint8_t)len,(uint8_t)(len>>8),(uint8_t)(len>>16),(uint8_t)(len>>24)};
    return stock_content_handle(c,2,p,sizeof(p),busy);
}
static stock_content_result_t finish(stock_content_t *c)
{uint8_t p=2;return stock_content_handle(c,2,&p,1,false);}
static void test_profile_atomic_save_and_errors(void)
{
    stock_content_t c;setup(&c);
    assert(command(&c,"broken").response[1]==0x13);
    assert(command(&c,"{}").response[1]==0x14);
    stock_content_result_t result=command(&c,"{\"nickname\":\"Synthetic\",\"img_mode\":\"fullscreen\"}");
    assert(result.response[1]==1&&result.event==STOCK_CONTENT_PROFILE&&result.changes.img_mode);
    assert(saves==1&&c.profile.bytes[STOCK_PROFILE_IMG_MODE]==1);
    stock_profile_t before=c.profile;save_result=STOCK_PROFILE_STORE_IO_ERROR;
    assert(command(&c,"{\"nickname\":\"NotSaved\"}").response[1]==0x16);
    assert(!memcmp(&before,&c.profile,sizeof(before)));
    save_result=STOCK_PROFILE_STORE_OK;
    assert(command(&c,"{\"avatar_name\":\"unknown\"}").response[1]==1);
    assert(!strcmp((char*)c.profile.bytes+STOCK_PROFILE_AVATAR_NAME,"default"));
}
static void test_avatar_upload_metadata_and_mapping_lease(void)
{
    stock_content_t c;setup(&c);
    assert(begin(&c,4,false).response[1]==0&&erases==1&&erased_length==8192);
    assert(flash[0][0]==0xff&&flash[1][0]==0x5a);
    stock_content_image_t image={0};assert(!stock_content_image_acquire(&c,STOCK_CONTENT_AVATAR,&image));
    uint8_t data[]={1,0xff,0xd8,0xff,0xd9};
    assert(stock_content_handle(&c,2,data,sizeof(data),false).response[1]==0);
    assert(flash[0][0]==0xff); /* No valid metadata before END. */
    stock_content_result_t result=finish(&c);
    assert(result.response[1]==0&&result.event==STOCK_CONTENT_AVATAR_READY);
    assert(!memcmp(flash[0],"JPG1\4\0\0\0",8));
    assert(!memcmp(flash[0]+4096,data+1,4));
    assert(c.profile.bytes[STOCK_PROFILE_AVATAR_NAME]==0&&saves==1);
    assert(stock_content_image_acquire(&c,STOCK_CONTENT_AVATAR,&image));
    assert(image.length==4&&!memcmp(image.jpeg,data+1,4));
    assert(begin(&c,4,false).response[1]==0x1b&&erases==1);
    stock_content_image_release(&image);stock_content_image_release(&image);
    assert(maps==1&&unmaps==1&&c.leases[0]==0);
    assert(begin(&c,4,false).response[1]==0&&erases==2);
}
static void test_fullscreen_mode_locked_and_disconnect_no_commit(void)
{
    stock_content_t c;setup(&c);
    assert(command(&c,"{\"img_mode\":\"fullscreen\"}").response[1]==1);
    assert(begin(&c,1,false).response[1]==0&&flash[1][0]==0xff&&flash[0][0]==0x5a);
    assert(command(&c,"{\"img_mode\":\"avatar\"}").response[1]==0x1b);
    uint8_t data[]={1,0x12};
    assert(stock_content_handle(&c,2,data,2,false).response[1]==0);
    stock_content_result_t result=finish(&c);
    assert(result.event==STOCK_CONTENT_FULLSCREEN_READY&&result.response[1]==0);
    assert(!memcmp(flash[1],"JPG1\1\0\0\0",8));
    assert(begin(&c,1,false).response[1]==0);
    unsigned previous_writes=writes;stock_content_disconnect(&c);
    assert(writes==previous_writes&&flash[1][0]==0xff&&finish(&c).response[1]==0x12);
}
static void test_bounds_busy_and_failure_do_not_commit(void)
{
    stock_content_t c;setup(&c);
    assert(begin(&c,4,true).response[1]==0x1b&&erases==0);
    assert(begin(&c,0x20000,false).response[1]==0x15&&erases==0);
    partitions[0].readonly=true;
    assert(begin(&c,1,false).response[1]==0x16&&erases==0);partitions[0].readonly=false;
    fail_erase=true;assert(begin(&c,1,false).response[1]==0x16);fail_erase=false;
    assert(begin(&c,2,false).response[1]==0);
    uint8_t data[]={1,0x42};assert(stock_content_handle(&c,2,data,2,false).response[1]==0);
    assert(finish(&c).response[1]==0x12&&flash[0][0]==0xff);
    assert(begin(&c,1,false).response[1]==0);fail_write=true;
    assert(stock_content_handle(&c,2,data,2,false).response[1]==0x16);
    assert(finish(&c).response[1]==0x12&&flash[0][0]==0xff);
    assert(stock_content_handle(&c,3,data,2,false).response[1]==0x18);
}
static void test_end_io_and_avatar_profile_commit_failures(void)
{
    stock_content_t c;setup(&c);
    uint8_t data[]={1,0x42};
    assert(begin(&c,1,false).response[1]==0);
    assert(stock_content_handle(&c,2,data,2,false).response[1]==0);
    fail_write=true;
    stock_content_result_t result=finish(&c);
    assert(result.response[1]==0x16&&result.event==STOCK_CONTENT_NONE&&flash[0][0]==0xff);
    fail_write=false;
    assert(begin(&c,1,false).response[1]==0);
    assert(stock_content_handle(&c,2,data,2,false).response[1]==0);
    save_result=STOCK_PROFILE_STORE_IO_ERROR;
    result=finish(&c);
    assert(result.response[1]==0x16&&result.event==STOCK_CONTENT_NONE);
    assert(!memcmp(flash[0],"JPG1",4)); /* Bytes committed; selection persistence failed. */
    assert(!strcmp((char*)c.profile.bytes+STOCK_PROFILE_AVATAR_NAME,"default"));
}
static void test_init_errors_and_header_bounds(void)
{
    stock_content_t c;setup(&c);load_result=STOCK_PROFILE_STORE_INVALID_DATA;
    assert(stock_content_init(&c)==STOCK_PROFILE_STORE_INVALID_DATA&&!c.initialized);
    assert(begin(&c,1,false).response[1]==0x16&&erases==0);
    load_result=STOCK_PROFILE_STORE_NOT_FOUND;
    assert(stock_content_init(&c)==STOCK_PROFILE_STORE_NOT_FOUND&&c.initialized&&saves==0);
    stock_content_image_t image={0};assert(!stock_content_image_acquire(&c,STOCK_CONTENT_AVATAR,&image));
    memcpy(flash[0],"JPG1\xff\xff\xff\xff",8);
    assert(!stock_content_image_acquire(&c,STOCK_CONTENT_AVATAR,&image)&&maps==0);
    memcpy(flash[0],"JPG1\0\0\0\0",8);
    assert(!stock_content_image_acquire(&c,STOCK_CONTENT_AVATAR,&image)&&maps==0);
}
static stock_content_result_t guarded(stock_content_t *c, uint32_t session,
    uint8_t type, const uint8_t *payload, size_t length)
{
    return stock_content_handle_guarded(c,session,session_guard,&session_live,type,payload,length,false);
}
static void test_empty_dispatch_statuses(void)
{
    stock_content_t c;setup(&c);
    assert(stock_content_handle(&c,1,NULL,0,false).response[1]==0x13);
    assert(stock_content_handle(&c,1,(const uint8_t*)"",0,false).response[1]==0x13);
    assert(stock_content_handle(&c,2,NULL,0,false).response[1]==0x12);
    for(unsigned type=0;type<256;type++)if(type!=1&&type!=2)
        assert(stock_content_handle(&c,(uint8_t)type,NULL,0,false).response[1]==0x18);
    assert(erases==0&&writes==0&&saves==0);
}
static void test_session_change_including_overflow_resets_transfer(void)
{
    stock_content_t c;setup(&c);
    uint8_t start[]={0,1,0,0,0},data[]={1,0x42},end=2;
    assert(guarded(&c,100,2,start,sizeof(start)).response[1]==0&&c.transfer.active);
    assert(guarded(&c,101,2,data,sizeof(data)).response[1]==0x12&&!c.transfer.active);
    assert(guarded(&c,101,2,&end,1).response[1]==0x12&&writes==0);
    assert(guarded(&c,101,2,start,sizeof(start)).response[1]==0);
    /* Generation change is sufficient even without a disconnect callback. */
    assert(guarded(&c,102,1,(const uint8_t*)"{}",2).response[1]==0x14&&!c.transfer.active);
    assert(guarded(&c,102,2,&end,1).response[1]==0x12&&writes==0);
    assert(guarded(&c,102,2,start,sizeof(start)).response[1]==0);
    session_live=false;
    assert(guarded(&c,102,2,data,sizeof(data)).response[1]==0x12&&!c.transfer.active);
    assert(writes==0);
    session_live=true;
    assert(guarded(&c,103,2,data,sizeof(data)).response[1]==0x12);
}
static void test_disconnect_during_erase_and_data_blocks_later_writes(void)
{
    stock_content_t c;setup(&c);
    uint8_t start[]={0,1,0,0,0},data[]={1,0x42},end=2;
    cancel_erase=true;
    stock_content_result_t out=guarded(&c,1,2,start,sizeof(start));
    assert(out.response[1]==0x12&&out.event==STOCK_CONTENT_NONE&&!c.transfer.active);
    assert(erases==1&&writes==0&&flash[0][0]==0xff);
    session_live=true;cancel_erase=false;
    assert(guarded(&c,2,2,&end,1).response[1]==0x12);
    assert(guarded(&c,2,2,start,sizeof(start)).response[1]==0);
    cancel_data=true;
    out=guarded(&c,2,2,data,sizeof(data));
    assert(out.response[1]==0x12&&!c.transfer.active&&writes==1&&flash[0][0]==0xff);
    assert(guarded(&c,2,2,&end,1).response[1]==0x12&&writes==1);
}
static void test_disconnect_before_and_during_metadata_has_no_success_event(void)
{
    stock_content_t c;setup(&c);
    uint8_t start[]={0,1,0,0,0},data[]={1,0x42},end=2;
    assert(guarded(&c,1,2,start,sizeof(start)).response[1]==0);
    assert(guarded(&c,1,2,data,sizeof(data)).response[1]==0);
    guard_checks=0;reject_guard_check=2; /* entry passes; pre-metadata guard fails */
    stock_content_result_t out=guarded(&c,1,2,&end,1);
    assert(out.response[1]==0x12&&out.event==STOCK_CONTENT_NONE);
    assert(writes==1&&saves==0&&flash[0][0]==0xff&&!c.transfer.active);
    setup(&c);
    assert(guarded(&c,2,2,start,sizeof(start)).response[1]==0);
    assert(guarded(&c,2,2,data,sizeof(data)).response[1]==0);
    cancel_metadata=true;
    out=guarded(&c,2,2,&end,1);
    assert(out.response[1]==0x12&&out.event==STOCK_CONTENT_NONE&&saves==0);
    assert(!memcmp(flash[0],"JPG1",4)); /* Already-issued metadata cannot be rolled back. */
}
static void test_profile_guard_suppresses_stale_publication(void)
{
    stock_content_t c;setup(&c);
    stock_profile_t before=c.profile;
    const char *json="{\"nickname\":\"NewName\"}";
    guard_checks=0;reject_guard_check=2; /* fake adapter's pre-save guard */
    stock_content_result_t out=guarded(&c,1,1,(const uint8_t*)json,strlen(json));
    assert(out.response[1]==0x12&&out.event==STOCK_CONTENT_NONE&&saves==0);
    assert(!memcmp(&before,&c.profile,sizeof(before)));
    session_live=true;reject_guard_check=0;cancel_save=true;
    out=guarded(&c,2,1,(const uint8_t*)json,strlen(json));
    assert(out.response[1]==0x12&&out.event==STOCK_CONTENT_NONE&&saves==1);
    assert(!memcmp(&before,&c.profile,sizeof(before)));
    assert(!strcmp((char*)persisted.bytes,"NewName")); /* Cancellation is not rollback. */
}
int main(void)
{
    test_profile_atomic_save_and_errors();test_avatar_upload_metadata_and_mapping_lease();
    test_fullscreen_mode_locked_and_disconnect_no_commit();test_bounds_busy_and_failure_do_not_commit();
    test_end_io_and_avatar_profile_commit_failures();test_init_errors_and_header_bounds();
    test_empty_dispatch_statuses();test_session_change_including_overflow_resets_transfer();
    test_disconnect_during_erase_and_data_blocks_later_writes();
    test_disconnect_before_and_during_metadata_has_no_success_event();
    test_profile_guard_suppresses_stale_publication();puts("Stock content: PASS (synthetic flash only)");return 0;
}
