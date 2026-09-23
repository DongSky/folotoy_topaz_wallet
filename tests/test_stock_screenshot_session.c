#include "stock_screenshot_session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t pixels[240*320*2];
static uint16_t u16(const uint8_t *p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t u32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static stock_shot_action_t command(stock_shot_t *s,uint8_t op,uint16_t id,uint16_t seq,uint64_t now){
    uint8_t p[]={op,id,id>>8,seq,seq>>8};
    return stock_shot_command(s,p,op==1?1:op==2?5:3,7,now);
}
static void begin(stock_shot_t *s){
    stock_shot_action_t a=command(s,1,0,0,0);assert(a.capture&&a.release&&!a.reply);
    assert(!stock_shot_captured(s,pixels,sizeof(pixels)-1));
    assert(stock_shot_captured(s,pixels,sizeof(pixels)));
}
int main(void){
    stock_shot_t s={0}; const uint8_t *p=NULL;
    assert(stock_shot_command(&s,NULL,0,7,0).status==0x10);
    uint8_t malformed[]={2,7,0};assert(stock_shot_command(&s,malformed,3,7,0).status==0x10);
    assert(command(&s,4,7,0,0).status==0x12);
    begin(&s);assert(command(&s,1,0,0,0).status==0x1b);
    assert(!stock_shot_packet(&s,13,0,&p));
    assert(stock_shot_packet(&s,20,0,&p)==14&&p[0]==1&&u16(p+1)==7);
    uint8_t saved[517];memcpy(saved,p,14);stock_shot_mark_sent(&s,0);
    assert(command(&s,2,8,0,1).status==0x12&&s.awaiting_ack);
    assert(command(&s,2,7,1,1).status==0x12&&s.awaiting_ack);
    assert(!stock_shot_packet(&s,20,2999,&p));
    for(unsigned i=1;i<=3;i++){
        assert(stock_shot_packet(&s,20,i*3000,&p)==14&&!memcmp(saved,p,14));
        stock_shot_mark_sent(&s,i*3000);
    }
    assert(!stock_shot_packet(&s,20,12000,&p));
    stock_shot_action_t a=stock_shot_poll(&s,12000);assert(a.reply&&a.release&&a.status==0x11);
    assert(!stock_shot_poll(&s,12001).reply);
    s=(stock_shot_t){0};begin(&s);
    unsigned packets=0;uint32_t offset=0;
    for(unsigned i=0;i<sizeof(pixels);i++)pixels[i]=(uint8_t)i;
    /* Recompute capture CRC after the immutable frame is prepared. */
    s=(stock_shot_t){0};begin(&s);
    while(s.phase!=STOCK_SHOT_COMPLETE){
        size_t n=stock_shot_packet(&s,244,0,&p);assert(n&&n<=244);
        if(p[0]==2){assert(u32(p+13)==offset);assert(!memcmp(p+17,pixels+offset,n-17));offset+=n-17;packets++;}
        if(p[0]==3){assert(u16(p+5)==packets&&u32(p+7)==sizeof(pixels));assert(u32(p+11)==stock_screenshot_crc32(0,pixels,sizeof(pixels)));}
        uint16_t seq=u16(p+3);stock_shot_mark_sent(&s,1);
        a=command(&s,2,7,seq,2);
        if(s.phase==STOCK_SHOT_COMPLETE)assert(a.reply&&a.release&&a.status==1);
        else assert(!a.reply);
        assert(command(&s,2,7,seq,3).status==0x12);
    }
    assert(offset==sizeof(pixels)&&!s.pixels);
    assert(command(&s,3,7,0,300002).status==0x12);
    assert(command(&s,3,7,0,300001).status==1&&s.phase==STOCK_SHOT_IDLE);
    begin(&s);a=command(&s,4,7,0,1);assert(a.release&&a.status==1&&s.phase==STOCK_SHOT_IDLE);
    /* Original counters are uint16, including default MTU23 wraparound. */
    begin(&s);offset=0;packets=0;
    while(s.phase!=STOCK_SHOT_COMPLETE){
        size_t n=stock_shot_packet(&s,20,0,&p);assert(n&&n<=20);
        if(p[0]==2){assert(n==19&&u32(p+13)==offset);assert(!memcmp(p+17,pixels+offset,2));offset+=2;packets++;}
        if(p[0]==3){assert(packets==76800&&u16(p+5)==11264&&u16(p+3)==11265);assert(u32(p+7)==sizeof(pixels)&&u32(p+11)==stock_screenshot_crc32(0,pixels,sizeof(pixels)));}
        uint16_t seq=u16(p+3);stock_shot_mark_sent(&s,0);command(&s,2,7,seq,0);
    }
    assert(offset==sizeof(pixels));command(&s,3,7,0,0);
    command(&s,1,0,0,10);assert(!stock_shot_poll(&s,30009).reply);assert(stock_shot_poll(&s,30010).status==0x11);
    puts("Stock screenshot session tests PASS");return 0;
}
