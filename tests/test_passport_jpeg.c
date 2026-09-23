#include "passport_jpeg.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    uint8_t jpeg[] = {0xff,0xd8,0xff,0xe0,0,4,0,0,0xff,0xc0,0,17,8,1,64,0,240,3,1,0x11,0,2,0x11,1,3,0x11,1};
    uint16_t w=7,h=9;
    assert(passport_jpeg_dimensions(jpeg,sizeof(jpeg),&w,&h) && w==240 && h==320);
    for(size_t i=0;i<sizeof(jpeg);++i) assert(!passport_jpeg_dimensions(jpeg,i,&w,&h));
    jpeg[9]=0xc2;assert(!passport_jpeg_dimensions(jpeg,sizeof(jpeg),&w,&h));jpeg[9]=0xc0;
    jpeg[16]=241;assert(!passport_jpeg_dimensions(jpeg,sizeof(jpeg),&w,&h));jpeg[16]=240;
    jpeg[11]=18;assert(!passport_jpeg_dimensions(jpeg,sizeof(jpeg),&w,&h));jpeg[11]=17;
    jpeg[5]=1;assert(!passport_jpeg_dimensions(jpeg,sizeof(jpeg),&w,&h));
    assert(!passport_jpeg_dimensions(NULL,0,&w,&h));
    puts("Passport JPEG bounds/format/dimensions: PASS");
}
