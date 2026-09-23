#include "stock_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned count; uint8_t type; size_t length; uint8_t payload[256]; } result_t;
static void received(void *arg, uint8_t type, const uint8_t *data, size_t length)
{
    result_t *result = arg;
    result->count++; result->type = type; result->length = length;
    if (length) memcpy(result->payload, data, length);
}
int main(void)
{
    /* Exact independent wire fixture: type2 image BEGIN total256 little endian. */
    const uint8_t frame[] = {1, 2, 5, 0, 0, 0, 1, 0, 0};
    for (size_t split = 0; split <= sizeof(frame); split++) {
        stock_stream_t stream = {0}; result_t result = {0};
        assert(stock_stream_feed(&stream, frame, split, received, &result) == STOCK_FRAME_OK);
        assert(stock_stream_feed(&stream, frame + split, sizeof(frame) - split, received, &result) == STOCK_FRAME_OK);
        assert(result.count == 1 && result.type == 2 && result.length == 5);
        assert(!memcmp(result.payload, frame + 4, 5));
    }
    stock_stream_t stream = {0}; result_t result = {0};
    uint8_t two[sizeof(frame)*2]; memcpy(two,frame,sizeof(frame));memcpy(two+sizeof(frame),frame,sizeof(frame));
    assert(stock_stream_feed(&stream,two,sizeof(two),received,&result)==STOCK_FRAME_OK);
    assert(result.count==1);
    const uint8_t badver[]={2,1,0,0}, oversize[]={1,1,1,1};
    assert(stock_stream_feed(&stream,badver,1,received,&result)==STOCK_FRAME_OK);
    assert(stream.used==1); /* stock waits for all four header bytes */
    assert(stock_stream_feed(&stream,badver+1,3,received,&result)==STOCK_FRAME_BAD_HEADER);
    assert(stream.used==0);
    assert(stock_stream_feed(&stream,oversize,sizeof(oversize),received,&result)==STOCK_FRAME_TOO_LARGE);
    assert(stream.used==0);
    assert(stock_stream_feed(&stream,frame,3,received,&result)==STOCK_FRAME_OK);
    stock_stream_reset(&stream); /* disconnect/timeout discards incomplete header */
    assert(stock_stream_feed(&stream,frame,sizeof(frame),received,&result)==STOCK_FRAME_OK);
    assert(result.count==2);
    uint8_t payload[256],encoded[260]; memset(payload,0xa5,sizeof(payload));
    assert(stock_frame_encode(3,payload,sizeof(payload),encoded,sizeof(encoded))==260);
    assert(encoded[0]==1&&encoded[1]==3&&encoded[2]==0&&encoded[3]==1);
    for(size_t i=0;i<260;i++)assert(stock_stream_feed(&stream,encoded+i,1,received,&result)==STOCK_FRAME_OK);
    assert(result.length==256&&!memcmp(result.payload,payload,256));
    assert(stock_frame_encode(1,payload,257,encoded,sizeof(encoded))==0);
    assert(stock_frame_encode(1,payload,256,encoded,259)==0);
    uint8_t response[2];stock_response_encode(2,0x14,response);
    assert(response[0]==2&&response[1]==0x14);
    assert(stock_frame_encode(6,NULL,0,encoded,sizeof(encoded))==4);
    assert(stock_stream_feed(&stream,encoded,4,received,&result)==STOCK_FRAME_OK);
    assert(result.type==6&&result.length==0);
    assert(stock_stream_feed(NULL,frame,sizeof(frame),received,&result)==STOCK_FRAME_BAD_HEADER);
    assert(stock_stream_feed(&stream,NULL,1,received,&result)==STOCK_FRAME_BAD_HEADER);
    assert(stock_stream_feed(&stream,frame,sizeof(frame),NULL,&result)==STOCK_FRAME_BAD_HEADER);
    assert(stock_frame_encode(1,NULL,1,encoded,sizeof(encoded))==0);
    puts("Stock frame envelope tests: PASS (not full protocol compatibility)");
}
