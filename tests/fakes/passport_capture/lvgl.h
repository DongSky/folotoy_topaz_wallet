#pragma once
#include <stdint.h>
#define LV_EVENT_FLUSH_START 62
#define LV_COLOR_FORMAT_RGB565 18
typedef struct _lv_display_t {int unused;} lv_display_t;
typedef struct {int32_t x1,y1,x2,y2;} lv_area_t;
typedef struct {struct {uint32_t stride;} header;uint8_t *data;} lv_draw_buf_t;
typedef struct {lv_area_t area;} lv_event_t;
typedef void (*lv_event_cb_t)(lv_event_t *event);
const void *lv_event_get_param(lv_event_t *event);
lv_draw_buf_t *lv_display_get_buf_active(lv_display_t *d);
int lv_display_get_color_format(lv_display_t *d);
int lv_area_get_width(const lv_area_t *area);
void *lv_display_get_screen_active(lv_display_t *d);
void lv_obj_invalidate_area(void *screen,const lv_area_t *area);
uint32_t lv_display_get_event_count(lv_display_t *d);
void lv_display_add_event_cb(lv_display_t *d,lv_event_cb_t cb,int code,void *user);
uint32_t lv_display_remove_event_cb_with_user_data(lv_display_t *d,lv_event_cb_t cb,void *user);
