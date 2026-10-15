#ifndef SGHEMU_KEY_TEXT_H
#define SGHEMU_KEY_TEXT_H
#include <stdint.h>
#include <string.h>
#define KEY_TEXT_CAPACITY 1024
typedef struct {
    uint8_t keys[KEY_TEXT_CAPACITY];
    unsigned head,count,key,pressed;
    uint64_t due;
} KeyText;
static inline void key_text_reset(KeyText *t){memset(t,0,sizeof(*t));}
static inline int key_text_key(KeyText *t,unsigned key)
{
    if(key>=22||t->count==KEY_TEXT_CAPACITY)return 0;
    t->keys[(t->head+t->count)%KEY_TEXT_CAPACITY]=(uint8_t)key;++t->count;return 1;
}
/* English abc/ABC mode. Case is selected on the handset, not by Shift. */
static inline int key_text_char(KeyText *t,unsigned ch)
{
    static const char *const groups[]={"abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"};
    unsigned key=0,taps=0,i;const char *p;
    if(ch>='A'&&ch<='Z')ch+='a'-'A';
    if(ch==' '){key=19;taps=1;}
    else if(ch=='.'){key=9;taps=1;}
    else if(ch>='a'&&ch<='z')for(i=0;i<8;++i){p=strchr(groups[i],(int)ch);if(p){key=10+i;taps=(unsigned)(p-groups[i])+1;break;}}
    if(!taps||taps+1>KEY_TEXT_CAPACITY-t->count)return 0;
    for(i=0;i<taps;++i)(void)key_text_key(t,key);
    (void)key_text_key(t,5); /* Right commits the character without a timeout. */
    return 1;
}
/* One edge at a time, using guest time: never collapse several transitions
   when the host stalls. The firmware must actually scan each down/up state. */
static inline int key_text_tick(KeyText *t,uint64_t now,unsigned *key,int *down)
{
    if(now<t->due||(!t->pressed&&!t->count))return 0;
    if(t->pressed){t->pressed=0;*key=t->key;*down=0;}
    else {t->key=t->keys[t->head];t->head=(t->head+1)%KEY_TEXT_CAPACITY;--t->count;
        t->pressed=1;*key=t->key;*down=1;}
    t->due=now+60000000u; /* 60 ms held, 60 ms released, both emulated. */
    return 1;
}
#endif
