#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <zlib.h>
#define TINFL_LZ_DICT_SIZE 32768
#define TINFL_FLAG_PARSE_ZLIB_HEADER 1u
#define TINFL_FLAG_HAS_MORE_INPUT 2u
typedef uint8_t mz_uint8;
typedef uint32_t mz_uint32;
typedef enum { TINFL_STATUS_FAILED=-1,TINFL_STATUS_DONE=0,TINFL_STATUS_NEEDS_MORE_INPUT=1,TINFL_STATUS_HAS_MORE_OUTPUT=2 } tinfl_status;
typedef struct { z_stream stream;bool initialized; } tinfl_decompressor;
#define tinfl_init(r) do { (r)->initialized=false; } while(0)
/* Test adapter uses streaming zlib to exercise row/chunk/budget behavior.
 * It does not establish ROM tinfl's behavior on physical ESP32-C3. */
static inline tinfl_status tinfl_decompress(tinfl_decompressor *r,const mz_uint8 *input,size_t *in_size,
    mz_uint8 *start,mz_uint8 *output,size_t *out_size,mz_uint32 flags)
{
    (void)start;(void)flags;
    if(!r->initialized){if(inflateInit(&r->stream)!=Z_OK)return TINFL_STATUS_FAILED;r->initialized=true;}
    r->stream.next_in=(Bytef*)input;r->stream.avail_in=(uInt)*in_size;
    r->stream.next_out=output;r->stream.avail_out=(uInt)*out_size;
    int rc=inflate(&r->stream,Z_NO_FLUSH);
    *in_size-=r->stream.avail_in;*out_size-=r->stream.avail_out;
    if(rc==Z_STREAM_END)return TINFL_STATUS_DONE;
    if(rc!=Z_OK&&rc!=Z_BUF_ERROR)return TINFL_STATUS_FAILED;
    return r->stream.avail_out==0?TINFL_STATUS_HAS_MORE_OUTPUT:TINFL_STATUS_NEEDS_MORE_INPUT;
}
static inline void stock_avatar_test_inflate_end(tinfl_decompressor *r){if(r->initialized)inflateEnd(&r->stream);}
