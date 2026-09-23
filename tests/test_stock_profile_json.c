#include "stock_profile.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int apply(stock_profile_t *p, const char *json, stock_profile_changes_t *c)
{ return stock_profile_apply_json(p, json, strlen(json), c); }

int main(void)
{
    stock_profile_t p;
    stock_profile_changes_t c;
    stock_profile_defaults(&p);
    assert(apply(&p, "{\"nickname\":\"Ada\",\"title\":\"Engineer\",\"intro\":\"Hello\"}", &c) == 3);
    assert(!strcmp((char *)p.bytes, "Ada") && c.nickname && c.title && c.intro);
    assert(!c.progress && !c.time_changed);
    assert(apply(&p, "{\"nickname\":false,\"name\":\"Alias\",\"role\":\"R\",\"subtitle\":\"S\"}", &c) == 3);
    assert(!strcmp((char *)p.bytes, "Alias"));
    assert(apply(&p, "{\"nickname\":\"\",\"name\":\"Ignored\"}", &c) == 1 && p.bytes[0] == 0);
    stock_profile_t before = p;
    assert(apply(&p, "{\"Nickname\":\"wrong case\",\"online\":1,\"fullscreen\":true,\"avatar\":false,\"img_mode\":1,\"battery\":\"100\"}", &c) == 0);
    assert(!memcmp(&before, &p, sizeof(p)));
    assert(apply(&p, "{\"battery\":-1,\"level\":1000,\"xp\":-3,\"xp_max\":0,\"token\":1e100,\"token_max\":-1,\"sleep_min\":9999,\"volume\":101,\"online\":false}", &c) == 9);
    assert(stock_profile_get_i32(&p,176) == 0 && stock_profile_get_i32(&p,180) == 999);
    assert(stock_profile_get_i32(&p,184) == 0 && stock_profile_get_i32(&p,188) == 1);
    assert(stock_profile_get_i32(&p,196) == 1000000000 && stock_profile_get_i32(&p,200) == 1);
    assert(stock_profile_get_i32(&p,204) == 1440 && stock_profile_get_i32(&p,220) == 100);
    assert(p.bytes[192] == 0 && c.online && c.progress);
    assert(apply(&p, "{\"battery\":1e100,\"level\":-1e100,\"xp\":1e100,\"xp_max\":1e100,\"token\":12.9,\"token_max\":2.9,\"sleep_min\":-1e100,\"volume\":-1e100}", &c) == 8);
    assert(stock_profile_get_i32(&p,176) == 100 && stock_profile_get_i32(&p,180) == 0);
    assert(stock_profile_get_i32(&p,184) == INT32_MAX && stock_profile_get_i32(&p,188) == INT32_MAX);
    assert(stock_profile_get_i32(&p,196) == 12 && stock_profile_get_i32(&p,200) == 2);
    assert(stock_profile_get_i32(&p,204) == 0 && stock_profile_get_i32(&p,220) == 0);
    assert(apply(&p, "{\"img_mode\":\"fullscreen\",\"avatar_name\":\"robot-01_test\",\"time\":1727000000.9,\"game_clear\":true}", &c) == 4);
    assert(p.bytes[216] == 1 && !strcmp((char *)p.bytes + 226,"robot-01_test"));
    assert(c.time_changed && c.time == 1727000000 && c.game_clear);
    stock_profile_set_i32(&p, 208, 123);
    assert(apply(&p,"{\"game_clear\":-0.25,\"time\":1e100,\"img_mode\":\"avatar\"}",&c) == 3);
    assert(c.game_clear && c.time == INT64_MAX && p.bytes[216] == 0);
    assert(stock_profile_get_i32(&p,208) == 123); /* dispatcher owns score clear */
    assert(apply(&p,"{\"game_clear\":false,\"time\":0,\"avatar_name\":\"../x\",\"img_mode\":\"other\"}",&c) == 0);
    assert(!c.game_clear && !c.time_changed);
    assert(apply(&p,"{\"avatar_name\":\"abcdefghijklmnop\"}",&c) == 0);
    assert(apply(&p,"{\"avatar_name\":\"ABCDEFGHIJK\"}",&c) == 0);
    assert(apply(&p,"{\"avatar_name\":\"\"}",&c) == 0);
    assert(apply(&p,"{\"avatar_name\":\"abcdefghijklmno\"}",&c) == 1);
    assert(apply(&p,"{\"time\":0.5}",&c) == 1 && c.time_changed && c.time == 0);

    /* Three/four-byte UTF-8 cannot be split at any fixed field boundary. */
    char input[600], text[180];
    memset(text, 'a', 46); memcpy(text+46,"\xe4\xb8\xad",4);
    snprintf(input,sizeof(input),"{\"nickname\":\"%s\"}",text);
    assert(apply(&p,input,&c) == 1 && strlen((char *)p.bytes) == 46);
    memset(text, 'b', 37); memcpy(text+37,"\xf0\x9f\x98\x80",5);
    snprintf(input,sizeof(input),"{\"title\":\"%s\"}",text);
    assert(apply(&p,input,&c) == 1 && strlen((char *)p.bytes+48) == 37);
    for (int i = 0; i < 30; ++i) memcpy(text+3*i,"\xe4\xb8\xad",3);
    text[90] = 0; snprintf(input,sizeof(input),"{\"intro\":\"%s\"}",text);
    assert(apply(&p,input,&c) == 1 && strlen((char *)p.bytes+88) == 84);
    for (int i = 0; i < 30; ++i) memcpy(text+4*i,"\xf0\x9f\x98\x80",4);
    text[120] = 0; snprintf(input,sizeof(input),"{\"intro\":\"%s\"}",text);
    assert(apply(&p,input,&c) == 1 && strlen((char *)p.bytes+88) == 84);
    memset(text,'c',29); text[29]=0; snprintf(input,sizeof(input),"{\"intro\":\"%s\"}",text);
    assert(apply(&p,input,&c) == 1 && strlen((char *)p.bytes+88) == 28);
    assert(apply(&p,"{\"nickname\":\"\\ud83d\\ude00\"}",&c)==1);
    assert(!strcmp((char *)p.bytes,"\xf0\x9f\x98\x80"));

    p.bytes[224]=0xa5; p.bytes[225]=0x5a; before=p;
    const char *bad[] = {"", "[]", "null", "{", "{\"nickname\":\"partial\",}",
        "{\"nickname\":\"partial\",\"token\":1e999}", "{} trailing", "{}{}",
        "{\"nickname\":\"\xc0\xaf\"}", "{\"intro\":\"\xed\xa0\x80\"}",
        "{\"title\":\"\xf4\x90\x80\x80\"}", "{\"intro\":\"\xe4\xb8\"}"};
    for (size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        memset(&c,0xff,sizeof(c));
        assert(apply(&p,bad[i],&c)==-1);
        assert(!memcmp(&p,&before,sizeof(p)) && !c.nickname && !c.time_changed && c.time==0);
    }
    const char embedded[]="{}\0junk";
    assert(stock_profile_apply_json(&p,embedded,sizeof(embedded)-1,&c)==-1);
    assert(stock_profile_apply_json(&p,"{}",3,&c)==0); /* optional final NUL */
    assert(apply(&p,"{\"nickname\":\"one\",\"nickname\":\"two\"}",&c)==1);
    assert(!strcmp((char *)p.bytes,"one")); /* cJSON first duplicate wins */
    assert(p.bytes[224]==0xa5 && p.bytes[225]==0x5a);
    assert(stock_profile_apply_json(NULL,"{}",2,&c)==-1);
    assert(stock_profile_apply_json(&p,NULL,0,&c)==-1);
    assert(apply(&p,"{\"online\":true}",NULL)==1 && p.bytes[192]==1);
    /* A 247-byte unpaired BLE command must not recurse through 120 arrays. */
    char deep[256];
    memcpy(deep, "{\"x\":", 5);
    memset(deep + 5, '[', 120); deep[125] = '0';
    memset(deep + 126, ']', 120); deep[246] = '}'; deep[247] = 0;
    before = p;
    assert(apply(&p,deep,&c)==-1 && !memcmp(&p,&before,sizeof(p)));
    assert(apply(&p,"{\"nickname\":\"[[[[[[[[[[[[[[[\"}",&c)==1);
    assert(apply(&p,"{\"nickname\":\"\\\"[[[[[[[[[[[[[[[\"}",&c)==1);
    puts("Stock JSON profile updater: PASS");
}
