/* Metadata depends solely on fetched bits, never PC, registers or bus state. */
enum {
    T_SHIFT=1,T_ADDSUB,T_IMMEDIATE,T_ALU,T_HIGH,T_LITERAL,T_REGISTER_MEMORY,
    T_IMMEDIATE_MEMORY,T_HALF,T_SP_MEMORY,T_ADDRESS,T_SP_ADD,T_PUSHPOP,
    T_MULTIPLE,T_CONDITIONAL,T_BRANCH,T_BL_PREFIX,T_BL_SUFFIX,T_RESERVED,
    T_MOV_IMM,T_CMP_IMM,T_ADD_IMM,T_SUB_IMM,
    T_ALU_AND,T_ALU_EOR,T_ALU_LSL,T_ALU_LSR,T_ALU_ASR,T_ALU_ADC,T_ALU_SBC,T_ALU_ROR,
    T_ALU_TST,T_ALU_NEG,T_ALU_CMP,T_ALU_CMN,T_ALU_ORR,T_ALU_MUL,T_ALU_BIC,T_ALU_MVN,
    T_HIGH_ADD,T_HIGH_CMP,T_HIGH_MOV,T_HIGH_BX,T_NOP,
    A_DP,A_SINGLE,A_BRANCH,A_BX,A_MRS,A_MSR,A_LONG_MUL,A_MUL,A_SWP,A_HALF,
    A_BLOCK,A_SWI,A_RESERVED
};
typedef struct {
    uint32_t opcode,immediate; /* ARM opcode tag; Thumb conditional stores its NZCV truth mask here */
    uint8_t kind,rd,rn,rm,rs,code,clocks,count;
} Decoded;
struct Arm7Cache {Decoded thumb[65536],arm[1024];BusFetchHint fetch;};
Arm7Cache *arm7_cache_create(void){return calloc(1,sizeof(Arm7Cache));}
void arm7_cache_destroy(Arm7Cache *cache){free(cache);}
static unsigned register_count(uint32_t mask);
static void decode_thumb(Decoded *d,uint32_t op)
{
    unsigned kind=T_RESERVED,i;
    d->opcode=op;d->rd=(uint8_t)(op&7);d->rs=(uint8_t)((op>>3)&7);
    d->rn=(uint8_t)((op>>6)&7);d->rm=0;d->code=0;d->count=0;
    d->immediate=0;d->clocks=1;
    switch((op>>12)&15){
    case 0:kind=T_SHIFT;break;
    case 1:kind=(op&0x800)?T_ADDSUB:T_SHIFT;break;
    case 2:case 3:kind=T_IMMEDIATE;break;
    case 4:kind=(op&0x800)?T_LITERAL:(op&0x400)?T_HIGH:T_ALU;break;
    case 5:kind=T_REGISTER_MEMORY;break;
    case 6:case 7:kind=T_IMMEDIATE_MEMORY;break;
    case 8:kind=T_HALF;break;
    case 9:kind=T_SP_MEMORY;break;
    case 10:kind=T_ADDRESS;break;
    case 11:if((op&0xff00)==0xb000)kind=T_SP_ADD;
        else if((op&0xf600)==0xb400)kind=T_PUSHPOP;break;
    case 12:kind=T_MULTIPLE;break;
    case 13:kind=T_CONDITIONAL;break;
    case 14:if(!(op&0x800))kind=T_BRANCH;break;
    case 15:kind=(op&0x800)?T_BL_SUFFIX:T_BL_PREFIX;break;
    }
    d->kind=(uint8_t)kind;
    switch(kind){
    case T_SHIFT:d->code=(uint8_t)((op>>11)&3);d->immediate=(op>>6)&31;break;
    case T_IMMEDIATE:d->rd=(uint8_t)((op>>8)&7);d->code=(uint8_t)((op>>11)&3);d->immediate=op&255;d->kind=(uint8_t)(T_MOV_IMM+d->code);break;
    case T_ALU:d->code=(uint8_t)((op>>6)&15);
        if((d->code>=2&&d->code<=4)||d->code==7)d->clocks=2;d->kind=(uint8_t)(T_ALU_AND+d->code);break;
    case T_HIGH:d->code=(uint8_t)((op>>8)&3);d->rd|=(uint8_t)((op>>4)&8);d->rs=(uint8_t)((op>>3)&15);
        if(d->code==3||((d->code==0||d->code==2)&&d->rd==15))d->clocks=3;
        d->kind=(uint8_t)(T_HIGH_ADD+d->code);if(d->code==2&&d->rd==d->rs&&d->rd!=15)d->kind=T_NOP;break;
    case T_LITERAL:d->rd=(uint8_t)((op>>8)&7);d->immediate=(op&255)*4;d->clocks=3;break;
    case T_REGISTER_MEMORY:d->code=(uint8_t)((op>>9)&7);d->clocks=d->code>=3?3:2;break;
    case T_IMMEDIATE_MEMORY:d->code=(op&0x1000)?1:4;d->immediate=((op>>6)&31)*d->code;d->clocks=(op&0x800)?3:2;break;
    case T_HALF:d->immediate=((op>>6)&31)*2;d->clocks=(op&0x800)?3:2;break;
    case T_SP_MEMORY:d->rd=(uint8_t)((op>>8)&7);d->immediate=(op&255)*4;d->clocks=(op&0x800)?3:2;break;
    case T_ADDRESS:d->rd=(uint8_t)((op>>8)&7);d->immediate=(op&255)*4;break;
    case T_SP_ADD:d->immediate=(op&127)*4;break;
    case T_PUSHPOP:d->count=(uint8_t)(register_count(op&255)+!!(op&0x100));
        d->clocks=(uint8_t)(d->count+1+!!(op&0x800)+((op&0x900)==0x900?2:0));break;
    case T_MULTIPLE:d->rn=(uint8_t)((op>>8)&7);d->count=(uint8_t)register_count(op&255);d->code=8;
        for(i=0;i<8;++i)if(op&(1u<<i)){d->code=(uint8_t)i;break;}
        d->clocks=(uint8_t)(d->count?d->count+1+!!(op&0x800):17+!!(op&0x800)+((op&0x800)?2:0));break;
    case T_CONDITIONAL:d->code=(uint8_t)((op>>8)&15);d->opcode=condition_mask[d->code];d->immediate=(uint32_t)((int32_t)(int8_t)op*2);break;
    case T_BRANCH:d->immediate=(op&0x7ff)<<1;if(d->immediate&0x800)d->immediate|=0xfffff000u;d->clocks=3;break;
    case T_BL_PREFIX:d->immediate=(op&0x7ff)<<12;if(d->immediate&0x400000)d->immediate|=0xff800000u;break;
    case T_BL_SUFFIX:d->immediate=(op&0x7ff)*2;d->clocks=3;break;
    default:break;
    }
}
static void decode_arm(Decoded *d,uint32_t op)
{
    d->opcode=op;d->rd=(uint8_t)((op>>12)&15);d->rn=(uint8_t)((op>>16)&15);
    d->rm=(uint8_t)(op&15);d->rs=(uint8_t)((op>>8)&15);
    d->code=(uint8_t)((op>>21)&15);d->count=0;
    if(plain_data_processing(op))d->kind=A_DP;
    else if((op&0x0c000000u)==0x04000000u)d->kind=A_SINGLE;
    else if((op&0x0e000000u)==0x0a000000u)d->kind=A_BRANCH;
    else if((op&0x0ffffff0u)==0x012fff10u)d->kind=A_BX;
    else if((op&0x0fbf0fffu)==0x010f0000u)d->kind=A_MRS;
    else if((op&0x0fb0fff0u)==0x0120f000u||(op&0x0fb0f000u)==0x0320f000u)d->kind=A_MSR;
    else if((op&0x0f8000f0u)==0x00800090u)d->kind=A_LONG_MUL;
    else if((op&0x0fc000f0u)==0x00000090u)d->kind=A_MUL;
    else if((op&0x0fb00ff0u)==0x01000090u)d->kind=A_SWP;
    else if((op&0x0e000090u)==0x00000090u&&(op&0x60u))d->kind=A_HALF;
    else if((op&0x0c000000u)==0)d->kind=A_DP;
    else if((op&0x0e000000u)==0x08000000u)d->kind=A_BLOCK;
    else if((op&0x0f000000u)==0x0f000000u)d->kind=A_SWI;
    else d->kind=A_RESERVED;
    d->immediate=ror(op&255,((op>>8)&15)*2);
    if(d->kind==A_SINGLE)d->immediate=op&0xfff;
    if(d->kind==A_HALF)d->immediate=((op>>4)&0xf0)|(op&15);
    if(d->kind==A_BRANCH){d->immediate=(op&0xffffff)<<2;if(d->immediate&0x2000000)d->immediate|=0xfc000000u;}
    if(d->kind==A_BLOCK){unsigned i;d->count=(uint8_t)register_count(op&65535);d->code=16;
        for(i=0;i<16;++i)if(op&(1u<<i)){d->code=(uint8_t)i;break;}}
    /* Classify once: static timing uses the execution class already decoded.
       Source-dependent multiply timing is still added immediately before execution. */
    switch(d->kind){
    case A_DP:d->clocks=(uint8_t)((!(op&(1u<<25))&&(op&16)?2:1)+
        (d->rd==15&&(d->code<8||d->code>11)?2:0));break;
    case A_SINGLE:case A_HALF:d->clocks=(op&(1u<<20))?(d->rd==15?5:3):2;break;
    case A_BRANCH:case A_BX:case A_SWI:d->clocks=3;break;
    case A_LONG_MUL:d->clocks=(uint8_t)(2+!!(op&(1u<<21)));break;
    case A_MUL:d->clocks=(uint8_t)(1+!!(op&(1u<<21)));break;
    case A_SWP:d->clocks=4;break;
    case A_BLOCK:d->clocks=(uint8_t)((d->count?d->count:16)+1+!!(op&(1u<<20))+
        ((op&(1u<<20))&&(op&0x8000)?2:0));break;
    default:d->clocks=1;break;
    }
}
