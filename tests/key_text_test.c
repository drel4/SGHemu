#include "key_text.h"
#include <stdio.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
int main(void)
{
    KeyText t;unsigned key=99,i;int down=-1;uint64_t now=0;
    static const unsigned expected[]={10,5,10,10,5,10,10,10,5,15,15,15,15,5,17,17,17,17,5,19,5,9,5};
    key_text_reset(&t);
    CHECK(key_text_char(&t,'A'));CHECK(key_text_char(&t,'b'));CHECK(key_text_char(&t,'c'));
    CHECK(key_text_char(&t,'s'));CHECK(key_text_char(&t,'z'));CHECK(key_text_char(&t,' '));CHECK(key_text_char(&t,'.'));
    CHECK(!key_text_char(&t,'@'));CHECK(!key_text_char(&t,0x430));CHECK(!key_text_char(&t,0));
    CHECK(t.count==sizeof(expected)/sizeof(expected[0]));
    for(i=0;i<sizeof(expected)/sizeof(expected[0]);++i){
        CHECK(key_text_tick(&t,now,&key,&down)&&key==expected[i]&&down==1);
        CHECK(!key_text_tick(&t,now+59999999,&key,&down));
        CHECK(key_text_tick(&t,now+60000000,&key,&down)&&key==expected[i]&&down==0);
        CHECK(!key_text_tick(&t,now+119999999,&key,&down));now+=120000000;
    }
    CHECK(!t.count&&!t.pressed);CHECK(!key_text_tick(&t,now,&key,&down));
    /* Queue overflow cannot insert half a character (or lose its Right). */
    key_text_reset(&t);for(i=0;i<KEY_TEXT_CAPACITY-3;++i)CHECK(key_text_key(&t,21));
    CHECK(!key_text_char(&t,'s'));CHECK(t.count==KEY_TEXT_CAPACITY-3);
    CHECK(key_text_char(&t,'b'));CHECK(t.count==KEY_TEXT_CAPACITY);CHECK(!key_text_key(&t,5));
    CHECK(!key_text_key(&t,22));
    /* Slow host / paused guest: one edge, with a fresh interval, not a burst. */
    key_text_reset(&t);CHECK(key_text_char(&t,'b'));
    CHECK(key_text_tick(&t,0,&key,&down)&&down==1);
    CHECK(key_text_tick(&t,10000000000ULL,&key,&down)&&down==0);
    CHECK(!key_text_tick(&t,10000000000ULL,&key,&down));
    CHECK(key_text_tick(&t,10060000000ULL,&key,&down)&&down==1&&key==10);
    key_text_reset(&t);CHECK(!t.count&&!t.pressed&&!t.due);
    CHECK(!key_text_tick(&t,now,&key,&down));
    printf("Keyboard multi-tap queue: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
