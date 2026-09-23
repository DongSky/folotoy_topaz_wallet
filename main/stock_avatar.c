#include "stock_avatar.h"
#include "esp_partition.h"
#include "miniz.h"
#include <stdlib.h>
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | p[3]; }
static uint32_t crc32_ieee(const uint8_t *p, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i=0;i<length;i++) { crc ^= p[i]; for(unsigned b=0;b<8;b++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u))); }
    return ~crc;
}
static size_t valid_name(const uint8_t *p)
{
    size_t n=0;
    while(n<16 && p[n]) {
        if(!((p[n]>='a'&&p[n]<='z')||(p[n]>='0'&&p[n]<='9')||p[n]=='_'||p[n]=='-')) return 0;
        n++;
    }
    if(!n||n==16) return 0;
    for(size_t i=n;i<16;i++) if(p[i]) return 0;
    return n;
}
stock_avatar_result_t stock_avatar_archive_validate(const uint8_t *p, size_t size, stock_avatar_archive_t *out)
{
    if(!out) return STOCK_AVATAR_INVALID;
    *out=(stock_avatar_archive_t){0};
    if(!p||size<16||memcmp(p,"AVA1",4)||p[4]!=1||!p[5]) return STOCK_AVATAR_INVALID;
    size_t index_end=le16(p+6), total=le32(p+8);
    if(index_end!=16u+28u*p[5]||index_end>total||total>size) return STOCK_AVATAR_INVALID;
    if(crc32_ieee(p+16,index_end-16)!=le32(p+12)) return STOCK_AVATAR_CRC;
    for(unsigned i=0;i<p[5];i++) {
        const uint8_t *entry=p+16+28*i;
        size_t offset=le32(entry+16), length=le32(entry+20);
        if(!valid_name(entry)||offset%4||offset<index_end||offset>total||!length||length>total-offset) return STOCK_AVATAR_INVALID;
        for(unsigned j=0;j<i;j++) {
            const uint8_t *prior=p+16+28*j;
            size_t previous=le32(prior+16), previous_length=le32(prior+20);
            if(!memcmp(entry,prior,16)||(offset<previous+previous_length&&previous<offset+length)) return STOCK_AVATAR_INVALID;
        }
    }
    *out=(stock_avatar_archive_t){p,size,total,p[5],(uint16_t)index_end};
    return STOCK_AVATAR_OK;
}
stock_avatar_result_t stock_avatar_archive_find(const stock_avatar_archive_t *archive, const char *name, const uint8_t **png, size_t *length)
{
    if(png)*png=NULL;
    if(length)*length=0;
    if(!archive||!archive->bytes||!name||!png||!length) return STOCK_AVATAR_INVALID;
    size_t n=0;while(n<16&&name[n])n++;
    if(!n||n==16)return STOCK_AVATAR_NOT_FOUND;
    for(unsigned i=0;i<archive->count;i++) {
        const uint8_t *e=archive->bytes+16+28*i;
        if(valid_name(e)!=n||memcmp(e,name,n))continue;
        size_t offset=le32(e+16), size=le32(e+20);
        if(crc32_ieee(archive->bytes+offset,size)!=le32(e+24))return STOCK_AVATAR_CRC;
        *png=archive->bytes+offset;*length=size;return STOCK_AVATAR_OK;
    }
    return STOCK_AVATAR_NOT_FOUND;
}
static bool chunk_type_valid(const uint8_t *type)
{
    for(unsigned i=0;i<4;i++)if(!((type[i]>='A'&&type[i]<='Z')||(type[i]>='a'&&type[i]<='z')))return false;
    return !(type[2]&32); /* PNG reserved bit must be zero. */
}
stock_avatar_result_t stock_avatar_png_info(const uint8_t *p, size_t size, stock_avatar_png_info_t *out)
{
    static const uint8_t signature[]={137,80,78,71,13,10,26,10};
    if(!out)return STOCK_AVATAR_INVALID;
    *out=(stock_avatar_png_info_t){0};
    if(!p||size<33||memcmp(p,signature,8))return STOCK_AVATAR_INVALID;
    bool ihdr=false,idat=false,idat_ended=false,plte=false;
    uint8_t color=0;size_t pos=8,compressed=0;
    stock_avatar_png_info_t info={0};
    while(pos<size) {
        if(size-pos<12)return STOCK_AVATAR_INVALID;
        size_t n=be32(p+pos);
        if(n>size-pos-12)return STOCK_AVATAR_INVALID;
        const uint8_t *type=p+pos+4,*data=p+pos+8;
        if(!chunk_type_valid(type))return STOCK_AVATAR_INVALID;
        if(crc32_ieee(type,n+4)!=be32(data+n))return STOCK_AVATAR_CRC;
        bool is_data=!memcmp(type,"IDAT",4);
        if(idat&&!is_data)idat_ended=true;
        if(!memcmp(type,"IHDR",4)) {
            if(ihdr||pos!=8||n!=13)return STOCK_AVATAR_INVALID;
            uint32_t w=be32(data),h=be32(data+4);color=data[9];
            if(w!=STOCK_AVATAR_WIDTH||h!=STOCK_AVATAR_HEIGHT||data[8]!=8||
                (color!=0&&color!=2&&color!=6)||data[10]||data[11]||data[12])return STOCK_AVATAR_INVALID;
            info.width=(uint16_t)w;info.height=(uint16_t)h;info.channels=color==0?1:color==2?3:4;ihdr=true;
        } else if(!ihdr) return STOCK_AVATAR_INVALID;
        else if(is_data) {
            if(idat_ended||n>SIZE_MAX-compressed)return STOCK_AVATAR_INVALID;
            idat=true;compressed+=n;
        } else if(!memcmp(type,"IEND",4)) {
            if(n||!idat||!compressed||pos+12!=size)return STOCK_AVATAR_INVALID;
            info.compressed_size=compressed;*out=info;return STOCK_AVATAR_OK;
        } else if(!memcmp(type,"PLTE",4)) {
            if(plte||idat||color==0||!n||n>768||n%3)return STOCK_AVATAR_INVALID;
            plte=true;
        } else if(!(type[0]&32)||!memcmp(type,"tRNS",4)) return STOCK_AVATAR_INVALID;
        pos+=n+12;
    }
    return STOCK_AVATAR_INVALID;
}
static uint8_t paeth(uint8_t a,uint8_t b,uint8_t c)
{
    int p=(int)a+b-c,da=abs(p-a),db=abs(p-b),dc=abs(p-c);
    return da<=db&&da<=dc?a:db<=dc?b:c;
}
bool stock_avatar_unfilter(uint8_t filter,uint8_t *row,const uint8_t *prior,size_t length,unsigned channels)
{
    if(!row||!channels||channels>4||filter>4)return false;
    for(size_t i=0;i<length;i++) {
        uint8_t left=i>=channels?row[i-channels]:0,up=prior?prior[i]:0,corner=prior&&i>=channels?prior[i-channels]:0;
        if(filter==1)row[i]+=left;
        else if(filter==2)row[i]+=up;
        else if(filter==3)row[i]+=(uint8_t)(((unsigned)left+up)/2);
        else if(filter==4)row[i]+=paeth(left,up,corner);
    }
    return true;
}
typedef struct {
    tinfl_decompressor inflater;
    uint8_t dictionary[TINFL_LZ_DICT_SIZE];
    uint8_t current[STOCK_AVATAR_WIDTH*4+1],prior[STOCK_AVATAR_WIDTH*4],bgra[STOCK_AVATAR_WIDTH*4];
    size_t fill,produced;unsigned row;
} decode_work_t;
static bool deliver(decode_work_t *w,const uint8_t *bytes,size_t n,const stock_avatar_png_info_t *info,stock_avatar_row_sink_t sink,void *context)
{
    size_t row_size=(size_t)info->width*info->channels,expected=(row_size+1)*info->height;
    if(w->produced>expected||n>expected-w->produced)return false;
    w->produced+=n;
    while(n) {
        size_t take=row_size+1-w->fill;if(take>n)take=n;
        memcpy(w->current+w->fill,bytes,take);w->fill+=take;bytes+=take;n-=take;
        if(w->fill!=row_size+1)continue;
        uint8_t *raw=w->current+1;
        if(!stock_avatar_unfilter(w->current[0],raw,w->row?w->prior:NULL,row_size,info->channels))return false;
        for(unsigned x=0;x<info->width;x++) {
            const uint8_t *pixel=raw+(size_t)x*info->channels;uint8_t *out=w->bgra+4*x;
            out[0]=pixel[info->channels==1?0:2];out[1]=pixel[info->channels==1?0:1];out[2]=pixel[0];out[3]=info->channels==4?pixel[3]:255;
        }
        if(!sink(context,w->row,w->bgra,(size_t)info->width*4))return false;
        memcpy(w->prior,raw,row_size);w->fill=0;w->row++;
    }
    return true;
}
stock_avatar_result_t stock_avatar_png_decode_rows(const uint8_t *p,size_t size,stock_avatar_row_sink_t sink,void *context)
{
    if(!sink)return STOCK_AVATAR_INVALID;
    stock_avatar_png_info_t info;
    stock_avatar_result_t rc=stock_avatar_png_info(p,size,&info);if(rc!=STOCK_AVATAR_OK)return rc;
    decode_work_t *w=calloc(1,sizeof(*w));if(!w)return STOCK_AVATAR_NO_MEMORY;
    tinfl_init(&w->inflater);
    size_t pos=8,consumed=0,dict_pos=0;bool done=false;
    rc=STOCK_AVATAR_INVALID;
    while(pos<size&&!done) {
        size_t n=be32(p+pos);const uint8_t *input=p+pos+8;
        if(!memcmp(p+pos+4,"IDAT",4)) {
            size_t remaining=n;
            do {
                size_t in_size=remaining,out_size=TINFL_LZ_DICT_SIZE-dict_pos;
                unsigned flags=TINFL_FLAG_PARSE_ZLIB_HEADER;
                if(consumed+remaining<info.compressed_size)flags|=TINFL_FLAG_HAS_MORE_INPUT;
                tinfl_status status=tinfl_decompress(&w->inflater,input,&in_size,w->dictionary,w->dictionary+dict_pos,&out_size,flags);
                input+=in_size;remaining-=in_size;consumed+=in_size;
                if(status<0||!deliver(w,w->dictionary+dict_pos,out_size,&info,sink,context))goto cleanup;
                dict_pos=(dict_pos+out_size)&(TINFL_LZ_DICT_SIZE-1);
                if(status==TINFL_STATUS_DONE) {done=true;break;}
                if(!in_size&&!out_size) {
                    if(!remaining&&consumed<info.compressed_size)break;
                    goto cleanup;
                }
                if(!remaining&&status==TINFL_STATUS_NEEDS_MORE_INPUT)break;
            } while(true);
        }
        pos+=n+12;
    }
    if(done&&consumed==info.compressed_size&&!w->fill&&w->row==info.height)rc=STOCK_AVATAR_OK;
cleanup:
#ifdef STOCK_AVATAR_HOST_TEST
    /* Host fake wraps zlib solely to exercise ROM-call boundary behavior. */
    stock_avatar_test_inflate_end(&w->inflater);
#endif
    free(w);return rc;
}

static stock_avatar_archive_t s_archive;
static const esp_partition_t *s_frame;
static esp_partition_mmap_handle_t s_archive_map,s_frame_map;
static uint32_t s_lease_counter,s_active_lease;
static bool s_initialized;
stock_avatar_result_t stock_avatar_init(void)
{
    if(s_initialized)return STOCK_AVATAR_OK;
    const esp_partition_t *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"imgava");
    if(!part)return STOCK_AVATAR_NOT_FOUND;
    if(!part->readonly)return STOCK_AVATAR_READONLY_REQUIRED;
    const void *bytes;
    if(esp_partition_mmap(part,0,part->size,ESP_PARTITION_MMAP_DATA,&bytes,&s_archive_map)!=ESP_OK)return STOCK_AVATAR_IO;
    stock_avatar_result_t rc=stock_avatar_archive_validate(bytes,part->size,&s_archive);
    if(rc!=STOCK_AVATAR_OK){esp_partition_munmap(s_archive_map);return rc;}
    s_frame=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"imgframe");
    if(!s_frame||s_frame->readonly||s_frame->encrypted||s_frame->size<STOCK_AVATAR_BGRA_SIZE||s_frame->size%4096) {
        esp_partition_munmap(s_archive_map);memset(&s_archive,0,sizeof(s_archive));return STOCK_AVATAR_IO;
    }
    s_initialized=true;return STOCK_AVATAR_OK;
}
stock_avatar_result_t stock_avatar_find(const char *name,const uint8_t **png,size_t *length)
{
    if(!s_initialized){if(png)*png=NULL;if(length)*length=0;return STOCK_AVATAR_NOT_FOUND;}
    return stock_avatar_archive_find(&s_archive,name,png,length);
}
typedef struct {stock_avatar_guard_t guard;void *context;stock_avatar_result_t error;} flash_sink_t;
static bool allowed(flash_sink_t *sink)
{
    if(sink->error!=STOCK_AVATAR_OK)return false;
    if(sink->guard&&!sink->guard(sink->context)){sink->error=STOCK_AVATAR_CANCELLED;return false;}
    return true;
}
static bool write_row(void *context,unsigned row,const uint8_t *bgra,size_t length)
{
    flash_sink_t *sink=context;
    if(!allowed(sink))return false;
    if(esp_partition_write(s_frame,(size_t)row*STOCK_AVATAR_WIDTH*4,bgra,length)!=ESP_OK){sink->error=STOCK_AVATAR_IO;return false;}
    return allowed(sink);
}
stock_avatar_result_t stock_avatar_prepare(const char *name,stock_avatar_guard_t guard,void *context,stock_avatar_image_t *image)
{
    if(!image)return STOCK_AVATAR_INVALID;
    if(image->lease)return STOCK_AVATAR_BUSY;
    *image=(stock_avatar_image_t){0};
    if(!s_initialized)return STOCK_AVATAR_NOT_FOUND;
    if(s_active_lease)return STOCK_AVATAR_BUSY;
    const uint8_t *png;size_t length;stock_avatar_png_info_t info;
    stock_avatar_result_t rc=stock_avatar_find(name,&png,&length);if(rc!=STOCK_AVATAR_OK)return rc;
    rc=stock_avatar_png_info(png,length,&info);if(rc!=STOCK_AVATAR_OK)return rc;
    flash_sink_t sink={guard,context,STOCK_AVATAR_OK};
    if(!allowed(&sink))return sink.error;
    size_t erase=(STOCK_AVATAR_BGRA_SIZE+4095u)&~(size_t)4095;
    if(esp_partition_erase_range(s_frame,0,erase)!=ESP_OK)return STOCK_AVATAR_IO;
    if(!allowed(&sink))return sink.error;
    rc=stock_avatar_png_decode_rows(png,length,write_row,&sink);
    if(sink.error!=STOCK_AVATAR_OK)return sink.error;
    if(rc!=STOCK_AVATAR_OK)return rc;
    if(!allowed(&sink))return sink.error;
    const void *pixels;
    if(esp_partition_mmap(s_frame,0,STOCK_AVATAR_BGRA_SIZE,ESP_PARTITION_MMAP_DATA,&pixels,&s_frame_map)!=ESP_OK)return STOCK_AVATAR_IO;
    if(!allowed(&sink)){esp_partition_munmap(s_frame_map);return sink.error;}
    if(++s_lease_counter==0)++s_lease_counter;
    s_active_lease=s_lease_counter;
    *image=(stock_avatar_image_t){pixels,STOCK_AVATAR_BGRA_SIZE,STOCK_AVATAR_WIDTH,STOCK_AVATAR_HEIGHT,STOCK_AVATAR_WIDTH*4,s_active_lease};
    return STOCK_AVATAR_OK;
}
void stock_avatar_release(stock_avatar_image_t *image)
{
    if(!image)return;
    if(image->lease&&image->lease==s_active_lease){esp_partition_munmap(s_frame_map);s_active_lease=0;}
    *image=(stock_avatar_image_t){0};
}
