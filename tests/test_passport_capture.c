#include "passport_capture.h"
#include "esp_partition.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_display_t display;
static lv_event_cb_t observer;
static lv_area_t requested;
static lv_draw_buf_t buffer;
static esp_partition_t partition={.size=0x40000};
static uint8_t flash[0x40000],pixels[240*40*2];
static unsigned locked,writes,maps,unmaps;
static bool lock_fail,write_fail,map_fail,registration_fail;
int64_t esp_timer_get_time(void){return 0;}
bool bsp_lvgl_lock(int timeout){(void)timeout;if(lock_fail)return false;assert(!locked);locked=1;return true;}
void bsp_lvgl_unlock(void){assert(locked);locked=0;}
const esp_partition_t *esp_partition_find_first(int type,int sub,const char *label)
{assert(type==1&&sub==255&&!strcmp(label,"screenshot"));return &partition;}
int esp_partition_erase_range(const esp_partition_t *p,size_t off,size_t len)
{assert(p==&partition&&!locked&&off==0&&len==0x26000);memset(flash,0xff,len);return 0;}
int esp_partition_write(const esp_partition_t *p,size_t off,const void *data,size_t len)
{assert(p==&partition&&!locked&&off+len<=sizeof(flash));if(write_fail)return -1;memcpy(flash+off,data,len);++writes;return 0;}
int esp_partition_mmap(const esp_partition_t *p,size_t off,size_t len,int mode,const void **data,unsigned *handle)
{assert(p==&partition&&!locked&&off==0&&len==153600&&mode==0);if(map_fail)return -1;*data=flash;*handle=7;++maps;return 0;}
void esp_partition_munmap(unsigned handle){assert(!locked&&handle==7);++unmaps;}
const void *lv_event_get_param(lv_event_t *e){return &e->area;}
lv_draw_buf_t *lv_display_get_buf_active(lv_display_t *d){assert(d==&display);return &buffer;}
int lv_display_get_color_format(lv_display_t *d){assert(d==&display);return 18;}
int lv_area_get_width(const lv_area_t *a){return a->x2-a->x1+1;}
void *lv_display_get_screen_active(lv_display_t *d){assert(locked&&d==&display);return d;}
void lv_obj_invalidate_area(void *s,const lv_area_t *a){assert(locked&&s==&display);requested=*a;}
uint32_t lv_display_get_event_count(lv_display_t *d){assert(locked&&d==&display);return observer?1:0;}
void lv_display_add_event_cb(lv_display_t *d,lv_event_cb_t cb,int code,void *user)
{assert(locked&&d==&display&&code==62&&!user);if(!registration_fail)observer=cb;}
uint32_t lv_display_remove_event_cb_with_user_data(lv_display_t *d,lv_event_cb_t cb,void *user)
{assert(locked&&d==&display&&cb==observer&&!user);observer=NULL;return 1;}
static void emit(int x1,int x2)
{
    assert(observer);lv_event_t e={.area={x1,requested.y1,x2,requested.y2}};
    unsigned stride=(x2-x1+1)*2;buffer=(lv_draw_buf_t){.header={stride},.data=pixels};
    for(int y=0;y<40;y++)for(int x=x1;x<=x2;x++){
        size_t pos=y*stride+(x-x1)*2;pixels[pos]=(uint8_t)x;pixels[pos+1]=(uint8_t)(requested.y1+y);
    }
    locked=1;observer(&e);locked=0;
}
static void capture(void)
{
    for(unsigned i=0;i<8;i++){
        assert(requested.y1==(int)i*40);
        emit(100,239);emit(100,239); /* duplicated partial flush must not count twice */
        assert(passport_capture_poll(i*10)==PASSPORT_CAPTURE_BUSY);
        emit(0,99);
        assert(passport_capture_poll(i*10)==(i==7?PASSPORT_CAPTURE_READY:PASSPORT_CAPTURE_BUSY));
    }
}
int main(void)
{
    size_t len=9;assert(!passport_capture_pixels(&len)&&len==0);
    assert(passport_capture_start(&display,0));assert(!passport_capture_start(&display,0));capture();
    const uint8_t *p=passport_capture_pixels(&len);assert(p==flash&&len==153600&&writes==8&&maps==1);
    for(unsigned y=0;y<320;y++)for(unsigned x=0;x<240;x++)assert(p[(y*240+x)*2]==(uint8_t)x&&p[(y*240+x)*2+1]==(uint8_t)y);
    assert(passport_capture_release()&&unmaps==1);
    assert(passport_capture_start(&display,0));emit(0,100);
    assert(passport_capture_poll(3000)==PASSPORT_CAPTURE_ERROR&&!passport_capture_pixels(&len));
    assert(passport_capture_release());
    assert(passport_capture_start(&display,0));emit(0,239);write_fail=true;
    assert(passport_capture_poll(1)==PASSPORT_CAPTURE_ERROR);write_fail=false;assert(passport_capture_release());
    assert(passport_capture_start(&display,0));lock_fail=true;assert(!passport_capture_release());lock_fail=false;
    assert(passport_capture_release()&&!observer);
    registration_fail=true;assert(!passport_capture_start(&display,0));registration_fail=false;
    partition.readonly=true;assert(!passport_capture_start(&display,0));partition.readonly=false;
    assert(passport_capture_start(&display,0));map_fail=true;
    for(unsigned i=0;i<8;i++){emit(0,239);passport_capture_poll(i);}
    assert(passport_capture_poll(9)==PASSPORT_CAPTURE_ERROR&&!passport_capture_pixels(&len));
    assert(passport_capture_release());
    puts("Passport strip capture/lifecycle/flash failures: PASS");
}
