#include "stock_screenshot_session.h"
#include <string.h>
#define FRAME_SIZE (240u * 320u * 2u)
static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static stock_shot_action_t response(uint8_t status) { return (stock_shot_action_t){.reply=true,.status=status}; }
stock_shot_action_t stock_shot_command(stock_shot_t *s, const uint8_t *p,
    size_t n, uint16_t id, uint64_t now)
{
    if (!s || !p || !n || (p[0]==1?n!=1:p[0]==2?n!=5:(p[0]==3||p[0]==4)?n!=3:true)) return response(0x10);
    if (p[0] == 1) {
        if (s->phase >= STOCK_SHOT_CAPTURING && s->phase <= STOCK_SHOT_END) return response(0x1b);
        if (!id) return response(0x1c);
        *s=(stock_shot_t){.phase=STOCK_SHOT_CAPTURING,.session=id,.deadline=now+30000};
        return (stock_shot_action_t){.capture=true,.release=true};
    }
    uint16_t session=le16(p+1);
    if (!session || session != s->session) return response(0x12);
    if (p[0] == 2) {
        if (!s->awaiting_ack || le16(p+3)!=s->sequence) return response(0x12);
        s->awaiting_ack=false; s->retries=0; s->packet_length=0; ++s->sequence;
        if (s->phase==STOCK_SHOT_BEGIN) s->phase=STOCK_SHOT_DATA;
        else if (s->phase==STOCK_SHOT_DATA) {
            s->offset += s->packet_pixels; ++s->packets;
            if (s->offset == FRAME_SIZE) s->phase=STOCK_SHOT_END;
        } else if (s->phase==STOCK_SHOT_END) {
            s->phase=STOCK_SHOT_COMPLETE; s->completed_at=now; s->pixels=NULL;
            return (stock_shot_action_t){.reply=true,.status=1,.release=true};
        } else return response(0x12);
        return (stock_shot_action_t){0};
    }
    bool recent=s->phase==STOCK_SHOT_COMPLETE && now-s->completed_at<300000;
    bool active=s->phase>=STOCK_SHOT_CAPTURING && s->phase<=STOCK_SHOT_END;
    if (!recent && !(p[0]==4 && active)) return response(0x12);
    memset(s,0,sizeof(*s));
    return (stock_shot_action_t){.reply=true,.status=1,.release=true};
}
bool stock_shot_captured(stock_shot_t *s,const uint8_t *pixels,size_t size)
{
    if (!s || s->phase!=STOCK_SHOT_CAPTURING || !pixels || size!=FRAME_SIZE) return false;
    s->pixels=pixels;s->pixels_size=size;s->crc=stock_screenshot_crc32(0,pixels,size);s->phase=STOCK_SHOT_BEGIN;
    return true;
}
size_t stock_shot_packet(stock_shot_t *s,size_t capacity,uint64_t now,const uint8_t **out)
{
    if (out) *out=NULL;
    if (!s || !out || s->phase<STOCK_SHOT_BEGIN || s->phase>STOCK_SHOT_END) return 0;
    if (s->awaiting_ack && now<s->deadline) return 0;
    if (s->awaiting_ack && s->retries>=3) return 0;
    if (s->packet_length) { if (s->packet_length>capacity) return 0; *out=s->packet;return s->packet_length; }
    if (s->phase==STOCK_SHOT_BEGIN) s->packet_length=stock_screenshot_begin(s->session,s->sequence,240,320,s->packet,capacity<sizeof(s->packet)?capacity:sizeof(s->packet));
    else if (s->phase==STOCK_SHOT_END) s->packet_length=stock_screenshot_end(s->session,s->sequence,s->packets,FRAME_SIZE,s->crc,s->packet,capacity<sizeof(s->packet)?capacity:sizeof(s->packet));
    else {
        if (capacity<=17) return 0;
        size_t count=(capacity-17)&~(size_t)1;
        if (count>500) count=500;
        if (count>FRAME_SIZE-s->offset) count=FRAME_SIZE-s->offset;
        if (!count) return 0;
        s->packet_pixels=count;
        s->packet_length=stock_screenshot_data(s->session,s->sequence,0,0,240,320,s->offset,
            s->pixels+s->offset,count,s->packet,sizeof(s->packet));
    }
    if (s->packet_length) *out=s->packet;
    return s->packet_length;
}
void stock_shot_mark_sent(stock_shot_t *s,uint64_t now)
{
    if (!s || !s->packet_length) return;
    if (s->awaiting_ack) ++s->retries;
    s->awaiting_ack=true;s->deadline=now+3000;
}
stock_shot_action_t stock_shot_poll(stock_shot_t *s,uint64_t now)
{
    if (s && ((s->awaiting_ack && now>=s->deadline && s->retries>=3) ||
              (s->phase==STOCK_SHOT_CAPTURING && now>=s->deadline))) {
        s->phase=STOCK_SHOT_FAILED;s->pixels=NULL;s->awaiting_ack=false;
        return (stock_shot_action_t){.reply=true,.status=0x11,.release=true};
    }
    return (stock_shot_action_t){0};
}
