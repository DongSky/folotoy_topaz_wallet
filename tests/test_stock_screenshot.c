#include "stock_screenshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    uint8_t output[520];memset(output,0xa5,sizeof(output));
    /* Literal device-sized BEGIN: total240*320*2=0x25800. */
    const uint8_t begin[]={1,0x34,0x12,0x78,0x56,0xf0,0,0x40,1,1,0,0x58,2,0};
    assert(stock_screenshot_begin(0x1234,0x5678,240,320,output,14)==14);
    assert(!memcmp(output,begin,14)&&output[14]==0xa5);
    assert(stock_screenshot_begin(1,0,240,320,output,13)==0);
    assert(stock_screenshot_begin(0,0,240,320,output,520)==0);
    assert(stock_screenshot_begin(1,0,241,320,output,520)==0);
    assert(stock_screenshot_begin(1,0,240,0,output,520)==0);
    assert(stock_screenshot_begin(1,0,240,320,NULL,520)==0);
    const uint8_t pixels[]={0x00,0xf8,0xe0,0x07};
    const uint8_t packet[]={2,0x34,0x12,0x78,0x56,3,0,4,0,2,0,2,0,4,0,0,0,0,0xf8,0xe0,7};
    assert(stock_screenshot_data(0x1234,0x5678,3,4,2,2,4,pixels,4,output,21)==21);
    assert(!memcmp(output,packet,sizeof(packet)));
    assert(stock_screenshot_data(1,0,3,4,2,2,4,pixels,4,output,20)==0);
    assert(stock_screenshot_data(1,0,3,4,2,2,6,pixels,4,output,520)==0);
    assert(stock_screenshot_data(1,0,3,4,2,2,UINT32_MAX,pixels,4,output,520)==0);
    assert(stock_screenshot_data(1,0,3,4,2,2,1,pixels,4,output,520)==0);
    assert(stock_screenshot_data(1,0,239,4,2,2,0,pixels,4,output,520)==0);
    assert(stock_screenshot_data(1,0,0,319,2,2,0,pixels,4,output,520)==0);
    assert(stock_screenshot_data(1,0,0,0,2,2,0,pixels,3,output,520)==0);
    assert(stock_screenshot_data(1,0,0,0,2,2,0,NULL,4,output,520)==0);
    uint8_t max_pixels[502]={0};
    assert(stock_screenshot_data(1,0,0,0,240,2,0,max_pixels,500,output,517)==517);
    assert(stock_screenshot_data(1,0,0,0,240,2,0,max_pixels,502,output,520)==0);
    const uint8_t end[]={3,0x34,0x12,0x78,0x56,0xab,0x09,0,0x58,2,0,0x26,0x39,0xf4,0xcb};
    assert(stock_screenshot_end(0x1234,0x5678,0x09ab,153600,0xcbf43926,output,15)==15);
    assert(!memcmp(output,end,sizeof(end)));
    assert(stock_screenshot_end(1,0,1,4,0,output,14)==0);
    const uint8_t vector[]="123456789";
    assert(stock_screenshot_crc32(0,vector,9)==0xcbf43926);
    uint32_t crc=stock_screenshot_crc32(0,vector,4);
    assert(stock_screenshot_crc32(crc,vector+4,5)==0xcbf43926);
    assert(stock_screenshot_crc32(crc,NULL,0)==crc);
    puts("Stock screenshot wire tests: PASS (no session/timing implementation)");
}
