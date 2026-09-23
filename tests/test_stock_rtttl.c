#include "stock_rtttl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void invalid(const char *text)
{
    assert(!stock_rtttl_validate((const uint8_t *)text, strlen(text), NULL));
}
int main(void)
{
    const char *score = "Test:d=4,o=4,b=120:c,c#,d.,8e5.,p,8P.,b#4";
    const uint32_t hz[] = {262,277,294,660,0,0,524};
    const uint32_t ms[] = {500,500,750,375,500,375,500};
    uint32_t duration;
    assert(stock_rtttl_validate((const uint8_t *)score, strlen(score), &duration));
    assert(duration == 3500);
    stock_rtttl_t parser;
    assert(stock_rtttl_init(&parser, (const uint8_t *)score, strlen(score)));
    stock_rtttl_note_t note;
    for (size_t i = 0; i < sizeof(hz)/sizeof(*hz); ++i) {
        assert(stock_rtttl_next(&parser, &note) == 1);
        assert(note.frequency == hz[i] && note.duration_ms == ms[i]);
    }
    assert(stock_rtttl_next(&parser, &note) == 0);
    score = "Defaults::a";
    assert(stock_rtttl_init(&parser, (const uint8_t *)score, strlen(score)));
    assert(stock_rtttl_next(&parser, &note) == 1);
    assert(note.frequency == 1760 && note.duration_ms == 952);
    score = "Dots:d=8,o=4,b=120:c.5,c5.,C#5,16p";
    assert(stock_rtttl_validate((const uint8_t *)score, strlen(score), &duration));
    assert(duration == 1125);
    invalid(""); invalid("missing"); invalid("empty::"); invalid("x:d=0:c");
    invalid("x:o=9:c"); invalid("x:b=0:c"); invalid("x::0c");
    invalid("x::c9"); invalid("x::b#8"); invalid("x::h"); invalid("x::c##");
    invalid("x::c.."); invalid("x::p#"); invalid("x::c4x");
    invalid("x:d=999999999999999999999999:c");
    invalid("x::999999999999999999999999c");
    invalid("x:b=1:1c,1c,1c"); /* 12 minutes */
    score = "limit:b=1:1c,1c,2c";
    assert(stock_rtttl_validate((const uint8_t *)score,strlen(score),&duration));
    assert(duration==STOCK_RTTTL_MAX_DURATION_MS);
    invalid("x:b=65535:65535c"); /* rounds to zero milliseconds */
    const uint8_t embedded[] = {'x',':',':','c',0,',','d'};
    assert(!stock_rtttl_validate(embedded, sizeof(embedded), NULL));
    uint8_t large[STOCK_RTTTL_CAPACITY + 1]; memset(large,' ',sizeof(large));
    assert(!stock_rtttl_validate(large,sizeof(large),NULL));
    int16_t whole[80], split[80]; uint16_t phase=0, other=0;
    stock_rtttl_pcm(whole,80,1000,&phase);
    stock_rtttl_pcm(split,7,1000,&other);
    stock_rtttl_pcm(split+7,73,1000,&other);
    assert(!memcmp(whole,split,sizeof(whole)) && phase==other);
    for (unsigned i=0;i<80;++i) assert(whole[i] == (i%8<4 ? 6000 : -6000));
    stock_rtttl_pcm(whole,80,0,&phase);
    for (unsigned i=0;i<80;++i) assert(whole[i]==0);
    assert(phase==0);
    puts("stock_rtttl: notes, timing, bounds and PCM PASS");
    return 0;
}
