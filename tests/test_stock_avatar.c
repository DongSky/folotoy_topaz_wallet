#include "stock_avatar.h"
#include "esp_partition.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define CAPACITY 0x26000u
static uint8_t archive_bytes[0x100000],frame_bytes[CAPACITY];
static esp_partition_t archive_part={.size=sizeof(archive_bytes),.readonly=true,.index=0};
static esp_partition_t frame_part={.size=sizeof(frame_bytes),.index=1};
static unsigned erases,writes,maps,unmaps,fail_row;
static bool missing_archive,fail_map,fail_erase,session_live=true,cancel_erase,cancel_row;
static void le16(uint8_t *p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void le32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static void be32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(24-i*8));}
static uint32_t crc(const uint8_t *p,size_t n){return (uint32_t)crc32(0,p,(uInt)n);}
static size_t append_chunk(uint8_t *out,const char *type,const uint8_t *bytes,size_t n)
{
    be32(out,(uint32_t)n);memcpy(out+4,type,4);if(n)memcpy(out+8,bytes,n);
    be32(out+8+n,crc(out+4,n+4));return n+12;
}
static void fix_chunk_crc(uint8_t *chunk)
{
    size_t n=((size_t)chunk[0]<<24)|((size_t)chunk[1]<<16)|((size_t)chunk[2]<<8)|chunk[3];
    be32(chunk+8+n,crc(chunk+4,n+4));
}
static uint8_t source_value(unsigned y,unsigned x,unsigned c)
{return (uint8_t)(x*17+y*23+c*61);}
static void make_scanlines(uint8_t *raw,unsigned channels,size_t *length,int extra,int short_output,int invalid_filter)
{
    size_t stride=96*channels;
    for(unsigned y=0;y<156;y++) {
        size_t base=y*(stride+1);raw[base]=0;
        for(unsigned x=0;x<96;x++)for(unsigned c=0;c<channels;c++)raw[base+1+x*channels+c]=source_value(y,x,c);
    }
    *length=(stride+1)*156;
    if(extra)raw[(*length)++]=42;
    if(short_output)(*length)--;
    if(invalid_filter)raw[0]=5;
}
static size_t make_png(uint8_t *png,unsigned channels,size_t split,bool empty_idat,int extra,int short_output,int invalid_filter)
{
    uint8_t *raw=malloc(60100),*compressed=malloc(70000);assert(raw&&compressed);
    size_t raw_size;make_scanlines(raw,channels,&raw_size,extra,short_output,invalid_filter);
    uLongf compressed_size=70000;assert(compress2(compressed,&compressed_size,raw,raw_size,Z_BEST_COMPRESSION)==Z_OK);
    memcpy(png,"\x89PNG\r\n\x1a\n",8);
    uint8_t ihdr[13]={0};be32(ihdr,96);be32(ihdr+4,156);ihdr[8]=8;ihdr[9]=channels==1?0:channels==3?2:6;
    size_t pos=8+append_chunk(png+8,"IHDR",ihdr,13),at=0;
    if(empty_idat)pos+=append_chunk(png+pos,"IDAT",NULL,0);
    while(at<compressed_size){size_t n=compressed_size-at;if(split&&n>split)n=split;pos+=append_chunk(png+pos,"IDAT",compressed+at,n);at+=n;}
    if(empty_idat)pos+=append_chunk(png+pos,"IDAT",NULL,0);
    pos+=append_chunk(png+pos,"IEND",NULL,0);free(raw);free(compressed);return pos;
}
static size_t make_archive(uint8_t *out,const uint8_t *payload,size_t length)
{
    memset(out,0,44);memcpy(out,"AVA1",4);out[4]=1;out[5]=1;le16(out+6,44);le32(out+8,(uint32_t)(44+length));
    memcpy(out+16,"default",7);le32(out+32,44);le32(out+36,(uint32_t)length);le32(out+40,crc(payload,length));
    memcpy(out+44,payload,length);le32(out+12,crc(out+16,28));return 44+length;
}
static void index_crc(uint8_t *out){le32(out+12,crc(out+16,(size_t)out[5]*28));}
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,esp_partition_subtype_t subtype,const char *label)
{
    assert(type==ESP_PARTITION_TYPE_DATA&&subtype==ESP_PARTITION_SUBTYPE_ANY);
    if(!strcmp(label,"imgava"))return missing_archive?NULL:&archive_part;
    assert(!strcmp(label,"imgframe"));return &frame_part;
}
esp_err_t esp_partition_mmap(const esp_partition_t *p,size_t offset,size_t n,esp_partition_mmap_memory_t memory,const void **out,esp_partition_mmap_handle_t *handle)
{
    assert(memory==ESP_PARTITION_MMAP_DATA&&offset==0&&n<=p->size);++maps;
    if(fail_map)return -1;
    *out=p->index?frame_bytes:archive_bytes;*handle=p->index+1;return ESP_OK;
}
void esp_partition_munmap(esp_partition_mmap_handle_t handle){assert(handle==1||handle==2);++unmaps;}
esp_err_t esp_partition_erase_range(const esp_partition_t *p,size_t offset,size_t n)
{
    assert(p==&frame_part&&!p->readonly&&offset==0&&n<=p->size&&n%4096==0);++erases;
    if(fail_erase)return -1;
    memset(frame_bytes,0xff,n);if(cancel_erase)session_live=false;return ESP_OK;
}
esp_err_t esp_partition_write(const esp_partition_t *p,size_t offset,const void *data,size_t n)
{
    assert(p==&frame_part&&!p->readonly&&offset<=p->size&&n<=p->size-offset&&n==384);++writes;
    if(fail_row&&writes==fail_row)return -1;
    const uint8_t *in=data;
    for(size_t i=0;i<n;i++){assert((frame_bytes[offset+i]&in[i])==in[i]);frame_bytes[offset+i]&=in[i];}
    if (cancel_row) session_live = false;
    return ESP_OK;
}
static bool guard(void *context){assert(context==&session_live);return session_live;}
typedef struct { unsigned channels,rows;bool stop; } row_context_t;
static bool check_row(void *context,unsigned row,const uint8_t *pixels,size_t n)
{
    row_context_t *c=context;assert(row==c->rows++&&n==384);
    if(c->stop)return false;
    for(unsigned x=0;x<96;x++){
        assert(pixels[4*x]==source_value(row,x,c->channels==1?0:2));
        assert(pixels[4*x+1]==source_value(row,x,c->channels==1?0:1));
        assert(pixels[4*x+2]==source_value(row,x,0));
        assert(pixels[4*x+3]==(c->channels==4?source_value(row,x,3):255));
    }
    return true;
}
static void test_archive_validation_lookup(void)
{
    uint8_t bytes[256];uint8_t payload[]={1,2,3,4};size_t n=make_archive(bytes,payload,4);
    stock_avatar_archive_t archive;const uint8_t *found;size_t length;
    assert(stock_avatar_archive_validate(bytes,n,&archive)==STOCK_AVATAR_OK);
    assert(stock_avatar_archive_find(&archive,"default",&found,&length)==STOCK_AVATAR_OK&&length==4&&!memcmp(found,payload,4));
    assert(stock_avatar_archive_find(&archive,"Default",&found,&length)==STOCK_AVATAR_NOT_FOUND&&found==NULL&&length==0);
    assert(stock_avatar_archive_find(&archive,"abcdefghijklmnop",&found,&length)==STOCK_AVATAR_NOT_FOUND);
    bytes[44]^=1;assert(stock_avatar_archive_find(&archive,"default",&found,&length)==STOCK_AVATAR_CRC);bytes[44]^=1;
    for(size_t cut=0;cut<n;cut++)assert(stock_avatar_archive_validate(bytes,cut,&archive)!=STOCK_AVATAR_OK);
    bytes[16]^=1;assert(stock_avatar_archive_validate(bytes,n,&archive)==STOCK_AVATAR_CRC);bytes[16]^=1;
    bytes[24]=1;index_crc(bytes);assert(stock_avatar_archive_validate(bytes,n,&archive)==STOCK_AVATAR_INVALID);
    make_archive(bytes,payload,4);le32(bytes+32,45);index_crc(bytes);assert(stock_avatar_archive_validate(bytes,n,&archive)==STOCK_AVATAR_INVALID);
    make_archive(bytes,payload,4);le32(bytes+36,UINT32_MAX);index_crc(bytes);assert(stock_avatar_archive_validate(bytes,n,&archive)==STOCK_AVATAR_INVALID);
    make_archive(bytes,payload,4);bytes[5]=2;le16(bytes+6,72);le32(bytes+8,80);memcpy(bytes+44,bytes+16,28);
    le32(bytes+32,72);le32(bytes+60,76);index_crc(bytes);assert(stock_avatar_archive_validate(bytes,80,&archive)==STOCK_AVATAR_INVALID); /* duplicate */
    memset(bytes+44,0,16);memcpy(bytes+44,"second",6);le32(bytes+60,72);index_crc(bytes);assert(stock_avatar_archive_validate(bytes,80,&archive)==STOCK_AVATAR_INVALID); /* overlap */
    le32(bytes+60,76);index_crc(bytes);assert(stock_avatar_archive_validate(bytes,80,&archive)==STOCK_AVATAR_OK);
}
static uint8_t test_paeth(uint8_t a,uint8_t b,uint8_t c)
{
    int prediction=a+b-c,da=abs(prediction-a),db=abs(prediction-b),dc=abs(prediction-c);return da<=db&&da<=dc?a:db<=dc?b:c;
}
static void test_all_row_filters(void)
{
    for(unsigned channels=1;channels<=4;channels++)for(unsigned filter=0;filter<=4;filter++)for(unsigned first=0;first<2;first++){
        uint8_t original[24],prior[24],filtered[24];
        for(unsigned i=0;i<24;i++){original[i]=(uint8_t)(i*71+33);prior[i]=(uint8_t)(i*53+17);}
        for(unsigned i=0;i<24;i++){
            uint8_t a=i>=channels?original[i-channels]:0,b=first?0:prior[i],c=first||i<channels?0:prior[i-channels];
            unsigned predictor=filter==0?0:filter==1?a:filter==2?b:filter==3?((unsigned)a+b)/2:test_paeth(a,b,c);
            filtered[i]=(uint8_t)(original[i]-predictor);
        }
        assert(stock_avatar_unfilter((uint8_t)filter,filtered,first?NULL:prior,24,channels));assert(!memcmp(filtered,original,24));
    }
    uint8_t byte=1;assert(!stock_avatar_unfilter(5,&byte,NULL,1,1)&&byte==1);
}
static void test_png_streaming_all_channels_and_chunk_splits(void)
{
    uint8_t *png=malloc(300000);assert(png);
    for(unsigned channels=1;channels<=4;channels++){if(channels==2)continue;
        for(unsigned split_case=0;split_case<3;split_case++){
            size_t n=make_png(png,channels,split_case==0?0:split_case==1?1:19,true,0,0,0);
            stock_avatar_png_info_t info;assert(stock_avatar_png_info(png,n,&info)==STOCK_AVATAR_OK&&info.channels==channels);
            row_context_t context={.channels=channels};assert(stock_avatar_png_decode_rows(png,n,check_row,&context)==STOCK_AVATAR_OK&&context.rows==156);
        }
    }
    free(png);
}
static void test_png_rejects_corruption_order_and_unsupported(void)
{
    uint8_t *png=malloc(300000),*copy=malloc(300000);assert(png&&copy);
    size_t n=make_png(png,4,0,false,0,0,0);stock_avatar_png_info_t info;
    for(size_t cut=0;cut<n;cut++)assert(stock_avatar_png_info(png,cut,&info)!=STOCK_AVATAR_OK);
    memcpy(copy,png,n);copy[45]^=1;assert(stock_avatar_png_info(copy,n,&info)==STOCK_AVATAR_CRC);
    memcpy(copy,png,n);be32(copy+16,97);fix_chunk_crc(copy+8);assert(stock_avatar_png_info(copy,n,&info)==STOCK_AVATAR_INVALID);
    const unsigned changed[]={24,25,26,27,28}; /* bitdepth/type/compression/filter/interlace */
    for(unsigned i=0;i<5;i++){memcpy(copy,png,n);copy[changed[i]]=1;fix_chunk_crc(copy+8);assert(stock_avatar_png_info(copy,n,&info)==STOCK_AVATAR_INVALID);}
    size_t off=33;memcpy(copy,png,off);off+=append_chunk(copy+off,"tRNS",NULL,0);memcpy(copy+off,png+33,n-33);assert(stock_avatar_png_info(copy,off+n-33,&info)==STOCK_AVATAR_INVALID);
    off=33;memcpy(copy,png,off);off+=append_chunk(copy+off,"ABCD",NULL,0);memcpy(copy+off,png+33,n-33);assert(stock_avatar_png_info(copy,off+n-33,&info)==STOCK_AVATAR_INVALID);
    memcpy(copy,png,n-12);off=n-12;off+=append_chunk(copy+off,"tEXt",NULL,0);off+=append_chunk(copy+off,"IDAT",NULL,0);off+=append_chunk(copy+off,"IEND",NULL,0);assert(stock_avatar_png_info(copy,off,&info)==STOCK_AVATAR_INVALID);
    for(unsigned bad=0;bad<3;bad++){
        size_t length=make_png(copy,4,19,false,bad==0,bad==1,bad==2);
        row_context_t context={.channels=4};assert(stock_avatar_png_decode_rows(copy,length,check_row,&context)==STOCK_AVATAR_INVALID);
    }
    /* Valid PNG CRC but broken zlib Adler32 must fail decoder. */
    /* Reject compressed trailing bytes even when IDAT/container CRC is valid. */
    size_t original_idat=n-57;
    memcpy(copy,png,33);memcpy(copy+41,png+41,original_idat);copy[41+original_idat]=0;
    be32(copy+33,(uint32_t)(original_idat+1));memcpy(copy+37,"IDAT",4);fix_chunk_crc(copy+33);
    append_chunk(copy+33+original_idat+13,"IEND",NULL,0);
    row_context_t trailing={.channels=4};assert(stock_avatar_png_decode_rows(copy,n+1,check_row,&trailing)==STOCK_AVATAR_INVALID);
    memcpy(copy,png,n);copy[n-17]^=1;fix_chunk_crc(copy+33);row_context_t context={.channels=4};
    assert(stock_avatar_png_decode_rows(copy,n,check_row,&context)==STOCK_AVATAR_INVALID);
    context=(row_context_t){.channels=4,.stop=true};assert(stock_avatar_png_decode_rows(png,n,check_row,&context)==STOCK_AVATAR_INVALID);
    free(png);free(copy);
}
static void test_flash_adapter_init_lease_and_failures(void)
{
    uint8_t *png=malloc(100000);assert(png);size_t n=make_png(png,4,19,false,0,0,0);make_archive(archive_bytes,png,n);
    missing_archive=true;assert(stock_avatar_init()==STOCK_AVATAR_NOT_FOUND);missing_archive=false;
    archive_part.readonly=false;assert(stock_avatar_init()==STOCK_AVATAR_READONLY_REQUIRED&&erases==0&&writes==0&&maps==0);archive_part.readonly=true;
    fail_map=true;assert(stock_avatar_init()==STOCK_AVATAR_IO);fail_map=false;
    archive_bytes[12]^=1;assert(stock_avatar_init()==STOCK_AVATAR_CRC);archive_bytes[12]^=1;
    frame_part.readonly=true;assert(stock_avatar_init()==STOCK_AVATAR_IO);frame_part.readonly=false;
    assert(stock_avatar_init()==STOCK_AVATAR_OK&&erases==0&&writes==0);
    unsigned before_maps=maps;assert(stock_avatar_init()==STOCK_AVATAR_OK&&maps==before_maps);
    stock_avatar_image_t image={0},second={0};session_live=false;
    assert(stock_avatar_prepare("default",guard,&session_live,&image)==STOCK_AVATAR_CANCELLED&&erases==0);
    session_live=true;cancel_erase=true;
    assert(stock_avatar_prepare("default",guard,&session_live,&image)==STOCK_AVATAR_CANCELLED&&erases==1&&writes==0);cancel_erase=false;session_live=true;
    fail_erase=true;assert(stock_avatar_prepare("default",guard,&session_live,&image)==STOCK_AVATAR_IO);fail_erase=false;
    fail_row=3;assert(stock_avatar_prepare("default",guard,&session_live,&image)==STOCK_AVATAR_IO&&writes==3&&!image.lease);fail_row=0;
    writes=0;cancel_row=true;assert(stock_avatar_prepare("default",guard,&session_live,&image)==STOCK_AVATAR_CANCELLED&&writes==1&&!image.lease);cancel_row=false;session_live=true;
    writes=0;assert(stock_avatar_prepare("default",guard,&session_live,&image)==STOCK_AVATAR_OK&&writes==156);
    assert(image.width==96&&image.height==156&&image.stride==384&&image.length==59904&&image.bgra==frame_bytes);
    row_context_t context={.channels=4};for(unsigned y=0;y<156;y++)assert(check_row(&context,y,image.bgra+384*y,384));
    unsigned before_erase=erases;assert(stock_avatar_prepare("default",guard,&session_live,&second)==STOCK_AVATAR_BUSY&&erases==before_erase);
    assert(stock_avatar_prepare("default",guard,&session_live,&image)==STOCK_AVATAR_BUSY&&image.lease);
    stock_avatar_image_t stale=image;stock_avatar_release(&image);stock_avatar_release(&image);
    assert(stock_avatar_prepare("default",guard,&session_live,&second)==STOCK_AVATAR_OK);
    unsigned before_unmap=unmaps;stock_avatar_release(&stale);assert(unmaps==before_unmap);stock_avatar_release(&second);
    /* No write on invalid container: archive payload/index CRC are updated synthetically. */
    png[28]=1;fix_chunk_crc(png+8);make_archive(archive_bytes,png,n);before_erase=erases;
    assert(stock_avatar_prepare("default",NULL,NULL,&image)==STOCK_AVATAR_INVALID&&erases==before_erase&&image.bgra==NULL);
    free(png);
}
int main(void)
{
    test_archive_validation_lookup();test_all_row_filters();test_png_streaming_all_channels_and_chunk_splits();
    test_png_rejects_corruption_order_and_unsupported();test_flash_adapter_init_lease_and_failures();
    puts("Stock avatar AVA1/PNG/flash tests: PASS (zlib host adapter; ROM/device untested)");return 0;
}
