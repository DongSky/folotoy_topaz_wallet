#include "stock_profile_nvs.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t stored[300], pending[300];
static size_t stored_length, pending_length;
static int init_error, open_error, get_error, set_error, commit_error;
static bool change_length;
static unsigned init_count, open_count, close_count, get_count, set_count, commit_count;
static nvs_open_mode_t opened_mode;
static bool save_live;
static enum { CANCEL_NONE, CANCEL_INIT, CANCEL_OPEN, CANCEL_SET, CANCEL_COMMIT } cancel_at;
static bool save_guard(void *context)
{ assert(context==&save_live);return save_live; }
static void reset(void)
{
    init_error=open_error=get_error=set_error=commit_error=0;
    init_count=open_count=close_count=get_count=set_count=commit_count=0;
    change_length=false;save_live=true;cancel_at=CANCEL_NONE;
}
esp_err_t nvs_flash_init(void) { ++init_count;if(cancel_at==CANCEL_INIT)save_live=false; return init_error; }
esp_err_t nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *handle)
{
    assert(!strcmp(name,"trae_cfg")); ++open_count; opened_mode=mode;
    *handle=123;if(cancel_at==CANCEL_OPEN)save_live=false; return open_error;
}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *output,size_t *length)
{
    assert(handle==123 && !strcmp(key,"profile") && opened_mode==NVS_READONLY);
    ++get_count;
    if(get_error) return get_error;
    if(output) { assert(*length>=stored_length); memcpy(output,stored,stored_length); }
    *length=stored_length + (change_length && output ? 1 : 0);
    return 0;
}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t length)
{
    assert(handle==123 && !strcmp(key,"profile") && opened_mode==NVS_READWRITE);
    assert(length==245); ++set_count;
    if(set_error) return set_error;
    memcpy(pending,value,length); pending_length=length;
    /* IDF can persist directly at set, so model the conservative real contract. */
    memcpy(stored,value,length);stored_length=length;
    if(cancel_at==CANCEL_SET)save_live=false;
    return 0;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle==123); ++commit_count;
    if(commit_error) return commit_error;
    memcpy(stored,pending,pending_length); stored_length=pending_length;
    if(cancel_at==CANCEL_COMMIT)save_live=false;return 0;
}
void nvs_close(nvs_handle_t handle) { assert(handle==123); ++close_count; }

int main(void)
{
    stock_profile_t p, before;
    stock_profile_defaults(&p);
    reset();
    assert(stock_profile_nvs_save(&p)==STOCK_PROFILE_STORE_OK);
    assert(init_count==1 && open_count==1 && set_count==1 && commit_count==1 && close_count==1);
    assert(stored_length==245 && stored[0]==16 && stored[177]==82);
    memset(&p,0xff,sizeof(p)); reset(); bool migrated=true;
    assert(stock_profile_nvs_load(&p,&migrated)==STOCK_PROFILE_STORE_OK && !migrated);
    assert(p.bytes[176]==82 && get_count==2 && close_count==1 && !set_count && !commit_count);
    memset(stored,0,sizeof(stored)); stored[0]=13; stored_length=197;
    memcpy(stored+1,"Legacy",7); stored[173]=10; stored[193]=0x81; stored[194]=0x72;
    reset();
    assert(stock_profile_nvs_load(&p,&migrated)==STOCK_PROFILE_STORE_OK && migrated);
    assert(!strcmp((char *)p.bytes,"Legacy") && p.bytes[204]==5 && p.bytes[224]==0x81);
    assert(stored[0]==13 && stored_length==197 && !set_count && !commit_count);
    before=p;
    reset(); init_error=0x110d; /* no-free-pages must NOT trigger erase */
    assert(stock_profile_nvs_load(&p,&migrated)==STOCK_PROFILE_STORE_IO_ERROR);
    assert(!migrated && !open_count && !memcmp(&p,&before,sizeof(p)));
    reset(); open_error=ESP_ERR_NVS_NOT_FOUND;
    assert(stock_profile_nvs_load(&p,NULL)==STOCK_PROFILE_STORE_NOT_FOUND && !close_count);
    reset(); get_error=ESP_ERR_NVS_NOT_FOUND;
    assert(stock_profile_nvs_load(&p,NULL)==STOCK_PROFILE_STORE_NOT_FOUND && close_count==1);
    reset(); get_error=ESP_ERR_NVS_TYPE_MISMATCH;
    assert(stock_profile_nvs_load(&p,NULL)==STOCK_PROFILE_STORE_INVALID_DATA && close_count==1);
    reset(); get_error=99;
    assert(stock_profile_nvs_load(&p,NULL)==STOCK_PROFILE_STORE_IO_ERROR && close_count==1);
    reset(); stored_length=300;
    assert(stock_profile_nvs_load(&p,NULL)==STOCK_PROFILE_STORE_INVALID_DATA && get_count==1);
    reset(); stored_length=0;
    assert(stock_profile_nvs_load(&p,NULL)==STOCK_PROFILE_STORE_INVALID_DATA && get_count==1);
    reset(); stored_length=197; change_length=true;
    assert(stock_profile_nvs_load(&p,NULL)==STOCK_PROFILE_STORE_INVALID_DATA);
    reset(); stored[0]=99;
    assert(stock_profile_nvs_load(&p,NULL)==STOCK_PROFILE_STORE_INVALID_DATA);
    assert(!memcmp(&p,&before,sizeof(p)));
    reset(); set_error=99;
    assert(stock_profile_nvs_save(&p)==STOCK_PROFILE_STORE_IO_ERROR && !commit_count && close_count==1);
    reset(); commit_error=99;
    assert(stock_profile_nvs_save(&p)==STOCK_PROFILE_STORE_IO_ERROR && commit_count==1 && close_count==1);
    reset(); init_error=99;
    assert(stock_profile_nvs_save(&p)==STOCK_PROFILE_STORE_IO_ERROR && !open_count);
    reset(); open_error=99;
    assert(stock_profile_nvs_save(&p)==STOCK_PROFILE_STORE_IO_ERROR && !close_count && !set_count);
    reset();
    assert(stock_profile_nvs_load(NULL,&migrated)==STOCK_PROFILE_STORE_INVALID_ARG && !migrated);
    assert(stock_profile_nvs_save(NULL)==STOCK_PROFILE_STORE_INVALID_ARG && !init_count);
    reset();save_live=false;
    assert(stock_profile_nvs_save_guarded(&p,save_guard,&save_live)==STOCK_PROFILE_STORE_CANCELLED);
    assert(!init_count&&!open_count&&!set_count&&!commit_count&&!close_count);
    reset();cancel_at=CANCEL_INIT;
    assert(stock_profile_nvs_save_guarded(&p,save_guard,&save_live)==STOCK_PROFILE_STORE_CANCELLED);
    assert(init_count==1&&!open_count&&!set_count&&!commit_count&&!close_count);
    reset();cancel_at=CANCEL_OPEN;
    assert(stock_profile_nvs_save_guarded(&p,save_guard,&save_live)==STOCK_PROFILE_STORE_CANCELLED);
    assert(init_count==1&&open_count==1&&!set_count&&!commit_count&&close_count==1);
    reset();cancel_at=CANCEL_SET;
    strcpy((char*)p.bytes,"CancelledDuringSet");
    assert(stock_profile_nvs_save_guarded(&p,save_guard,&save_live)==STOCK_PROFILE_STORE_CANCELLED);
    assert(set_count==1&&!commit_count&&close_count==1);
    assert(!strcmp((char*)stored+1,"CancelledDuringSet")); /* no rollback promise */
    reset();cancel_at=CANCEL_COMMIT;
    strcpy((char*)p.bytes,"CancelledDuringCommit");
    assert(stock_profile_nvs_save_guarded(&p,save_guard,&save_live)==STOCK_PROFILE_STORE_CANCELLED);
    assert(set_count==1&&commit_count==1&&close_count==1);
    assert(!strcmp((char*)stored+1,"CancelledDuringCommit"));
    reset();
    assert(stock_profile_nvs_save_guarded(&p,save_guard,&save_live)==STOCK_PROFILE_STORE_OK);
    assert(set_count==1&&commit_count==1&&close_count==1);
    puts("Stock profile NVS boundary/fault/session tests: PASS");
}
