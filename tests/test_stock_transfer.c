#include "stock_transfer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t bytes[32]; size_t used; uint32_t expected;
    bool committed; unsigned starts;
    stock_sink_result_t begin_error, data_error, end_error;
} store_t;
static stock_sink_result_t begin(void *arg, uint32_t total)
{
    store_t *s=arg;
    if(s->begin_error) return s->begin_error;
    s->used=0; s->expected=total;s->committed=false;s->starts++;
    return STOCK_SINK_OK;
}
static stock_sink_result_t data(void *arg,const uint8_t *p,size_t n)
{
    store_t *s=arg;
    if(s->data_error)return s->data_error;
    assert(n<=sizeof(s->bytes)-s->used);
    if(n)memcpy(s->bytes+s->used,p,n);
    s->used+=n;return STOCK_SINK_OK;
}
static stock_sink_result_t end(void *arg)
{
    store_t *s=arg;
    if(s->end_error)return s->end_error;
    s->committed=true;return STOCK_SINK_OK;
}
static void check(stock_transfer_t *s,stock_transfer_sink_t *sink,
                  const uint8_t *p,size_t n,uint8_t status,bool completed)
{
    stock_transfer_result_t r=stock_transfer_receive(s,sink,p,n);
    assert(r.status==status && r.completed==completed);
}
int main(void)
{
    const uint8_t ib[]={0,3,0,0,0},ab[]={0,3,0};
    const uint8_t chunk[]={1,'a','b','c'},done[]={2},empty[]={1};
    for(int kind=STOCK_TRANSFER_IMAGE;kind<=STOCK_TRANSFER_AUDIO;kind++) {
        store_t store={0};stock_transfer_sink_t sink={&store,begin,data,end};
        stock_transfer_t s;stock_transfer_init(&s,(stock_transfer_kind_t)kind,32);
        const uint8_t *b=kind==STOCK_TRANSFER_IMAGE?ib:ab;
        size_t bn=kind==STOCK_TRANSFER_IMAGE?sizeof(ib):sizeof(ab);
        check(&s,&sink,chunk,sizeof(chunk),0x12,false);
        check(&s,&sink,b,bn,0,false);assert(s.active);
        check(&s,&sink,empty,1,0,false);
        check(&s,&sink,chunk,sizeof(chunk),0,false);
        check(&s,&sink,done,1,kind==STOCK_TRANSFER_IMAGE?0:1,true);
        assert(!s.active && store.committed && store.used==3);
        assert(!memcmp(store.bytes,"abc",3));
        check(&s,&sink,done,1,0x12,false);
        /* New BEGIN replaces an active transfer; short BEGIN preserves it. */
        check(&s,&sink,b,bn,0,false);
        check(&s,&sink,chunk,sizeof(chunk),0,false);
        check(&s,&sink,b,1,0x12,false);assert(s.active&&s.received==3);
        check(&s,&sink,b,bn,0,false);assert(s.active&&s.received==0&&store.used==0);
        check(&s,&sink,done,1,0x12,false);assert(!s.active&&!store.committed);
        /* Stock permits received > declared until END, within capacity. */
        check(&s,&sink,b,bn,0,false);
        check(&s,&sink,chunk,sizeof(chunk),0,false);
        check(&s,&sink,chunk,sizeof(chunk),0,false);
        check(&s,&sink,done,1,0x12,false);assert(!store.committed);
        const uint8_t zero[]={0,0,0,0,0,0xff};
        check(&s,&sink,zero,sizeof(zero),0,false);
        const uint8_t end_extra[]={2,0xaa};
        check(&s,&sink,end_extra,2,kind==STOCK_TRANSFER_IMAGE?0:1,true);
        check(&s,&sink,b,bn,0,false);
        const uint8_t unknown[]={3};check(&s,&sink,unknown,1,0x12,false);
        check(&s,&sink,NULL,0,0x12,false);assert(s.active);
        uint8_t oversized[257]={1};
        check(&s,&sink,oversized,sizeof(oversized),0x12,false);assert(s.active);
        const uint8_t huge[]={0,0xff,0xff,0xff,0xff};
        check(&s,&sink,huge,sizeof(huge),0x15,false);assert(!s.active);
        check(&s,&sink,b,bn,0,false);
        s.received=UINT32_MAX;s.capacity=UINT32_MAX;
        check(&s,&sink,chunk,sizeof(chunk),kind==STOCK_TRANSFER_IMAGE?0x16:0x15,false);assert(!s.active);
        stock_transfer_init(&s,(stock_transfer_kind_t)kind,3);
        check(&s,&sink,b,bn,0,false);
        check(&s,&sink,chunk,sizeof(chunk),0,false);
        check(&s,&sink,chunk,sizeof(chunk),kind==STOCK_TRANSFER_IMAGE?0x16:0x15,false);assert(!s.active);
        store.begin_error=STOCK_SINK_IO;
        check(&s,&sink,b,bn,0x16,false);assert(!s.active);
        store.begin_error=STOCK_SINK_LIMIT;
        check(&s,&sink,b,bn,0x15,false);store.begin_error=STOCK_SINK_OK;
        check(&s,&sink,b,bn,0,false);
        store.data_error=STOCK_SINK_IO;
        check(&s,&sink,chunk,sizeof(chunk),0x16,false);assert(!s.active);
        store.data_error=STOCK_SINK_OK;
        check(&s,&sink,b,bn,0,false);check(&s,&sink,chunk,sizeof(chunk),0,false);
        store.end_error=STOCK_SINK_INVALID;
        check(&s,&sink,done,1,kind==STOCK_TRANSFER_AUDIO?0x13:0x16,false);
        assert(!s.active&&!store.committed);
        store.end_error=STOCK_SINK_IO;
        check(&s,&sink,b,bn,0,false);check(&s,&sink,chunk,sizeof(chunk),0,false);
        check(&s,&sink,done,1,0x16,false);
        stock_transfer_reset(&s);assert(!s.active&&s.capacity==3);
        /* Header endianness is checked independently of the encoder. */
        stock_transfer_init(&s,(stock_transfer_kind_t)kind,1000);
        const uint8_t total256[]={0,0,1,0,0};
        check(&s,&sink,total256,5,0,false);
        assert(s.declared==256&&store.expected==256);
    }
    puts("Stock transfer tests: PASS (wire core only)");
}
